// ============================================================================
// TestCenterPage.cpp — Test Center 实现 (结构化生成 + 原始串收发)
// ============================================================================

#include "TestCenterPage.h"

#include "BtnStyles.h"
#include "ToolFrames.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QLabel *analysisValue(const QString &text)
{
    auto *l = new QLabel(text);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral(
        "font-family:SF Mono,'Cascadia Mono',Consolas,'Courier New',monospace;"
        "color:#0f172a; border:none;"));
    return l;
}

void fillForm(QFormLayout *lay, const QVector<TestCenter::Field> &rows)
{
    // 不能用 while(rowCount()>0) takeAt(0): Qt 6.11 实测 takeAt 取空全部条目后
    // rowCount() 仍保持原值, 循环不终止 (见 tst_modbusgen takeRow 回归注释);
    // takeRow(0) 是行级原子操作, 行数正常归零
    while (lay->rowCount() > 0)
    {
        const auto row = lay->takeRow(0);
        for (QLayoutItem *it : {row.labelItem, row.fieldItem})
        {
            if (!it)
                continue;
            if (QWidget *w = it->widget())
                w->deleteLater();
            delete it;
        }
    }
    for (const auto &f : rows)
        lay->addRow(f.name, analysisValue(f.value));
}
} // namespace

TestCenterPage::TestCenterPage(MasterClient *mb, QWidget *parent)
    : QWidget(parent)
    , m_mb(mb)
{
    setWindowTitle(tr("Test Center"));
    buildUi();
    setClient(mb);
}

// 活动连接切换时主窗重指向: 转接 rawDone/连接态到新 client (可为空 = 禁用发送)
void TestCenterPage::setClient(MasterClient *mb)
{
    if (m_mb == mb)
        return;
    if (m_mb)
        disconnect(m_mb, nullptr, this, nullptr);
    m_mb = mb;
    if (m_mb)
    {
        connect(m_mb, &MasterClient::rawDone, this, &TestCenterPage::onRawDone);
        setConnected(m_mb->isConnected(), m_mb->kind() == MasterClient::Kind::Tcp);
    }
    else
    {
        setConnected(false, false);
    }
}

