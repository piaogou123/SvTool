#pragma once
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include "data_loader.h"

class ChartView;
class DirectionDialog;
class QCheckBox;
class QComboBox;
class QLabel;
class QScrollArea;
class QToolBar;
class QVBoxLayout;
class QHBoxLayout;
class QWidget;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void onOpenFile();
    void onFileDropped(const QString &path);
    void onCursorMoved(double time, int idx, const QMap<QString, double> &values);
    void onVisibilityChanged();
    void onModeChanged(int index);
    void onShowDirectionAnalysis();

private:
    void loadFile(const QString &filePath);
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupCursorDock();
    void setupResponseDock();
    void setupStatusBar();
    void updateResponseAt(int idx);
    void rebuildAxisWidgets(const QStringList &axisNames);
    void rebuildStatsHtml();
    QString unitFor(const QString &axis) const;   // 当前模式下的单位文本

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    ChartView  *m_chartView;
    QLabel     *m_lblCursor;
    QLabel     *m_lblFileInfo;

    // Dynamic per-axis toolbar widgets
    QToolBar    *m_toolBar;
    QComboBox   *m_modeCombo;        // 位置误差 / 速度误差
    QWidget     *m_chkContainer;
    QHBoxLayout *m_chkLayout;
    QMap<QString, QCheckBox*> m_chkShow;

    // Response-time panel
    QLabel      *m_lblRespTitle;
    QScrollArea *m_scrollResp;
    QWidget     *m_respContainer;
    QVBoxLayout *m_respLayout;
    QMap<QString, QLabel*> m_lblResp;
    QMap<QString, QString> m_statsHtml;   // 每轴统计 HTML(载入时生成一次)

    // Direction analysis dialog (non-modal, created on first use)
    DirectionDialog *m_dirDialog;
    QAction         *m_dirAct;

    // Data
    Dataset    m_curData;
};
