#!/usr/bin/env python3
"""modbus_sim.py — 标准数字变送器 (standard-tx 档案) Modbus TCP 模拟器.

无硬件端到端验证用 dev 工具 (不随产品分发). 纯标准库, 无第三方依赖.

寄存器布局 (plugins/standard-tx/profile.lua):
  5        U16  通道数 (1-16, --channels 可调)
  6        U16  恢复出厂命令 (写 111 即应答成功)
  7-8      U32  固件版本 (0x00010002)
  259      U16  单位码 (2 = kg)
  每通道 ch (1..N), base = 200+(ch-1)*100:
    base+0      U16  状态 (bit3 稳定 / bit4 故障) — 除 ch3 外恒稳定
    base+1      U16  小数点 (1 → 0.1kg)
    base+3..4   S32  实时值 (显示值×10)  = ch*100 + 60·sin(t/2.5s+ch) + 8·sin(t/0.2s)
    base+5      U16  实时值 i16 截断
    base+9..10  S32  采样码 (实时值×97)
    base+11..12 S32  峰值 (实时+300)
    base+13..14 S32  谷值 (实时-300)
    base+37/38-39/40-41/42  标定写入区 (灵敏度/满量程/砝码/命令) — 只存不 effects

用法: python3 scripts/modbus_sim.py [--channels 16] [--port 1502]
"""
import argparse
import math
import socket
import socketserver
import struct
import threading
import time

REG = {}          # 写入存储 (FC06/FC10 落 here, FC03 优先返回)
LOCK = threading.Lock()
NCH = 16
UNIT_CODE = 2


def display_value(ch: int, t_ms: float) -> float:
    """通道 ch 的显示值 (kg): 慢波 + 快纹, ch3 幅度更大 (模拟不稳定)."""
    amp = 60.0 if ch != 3 else 260.0
    return ch * 100.0 + amp * math.sin(t_ms / 2500.0 + ch) + 8.0 * math.sin(t_ms / 200.0 + ch * 2)


def status_word(ch: int) -> int:
    stable = 0 if ch == 3 else 8  # bit3
    return stable


def s32_words(value: float) -> tuple[int, int]:
    """有符号 32 位值 → (低字, 高字)."""
    v = int(round(value)) & 0xFFFFFFFF
    return v & 0xFFFF, (v >> 16) & 0xFFFF


def computed_reg(addr: int, t_ms: float) -> int:
    if addr == 5:
        return NCH
    if addr == 7:
        return 0x0001
    if addr == 8:
        return 0x0002
    if addr == 259:
        return UNIT_CODE
    if 200 <= addr < 200 + NCH * 100:
        ch = (addr - 200) // 100 + 1
        off = (addr - 200) % 100
        if off == 0:
            return status_word(ch)
        if off == 1:
            return 1  # decimals
        disp = display_value(ch, t_ms)
        if off in (3, 4):
            lo, hi = s32_words(disp * 10)      # S32 = 高字在前: 偶偏移放高字
            return hi if off == 3 else lo
        if off == 5:
            return int(disp) & 0xFFFF
        if off in (9, 10):
            lo, hi = s32_words(disp * 970.0)
            return hi if off == 9 else lo
        if off in (11, 12):
            lo, hi = s32_words((disp + 300.0) * 10)
            return hi if off == 11 else lo
        if off in (13, 14):
            lo, hi = s32_words((disp - 300.0) * 10)
            return hi if off == 13 else lo
    return 0


def read_regs(addr: int, count: int) -> list[int]:
    t = time.time() * 1000.0
    with LOCK:
        return [REG.get(addr + i, computed_reg(addr + i, t)) & 0xFFFF
                for i in range(count)]


def write_regs(addr: int, values: list[int]):
    with LOCK:
        for i, v in enumerate(values):
            REG[addr + i] = v & 0xFFFF
    if any(200 <= addr + i < 200 + NCH * 100 and (addr + i - 200) % 100 == 42
           for i in range(len(values))):
        print(f"[sim] 标定命令写入 base+42 = {values}", flush=True)


def mbap_handle(data: bytes) -> bytes | None:
    """处理一帧 MBAP 请求 → 应答字节 (不完整/非法返回 None)."""
    if len(data) < 8:
        return None
    tid, pid, length, uid = struct.unpack(">HHHB", data[:7])
    body = data[7:6 + length]
    if not body:
        return None
    fc = body[0]
    if fc == 3 and len(body) >= 5:
        addr, count = struct.unpack(">HH", body[1:5])
        vals = read_regs(addr, count)
        payload = struct.pack(">B", 2 * count) + b"".join(struct.pack(">H", v) for v in vals)
        return struct.pack(">HHHBB", tid, pid, len(payload) + 2, uid, 3) + payload
    if fc == 6 and len(body) >= 5:
        addr, value = struct.unpack(">HH", body[1:5])
        write_regs(addr, [value])
        return struct.pack(">HHHBBB", tid, pid, 6, uid, 6, 0) + body[1:5]
    if fc == 16 and len(body) >= 6:
        addr, count = struct.unpack(">HH", body[1:5])
        bc = body[5]
        vals = list(struct.unpack(">" + "H" * count, body[6:6 + bc]))
        write_regs(addr, vals)
        return struct.pack(">HHHHB", tid, pid, 6, uid, 16) + body[1:5]
    # 不支持 → 异常应答
    return struct.pack(">HHHBB", tid, pid, 3, uid, fc | 0x80) + b"\x01"


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        print(f"[sim] 客户端接入: {self.client_address}", flush=True)
        buf = b""
        while True:
            try:
                chunk = self.request.recv(1024)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
            while len(buf) >= 7:
                length = struct.unpack(">H", buf[4:6])[0]
                if len(buf) < 6 + length:
                    break
                resp = mbap_handle(buf[:6 + length])
                buf = buf[6 + length:]
                if resp:
                    self.request.sendall(resp)
        print("[sim] 客户端断开", flush=True)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    global NCH
    ap = argparse.ArgumentParser(description="standard-tx 多通道变送器模拟器")
    ap.add_argument("--channels", type=int, default=16, help="通道数 1-16 (默认 16)")
    ap.add_argument("--port", type=int, default=1502, help="TCP 端口 (默认 1502)")
    a = ap.parse_args()
    NCH = max(1, min(16, a.channels))
    srv = Server(("127.0.0.1", a.port), Handler)
    print(f"[sim] standard-tx 模拟器: 127.0.0.1:{a.port}, 通道数={NCH}, 单位=kg", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
