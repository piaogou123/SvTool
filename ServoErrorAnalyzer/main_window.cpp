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
    setWindowTitle("Servo Error Analyzer");
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
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *openAct = fileMenu->addAction("&Open...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFile);

    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction("E&xit");
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
}

// --- Toolbar -----------------------------------------------------------

void MainWindow::setupToolBar()
{
    m_toolBar = addToolBar("Main");
    m_toolBar->setMovable(false);

    QAction *openAct = m_toolBar->addAction("Open");
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
    QDockWidget *dock = new QDockWidget("Cursor Info", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *container = new QWidget();
    QHBoxLayout *lay = new QHBoxLayout(container);
    lay->setContentsMargins(8, 4, 8, 4);

    m_lblCursor = new QLabel("Move mouse over chart to see error values");
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
    QDockWidget *dock = new QDockWidget("Response Time", this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *outer = new QWidget();
    QVBoxLayout *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    m_lblRespTitle = new QLabel("Load a CSV file to see analysis");
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
    m_lblFileInfo = new QLabel("No file loaded.  File > Open or drag a CSV onto the chart.");
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
        this, "Open CSV Data File", QString(),
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
    QString text = QString("<table><tr><td><b>Time:</b></td><td>%1 s</td></tr>").arg(time, 0, 'f', 6);

    for (auto it = errors.begin(); it != errors.end(); ++it) {
        const QString &name = it.key();
        double err = it.value();
        QString unit = (name == "C" || name == "A") ? "deg" : "mm";
        QColor c = m_chartView->isSeriesVisible(name)
                       ? ChartView::axisColor(m_curData.axisOrder.indexOf(name))
                       : QColor(128, 128, 128);
        text += QString("<tr><td><b style='color:%1'>%2 Error:</b></td><td>%3 %4</td></tr>")
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
        QMessageBox::warning(this, "Load Error", error);
        return;
    }

    m_curData = data;
    rebuildAxisWidgets(data.axisOrder);

    m_chartView->setAxisData(data);

    m_dirAct->setEnabled(true);
    if (m_dirDialog && m_dirDialog->isVisible())
        m_dirDialog->updateData(m_curData);

    m_lblRespTitle->setText(
        "<b>Response Time</b>  <span style='color:#888;font-size:11px;'>(cursor point detail)</span>");

    QFileInfo fi(filePath);
    m_lblFileInfo->setText(QString("File: %1  |  %2 data points")
        .arg(fi.fileName()).arg(data.size()));
    setWindowTitle(QString("Servo Error Analyzer - %1").arg(fi.fileName()));
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

        QString html = QString(
            "<div style='font-weight:bold;font-size:14px;margin-bottom:4px;'>%1 Axis</div>"
            "<table style='font-size:12px;' cellspacing=1>"
            "<tr><td>Start Time</td><td align=right><b>%2 s</b></td></tr>"
            "<tr><td>End Time</td><td align=right><b>%3 s</b></td></tr>"
            "<tr><td>Start Pos</td><td align=right>%4</td></tr>"
            "<tr><td>End Pos</td><td align=right>%5</td></tr>"
            "<tr><td>Response</td><td align=right><b>%6</b></td></tr>"
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