void TestCenterPage::buildUi()
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto makeScroll = [&root](int stretch) {
        auto *sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        root->addWidget(sa, stretch);
        auto *body = new QWidget;
        auto *lay = new QVBoxLayout(body);
        lay->setSpacing(10);
        sa->setWidget(body);
        return lay;
    };
    QVBoxLayout *left = makeScroll(5);
    QVBoxLayout *right = makeScroll(6);

    // ================= 左列: 结构化生成 =================
    auto *protoCard = ToolUi::card(tr("协议类型"));
    auto *protoLay = ToolUi::cardLayout(protoCard);
    auto *protoRow = new QHBoxLayout;
    m_rtuBtn = ToolUi::toggle(tr("Modbus RTU"), true);
    m_rtuBtn->setObjectName(QStringLiteral("tc.protoRtu"));
    m_tcpBtn = ToolUi::toggle(tr("Modbus TCP"));
    m_tcpBtn->setObjectName(QStringLiteral("tc.protoTcp"));
    protoRow->addWidget(m_rtuBtn);
    protoRow->addWidget(m_tcpBtn);
    protoRow->addStretch();
    protoLay->addLayout(protoRow);
    left->addWidget(protoCard);
    auto *protoGrp = new QButtonGroup(this);
    protoGrp->addButton(m_rtuBtn, 0);
    protoGrp->addButton(m_tcpBtn, 1);
    connect(protoGrp, &QButtonGroup::idClicked, this, [this](int id) {
        m_tcp = id == 1;
        updatePreview();
    });

    auto *paramCard = ToolUi::card(tr("基本参数 (Read/Write)"));
    auto *paramLay = ToolUi::cardLayout(paramCard);
    left->addWidget(paramCard);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    paramLay->addLayout(form);

    m_slave = new QSpinBox;
    m_slave->setObjectName(QStringLiteral("tc.slave"));
    m_slave->setRange(1, 247);
    m_slave->setValue(1);
    form->addRow(tr("从机地址 (Slave ID)"), m_slave);

    m_fc = new QComboBox;
    m_fc->setObjectName(QStringLiteral("tc.fc"));
    for (const TestCenter::FcInfo &f : TestCenter::kFcTable)
        m_fc->addItem(QString::fromUtf8(f.label), int(f.code));
    m_fc->setCurrentIndex(2); // 03
    form->addRow(tr("功能码"), m_fc);

    m_addr = new QSpinBox;
    m_addr->setObjectName(QStringLiteral("tc.addr"));
    m_addr->setRange(0, 65535);
    form->addRow(tr("起始地址"), m_addr);

    m_qty = new QSpinBox;
    m_qty->setObjectName(QStringLiteral("tc.qty"));
    m_qty->setRange(0, 65535); // 读数量/写数量/单写值 (0x17 用写数量框)
    m_qty->setValue(10);
    form->addRow(tr("数量/值"), m_qty);
    m_qtyHint = new QLabel;
    m_qtyHint->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px; border:none;"));
    form->addRow(QString(), m_qtyHint);

    // FC17 专属: 写地址/写数量
    m_fc17Wrap = new QWidget;
    auto *w17 = new QHBoxLayout(m_fc17Wrap);
    w17->setContentsMargins(0, 0, 0, 0);
    w17->addWidget(new QLabel(tr("写地址")));
    m_addr2 = new QSpinBox;
    m_addr2->setObjectName(QStringLiteral("tc.addr2"));
    m_addr2->setRange(0, 65535);
    w17->addWidget(m_addr2);
    w17->addWidget(new QLabel(tr("写数量")));
    m_qty2 = new QSpinBox;
    m_qty2->setObjectName(QStringLiteral("tc.qty2"));
    m_qty2->setRange(1, 121);
    m_qty2->setValue(2);
    w17->addWidget(m_qty2);
    w17->addStretch();
    form->addRow(QString(), m_fc17Wrap);
    m_fc17Wrap->hide();

    // 写数据 + 十进制转换
    m_writeWrap = new QWidget;
    auto *wLay = new QVBoxLayout(m_writeWrap);
    wLay->setContentsMargins(0, 0, 0, 0);
    wLay->setSpacing(4);
    wLay->addWidget(new QLabel(tr("写入数据 (十六进制，空格分隔)")));
    m_writeData = new QPlainTextEdit;
    m_writeData->setObjectName(QStringLiteral("tc.writeData"));
    m_writeData->setPlaceholderText(tr("例如: 00 0A 00 14 00 1E"));
    m_writeData->setMaximumHeight(72);
    wLay->addWidget(m_writeData);
    auto *decRow = new QHBoxLayout;
    decRow->setSpacing(6);
    decRow->addWidget(new QLabel(tr("十进制数值 (自动转十六进制)")));
    m_dec = new QLineEdit;
    m_dec->setObjectName(QStringLiteral("tc.dec"));
    m_dec->setPlaceholderText(tr("如: 0、1000、-5、3.14"));
    auto *decBtn = new QPushButton(tr("转换填充"));
    decBtn->setObjectName(QStringLiteral("tc.decBtn"));
    decRow->addWidget(m_dec, 1);
    decRow->addWidget(decBtn);
    wLay->addLayout(decRow);
    m_decHint = new QLabel;
    m_decHint->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px; border:none;"));
    wLay->addWidget(m_decHint);
    form->addRow(QString(), m_writeWrap);

    m_genBtn = new QPushButton(tr("生成并填入发送框"));
    m_genBtn->setObjectName(QStringLiteral("tc.genBtn"));
    BtnStyles::apply(m_genBtn, QStringLiteral("green"));
    paramLay->addWidget(m_genBtn);

    auto *descCard = ToolUi::card(tr("功能码说明"));
    auto *descLay = ToolUi::cardLayout(descCard);
    m_fnDesc = new QLabel;
    m_fnDesc->setWordWrap(true);
    descLay->addWidget(m_fnDesc);
    left->addWidget(descCard);
    left->addStretch();

    // ================= 右列: 发送框 / 响应 / 预览 =================
    auto *sendCard = ToolUi::card(tr("发送帧 (完整线级帧 — RTU 含 CRC)"));
    auto *sendLay = ToolUi::cardLayout(sendCard);
    right->addWidget(sendCard);
    m_wireEdit = new QPlainTextEdit;
    m_wireEdit->setObjectName(QStringLiteral("tc.wireEdit"));
    m_wireEdit->setPlaceholderText(
        tr("01 03 00 00 00 0A C5 CD — 支持无间隔/逗号分隔; \"//\" 注释行"));
    m_wireEdit->setMaximumHeight(88);
    sendLay->addWidget(m_wireEdit);
    auto *sendRow = new QHBoxLayout;
    m_crcBtn = new QPushButton(tr("追加 CRC (Add Check)"));
    m_crcBtn->setObjectName(QStringLiteral("tc.crcBtn"));
    m_sendBtn = new QPushButton(tr("发送"));
    m_sendBtn->setObjectName(QStringLiteral("tc.sendBtn"));
    BtnStyles::apply(m_sendBtn, QStringLiteral("green"));
    sendRow->addWidget(m_crcBtn);
    sendRow->addStretch();
    sendRow->addWidget(m_sendBtn);
    sendLay->addLayout(sendRow);

    auto *respCard = ToolUi::card(tr("响应"));
    auto *respLay = ToolUi::cardLayout(respCard);
    right->addWidget(respCard);
    m_respHex = ToolUi::resultLabel(tr("发送后显示从机应答…"));
    m_respHex->setObjectName(QStringLiteral("tc.respHex"));
    respLay->addWidget(m_respHex);
    m_respElapsed = new QLabel;
    m_respElapsed->setStyleSheet(QStringLiteral("color:#64748b; font-size:12px; border:none;"));
    respLay->addWidget(m_respElapsed);
    auto *respBody = new QWidget;
    m_respAnalysis = new QFormLayout(respBody);
    m_respAnalysis->setHorizontalSpacing(12);
    m_respAnalysis->setVerticalSpacing(6);
    respLay->addWidget(respBody);

    auto *prevCard = ToolUi::card(tr("完整报文预览 (离线生成)"));
    auto *prevLay = ToolUi::cardLayout(prevCard);
    right->addWidget(prevCard);
    m_aduHex = ToolUi::resultLabel(tr("—"));
    m_aduHex->setObjectName(QStringLiteral("tc.aduHex"));
    prevLay->addWidget(m_aduHex);
    m_aduSpaced = ToolUi::resultLabel(tr("—"));
    prevLay->addWidget(m_aduSpaced);
    auto *copyRow = new QHBoxLayout;
    auto *cpHex = new QPushButton(tr("复制十六进制"));
    auto *cpSpaced = new QPushButton(tr("复制带空格"));
    BtnStyles::apply(cpHex, QStringLiteral("blue"));
    BtnStyles::apply(cpSpaced, QStringLiteral("blue"));
    copyRow->addWidget(cpHex);
    copyRow->addWidget(cpSpaced);
    copyRow->addStretch();
    prevLay->addLayout(copyRow);
    auto *anaBody = new QWidget;
    m_analysis = new QFormLayout(anaBody);
    m_analysis->setHorizontalSpacing(12);
    m_analysis->setVerticalSpacing(6);
    prevLay->addWidget(anaBody);
    connect(cpHex, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_aduHex->text());
        m_tip->showMsg(tr("已复制到剪贴板"), true);
    });
    connect(cpSpaced, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_aduSpaced->text());
        m_tip->showMsg(tr("已复制到剪贴板"), true);
    });

    m_tip = new ToolUi::TipLabel(this);
    right->addWidget(m_tip);
    right->addStretch();

    // ================= 信号 =================
    connect(m_fc, &QComboBox::currentIndexChanged, this,
            [this](int) { updateFcHints(); });
    connect(m_genBtn, &QPushButton::clicked, this, &TestCenterPage::generate);
    connect(decBtn, &QPushButton::clicked, this, &TestCenterPage::convertDecimal);
    connect(m_dec, &QLineEdit::returnPressed, this, &TestCenterPage::convertDecimal);
    connect(m_crcBtn, &QPushButton::clicked, this, [this] {
        if (m_tcp)
        {
            m_tip->showMsg(tr("TCP 无 CRC (MBAP 头由发送/预览自动构造)"), false);
            return;
        }
        const QByteArray wire = TestCenter::parseWireHex(m_wireEdit->toPlainText(), nullptr);
        if (wire.isEmpty())
        {
            m_tip->showMsg(tr("发送框为空或格式错误"), false);
            return;
        }
        // 已含正确 CRC 时不再追加 (避免双 CRC 坏帧)
        if (wire.size() >= 4)
        {
            const quint16 want = ToolFrames::crc16(wire.left(wire.size() - 2));
            const quint16 tail = quint16(quint8(wire.at(wire.size() - 2))
                                         | (quint8(wire.at(wire.size() - 1)) << 8));
            if (want == tail)
            {
                m_tip->showMsg(tr("发送框已含正确 CRC, 无需追加"), true);
                return;
            }
        }
        const QByteArray framed = TestCenter::addCheckRtu(wire);
        m_wireEdit->setPlainText(ToolFrames::hexSpaced(framed));
        m_tip->showMsg(tr("已追加 CRC16 (低字节在前) — 发送时自动剥离"), true);
        updatePreview();
    });
    connect(m_sendBtn, &QPushButton::clicked, this, &TestCenterPage::send);
    connect(m_wireEdit, &QPlainTextEdit::textChanged, this, [this] { updatePreview(); });
    connect(m_slave, &QSpinBox::valueChanged, this, [this](int) { updatePreview(); });

    updateFcHints();
}

