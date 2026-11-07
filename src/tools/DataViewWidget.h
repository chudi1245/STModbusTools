#pragma once

// ============================================================================
// DataViewWidget.h — 读/写定义数据窗口内容 (MDI 子窗口体, 对标 Modbus Poll)
//   窗口体只含数据表格 — 定义参数由主窗 F8 对话框下发 (setDefinition);
//   读窗口: 扫描周期轮询, 双击值单元格 → 写值对话框, 右键批量写;
//   写窗口: 网格内联编辑 (不直发), 扫描速率>0 且启用 = 周期性写网格数据,
//   禁用/速率为 0 时 F6 写一次;
//   错误不落本窗口 — 经 statusMessage 上报主窗状态栏 (Modbus Poll 口径)
// ============================================================================

#include "DefDialogs.h"
#include "MasterClient.h"
#include "PollDefinition.h"
#include "PollTableModel.h"

#include <QSet>
#include <QVector>
#include <QWidget>

#include <functional>

class QFile;
class QEvent;
class QLabel;
class QTableView;
class QTimer;

class DataViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DataViewWidget(MasterClient *mb, QWidget *parent = nullptr);

    const PollDefinition &definition() const { return m_def; }
    void setDefinition(const PollDefinition &d); // F8 下发 (重建网格)
    void setConnected(bool on);
    void setPollPaused(bool on); // 全局扫描暂停 (工具栏 停止)
    void setPlcAddr(bool on);    // 行头地址 PLC 风格 (Base 1 + 区前缀)
    void setAddrInCell(bool on); // Name 列前缀地址
    void readWriteOnce();        // F6: 读窗口读一次 / 写窗口写一次

    // ---- 显示菜单入口 (主窗作用于活动窗口) ----
    //   缩放/值名/变色/格式/样式: 活动窗口表格有选中行时只作用于选中行
    //   (行级覆盖), 无选中 = 窗口级默认; 变化经 definitionAutoChanged 同步
    //   主窗工作区数据; 样式中的列宽/行头宽恒为窗口级
    void editScale();
    void editRules();
    void editAlias();
    void editValueNames();
    void applyDisplayFormat(ModbusFormat::Fmt f, ModbusFormat::ByteOrder o);
    void editCellLook();       // 单元格样式… (字体/行高/背景 + 列宽)
    void clearCellStyles();    // 清除选中行 (无选中 = 全部行) 的显示覆盖
    bool hasRowSelection() const; // 供主窗格式菜单勾选态 (混合态不勾)
    void toggleLogging(); // 记录… (配置 + 开/停)
    void exportTable();   // 导出 CSV (对话框选路径)
    static bool exportCsv(const QString &path, const PollTableModel *model); // 可测

signals:
    void statusMessage(const QString &msg, bool ok); // → 主窗状态栏/提示
    void definitionAutoChanged(const PollDefinition &d); // 出错自动停等被动变化

private slots:
    void pollOnce();
    void onReadDone(int fc, int slave, quint16 addr, bool ok,
                    const QVector<quint16> &values, const QString &errText,
                    double elapsedMs);
    void onWriteDone(int slave, quint16 addr, int count, bool ok,
                     const QString &errText, double elapsedMs);

private:
    bool eventFilter(QObject *watched, QEvent *event) override; // 行头宽拖拽回写
    void buildUi();
    void applyDefinition(); // m_def → 模型/列显隐/几何(行高列宽)/轮询调度
    void updatePolling();
    void openWriteDialog(int row); // 读窗口: 双击 → 写值对话框
    void writeSelected();          // 右键: 写入选中 (批量对话框)
    void startLogging();
    void stopLogging();
    void writeLogRow(const QString &valueCell, bool isErr, const QString &errText);
    QVector<int> selectedRows() const;   // 排序去重的选中行
    void syncDefToMainWindow();          // definitionAutoChanged → 主窗 m_defs
    // 行级覆盖写入: 选中行各写一份 RowStyle (只置 kind 对应 has 位)
    void setRowsFmt(const QVector<int> &rows, ModbusFormat::Fmt f, ModbusFormat::ByteOrder o);
    void setRowsScale(const QVector<int> &rows, bool on, double m, double x1, double y1);
    void setRowsAffix(const QVector<int> &rows, const QString &pfx, const QString &sfx);
    void setRowsRules(const QVector<int> &rows, const PollDefinition::ColorRule r[2]);
    void setRowsVNames(const QVector<int> &rows, const QMap<quint16, QString> &vn);
    void setRowsLook(const QVector<int> &rows, const PollDefinition &look);

    MasterClient *m_mb = nullptr;
    PollDefinition m_def;
    PollTableModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QTimer *m_poll = nullptr;
    bool m_inFlight = false;  // 在途守卫 (多段轮询链同一时刻仅一段在途)
    int m_flightSeg = -1;     // 在途段下标 (单段 = 0; 空闲 = -1)
    double m_cycleMs = 0.0;   // 本轮各段耗时累计 (周期完成时上报)
    bool m_connected = false;
    bool m_pollPaused = false;
    bool m_applyingDef = false; // applyDefinition 程序化下发中 (抑制拖拽回写)
    QSet<int> m_customHeightRows; // 上次下发的自定义行高行 (重建时先清残留)

    // 写对话框在途 (应答路由: OK+自动关 → 关闭; 否则框内显示结果)
    QDialog *m_writeDlg = nullptr;
    std::function<void(bool, const QString &)> m_writeResult;
    int m_writeDlgSlave = 0;
    quint16 m_writeDlgAddr = 0;
    int m_pendingRow = -1;     // 读窗口单点写的乐观上屏行
    QVector<quint16> m_pendingVals;

    // CSV 记录 (Logging)
    DefDialogs::LogConfig m_logCfg;
    QFile *m_logFile = nullptr;
    QTimer *m_logTimer = nullptr;
    QString m_lastLogRow;
};
