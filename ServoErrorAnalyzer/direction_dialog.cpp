#include "direction_dialog.h"
#include "circularity_widget.h"
#include "direction_defs.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFont>
#include <QCheckBox>
#include <QComboBox>

enum Col {
    kColName    = 0,
    kColCmdSize = 1,
    kColFbSize  = 2,
    kColSizeErr = 3,
    kColCount   = 4
};

// -------------------------------------------------------------------------
// Metric card helpers
// -------------------------------------------------------------------------

// Build a single metric card with title + value + unit
// accentColor is used for the left border accent and the title text.
static QLabel *makeMetricCard(const QColor &accent)
{
    QLabel *lbl = new QLabel();
    lbl->setTextFormat(Qt::RichText);
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setMinimumHeight(76);
    lbl->setStyleSheet(QString(
        "QLabel { background: white; border: 1px solid #d0d0d0; "
        "border-left: 4px solid %1; border-radius: 6px; padding: 8px 12px; }"
    ).arg(accent.name()));
    return lbl;
}

static void setCardContent(QLabel *card,
                            const QString &title,
                            const QString &value,
                            const QString &unit,
                            const QColor &accent,
                            const QColor &valueColor = QColor(40, 40, 40))
{
    QString html = QString(
        "<div style='text-align:center; line-height:1.2;'>"
        "<div style='color:%1; font-size:11px; font-weight:bold; letter-spacing:1px;'>%2</div>"
        "<div style='color:%5; font-size:22px; font-weight:bold; margin-top:2px;'>%3</div>"
        "<div style='color:#777; font-size:10px; margin-top:1px;'>%4</div>"
        "</div>"
    ).arg(accent.name(), title, value, unit, valueColor.name());
    card->setText(html);
}

// Create a colored, pre-checked toggle checkbox for the chart toolbar.
static QCheckBox *makeToggle(const QString &text, const QColor &color)
{
    QCheckBox *cb = new QCheckBox(text);
    cb->setChecked(true);
    cb->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color.name()));
    return cb;
}

static QFrame *makeVSep(QWidget *parent)
{
    QFrame *sep = new QFrame(parent);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #d0d0d0;");
    return sep;
}

// -------------------------------------------------------------------------

DirectionDialog::DirectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowTitle(QString::fromUtf8("方向尺寸分析"));
    resize(1000, 900);
    setMinimumSize(660, 560);
}

void DirectionDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ====== Top: metric cards row ========================================
    QHBoxLayout *cardRow = new QHBoxLayout();
    cardRow->setSpacing(8);

    m_cardRoundness   = makeMetricCard(QColor(220,  60,  60));   // red
    m_cardAvgRadius   = makeMetricCard(QColor( 30, 110, 200));   // blue
    m_cardRadiusRange = makeMetricCard(QColor(120, 120, 130));   // gray
    m_cardReversal    = makeMetricCard(QColor(240, 140,   0));   // orange

    cardRow->addWidget(m_cardRoundness, 1);
    cardRow->addWidget(m_cardAvgRadius, 1);
    cardRow->addWidget(m_cardRadiusRange, 1);
    cardRow->addWidget(m_cardReversal, 1);
    root->addLayout(cardRow);

    // ====== Middle: circularity chart — takes all remaining space =========
    m_chart = new CircularityWidget(this);
    m_chart->setMinimumSize(400, 400);

    // ====== Toolbar: visibility toggles for the chart ====================
    QHBoxLayout *toolRow = new QHBoxLayout();
    toolRow->setSpacing(14);

    QLabel *toolLbl = new QLabel(QString::fromUtf8("显示:"), this);
    toolLbl->setStyleSheet("color: #555; font-weight: bold;");
    toolRow->addWidget(toolLbl);

    // 指令(位置)轨迹 — 青色虚线
    QCheckBox *cbCmd = makeToggle(
        QString::fromUtf8("指令(位置)"), QColor(0, 160, 150));
    connect(cbCmd, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setCommandVisible(on); });
    toolRow->addWidget(cbCmd);

    // 反馈轨迹 — 蓝色实线
    QCheckBox *cbFb = makeToggle(
        QString::fromUtf8("反馈"), QColor(0, 80, 200));
    connect(cbFb, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setFeedbackVisible(on); });
    toolRow->addWidget(cbFb);

    // 基准圆(指令拟合圆)— 灰色点线
    QCheckBox *cbRef = makeToggle(
        QString::fromUtf8("基准圆"), QColor(120, 120, 125));
    connect(cbRef, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setReferenceVisible(on); });
    toolRow->addWidget(cbRef);

    // 换向毛刺标记
    QCheckBox *cbRev = makeToggle(
        QString::fromUtf8("换向毛刺"), QColor(200, 90, 0));
    connect(cbRev, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setReversalVisible(on); });
    toolRow->addWidget(cbRev);

    toolRow->addWidget(makeVSep(this));

    // 各方向尺寸标注开关(颜色/文案统一取自 dir_defs)
    const QString sizeSfx = QString::fromUtf8(" 尺寸");
    const QString dirName[4] = {
        "X" + sizeSfx,
        "Y" + sizeSfx,
        QString::fromUtf8("对角线1") + sizeSfx,
        QString::fromUtf8("对角线2") + sizeSfx,
    };
    for (int d = 0; d < dir_defs::kCount; ++d) {
        QCheckBox *cb = makeToggle(dirName[d], dir_defs::color(d));
        connect(cb, &QCheckBox::toggled,
                this, [this, d](bool on){ m_chart->setSizeVisible(d, on); });
        toolRow->addWidget(cb);
    }

    toolRow->addWidget(makeVSep(this));

    // 径向误差放大倍数(圆度仪式显示)
    QLabel *magLbl = new QLabel(QString::fromUtf8("误差放大:"), this);
    magLbl->setStyleSheet("color: #555; font-weight: bold;");
    toolRow->addWidget(magLbl);

    m_magCombo = new QComboBox(this);
    const int mags[] = { 1, 5, 10, 20, 50, 100, 200, 500 };
    for (int m : mags)
        m_magCombo->addItem(QString::fromUtf8("×%1").arg(m), m);
    connect(m_magCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                m_chart->setMagnification(
                    m_magCombo->currentData().toDouble());
            });
    toolRow->addWidget(m_magCombo);

    toolRow->addStretch(1);
    root->addLayout(toolRow);

    root->addWidget(m_chart, 1);

    // ====== Bottom: "no data" placeholder ================================
    m_lblNoData = new QLabel(QString::fromUtf8("请先加载 CSV 文件"), this);
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #999; font-size: 13px; padding: 6px;");
    root->addWidget(m_lblNoData, 0);

    // ====== Bottom: focused 4-column table ===============================
    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColCount);

    QStringList hdrs;
    hdrs << QString::fromUtf8("方向");
    hdrs << QString::fromUtf8("指令尺寸 (mm)");
    hdrs << QString::fromUtf8("反馈尺寸 (mm)");
    hdrs << QString::fromUtf8("尺寸偏差 (mm)");

    m_table->setHorizontalHeaderLabels(hdrs);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(false);
    m_table->setStyleSheet(
        "QTableWidget { font-size: 13px; border: 1px solid #d0d0d0; }"
        "QHeaderView::section { background: #f5f5f7; font-weight: bold; "
        "font-size: 12px; padding: 6px; border: none; border-right: 1px solid #d0d0d0; }");
    m_table->hide();

    root->addWidget(m_table, 0);

    updateMetricCards();
}

// -------------------------------------------------------------------------
// Cell helpers
// -------------------------------------------------------------------------

static QTableWidgetItem *makeCell(const QString &text, const QColor &bg,
                                   bool bold = false, int fontPt = 0)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setBackground(bg);
    if (bold || fontPt > 0) {
        QFont f = item->font();
        if (bold)       f.setBold(true);
        if (fontPt > 0) f.setPointSize(fontPt);
        item->setFont(f);
    }
    return item;
}