void TestCenterPage::setConnected(bool on, bool tcp)
{
    m_connected = on;
    if (on)
    {
        m_tcp = tcp; // 协议锁定为连接类型
        (tcp ? m_tcpBtn : m_rtuBtn)->setChecked(true);
    }
    m_rtuBtn->setEnabled(!on);
    m_tcpBtn->setEnabled(!on);
    m_sendBtn->setEnabled(on);
    m_sendBtn->setToolTip(on ? QString() : tr("连接后可用 (离线生成/解析不受影响)"));
    updatePreview();
}

void TestCenterPage::updateFcHints()
{
    const quint8 fc = quint8(m_fc->currentData().toInt());
    for (const TestCenter::FcInfo &f : TestCenter::kFcTable)
        if (f.code == fc)
            m_fnDesc->setText(QStringLiteral("<b>%1:</b> %2").arg(QString::fromUtf8(f.label),
                                                                  QString::fromUtf8(f.desc)));
    m_fc17Wrap->setVisible(fc == 0x17);
    m_writeWrap->setVisible(TestCenter::isWriteFc(fc) || fc == 0x17);
    if (fc == 0x05)
    {
        m_qtyHint->setText(tr("线圈值: 0 (0000=OFF) 或 65280 (FF00=ON)"));
        m_writeData->setPlaceholderText(tr("FF00 或 0000"));
        m_decHint->setText(tr("非零=ON(FF00)，0=OFF(0000)，转换后填入 数量/值 框"));
    }
    else if (fc == 0x06)
    {
        m_qtyHint->setText(tr("寄存器值: 0-65535 (如 FF00 填 65280)"));
        m_writeData->setPlaceholderText(tr("例如: 00 1E"));
        m_decHint->setText(tr("范围 -32768~65535，转换后填入 数量/值 框"));
    }
    else if (fc == 0x0F)
    {
        m_qtyHint->setText(tr("线圈数量: 1-1968，写入数据字节数 = 数量/8 向上取整"));
        m_writeData->setPlaceholderText(tr("线圈数据字节，如: 03 01 (3个线圈: ON,OFF,ON)"));
        m_decHint->setText(tr("按位打包填入写入数据 (bit0=第一个线圈)，十进制转换上限 64 线圈"));
    }
    else if (fc == 0x10)
    {
        m_qtyHint->setText(tr("寄存器数量: 1-123，写入数据需 数量×2 字节"));
        m_writeData->setPlaceholderText(tr("寄存器数据，如: 00 0A 00 14 (2个寄存器)"));
        m_decHint->setText(tr("整数按数量补齐字节 (大端序)；浮点 IEEE754 float32，≥2 寄存器，余下补 00"));
    }
    else if (fc == 0x17)
    {
        m_qtyHint->setText(tr("读数量: 1-125 (写数量在上方独立框)"));
        m_writeData->setPlaceholderText(tr("写数据，需 写数量×2 字节"));
        m_decHint->setText(tr("十进制转换按写数量展开 (整数大端/浮点 float32)"));
    }
    else
    {
        m_qtyHint->setText(TestCenter::isBitFc(fc) ? tr("读取数量: 1-2000")
                                                   : tr("读取寄存器数量: 1-125"));
    }
}

