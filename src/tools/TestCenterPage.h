#pragma once

// ============================================================================
// TestCenterPage.h — Test Center (对标 Modbus Poll Alt+T; 旧 ModbusGenTool 替代)
//   结构化生成 (01-10 + 17, 参数校验/十进制转换/功能码说明) →
//   线级字节编辑框 (容忍空格/逗号/无间隔 + "//" 注释) →
//   [追加 CRC] Add Check / [发送] (剥离 CRC/MBAP 后经 sendRaw 真实收发) →
//   响应解析 (字段表 + 异常码中文 + 耗时); 未连接时离线生成/解析仍可用
// ============================================================================

#include "MasterClient.h"
#include "TestCenterCore.h"
#include "ToolUi.h"

#include <QWidget>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class TestCenterPage : public QWidget
{
    Q_OBJECT
public:
    explicit TestCenterPage(MasterClient *mb, QWidget *parent = nullptr);

    void setClient(MasterClient *mb); // 活动连接切换时重指向 (可空 = 禁用收发)
    void setConnected(bool on, bool tcp); // 连接联动: 协议锁定 + 发送使能

private slots:
    void generate();       // 结构化参数 → 线级字节 → 输入框 + ADU 预览
    void convertDecimal(); // 十进制转换填充
    void send();           // 解析输入 → 剥离 → sendRaw
    void onRawDone(int slave, const QByteArray &respPdu, bool ok,
                   const QString &errText, double elapsedMs);

private:
    void buildUi();
    void updateFcHints();
    void updatePreview(); // 输入框内容 → ADU 预览 + 请求解析 (离线)

    MasterClient *m_mb = nullptr;
    bool m_connected = false;
    bool m_tcp = false; // RTU/TCP (离线生成帧类型; 连接时锁定为连接类型)

    QPushButton *m_rtuBtn = nullptr;
    QPushButton *m_tcpBtn = nullptr;
    QSpinBox *m_slave = nullptr;
    QComboBox *m_fc = nullptr;
    QSpinBox *m_addr = nullptr;
    QSpinBox *m_qty = nullptr;
    QWidget *m_fc17Wrap = nullptr; // 17 专属: 写地址/写数量
    QSpinBox *m_addr2 = nullptr;
    QSpinBox *m_qty2 = nullptr;
    QWidget *m_writeWrap = nullptr;
    QPlainTextEdit *m_writeData = nullptr;
    QLineEdit *m_dec = nullptr;
    QLabel *m_qtyHint = nullptr;
    QLabel *m_decHint = nullptr;
    QPushButton *m_genBtn = nullptr;
    QLabel *m_fnDesc = nullptr;

    QPlainTextEdit *m_wireEdit = nullptr;
    QPushButton *m_crcBtn = nullptr;
    QPushButton *m_sendBtn = nullptr;

    QLabel *m_aduHex = nullptr;
    QLabel *m_aduSpaced = nullptr;
    QFormLayout *m_analysis = nullptr;
    QLabel *m_respHex = nullptr;
    QLabel *m_respElapsed = nullptr;
    QFormLayout *m_respAnalysis = nullptr;
    ToolUi::TipLabel *m_tip = nullptr;
};
