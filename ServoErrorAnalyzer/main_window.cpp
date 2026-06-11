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
#include <QComboBox>
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

#include <cmath>


static QLabel *makeAxisResponseLabel(int colorIdx)
{
    QLabel *lbl = new QLabel(QString::fromUtf8("—"));
    lbl->setTextFormat(Qt::RichText);
    lbl->setStyleSheet(QString(
        "QLabel { background: %1; border-radius: 6px; padding: 10px; "
        "color: #fff; font-size: 12px; }"
    ).arg(ChartView::axisColor(colorIdx).name()));
    lbl->setMinimumHeight(150);
    lbl->setWordWrap(true);
    return lbl;
}

static QString fmtMs(double seconds)
{
    if (std::isnan(seconds))
        return QString::fromUtf8("—");
    return QString::number(seconds * 1000.0, 'f', 3) + " ms";
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
    setWindowTitle(QString::fromUtf8("伺服误差分析仪"));
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
    QMenu *fileMenu = menuBar()->addMenu(QString::fromUtf8("文件(&F)"));

    QAction *openAct = fileMenu->addAction(QString::fromUtf8("打开(&O)..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFile);

    fileMenu->addSeparator();

    QAction *exitAct = fileMenu->addAction(QString::fromUtf8("退出(&X)"));
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, this, &QWidget::close);
}

// --- Toolbar -----------------------------------------------------------

void MainWindow::setupToolBar()
{
    m_toolBar = addToolBar(QString::fromUtf8("主工具栏"));
    m_toolBar->setMovable(false);

    QAction *openAct = m_toolBar->addAction(QString::fromUtf8("打开"));
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_toolBar->addSeparator();

    m_dirAct = m_toolBar->addAction(QString::fromUtf8("方向分析"));
    m_dirAct->setEnabled(false);
    connect(m_dirAct, &QAction::triggered,
            this, &MainWindow::onShowDirectionAnalysis);

    m_toolBar->addSeparator();

    // 显示模式:位置误差 / 速度误差(文件含速度列时可用)
    QLabel *modeLbl = new QLabel(QString::fromUtf8(" 显示: "), m_toolBar);
    modeLbl->setStyleSheet("color: #555; font-weight: bold;");
    m_toolBar->addWidget(modeLbl);

    m_modeCombo = new QComboBox(m_toolBar);
    m_modeCombo->addItem(QString::fromUtf8("位置误差"));
    m_modeCombo->addItem(QString::fromUtf8("速度误差"));
    m_modeCombo->setEnabled(false);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    m_toolBar->addWidget(m_modeCombo);

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
    QDockWidget *dock = new QDockWidget(QString::fromUtf8("光标信息"), this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *container = new QWidget();
    QHBoxLayout *lay = new QHBoxLayout(container);
    lay->setContentsMargins(8, 4, 8, 4);

    m_lblCursor = new QLabel(
        QString::fromUtf8("在图表上移动鼠标以查看误差值"));
    m_lblCursor->setTextFormat(Qt::RichText);
    m_lblCursor->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // stretch=1: label fills the full width so word-wrap never changes row count
    lay->addWidget(m_lblCursor, 1);

    // Fixed height breaks the layout-feedback loop that causes value jumping:
    //   setText() → dock resizes → ChartView resizes → plotArea changes →
    //   same mouse pixel maps to different time → different index → setText() again
    container->setFixedHeight(92);

    dock->setWidget(container);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

// --- Response-time dock (right) ----------------------------------------

void MainWindow::setupResponseDock()
{
    QDockWidget *dock = new QDockWidget(QString::fromUtf8("响应时间"), this);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *outer = new QWidget();
    QVBoxLayout *outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    m_lblRespTitle = new QLabel(
        QString::fromUtf8("请加载 CSV 文件以查看分析"));
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

    outer->setMinimumSize(250, 450);
    dock->setWidget(outer);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

// --- Status bar --------------------------------------------------------

void MainWindow::setupStatusBar()
{
    m_lblFileInfo = new QLabel(QString::fromUtf8(
        "未加载文件。请从 文件 > 打开 菜单,或将 CSV 文件拖入图表。"));
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

// 每轴全程统计 HTML(载入时生成一次,光标移动时直接拼接)
void MainWindow::rebuildStatsHtml()
{
    m_statsHtml.clear();
    for (const QString &name : m_curData.axisOrder) {
        const AxisChannel &ch = m_curData.axes[name];
        const ResponseStats &st = ch.stats;
        const QString posUnit =
            (name == "C" || name == "A") ? "deg" : "mm";

        QString html = QString::fromUtf8(
            "<div style='font-weight:bold;font-size:14px;margin-bottom:2px;'>"
            "%1 轴</div>"
            "<div style='font-size:11px;opacity:0.85;margin-bottom:2px;'>"
            "── 全程统计 ──</div>").arg(name);

        if (st.valid) {
            html += QString::fromUtf8(
                "<table style='font-size:11px;' cellspacing=1>"
                "<tr><td>平均响应</td><td align=right><b>%1</b></td></tr>"
                "<tr><td>最大响应</td><td align=right>%2</td></tr>"
                "<tr><td>中位数</td><td align=right>%3</td></tr>"
                "<tr><td>标准差</td><td align=right>%4</td></tr>"
                "<tr><td>无响应点</td><td align=right>%5 / %6</td></tr>"
                "<tr><td>最大|误差|</td><td align=right><b>%7 %9</b></td></tr>"
                "<tr><td>RMS 误差</td><td align=right>%8 %9</td></tr>"
                "</table>")
                .arg(fmtMs(st.avg))
                .arg(fmtMs(st.max))
                .arg(fmtMs(st.median))
                .arg(fmtMs(st.stdDev))
                .arg(st.noRespCount)
                .arg(st.movingCount + st.noRespCount)
                .arg(st.maxAbsErr, 0, 'f', 6)
                .arg(st.rmsErr, 0, 'f', 6)
                .arg(posUnit);
        } else {
            html += QString::fromUtf8(
                "<div style='font-size:11px;'><i>无运动段</i></div>");
        }
        m_statsHtml[name] = html;
    }
}

// --- Slots -------------------------------------------------------------

void MainWindow::onOpenFile()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8("打开 CSV 数据文件"),
        QString(),
        "CSV Files (*.csv);;All Files (*)");
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::onFileDropped(const QString &path)
{
    loadFile(path);
}

QString MainWindow::unitFor(const QString &axis) const
{
    const bool deg = (axis == "C" || axis == "A");
    if (m_modeCombo->currentIndex() == 1)   // 速度误差
        return deg ? QString::fromUtf8("deg/min") : QString::fromUtf8("mm/min");
    return deg ? QString::fromUtf8("deg") : QString::fromUtf8("mm");
}

void MainWindow::onCursorMoved(double time, int idx,
                               const QMap<QString, double> &values)
{
    // Bottom panel: error values — dynamic table
    QString text = QString::fromUtf8(
        "<table><tr><td><b>时间:</b></td>"
        "<td>%1 s</td></tr>").arg(time, 0, 'f', 6);

    for (auto it = values.begin(); it != values.end(); ++it) {
        const QString &name = it.key();
        double v = it.value();
        QColor c = m_chartView->isSeriesVisible(name)
                       ? ChartView::axisColor(m_curData.axisOrder.indexOf(name))
                       : QColor(128, 128, 128);
        text += QString::fromUtf8(
            "<tr><td><b style='color:%1'>%2 误差:</b></td>"
            "<td>%3 %4</td></tr>")
                    .arg(c.name()).arg(name)
                    .arg(v, 0, 'f', 6).arg(unitFor(name));
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

    // ChartView 内部决定是否重置视野(用户已缩放则保留当前视野)
    m_chartView->setSeriesVisible(axis, cb->isChecked());
}

void MainWindow::onModeChanged(int index)
{
    m_chartView->setDisplayMode(index == 1 ? ChartView::Mode::VelError
                                           : ChartView::Mode::PosError);
}

void MainWindow::loadFile(const QString &filePath)
{
    Dataset data;
    QString error;
    if (!DataLoader::loadCsv(filePath, data, &error)) {
        QMessageBox::warning(this,
            QString::fromUtf8("加载错误"), error);
        return;
    }

    m_curData = data;
    rebuildAxisWidgets(data.axisOrder);
    rebuildStatsHtml();

    // 速度误差模式仅在文件含速度列时可用
    bool hasVel = false;
    for (const auto &name : data.axisOrder)
        if (data.axes[name].hasVelocity()) { hasVel = true; break; }
    if (!hasVel)
        m_modeCombo->setCurrentIndex(0);
    m_modeCombo->setEnabled(hasVel);

    m_chartView->setAxisData(data);

    m_dirAct->setEnabled(true);
    if (m_dirDialog && m_dirDialog->isVisible())
        m_dirDialog->updateData(m_curData);

    m_lblRespTitle->setText(QString::fromUtf8(
        "<b>响应时间</b>"
        "  <span style='color:#888;font-size:11px;'>"
        "(全程统计 + 光标点详情)</span>"));

    QFileInfo fi(filePath);
    m_lblFileInfo->setText(
        QString::fromUtf8("文件:%1  |  %2 个数据点")
        .arg(fi.fileName()).arg(data.size()));
    setWindowTitle(
        QString::fromUtf8("伺服误差分析仪 - %1").arg(fi.fileName()));
}

// --- Response panel: per-point detail -----------------------------------

void MainWindow::updateResponseAt(int idx)
{
    if (idx < 0 || idx >= m_curData.size()) return;

    double t0 = m_curData.time[idx];

    for (const QString &name : m_curData.axisOrder) {
        const AxisChannel &ch = m_curData.axes[name];

        int j = ch.bestIdx[idx];
        double resp = ch.respLag[idx];
        const bool noResp = std::isnan(resp);

        QString html = m_statsHtml.value(name);
        html += QString::fromUtf8(
            "<div style='font-size:11px;opacity:0.85;margin-top:4px;"
            "margin-bottom:2px;'>── 光标点详情 ──</div>");

        if (noResp) {
            html += QString::fromUtf8(
                "<table style='font-size:12px;' cellspacing=1>"
                "<tr><td>起始时间</td><td align=right><b>%1 s</b></td></tr>"
                "<tr><td>起始位置</td><td align=right>%2</td></tr>"
                "<tr><td>响应延迟</td><td align=right><b>—(未到达)</b></td></tr>"
                "</table>")
                .arg(t0, 0, 'f', 6)
                .arg(ch.cmd[idx], 0, 'f', 6);
        } else {
            html += QString::fromUtf8(
                "<table style='font-size:12px;' cellspacing=1>"
                "<tr><td>起始时间</td><td align=right><b>%1 s</b></td></tr>"
                "<tr><td>结束时间</td><td align=right><b>%2 s</b></td></tr>"
                "<tr><td>起始位置</td><td align=right>%3</td></tr>"
                "<tr><td>到达位置</td><td align=right>%4</td></tr>"
                "<tr><td>响应延迟</td><td align=right><b>%5</b></td></tr>"
                "</table>")
                .arg(t0, 0, 'f', 6)
                .arg(m_curData.time[j], 0, 'f', 6)
                .arg(ch.cmd[idx], 0, 'f', 6)
                .arg(ch.fb[j], 0, 'f', 6)
                .arg(fmtMs(resp));
        }

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