static QTableWidgetItem *makeNumCell(double val, const QColor &bg,
                                      int prec = 4, bool bold = false)
{
    return makeCell(QString::number(val, 'f', prec), bg, bold);
}

static QTableWidgetItem *makeSizeErrCell(double val, const QColor &bg)
{
    // Prefix sign explicitly so positive/negative is unambiguous
    QString txt = (val >= 0 ? "+" : "") + QString::number(val, 'f', 4);
    QTableWidgetItem *item = makeCell(txt, bg, true);
    if (val > 1e-9)
        item->setForeground(QColor(200, 0, 0));    // red = oversized
    else if (val < -1e-9)
        item->setForeground(QColor(0, 80, 200));   // blue = undersized
    else
        item->setForeground(QColor(60, 60, 60));
    return item;
}

void DirectionDialog::buildTable(const QVector<DirectionStats> &stats)
{
    m_table->setRowCount(stats.size());
    const int rowH = 38;
    for (int row = 0; row < stats.size(); ++row)
        m_table->setRowHeight(row, rowH);

    for (int row = 0; row < stats.size(); ++row) {
        const DirectionStats &s = stats[row];
        // 行底色:方向色向白色混合 88%
        const QColor bg = dir_defs::blendWhite(
            dir_defs::color(row % dir_defs::kCount), 88);

        m_table->setItem(row, kColName,    makeCell(s.name, bg, true, 11));
        m_table->setItem(row, kColCmdSize, makeNumCell(s.cmdSize,   bg));
        m_table->setItem(row, kColFbSize,  makeNumCell(s.fbSize,    bg));
        m_table->setItem(row, kColSizeErr, makeSizeErrCell(s.sizeErr, bg));
    }
}

// -------------------------------------------------------------------------
// Metric cards
// -------------------------------------------------------------------------

void DirectionDialog::updateMetricCards()
{
    const bool has = m_chart && m_chart->hasData();

    setCardContent(m_cardRoundness,
        QString::fromUtf8("真圆度"),
        has ? QString::number(m_chart->roundness(), 'f', 4) : QString::fromUtf8("—"),
        QString::fromUtf8("mm  (最大−最小半径, LSC)"),
        QColor(220, 60, 60), QColor(180, 40, 40));

    setCardContent(m_cardAvgRadius,
        QString::fromUtf8("平均半径"),
        has ? QString::number(m_chart->avgRadius(), 'f', 4) : QString::fromUtf8("—"),
        QString::fromUtf8("mm  (反馈轨迹)"),
        QColor(30, 110, 200), QColor(30, 90, 170));

    setCardContent(m_cardRadiusRange,
        QString::fromUtf8("半径范围"),
        has ? QString("%1 ~ %2")
                  .arg(m_chart->minRadius(), 0, 'f', 4)
                  .arg(m_chart->maxRadius(), 0, 'f', 4)
            : QString::fromUtf8("—"),
        "mm",
        QColor(120, 120, 130), QColor(60, 60, 70));

    const double rev = has ? m_chart->maxReversalDev() : -1.0;
    setCardContent(m_cardReversal,
        QString::fromUtf8("最大换向毛刺"),
        rev >= 0 ? QString::number(rev, 'f', 4) : QString::fromUtf8("—"),
        QString::fromUtf8("mm  (换向点±5°,对反馈拟合圆)"),
        QColor(240, 140, 0), QColor(200, 110, 0));
}

// -------------------------------------------------------------------------
// Public: update everything from a new dataset
// -------------------------------------------------------------------------

void DirectionDialog::updateData(const Dataset &data)
{
    m_chart->setData(data);
    updateMetricCards();

    const QVector<DirectionStats> stats = m_chart->directionStats();
    if (stats.isEmpty()) {
        m_table->hide();
        m_lblNoData->show();
        return;
    }

    m_lblNoData->hide();
    m_table->show();
    buildTable(stats);

    // Fix table height to exactly fit its rows (no wasted space)
    int tableH = m_table->horizontalHeader()->height()
                 + stats.size() * 38 + 4;
    m_table->setFixedHeight(tableH);
}