void TestCenterPage::generate()
{
    TestCenter::BuildSpec s;
    s.slave = m_slave->value();
    s.fc = quint8(m_fc->currentData().toInt());
    s.addr = m_addr->value();
    s.qty = m_qty->value();
    s.addr2 = m_addr2->value();
    s.qty2 = m_qty2->value();
    // 05/06: 十进制框优先 (值语义), 否则写数据框首词 (如 "FF00")
    if (s.fc == 0x05 || s.fc == 0x06)
    {
        const QString d = m_dec->text().trimmed();
        s.writeHex = d.isEmpty() ? m_writeData->toPlainText() : d;
        bool ok = false;
        const qlonglong v = s.writeHex.toLongLong(&ok);
        if (ok)
            s.qty = int(v);
        else
        {
            bool hexOk = false;
            const uint h = s.writeHex.toUInt(&hexOk, 16);
            if (hexOk)
                s.qty = int(h);
        }
    }
    else
    {
        s.writeHex = m_writeData->toPlainText();
    }
    QString errText;
    const QByteArray wire = TestCenter::buildWire(s, &errText);
    if (wire.isEmpty())
    {
        m_tip->showMsg(errText, false);
        return;
    }
    // RTU 直接生成完整帧 (从机 + PDU + CRC16); TCP 无 CRC, MBAP 头由发送构造
    const QByteArray frame = m_tcp ? wire : TestCenter::addCheckRtu(wire);
    m_wireEdit->setPlainText(ToolFrames::hexSpaced(frame));
    m_tip->showMsg(m_tcp ? tr("已生成并填入发送框")
                         : tr("已生成并填入发送框 (完整帧, 含 CRC16)"),
                   true);
    updatePreview();
}

