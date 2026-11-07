// ============================================================================
// PollDefinition.cpp — 轮询定义 JSON 序列化 (工作区文件格式)
// ============================================================================

#include "PollDefinition.h"

#include <QColor>
#include <QJsonArray>

namespace {
QJsonObject ruleToJson(const PollDefinition::ColorRule &r)
{
    QJsonObject o;
    o.insert(QStringLiteral("op"), r.op);
    o.insert(QStringLiteral("lo"), r.lo);
    o.insert(QStringLiteral("hi"), r.hi);
    return o;
}

PollDefinition::ColorRule ruleFromJson(const QJsonObject &o)
{
    PollDefinition::ColorRule r;
    r.op = qBound(0, o.value(QStringLiteral("op")).toInt(0), 5);
    r.lo = o.value(QStringLiteral("lo")).toDouble(0);
    r.hi = o.value(QStringLiteral("hi")).toDouble(0);
    return r;
}
} // namespace

QVector<PollDefinition::Segment> PollDefinition::segments() const
{
    if (!segs.isEmpty() && !isWriteFc())
        return segs;
    return QVector<Segment> {{addr, qty}};
}

int PollDefinition::totalQty() const
{
    if (!segs.isEmpty() && !isWriteFc())
    {
        int n = 0;
        for (const Segment &s : segs)
            n += s.qty;
        return n;
    }
    return qty;
}

int PollDefinition::rowAddr(int row) const
{
    if (segs.isEmpty() || isWriteFc())
        return row >= 0 && row < qty ? int(addr) + row : -1;
    int base = 0;
    for (const Segment &s : segs)
    {
        if (row < base + s.qty)
            return int(s.addr) + (row - base);
        base += s.qty;
    }
    return -1;
}

int PollDefinition::rowForAddr(quint16 a) const
{
    if (segs.isEmpty() || isWriteFc())
        return a >= addr && a < quint16(int(addr) + qty) ? int(a) - int(addr) : -1;
    int base = 0;
    for (const Segment &s : segs)
    {
        if (a >= s.addr && a < quint16(int(s.addr) + s.qty))
            return base + int(a) - int(s.addr);
        base += s.qty;
    }
    return -1;
}

int PollDefinition::segmentIndexAt(int row) const
{
    if (segs.isEmpty() || isWriteFc() || row < 0)
        return -1;
    int base = 0;
    for (int i = 0; i < segs.size(); ++i)
    {
        if (row < base + segs[i].qty)
            return i;
        base += segs[i].qty;
    }
    return -1;
}

