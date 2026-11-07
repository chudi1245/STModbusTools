#pragma once

// ============================================================================
// ConnDialog.h — F3 连接设置对话框 (对标 Modbus Poll Connection Setup)
//   连接类型 (RTU 串口 / Modbus TCP) + 串口组 / TCP 组 + 响应超时 + 重试;
//   确定 = 编辑回写 (应用与连接由主窗触发); 串口枚举在对话框内完成
// ============================================================================

#include "AppSettings.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ConnDialog {

// 确定 → 写回 c 并返回 true
inline bool edit(QWidget *parent, AppSettings::MbToolConn &c)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("连接设置 (Connection Setup) — F3"));
    auto *root = new QVBoxLayout(&dlg);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    auto *kind = new QComboBox;
    kind->setObjectName(QStringLiteral("f3.kind"));
    kind->addItem(QObject::tr("Modbus RTU 串口"));
    kind->addItem(QObject::tr("Modbus TCP/IP"));
    kind->setCurrentIndex(qBound(0, c.kind, 1));
    form->addRow(QObject::tr("连接类型 (Connect As)"), kind);
    root->addLayout(form);

    // ---- 串口组 ----
    auto *serialBox = new QGroupBox(QObject::tr("串口 (Serial)"));
    auto *sform = new QFormLayout(serialBox);
    sform->setHorizontalSpacing(12);
    auto *sRow = new QHBoxLayout;
    auto *port = new QComboBox;
    port->setObjectName(QStringLiteral("f3.port"));
    port->setMinimumWidth(160);
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
        port->addItem(info.description().isEmpty()
                          ? info.portName()
                          : QStringLiteral("%1 (%2)").arg(info.portName(), info.description()),
                      info.portName());
    if (!c.portName.isEmpty())
    {
        const int pi = port->findData(c.portName);
        if (pi >= 0)
            port->setCurrentIndex(pi);
    }
    sRow->addWidget(port, 1);
    sform->addRow(QObject::tr("端口 (Port)"), sRow);

    auto *baud = new QComboBox;
    baud->setObjectName(QStringLiteral("f3.baud"));
    baud->setEditable(true);
    for (int b : {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200})
        baud->addItem(QString::number(b));
    baud->setCurrentText(QString::number(c.baud));
    sform->addRow(QObject::tr("波特率 (Baud Rate)"), baud);

    auto *parity = new QComboBox;
    parity->setObjectName(QStringLiteral("f3.parity"));
    parity->addItems({QObject::tr("无 (None)"), QObject::tr("偶 (Even)"),
                      QObject::tr("奇 (Odd)")});
    parity->setCurrentIndex(qBound(0, c.parity, 2));
    sform->addRow(QObject::tr("校验 (Parity)"), parity);

    auto *stop = new QComboBox;
    stop->setObjectName(QStringLiteral("f3.stop"));
    stop->addItem(QStringLiteral("1"));
    stop->addItem(QStringLiteral("2"));
    stop->setCurrentIndex(qBound(0, c.stopBits - 1, 1));
    sform->addRow(QObject::tr("停止位 (Stop Bits)"), stop);
    root->addWidget(serialBox);

    // ---- TCP 组 ----
    auto *tcpBox = new QGroupBox(QObject::tr("远程服务器 (Remote Server)"));
    auto *tform = new QFormLayout(tcpBox);
    tform->setHorizontalSpacing(12);
    auto *host = new QLineEdit(c.host);
    host->setObjectName(QStringLiteral("f3.host"));
    tform->addRow(QObject::tr("主机 (IP Address)"), host);
    auto *tcpPort = new QSpinBox;
    tcpPort->setObjectName(QStringLiteral("f3.tcpPort"));
    tcpPort->setRange(1, 65535);
    tcpPort->setValue(c.tcpPort);
    tform->addRow(QObject::tr("端口 (Server Port)"), tcpPort);
    root->addWidget(tcpBox);

    // ---- 高级 ----
    auto *adv = new QHBoxLayout;
    adv->addWidget(new QLabel(QObject::tr("响应超时 (Response Timeout)")));
    auto *timeout = new QSpinBox;
    timeout->setObjectName(QStringLiteral("f3.timeout"));
    timeout->setRange(100, 10000);
    timeout->setValue(c.timeoutMs);
    timeout->setSuffix(QObject::tr(" ms"));
    adv->addWidget(timeout);
    adv->addSpacing(16);
    adv->addWidget(new QLabel(QObject::tr("重试次数 (Retries)")));
    auto *retries = new QSpinBox;
    retries->setObjectName(QStringLiteral("f3.retries"));
    retries->setRange(0, 5);
    retries->setValue(c.retries);
    adv->addWidget(retries);
    adv->addStretch();
    root->addLayout(adv);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    root->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // 类型切换: 互斥显示参数组
    auto syncKind = [serialBox, tcpBox, kind]() {
        serialBox->setVisible(kind->currentIndex() == 0);
        tcpBox->setVisible(kind->currentIndex() == 1);
    };
    QObject::connect(kind, &QComboBox::currentIndexChanged, syncKind);
    syncKind();

    if (dlg.exec() != QDialog::Accepted)
        return false;
    c.kind = kind->currentIndex();
    c.portName = port->currentData().toString();
    c.baud = qBound(300, baud->currentText().toInt(), 256000);
    c.parity = parity->currentIndex();
    c.stopBits = stop->currentIndex() + 1;
    c.host = host->text().trimmed();
    c.tcpPort = tcpPort->value();
    c.timeoutMs = timeout->value();
    c.retries = retries->value();
    return true;
}

} // namespace ConnDialog
