#include "main_window.h"
#include "chart_view.h"
#include "data_loader.h"
#include "direction_dialog.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QScrollArea>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QAction>
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>


static QLabel *makeAxisResponseLabel(int colorIdx)
{
    QLabel *lbl = new QLabel("—");
    lbl->setTextFormat(Qt::RichText);
    lbl->setStyleSheet(QString(
        "QLabel { background: %1; border-radius: 6px; padding: 10px; "
        "color: #fff; font-size: 12px; }"
    ).arg(ChartView::axisColor(colorIdx).name()));
    lbl->setMinimumHeight(110);
    lbl->setWordWrap(true);
    return lbl;
}

// --- MainWindow ----------------------------------------------------------

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_dirDialog(nullptr)
    , m_dirAct(nullptr)
{
    setupUi();
}

void MainWindow::setupUi()
{
    setWindowTitle(QString::fromUtf8(
        "\xe4\xbc\xba\xe6\x9c\x8d\xe8\xaf\xaf\xe5\xb7\xae\xe5\x88\x86\xe6\x9e\x90\xe4\xbb\xaa"));  // 伺服误差分析仪
    resize(1200, 750);
    setAcceptDrops(true);

    m_chartView = new ChartView(this);
    setCentralWidget(m_chartView);

    connect(m_chartView, &ChartView::fileDropped,
            this, &MainWindow::onFileDropped);
    connect(m_chartView, &ChartView::cursorMoved,
            this, &MainWindow::onCursorMoved);

    setupMenuBar();
    setupToolBar();
    setupCursorDock();
    setupResponseDock();
    setupStatusBar();
}

// --- Menu bar ----------------------------------------------------------

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(
        QString::fromUtf8("\xe6\x96\x87\xe4\xbb\xb6(&F)"));  // 文件

    QAction *openAct = fileMenu->addAction(
        QString::fromUtf8("\xe6\x89\x93\xe5\xbc\x80(&O)..."));  // 打开
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFile);

    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction(
        QString::fromUtf8("\xe9\x80\x80\xe5\x87\xba(&X)"));  // 退出
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
}

// --- Toolbar -----------------------------------------------------------

void MainWindow::setupToolBar()
{
    m_toolBar = addToolBar(
        QString::fromUtf8("\xe4\xb8\xbb\xe5\xb7\xa5\xe5\x85\xb7\xe6\xa0\x8f"));  // 主工具栏
    m_toolBar->setMovable(false);

    QAction *openAct = m_toolBar->addAction(
        QString::fromUtf8("\xe6\x89\x93\xe5\xbc\x80"));  // 打开
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_toolBar->addSeparator();

    m_dirAct = m_toolBar->addAction(
        QString::fromUtf8("\xe6\x96\xb9\xe5\x90\x91\xe5\x88\x86\xe6\x9e\x90"));  // 方向分析
    m_dirAct->setEnabled(false);
    connect(m_dirAct, &QAction::triggered,
            this, &MainWindow::onShowDirectionAnalysis);

    m_toolBar->addSeparator();

    // Container for per-axis checkboxes — populated in rebuildAxisWidgets()
    m_chkContainer = new QWidget(m_toolBar);
    m_chkLayout = new QHBoxLayout(m_chkContainer);
    m_chkLayout->setContentsMargins(0, 0, 0, 0);
    m_chkLayout->setSpacing(4);
    m_toolBar->addWidget(m_chkContainer);
}

// --- Cursor dock (bottom) ----------------------------------------------