QJsonObject PollDefinition::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("connId"), connectionId); // 多连接归属 (v3; 空为首/活动连接)
    o.insert(QStringLiteral("slave"), slave);
    o.insert(QStringLiteral("fc"), fc);
    o.insert(QStringLiteral("addr"), int(addr));
    o.insert(QStringLiteral("qty"), qty);
    o.insert(QStringLiteral("rateMs"), scanRateMs);
    o.insert(QStringLiteral("enabled"), enabled);
    o.insert(QStringLiteral("autoStop"), autoStopOnError);
    o.insert(QStringLiteral("hideNames"), hideNames);
    o.insert(QStringLiteral("addrInCell"), addrInCell);
    o.insert(QStringLiteral("plcBase1"), plcBase1);
    o.insert(QStringLiteral("fmt"), ModbusFormat::fmtKey(fmt));
    o.insert(QStringLiteral("order"), ModbusFormat::orderKey(order));
    QJsonArray names;
    for (const QString &s : nameCells)
        names.append(s);
    o.insert(QStringLiteral("names"), names);

    QJsonObject scale;
    scale.insert(QStringLiteral("on"), scaleOn);
    scale.insert(QStringLiteral("m"), scaleM);
    scale.insert(QStringLiteral("x1"), scaleX1);
    scale.insert(QStringLiteral("y1"), scaleY1);
    o.insert(QStringLiteral("scale"), scale);

    // 值前缀/后缀 (常用于单位; 任一非空才落盘)
    if (!valPrefix.isEmpty() || !valSuffix.isEmpty())
    {
        QJsonObject af;
        af.insert(QStringLiteral("p"), valPrefix);
        af.insert(QStringLiteral("s"), valSuffix);
        o.insert(QStringLiteral("affix"), af);
    }

    QJsonArray rl;
    rl.append(ruleToJson(rules[0]));
    rl.append(ruleToJson(rules[1]));
    o.insert(QStringLiteral("rules"), rl);

    // 外观 (窗口级默认; 行级字体/行高/背景覆盖在 rowStyles)
    QJsonObject look;
    look.insert(QStringLiteral("fontPt"), fontPt);
    look.insert(QStringLiteral("bold"), fontBold);
    look.insert(QStringLiteral("italic"), fontItalic);
    look.insert(QStringLiteral("underline"), fontUnderline);
    look.insert(QStringLiteral("rowH"), rowHeight);
    if (!bg.isEmpty())
        look.insert(QStringLiteral("bg"), bg);
    look.insert(QStringLiteral("nameColW"), nameColW);
    look.insert(QStringLiteral("valueColW"), valueColW);
    if (vHeaderW > 0)
        look.insert(QStringLiteral("vHeaderW"), vHeaderW);
    o.insert(QStringLiteral("look"), look);

    QJsonObject vn;
    for (auto it = valueNames.cbegin(); it != valueNames.cend(); ++it)
        vn.insert(QString::number(it.key()), it.value());
    o.insert(QStringLiteral("vnames"), vn);

    // 多段 (≥2 段才落盘; segs[0] 与 addr/qty 镜像, 旧版读到首段)
    if (segs.size() >= 2)
    {
        QJsonArray sa;
        for (const Segment &s : segs)
        {
            QJsonObject so;
            so.insert(QStringLiteral("addr"), int(s.addr));
            so.insert(QStringLiteral("qty"), s.qty);
            sa.append(so);
        }
        o.insert(QStringLiteral("segs"), sa);
    }

    // 行级显示覆盖 (只落 has 置位的字段)
    if (!rowStyles.isEmpty())
    {
        QJsonObject rs;
        for (auto it = rowStyles.cbegin(); it != rowStyles.cend(); ++it)
        {
            const RowStyle &st = it.value();
            QJsonObject ro;
            if (st.hasFmt)
            {
                ro.insert(QStringLiteral("fmt"), ModbusFormat::fmtKey(st.fmt));
                ro.insert(QStringLiteral("order"), ModbusFormat::orderKey(st.order));
            }
            if (st.hasScale)
            {
                QJsonObject sc;
                sc.insert(QStringLiteral("on"), st.scaleOn);
                sc.insert(QStringLiteral("m"), st.scaleM);
                sc.insert(QStringLiteral("x1"), st.scaleX1);
                sc.insert(QStringLiteral("y1"), st.scaleY1);
                ro.insert(QStringLiteral("scale"), sc);
            }
            if (st.hasAffix) // 空串覆盖 (显式去掉窗口级单位) 也要落盘
            {
                QJsonObject ao;
                ao.insert(QStringLiteral("p"), st.valPrefix);
                ao.insert(QStringLiteral("s"), st.valSuffix);
                ro.insert(QStringLiteral("affix"), ao);
            }
            if (st.hasRules)
            {
                QJsonArray rl;
                rl.append(ruleToJson(st.rules[0]));
                rl.append(ruleToJson(st.rules[1]));
                ro.insert(QStringLiteral("rules"), rl);
            }
            if (st.hasVNames)
            {
                QJsonObject vo;
                for (auto vi = st.valueNames.cbegin(); vi != st.valueNames.cend(); ++vi)
                    vo.insert(QString::number(vi.key()), vi.value());
                ro.insert(QStringLiteral("vnames"), vo);
            }
            if (st.hasFont)
            {
                QJsonObject fo;
                fo.insert(QStringLiteral("pt"), st.fontPt);
                fo.insert(QStringLiteral("b"), st.bold);
                fo.insert(QStringLiteral("i"), st.italic);
                fo.insert(QStringLiteral("u"), st.underline);
                ro.insert(QStringLiteral("font"), fo);
            }
            if (st.hasHeight)
                ro.insert(QStringLiteral("rowH"), st.rowHeight);
            if (st.hasBg && !st.bg.isEmpty())
                ro.insert(QStringLiteral("bg"), st.bg);
            if (!ro.isEmpty())
                rs.insert(QString::number(it.key()), ro);
        }
        if (!rs.isEmpty())
            o.insert(QStringLiteral("rowStyles"), rs);
    }
    return o;
}

