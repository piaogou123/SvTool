#include "direction_dialog.h"
#include "circularity_widget.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFont>
#include <QCheckBox>

// Row colors aligned with direction-line colors used in the chart.
static const QColor kRowColors[] = {
    QColor(255, 230, 230),  // X  — light red
    QColor(220, 245, 220),  // Y  — light green
    QColor(255, 240, 210),  // D1 — light orange
    QColor(235, 220, 250),  // D2 — light purple
};

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

// -------------------------------------------------------------------------

DirectionDialog::DirectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    // 方向尺寸分析
    setWindowTitle(QString::fromUtf8(
        "\xe6\x96\xb9\xe5\x90\x91\xe5\xb0\xba\xe5\xaf\xb8\xe5\x88\x86\xe6\x9e\x90"));
    resize(980, 880);
    setMinimumSize(640, 540);
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

    // Default placeholder content
    setCardContent(m_cardRoundness,
        QString::fromUtf8("\xe7\x9c\x9f\xe5\x9c\x86\xe5\xba\xa6"),  // 真圆度
        "—", "mm", QColor(220, 60, 60));
    setCardContent(m_cardAvgRadius,
        QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\x8d\x8a\xe5\xbe\x84"),  // 平均半径
        "—", "mm", QColor(30, 110, 200));
    setCardContent(m_cardRadiusRange,
        QString::fromUtf8("\xe5\x8d\x8a\xe5\xbe\x84\xe8\x8c\x83\xe5\x9b\xb4"),  // 半径范围
        "—", "min ~ max", QColor(120, 120, 130));

    cardRow->addWidget(m_cardRoundness, 1);
    cardRow->addWidget(m_cardAvgRadius, 1);
    cardRow->addWidget(m_cardRadiusRange, 1);
    root->addLayout(cardRow);

    // ====== Middle: circularity chart — takes all remaining space =========
    m_chart = new CircularityWidget(this);
    m_chart->setMinimumSize(400, 400);

    // ====== Toolbar: visibility toggles for the chart ====================
    // Colors mirror the trajectory / direction-line colors in the chart.
    QHBoxLayout *toolRow = new QHBoxLayout();
    toolRow->setSpacing(14);

    // 显示:  (label prefix)
    QLabel *toolLbl = new QLabel(
        QString::fromUtf8("\xe6\x98\xbe\xe7\xa4\xba:"), this);  // 显示:
    toolLbl->setStyleSheet("color: #555; font-weight: bold;");
    toolRow->addWidget(toolLbl);

    // 指令(位置) trajectory — orange (matches command path)
    QCheckBox *cbCmd = makeToggle(
        QString::fromUtf8("\xe6\x8c\x87\xe4\xbb\xa4(\xe4\xbd\x8d\xe7\xbd\xae)"),
        QColor(0, 160, 150));
    connect(cbCmd, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setCommandVisible(on); });
    toolRow->addWidget(cbCmd);

    // 反馈 trajectory — blue (matches feedback path)
    QCheckBox *cbFb = makeToggle(
        QString::fromUtf8("\xe5\x8f\x8d\xe9\xa6\x88"),
        QColor(0, 80, 200));
    connect(cbFb, &QCheckBox::toggled,
            this, [this](bool on){ m_chart->setFeedbackVisible(on); });
    toolRow->addWidget(cbFb);

    // Separator
    QFrame *sep = new QFrame(this);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #d0d0d0;");
    toolRow->addWidget(sep);

    // Per-direction size annotation toggles. Dir index matches the chart:
    // 0=X, 1=Y, 2=对角线1, 3=对角线2. Colors mirror kDirClr in the chart.
    const QColor dirClr[4] = {
        QColor(220,  30,  30),   // X  — red
        QColor(  0, 150,   0),   // Y  — green
        QColor(240, 140,   0),   // 对角线1 — orange
        QColor(140,  20, 200),   // 对角线2 — purple
    };
    // 尺寸 suffix
    const QString sizeSfx =
        QString::fromUtf8(" \xe5\xb0\xba\xe5\xaf\xb8");  // " 尺寸"
    const QString diag = QString::fromUtf8("\xe5\xaf\xb9\xe8\xa7\x92\xe7\xba\xbf"); // 对角线
    const QString dirName[4] = {
        "X" + sizeSfx,
        "Y" + sizeSfx,
        diag + "1" + sizeSfx,
        diag + "2" + sizeSfx,
    };
    for (int d = 0; d < 4; ++d) {
        QCheckBox *cb = makeToggle(dirName[d], dirClr[d]);
        connect(cb, &QCheckBox::toggled,
                this, [this, d](bool on){ m_chart->setSizeVisible(d, on); });
        toolRow->addWidget(cb);
    }

    toolRow->addStretch(1);
    root->addLayout(toolRow);

    root->addWidget(m_chart, 1);

    // ====== Bottom: "no data" placeholder ================================
    m_lblNoData = new QLabel(
        // 请先加载 CSV 文件
        QString::fromUtf8(
            "\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd CSV \xe6\x96\x87\xe4\xbb\xb6"),
        this);
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #999; font-size: 13px; padding: 6px;");
    root->addWidget(m_lblNoData, 0);

    // ====== Bottom: focused 4-column table ===============================
    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColCount);

    QStringList hdrs;
    // 方向
    hdrs << QString::fromUtf8("\xe6\x96\xb9\xe5\x90\x91");
    // 指令尺寸 (mm)
    hdrs << QString::fromUtf8(
        "\xe6\x8c\x87\xe4\xbb\xa4\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 反馈尺寸 (mm)
    hdrs << QString::fromUtf8(
        "\xe5\x8f\x8d\xe9\xa6\x88\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 尺寸偏差 (mm)
    hdrs << QString::fromUtf8(
        "\xe5\xb0\xba\xe5\xaf\xb8\xe5\x81\x8f\xe5\xb7\xae (mm)");

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
        const QColor bg = kRowColors[row % 4];

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
    if (!m_chart || !m_chart->hasData()) {
        setCardContent(m_cardRoundness,
            QString::fromUtf8("\xe7\x9c\x9f\xe5\x9c\x86\xe5\xba\xa6"),
            "—", "mm", QColor(220, 60, 60));
        setCardContent(m_cardAvgRadius,
            QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\x8d\x8a\xe5\xbe\x84"),
            "—", "mm", QColor(30, 110, 200));
        setCardContent(m_cardRadiusRange,
            QString::fromUtf8("\xe5\x8d\x8a\xe5\xbe\x84\xe8\x8c\x83\xe5\x9b\xb4"),
            "—", "min ~ max (mm)", QColor(120, 120, 130));
        return;
    }

    double rd  = m_chart->roundness();
    double avg = m_chart->avgRadius();
    double rmn = m_chart->minRadius();
    double rmx = m_chart->maxRadius();

    setCardContent(m_cardRoundness,
        QString::fromUtf8("\xe7\x9c\x9f\xe5\x9c\x86\xe5\xba\xa6"),
        QString::number(rd, 'f', 4),
        QString::fromUtf8("mm  (\xe6\x9c\x80\xe5\xa4\xa7\xe2\x88\x92\xe6\x9c\x80\xe5\xb0\x8f\xe5\x8d\x8a\xe5\xbe\x84)"),  // mm (最大−最小半径)
        QColor(220, 60, 60), QColor(180, 40, 40));

    setCardContent(m_cardAvgRadius,
        QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\x8d\x8a\xe5\xbe\x84"),
        QString::number(avg, 'f', 4),
        QString::fromUtf8("mm  (\xe5\x8f\x8d\xe9\xa6\x88\xe8\xbd\xa8\xe8\xbf\xb9)"),  // mm (反馈轨迹)
        QColor(30, 110, 200), QColor(30, 90, 170));

    setCardContent(m_cardRadiusRange,
        QString::fromUtf8("\xe5\x8d\x8a\xe5\xbe\x84\xe8\x8c\x83\xe5\x9b\xb4"),
        QString("%1 ~ %2").arg(rmn, 0, 'f', 4).arg(rmx, 0, 'f', 4),
        "mm",
        QColor(120, 120, 130), QColor(60, 60, 70));
}

// -------------------------------------------------------------------------
// Public: update everything from a new dataset
// -------------------------------------------------------------------------

void DirectionDialog::updateData(const Dataset &data)
{
    m_chart->setData(data);
    updateMetricCards();

    QVector<DirectionStats> stats = DataLoader::computeDirectionStats(data);
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
