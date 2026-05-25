#include "direction_dialog.h"
#include "circularity_widget.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>

static const QColor kRowColors[] = {
    QColor(255, 220, 220),  // X  — light red
    QColor(210, 240, 210),  // Y  — light green
    QColor(255, 240, 205),  // D1 — light orange
    QColor(230, 215, 250),  // D2 — light purple
};

enum Col {
    kColName    = 0,
    kColCmdSize = 1,
    kColFbSize  = 2,
    kColSizeErr = 3,
    kColErrMax  = 4,
    kColErrMin  = 5,
    kColErrAvg  = 6,
    kColErrStd  = 7,
    kColCount   = 8
};

// -------------------------------------------------------------------------

DirectionDialog::DirectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    // 方向尺寸分析
    setWindowTitle(QString::fromUtf8(
        "\xe6\x96\xb9\xe5\x90\x91\xe5\xb0\xba\xe5\xaf\xb8\xe5\x88\x86\xe6\x9e\x90"));
    resize(960, 860);
    setMinimumSize(600, 500);
}

void DirectionDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ---- Top: circularity chart — stretch=1, fills all available space ----
    m_chart = new CircularityWidget(this);
    m_chart->setMinimumSize(400, 400);
    root->addWidget(m_chart, 1);   // stretch factor 1: takes all remaining height

    // ---- Bottom: "no data" placeholder ----------------------------------
    // 请先加载 CSV 文件
    m_lblNoData = new QLabel(
        QString::fromUtf8(
            "\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd CSV \xe6\x96\x87\xe4\xbb\xb6"),
        this);
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #999; font-size: 13px;");
    root->addWidget(m_lblNoData, 0);

    // ---- Bottom: stats table — fixed height, no stretch ------------------
    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColCount);

    QStringList hdrs;
    // 方向
    hdrs << QString::fromUtf8("\xe6\x96\xb9\xe5\x90\x91");
    // 指令尺寸 (mm)
    hdrs << QString::fromUtf8("\xe6\x8c\x87\xe4\xbb\xa4\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 反馈尺寸 (mm)
    hdrs << QString::fromUtf8("\xe5\x8f\x8d\xe9\xa6\x88\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 尺寸偏差 (mm)
    hdrs << QString::fromUtf8("\xe5\xb0\xba\xe5\xaf\xb8\xe5\x81\x8f\xe5\xb7\xae (mm)");
    // 跟随误差最大 (mm)
    hdrs << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe6\x9c\x80\xe5\xa4\xa7 (mm)");
    // 跟随误差最小 (mm)
    hdrs << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe6\x9c\x80\xe5\xb0\x8f (mm)");
    // 跟随误差均值 (mm)
    hdrs << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe5\x9d\x87\xe5\x80\xbc (mm)");
    // 标准差 (mm)
    hdrs << QString::fromUtf8("\xe6\xa0\x87\xe5\x87\x86\xe5\xb7\xae (mm)");

    m_table->setHorizontalHeaderLabels(hdrs);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(false);
    m_table->setStyleSheet(
        "QTableWidget { font-size: 12px; }"
        "QHeaderView::section { font-weight: bold; font-size: 11px; padding: 3px; }");
    m_table->hide();

    root->addWidget(m_table, 0);   // stretch factor 0: stays compact at bottom
}

// -------------------------------------------------------------------------
// Data update
// -------------------------------------------------------------------------

static QTableWidgetItem *makeCell(const QString &text, const QColor &bg,
                                   bool bold = false)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setBackground(bg);
    if (bold) {
        QFont f = item->font();
        f.setBold(true);
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
    QTableWidgetItem *item = makeNumCell(val, bg, 4, true);
    if (val > 1e-9)
        item->setForeground(QColor(180, 0, 0));   // red = over
    else if (val < -1e-9)
        item->setForeground(QColor(0, 0, 200));   // blue = under
    return item;
}

void DirectionDialog::buildTable(const QVector<DirectionStats> &stats)
{
    m_table->setRowCount(stats.size());
    for (int row = 0; row < stats.size(); ++row)
        m_table->setRowHeight(row, 34);

    for (int row = 0; row < stats.size(); ++row) {
        const DirectionStats &s = stats[row];
        const QColor bg = kRowColors[row % 4];

        m_table->setItem(row, kColName,    makeCell(s.name, bg, true));
        m_table->setItem(row, kColCmdSize, makeNumCell(s.cmdSize,   bg));
        m_table->setItem(row, kColFbSize,  makeNumCell(s.fbSize,    bg));
        m_table->setItem(row, kColSizeErr, makeSizeErrCell(s.sizeErr, bg));
        m_table->setItem(row, kColErrMax,  makeNumCell(s.errMax,    bg));
        m_table->setItem(row, kColErrMin,  makeNumCell(s.errMin,    bg));
        m_table->setItem(row, kColErrAvg,  makeNumCell(s.errAvg,    bg));
        m_table->setItem(row, kColErrStd,  makeNumCell(s.errStdDev, bg));
    }
}

void DirectionDialog::updateData(const Dataset &data)
{
    m_chart->setData(data);

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
                 + stats.size() * 34 + 4;
    m_table->setFixedHeight(tableH);
}
