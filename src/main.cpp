// ============================================================================
// main.cpp — Modbus 调试工具 (主站) 独立程序入口
//   自 WeightTool 称重测力调试软件拆分的独立项目 (仓库 modbustools)。
//   QSettings 应用身份 ModbusTools; 首次启动从主程序遗留设置搬入
//   mbtool/ 连接参数与语言 (见 AppSettings::migrateLegacyIfNeeded)。
//   观感基座 (浅色锁定/Fusion/QSS/px 字号) 与拆分前保持一致。
// ============================================================================

#include "AppSettings.h"
#include "ModbusMasterTool.h"
#include "Theme.h"

#include <QApplication>
#include <QColor>
#include <QFile>
#include <QIcon>
#include <QLocale>
#include <QPalette>
#include <QStyleHints>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/icon/modbustools.png"));
    // QSettings 应用名保持历史名 ModbusTools (组织名 ST072G): 老用户设置迁移与
    // 工作区文件兼容依赖它; 产品更名为 STModbusTools 不改此内部标识
    app.setApplicationName("ModbusTools");
    app.setOrganizationName("ST072G");
    AppSettings::migrateLegacyIfNeeded();

    // ---- 固定浅色配色 (必须在窗口构造前) ----
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);

    // ---- 统一观感基座: Fusion + 全局 QSS (resources/app.qss) + Design Token ----
    QApplication::setStyle(QStringLiteral("Fusion"));
    {
        QPalette pal = app.palette();
        pal.setColor(QPalette::Window, QColor(Theme::Bg));
        pal.setColor(QPalette::Base, QColor(Theme::Panel));
        pal.setColor(QPalette::Text, QColor(Theme::Text));
        pal.setColor(QPalette::WindowText, QColor(Theme::Text));
        pal.setColor(QPalette::ButtonText, QColor(Theme::Text));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#9aa3ad"));
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#9aa3ad"));
        app.setPalette(pal);
        QFile qss(QStringLiteral(":/resources/app.qss"));
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
            app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    }

    // ---- 界面语言: 中文源码, English 经 ModbusTools_en.qm 提供 ----
    const QString lang = AppSettings::language();
    const bool wantEn = (lang == QLatin1String("en"))
                        || (lang.isEmpty()
                            && QLocale::system().language() != QLocale::Chinese);
    QTranslator appTranslator;
    if (wantEn
        && (appTranslator.load(QStringLiteral("ModbusTools_en"),
                               QCoreApplication::applicationDirPath())
            || appTranslator.load(QStringLiteral("ModbusTools_en"),
                                  QCoreApplication::applicationDirPath()
                                      + QStringLiteral("/../Resources"))))
        app.installTranslator(&appTranslator);

    // 字号统一 96dpi 基准 px
    QFont f = app.font();
    f.setPixelSize(13);
    app.setFont(f);

    ModbusMasterTool w;
    w.resize(1080, 760);
    w.show();

    return app.exec();
}