void MainWindow::setupCursorDock()
{
    QDockWidget *dock = new QDockWidget(
        QString::fromUtf8("\xe5\x85\x89\xe6\xa0\x87\xe4\xbf\xa1\xe6\x81\xaf"), this);  // 光标信息
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *container = new QWidget();
    QHBoxLayout *lay = new QHBoxLayout(container);
    lay->setContentsMargins(8, 4, 8, 4);

    // 在图表上移动鼠标以查看误差值
    m_lblCursor = new QLabel(QString::fromUtf8(
        "\xe5\x9c\xa8\xe5\x9b\xbe\xe8\xa1\xa8\xe4\xb8\x8a\xe7\xa7\xbb\xe5\x8a\xa8"
        "\xe9\xbc\xa0\xe6\xa0\x87\xe4\xbb\xa5\xe6\x9f\xa5\xe7\x9c\x8b\xe8\xaf\xaf"
        "\xe5\xb7\xae\xe5\x80\xbc"));
    m_lblCursor->setWordWrap(true);
    m_lblCursor->setTextFormat(Qt::RichText);
    lay->addWidget(m_lblCursor);
    lay->addStretch();

    dock->setWidget(container);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

// --- Response-time dock (right) ----------------------------------------

void MainWindow::setupResponseDock()
{
    QDockWidget *dock = new QDockWidget(
        QString::fromUtf8("\xe5\x93\x8d\xe5\xba\x94\xe6\x97\xb6\xe9\x97\xb4"), this);  // 响应时间
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *outer = new QWidget();
    QVBoxLayout *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    // 请加载 CSV 文件以查看分析
    m_lblRespTitle = new QLabel(QString::fromUtf8(
        "\xe8\xaf\xb7\xe5\x8a\xa0\xe8\xbd\xbd CSV "
        "\xe6\x96\x87\xe4\xbb\xb6\xe4\xbb\xa5\xe6\x9f\xa5\xe7\x9c\x8b\xe5\x88\x86\xe6\x9e\x90"));
    m_lblRespTitle->setWordWrap(true);
    m_lblRespTitle->setStyleSheet("font-weight: bold; font-size: 13px; color: #555;");
    outerLayout->addWidget(m_lblRespTitle);

    // Scroll area for per-axis response labels
    m_scrollResp = new QScrollArea();
    m_scrollResp->setWidgetResizable(true);
    m_scrollResp->setFrameShape(QFrame::NoFrame);

    m_respContainer = new QWidget();
    m_respLayout = new QVBoxLayout(m_respContainer);
    m_respLayout->setContentsMargins(0, 0, 0, 0);
    m_respLayout->setSpacing(6);
    m_respLayout->addStretch();

    m_scrollResp->setWidget(m_respContainer);
    outerLayout->addWidget(m_scrollResp, 1);

    outer->setMinimumSize(230, 450);
    dock->setWidget(outer);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

// --- Status bar --------------------------------------------------------

void MainWindow::setupStatusBar()
{
    // 未加载文件。请从 文件 > 打开 菜单，或将 CSV 文件拖入图表。
    m_lblFileInfo = new QLabel(QString::fromUtf8(
        "\xe6\x9c\xaa\xe5\x8a\xa0\xe8\xbd\xbd\xe6\x96\x87\xe4\xbb\xb6\xe3\x80\x82"
        "\xe8\xaf\xb7\xe4\xbb\x8e \xe6\x96\x87\xe4\xbb\xb6 > "
        "\xe6\x89\x93\xe5\xbc\x80 \xe8\x8f\x9c\xe5\x8d\x95\xef\xbc\x8c"
        "\xe6\x88\x96\xe5\xb0\x86 CSV "
        "\xe6\x96\x87\xe4\xbb\xb6\xe6\x8b\x96\xe5\x85\xa5\xe5\x9b\xbe\xe8\xa1\xa8\xe3\x80\x82"));
    statusBar()->addWidget(m_lblFileInfo);
}

// --- Rebuild per-axis widgets ------------------------------------------

void MainWindow::rebuildAxisWidgets(const QStringList &axisNames)
{
    // Remove old checkboxes
    QLayoutItem *child;
    while ((child = m_chkLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    m_chkShow.clear();

    // Remove old response labels
    while (m_respLayout->count() > 1) {  // keep the trailing stretch
        QLayoutItem *item = m_respLayout->takeAt(0);
        if (item->widget()) delete item->widget();
        delete item;
    }
    m_lblResp.clear();

    // Create new widgets per axis
    for (int i = 0; i < axisNames.size(); ++i) {
        const QString &name = axisNames[i];
        QColor c = ChartView::axisColor(i);

        // Toolbar checkbox
        QCheckBox *cb = new QCheckBox(name, m_chkContainer);
        cb->setChecked(true);
        cb->setStyleSheet(QString("color: %1; font-weight: bold;").arg(c.name()));
        connect(cb, &QCheckBox::toggled, this, &MainWindow::onVisibilityChanged);
        m_chkLayout->addWidget(cb);
        m_chkShow[name] = cb;

        // Response label (insert before the stretch)
        QLabel *lbl = makeAxisResponseLabel(i);
        m_respLayout->insertWidget(m_respLayout->count() - 1, lbl); // before stretch
        m_lblResp[name] = lbl;
    }
}

// --- Slots -------------------------------------------------------------

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        // 打开 CSV 数据文件
        QString::fromUtf8("\xe6\x89\x93\xe5\xbc\x80 CSV \xe6\x95\xb0\xe6\x8d\xae\xe6\x96\x87\xe4\xbb\xb6"),
        QString(),
        "CSV Files (*.csv);;All Files (*)");
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::onFileDropped(const QString &path)
{
    loadFile(path);
}

void MainWindow::onCursorMoved(double time, int idx,
                               const QMap<QString, double> &errors)
{
    // Bottom panel: error values — dynamic table
    // 时间：
    QString text = QString::fromUtf8(
        "<table><tr><td><b>\xe6\x97\xb6\xe9\x97\xb4\xef\xbc\x9a</b></td>"
        "<td>%1 s</td></tr>").arg(time, 0, 'f', 6);

    for (auto it = errors.begin(); it != errors.end(); ++it) {
        const QString &name = it.key();
        double err = it.value();
        QString unit = (name == "C" || name == "A") ? "deg" : "mm";
        QColor c = m_chartView->isSeriesVisible(name)
                       ? ChartView::axisColor(m_curData.axisOrder.indexOf(name))
                       : QColor(128, 128, 128);
        // %2 误差：
        text += QString::fromUtf8(
            "<tr><td><b style='color:%1'>%2 "
            "\xe8\xaf\xaf\xe5\xb7\xae\xef\xbc\x9a</b></td>"
            "<td>%3 %4</td></tr>")
                    .arg(c.name()).arg(name).arg(err, 0, 'f', 6).arg(unit);
    }
    text += "</table>";
    m_lblCursor->setText(text);

    // Right panel: per-point response details
    updateResponseAt(idx);
}

void MainWindow::onVisibilityChanged()
{
    QCheckBox *cb = qobject_cast<QCheckBox*>(sender());
    if (!cb) return;

    // Find axis name from checkbox
    QString axis;
    for (auto it = m_chkShow.begin(); it != m_chkShow.end(); ++it) {
        if (it.value() == cb) { axis = it.key(); break; }
    }
    if (axis.isEmpty()) return;

    m_chartView->setSeriesVisible(axis, cb->isChecked());
    m_chartView->zoomReset();
}

void MainWindow::loadFile(const QString &filePath)
{
    Dataset data;
    QString error;
    if (!DataLoader::loadCsv(filePath, data, &error)) {
        QMessageBox::warning(this,
            QString::fromUtf8("\xe5\x8a\xa0\xe8\xbd\xbd\xe9\x94\x99\xe8\xaf\xaf"),  // 加载错误
            error);
        return;
    }

    m_curData = data;
    rebuildAxisWidgets(data.axisOrder);

    m_chartView->setAxisData(data);

    m_dirAct->setEnabled(true);
    if (m_dirDialog && m_dirDialog->isVisible())
        m_dirDialog->updateData(m_curData);

    // <b>响应时间</b>  <span>(光标点详情)</span>
    m_lblRespTitle->setText(QString::fromUtf8(
        "<b>\xe5\x93\x8d\xe5\xba\x94\xe6\x97\xb6\xe9\x97\xb4</b>"
        "  <span style='color:#888;font-size:11px;'>"
        "(\xe5\x85\x89\xe6\xa0\x87\xe7\x82\xb9\xe8\xaf\xa6\xe6\x83\x85)"
        "</span>"));

    QFileInfo fi(filePath);
    // 文件：%1  |  %2 个数据点
    m_lblFileInfo->setText(
        QString::fromUtf8("\xe6\x96\x87\xe4\xbb\xb6\xef\xbc\x9a%1  |  %2 "
                          "\xe4\xb8\xaa\xe6\x95\xb0\xe6\x8d\xae\xe7\x82\xb9")
        .arg(fi.fileName()).arg(data.size()));
    // 伺服误差分析仪 - %1
    setWindowTitle(
        QString::fromUtf8("\xe4\xbc\xba\xe6\x9c\x8d\xe8\xaf\xaf\xe5\xb7\xae"
                          "\xe5\x88\x86\xe6\x9e\x90\xe4\xbb\xaa - %1")
        .arg(fi.fileName()));
}

// --- Response panel: per-point detail -----------------------------------

static QString fmtMs(double seconds) {
    return QString::number(seconds * 1000.0, 'f', 3) + " ms";
}

void MainWindow::updateResponseAt(int idx)
{
    if (idx < 0 || idx >= m_curData.size()) return;

    double t0 = m_curData.time[idx];

    for (const QString &name : m_curData.axisOrder) {
        const AxisChannel &ch = m_curData.axes[name];

        int j = ch.bestIdx[idx];
        double t1 = m_curData.time[j];
        double startPos = ch.cmd[idx];
        double endPos   = ch.fb[j];
        double resp     = ch.respLag[idx];

        // %1轴 / 起始时间 / 结束时间 / 起始位置 / 到达位置 / 响应延迟
        QString html = QString::fromUtf8(
            "<div style='font-weight:bold;font-size:14px;margin-bottom:4px;'>"
            "%1 \xe8\xbd\xb4</div>"
            "<table style='font-size:12px;' cellspacing=1>"
            "<tr><td>\xe8\xb5\xb7\xe5\xa7\x8b\xe6\x97\xb6\xe9\x97\xb4</td>"
            "<td align=right><b>%2 s</b></td></tr>"
            "<tr><td>\xe7\xbb\x93\xe6\x9d\x9f\xe6\x97\xb6\xe9\x97\xb4</td>"
            "<td align=right><b>%3 s</b></td></tr>"
            "<tr><td>\xe8\xb5\xb7\xe5\xa7\x8b\xe4\xbd\x8d\xe7\xbd\xae</td>"
            "<td align=right>%4</td></tr>"
            "<tr><td>\xe5\x88\xb0\xe8\xbe\xbe\xe4\xbd\x8d\xe7\xbd\xae</td>"
            "<td align=right>%5</td></tr>"
            "<tr><td>\xe5\x93\x8d\xe5\xba\x94\xe5\xbb\xb6\xe8\xbf\x9f</td>"
            "<td align=right><b>%6</b></td></tr>"
            "</table>"
        ).arg(name)
         .arg(t0, 0, 'f', 6)
         .arg(t1, 0, 'f', 6)
         .arg(startPos, 0, 'f', 6)
         .arg(endPos, 0, 'f', 6)
         .arg(fmtMs(resp));

        QLabel *lbl = m_lblResp.value(name);
        if (lbl) lbl->setText(html);
    }
}

// --- Direction analysis ------------------------------------------------

void MainWindow::onShowDirectionAnalysis()
{
    if (!m_dirDialog) {
        m_dirDialog = new DirectionDialog(this);
    }
    // Always refresh data when showing — ensures a newly loaded file
    // is reflected even if the dialog was hidden when the file was loaded.
    m_dirDialog->updateData(m_curData);
    m_dirDialog->show();
    m_dirDialog->raise();
    m_dirDialog->activateWindow();
}

// --- Drag-and-drop on the main window itself ---------------------------
void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString path = urls.first().toLocalFile();
        if (!path.isEmpty())
            loadFile(path);
    }
}