PollDefinition PollDefinition::fromJson(const QJsonObject &o)
{
    PollDefinition d; // 默认值兜底: 缺失或类型不符的字段一律落默认
    d.name = o.value(QStringLiteral("name")).toString();
    d.connectionId = o.value(QStringLiteral("connId")).toString(); // v3 多连接归属
    d.slave = o.value(QStringLiteral("slave")).toInt(1);
    d.fc = o.value(QStringLiteral("fc")).toInt(3);
    d.addr = quint16(o.value(QStringLiteral("addr")).toInt(0) & 0xFFFF);
    d.qty = o.value(QStringLiteral("qty")).toInt(10);
    d.scanRateMs = o.value(QStringLiteral("rateMs")).toInt(1000);
    d.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    d.autoStopOnError = o.value(QStringLiteral("autoStop")).toBool(false);
    d.hideNames = o.value(QStringLiteral("hideNames")).toBool(false);
    d.addrInCell = o.value(QStringLiteral("addrInCell")).toBool(false);
    d.plcBase1 = o.value(QStringLiteral("plcBase1")).toBool(false);
    d.fmt = ModbusFormat::fmtFromKey(o.value(QStringLiteral("fmt")).toString());
    d.order = ModbusFormat::orderFromKey(o.value(QStringLiteral("order")).toString());
    const QJsonArray names = o.value(QStringLiteral("names")).toArray();
    for (const auto &v : names)
        d.nameCells.append(v.toString());

    const QJsonObject scale = o.value(QStringLiteral("scale")).toObject();
    d.scaleOn = scale.value(QStringLiteral("on")).toBool(false);
    d.scaleM = scale.value(QStringLiteral("m")).toDouble(1.0);
    d.scaleX1 = scale.value(QStringLiteral("x1")).toDouble(0);
    d.scaleY1 = scale.value(QStringLiteral("y1")).toDouble(0);

    const QJsonObject af = o.value(QStringLiteral("affix")).toObject();
    if (!af.isEmpty())
    {
        d.valPrefix = af.value(QStringLiteral("p")).toString();
        d.valSuffix = af.value(QStringLiteral("s")).toString();
    }

    const QJsonArray rl = o.value(QStringLiteral("rules")).toArray();
    if (rl.size() >= 1)
        d.rules[0] = ruleFromJson(rl.at(0).toObject());
    if (rl.size() >= 2)
        d.rules[1] = ruleFromJson(rl.at(1).toObject());

    // 外观 (窗口级默认; 缺失 = 旧文件默认值)
    const QJsonObject look = o.value(QStringLiteral("look")).toObject();
    if (!look.isEmpty())
    {
        d.fontPt = qBound(6, look.value(QStringLiteral("fontPt")).toInt(9), 48);
        d.fontBold = look.value(QStringLiteral("bold")).toBool(false);
        d.fontItalic = look.value(QStringLiteral("italic")).toBool(false);
        d.fontUnderline = look.value(QStringLiteral("underline")).toBool(false);
        d.rowHeight = qBound(14, look.value(QStringLiteral("rowH")).toInt(22), 200);
        const QString bg = look.value(QStringLiteral("bg")).toString();
        if (QColor::isValidColorName(bg))
            d.bg = bg;
        d.nameColW =
            qBound(40, look.value(QStringLiteral("nameColW")).toInt(200), 4000);
        d.valueColW =
            qBound(40, look.value(QStringLiteral("valueColW")).toInt(150), 4000);
        const int vhw = look.value(QStringLiteral("vHeaderW")).toInt(0);
        d.vHeaderW = vhw == 0 ? 0 : qBound(64, vhw, 400);
    }

    const QJsonObject vn = o.value(QStringLiteral("vnames")).toObject();
    for (auto it = vn.begin(); it != vn.end(); ++it)
    {
        bool numOk = false;
        const qlonglong k = qlonglong(it.key().toULongLong(&numOk));
        if (numOk && k >= 0 && k <= 65535)
            d.valueNames.insert(quint16(k), it.value().toString());
    }

    // 多段 (v2; 缺失 = 旧文件单段, 回落 addr/qty)
    const QJsonArray sa = o.value(QStringLiteral("segs")).toArray();
    for (const auto &v : sa)
    {
        const QJsonObject so = v.toObject();
        Segment s;
        s.addr = quint16(so.value(QStringLiteral("addr")).toInt(0) & 0xFFFF);
        s.qty = so.value(QStringLiteral("qty")).toInt(1);
        d.segs.append(s);
    }
    if (d.segs.size() < 2)
        d.segs.clear(); // 单段回落 addr/qty 表示

    // 行级显示覆盖 (v2)
    const QJsonObject rs = o.value(QStringLiteral("rowStyles")).toObject();
    for (auto it = rs.begin(); it != rs.end(); ++it)
    {
        bool numOk = false;
        const int row = it.key().toInt(&numOk);
        if (!numOk || row < 0)
            continue;
        const QJsonObject ro = it.value().toObject();
        RowStyle st;
        if (ro.contains(QStringLiteral("fmt")))
        {
            st.hasFmt = true;
            st.fmt = ModbusFormat::fmtFromKey(ro.value(QStringLiteral("fmt")).toString());
            st.order = ModbusFormat::orderFromKey(
                ro.value(QStringLiteral("order")).toString());
        }
        const QJsonObject sc = ro.value(QStringLiteral("scale")).toObject();
        if (!sc.isEmpty())
        {
            st.hasScale = true;
            st.scaleOn = sc.value(QStringLiteral("on")).toBool(false);
            st.scaleM = sc.value(QStringLiteral("m")).toDouble(1.0);
            st.scaleX1 = sc.value(QStringLiteral("x1")).toDouble(0);
            st.scaleY1 = sc.value(QStringLiteral("y1")).toDouble(0);
        }
        if (ro.contains(QStringLiteral("affix")))
        {
            const QJsonObject ao = ro.value(QStringLiteral("affix")).toObject();
            st.hasAffix = true;
            st.valPrefix = ao.value(QStringLiteral("p")).toString();
            st.valSuffix = ao.value(QStringLiteral("s")).toString();
        }
        const QJsonArray rl = ro.value(QStringLiteral("rules")).toArray();
        if (rl.size() >= 2)
        {
            st.hasRules = true;
            st.rules[0] = ruleFromJson(rl.at(0).toObject());
            st.rules[1] = ruleFromJson(rl.at(1).toObject());
        }
        const QJsonObject vo = ro.value(QStringLiteral("vnames")).toObject();
        if (!vo.isEmpty())
        {
            st.hasVNames = true;
            for (auto vi = vo.begin(); vi != vo.end(); ++vi)
            {
                bool vOk = false;
                const qlonglong k = qlonglong(vi.key().toULongLong(&vOk));
                if (vOk && k >= 0 && k <= 65535)
                    st.valueNames.insert(quint16(k), vi.value().toString());
            }
        }
        const QJsonObject fo = ro.value(QStringLiteral("font")).toObject();
        if (!fo.isEmpty())
        {
            st.hasFont = true;
            st.fontPt = qBound(6, fo.value(QStringLiteral("pt")).toInt(9), 48);
            st.bold = fo.value(QStringLiteral("b")).toBool(false);
            st.italic = fo.value(QStringLiteral("i")).toBool(false);
            st.underline = fo.value(QStringLiteral("u")).toBool(false);
        }
        if (ro.contains(QStringLiteral("rowH")))
        {
            st.hasHeight = true;
            st.rowHeight =
                qBound(14, ro.value(QStringLiteral("rowH")).toInt(22), 200);
        }
        const QString bg = ro.value(QStringLiteral("bg")).toString();
        if (QColor::isValidColorName(bg))
        {
            st.hasBg = true;
            st.bg = bg;
        }
        if (st.hasFmt || st.hasScale || st.hasAffix || st.hasRules || st.hasVNames
            || st.hasFont || st.hasHeight || st.hasBg)
            d.rowStyles.insert(row, st);
    }

    d.clamp();
    return d;
}
