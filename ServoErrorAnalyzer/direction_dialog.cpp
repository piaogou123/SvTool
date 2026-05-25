#include "direction_dialog.h"
#include "circularity_widget.h"

#include <QSplitter>
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
    resize(720, 760);
}

void DirectionDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);

    // ---- Top: circularity chart -----------------------------------------
    m_chart = new CircularityWidget(splitter);
    m_chart->setMinimumSize(400, 360);
    splitter->addWidget(m_chart);

    // ---- Bottom: stats table --------------------------------------------
    QWidget *rightPanel = new QWidget(splitter);
    QVBoxLayout *rlay = new QVBoxLayout(rightPanel);
    rlay->setContentsMargins(0, 4, 0, 0);
    rlay->setSpacing(4);

    // 请先加载 CSV 文件
    m_lblNoData = new QLabel(
        QString::fromUtf8(
            "\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd CSV \xe6\x96\x87\xe4\xbb\xb6"),
        rightPanel);
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #999; font-size: 13px;");
    rlay->addWidget(m_lblNoData);

    m_table = new QTableWidget(rightPanel);
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

    rlay->addWidget(m_table, 1);
    splitter->addWidget(rightPanel);

    // Chart gets ~60% of height, table gets ~40%
    splitter->setSizes({ 420, 280 });
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);

    root->addWidget(splitter, 1);
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

    // Resize table height to fit rows
    int tableH = m_table->horizontalHeader()->height()
                 + stats.size() * 34 + 4;
    m_table->setMaximumHeight(tableH + 20);
}