void TestCenterPage::convertDecimal()
{
    const quint8 fc = quint8(m_fc->currentData().toInt());
    const int width = (fc == 0x17) ? m_qty2->value() : m_qty->value();
    QString errText;
    const QString hex = TestCenter::convertDecimalToHex(fc, width, m_dec->text(), &errText);
    if (hex.isEmpty())
    {
        m_tip->showMsg(errText, false);
        return;
    }
    if (fc == 0x05 || fc == 0x06)
    {
        bool ok = false;
        const uint v = hex.toUInt(&ok, 16);
        if (ok)
            m_qty->setValue(int(v));
        m_tip->showMsg(tr("已转换并填入 数量/值: %1").arg(hex), true);
        return;
    }
    m_writeData->setPlainText(hex);
    m_tip->showMsg(tr("已转换: %1 → %2").arg(m_dec->text().trimmed(), hex), true);
}

void TestCenterPage::send()
{
    QString errText;
    const QByteArray wire = TestCenter::parseWireHex(m_wireEdit->toPlainText(), &errText);
    if (wire.isEmpty())
    {
        m_tip->showMsg(errText.isEmpty() ? tr("发送帧为空") : errText, false);
        return;
    }
    if (wire.size() < 2)
    {
        m_tip->showMsg(tr("至少需要 从机地址 + 功能码 2 字节"), false);
        return;
    }
    QString note;
    const QByteArray stripped = TestCenter::stripForSend(m_tcp, wire, &note);
    const int slave = quint8(stripped.at(0));
    if (!m_mb || !m_mb->sendRaw(slave, stripped.mid(1)))
    {
        m_tip->showMsg(tr("发送失败: 未连接或请求被拒绝"), false);
        return;
    }
    m_tip->showMsg(note.isEmpty() ? tr("已发送 → 从机 %1").arg(slave)
                                  : tr("%1 · 已发送 → 从机 %2").arg(note).arg(slave),
                  true);
}

void TestCenterPage::onRawDone(int slave, const QByteArray &respPdu, bool ok,
                               const QString &errText, double elapsedMs)
{
    Q_UNUSED(slave);
    if (!ok && respPdu.isEmpty())
    {
        m_respHex->setText(tr("— %1").arg(errText));
        m_respElapsed->setText(QString());
        fillForm(m_respAnalysis, {});
        return;
    }
    m_respHex->setText(ToolFrames::hexSpaced(respPdu));
    m_respElapsed->setText(tr("耗时 %1 ms · %2")
                               .arg(QString::number(elapsedMs, 'f', 1))
                               .arg(ok ? QStringLiteral("OK") : errText));
    fillForm(m_respAnalysis, TestCenter::analyzeResponse(respPdu));
}

void TestCenterPage::updatePreview()
{
    QString errText;
    const QByteArray wire = TestCenter::parseWireHex(m_wireEdit->toPlainText(), &errText);
    if (wire.isEmpty())
    {
        m_aduHex->setText(tr("—"));
        m_aduSpaced->setText(tr("—"));
        fillForm(m_analysis, {});
        return;
    }
    // 发送框可为完整帧 (含 CRC / MBAP) 或裸线级字节 — 与发送同口径剥离后再包完整 ADU
    const QByteArray body = TestCenter::stripForSend(m_tcp, wire, nullptr);
    const QByteArray adu = m_tcp ? ToolFrames::tcpFrame(body) : TestCenter::addCheckRtu(body);
    m_aduHex->setText(ToolFrames::hexPlain(adu));
    m_aduSpaced->setText(ToolFrames::hexSpaced(adu));
    QVector<TestCenter::Field> rows = TestCenter::analyzeRequest(body);
    if (!m_tcp)
    {
        const quint16 crc = ToolFrames::crc16(body);
        rows.append({QStringLiteral("CRC 校验"),
                     QStringLiteral("0x%1 (低字节在前)")
                         .arg(crc, 4, 16, QLatin1Char('0'))});
    }
    else
    {
        rows.append({QStringLiteral("MBAP"),
                     QStringLiteral("长度 %1 (事务标识符发送时生成)")
                         .arg(body.size())});
    }
    fillForm(m_analysis, rows);
}
