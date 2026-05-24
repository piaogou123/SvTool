#pragma once
#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include "data_loader.h"

class ChartView;
class DirectionDialog;
class QCheckBox;
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
    void onCursorMoved(double time, int idx, const QMap<QString, double> &errors);
    void onVisibilityChanged();
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

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    ChartView  *m_chartView;
    QLabel     *m_lblCursor;
    QLabel     *m_lblFileInfo;

    // Dynamic per-axis toolbar widgets
    QToolBar    *m_toolBar;
    QWidget     *m_chkContainer;
    QHBoxLayout *m_chkLayout;
    QMap<QString, QCheckBox*> m_chkShow;

    // Response-time panel
    QLabel      *m_lblRespTitle;
    QScrollArea *m_scrollResp;
    QWidget     *m_respContainer;
    QVBoxLayout *m_respLayout;
    QMap<QString, QLabel*> m_lblResp;

    // Direction analysis dialog (non-modal, created on first use)
    DirectionDialog *m_dirDialog;
    QAction         *m_dirAct;

    // Data
    Dataset    m_curData;
};
