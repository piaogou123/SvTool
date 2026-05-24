#include "direction_dialog.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QLabel>
#include <QFont>

// Row background colors matching chart axis palette
static const QColor kRowColors[] = {
    QColor(255, 220, 220),   // X  — light red
    QColor(210, 240, 210),   // Y  — light green
    QColor(255, 240, 205),   // D1 — light orange
    QColor(230, 215, 250),   // D2 — light purple
};

// Column indices
enum Col {
    kColName    = 0,
    kColCmdSize = 1,   // 指令尺寸
    kColFbSize  = 2,   // 反馈尺寸
    kColSizeErr = 3,   // 尺寸偏差 = fbSize - cmdSize
    kColErrMax  = 4,   // 跟随误差 最大
    kColErrMin  = 5,   // 跟随误差 最小
    kColErrAvg  = 6,   // 跟随误差 均值
    kColErrStd  = 7,   // 跟随误差 标准差
    kColCount   = 8
};

DirectionDialog::DirectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    // 方向尺寸分析
    setWindowTitle(QString::fromUtf8(
        "\xe6\x96\xb9\xe5\x90\x91\xe5\xb0\xba\xe5\xaf\xb8\xe5\x88\x86\xe6\x9e\x90"));
    resize(920, 200);
}

void DirectionDialog::setupUi()
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);

    // 请先加载 CSV 文件
    m_lblNoData = new QLabel(
        QString::fromUtf8(
            "\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd CSV \xe6\x96\x87\xe4\xbb\xb6"));
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #888; font-size: 14px;");
    lay->addWidget(m_lblNoData);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(kColCount);

    // Column headers
    QStringList headers;
    // 方向
    headers << QString::fromUtf8("\xe6\x96\xb9\xe5\x90\x91");
    // 指令尺寸 (mm)
    headers << QString::fromUtf8("\xe6\x8c\x87\xe4\xbb\xa4\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 反馈尺寸 (mm)
    headers << QString::fromUtf8("\xe5\x8f\x8d\xe9\xa6\x88\xe5\xb0\xba\xe5\xaf\xb8 (mm)");
    // 尺寸偏差 (mm)
    headers << QString::fromUtf8("\xe5\xb0\xba\xe5\xaf\xb8\xe5\x81\x8f\xe5\xb7\xae (mm)");
    // 跟随误差最大 (mm)
    headers << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe6\x9c\x80\xe5\xa4\xa7 (mm)");
    // 跟随误差最小 (mm)
    headers << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe6\x9c\x80\xe5\xb0\x8f (mm)");
    // 跟随误差均值 (mm)
    headers << QString::fromUtf8(
        "\xe8\xb7\x9f\xe9\x9a\x8f\xe8\xaf\xaf\xe5\xb7\xae\xe5\x9d\x87\xe5\x80\xbc (mm)");
    // 标准差 (mm)
    headers << QString::fromUtf8("\xe6\xa0\x87\xe5\x87\x86\xe5\xb7\xae (mm)");

    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(true);
    m_table->setAlternatingRowColors(false);
    m_table->setStyleSheet(
        "QTableWidget { font-size: 13px; }"
        "QHeaderView::section { font-weight: bold; font-size: 12px; padding: 4px; }");
    m_table->hide();

    lay->addWidget(m_table);
}

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
    QString s = QString::number(val, 'f', prec);
    QTableWidgetItem *item = makeCell(s, bg, bold);
    // Colour sizeErr: red for overshoot, blue for undershoot
    return item;
}

// Size-error cell: highlight red if positive (over), blue if negative (under)
static QTableWidgetItem *makeSizeErrCell(double val, const QColor &bg)
{
    QTableWidgetItem *item = makeNumCell(val, bg, 4, true);
    if (val > 0.0)
        item->setForeground(QColor(180, 0, 0));
    else if (val < 0.0)
        item->setForeground(QColor(0, 0, 200));
    return item;
}

void DirectionDialog::updateData(const Dataset &data)
{
    QVector<DirectionStats> stats = DataLoader::computeDirectionStats(data);

    if (stats.isEmpty()) {
        m_table->hide();
        m_lblNoData->show();
        return;
    }

    m_lblNoData->hide();
    m_table->show();
    m_table->setRowCount(stats.size());

    for (int row = 0; row < stats.size(); ++row)
        m_table->setRowHeight(row, 34);

    for (int row = 0; row < stats.size(); ++row) {
        const DirectionStats &s = stats[row];
        const QColor bg = kRowColors[row % 4];

        m_table->setItem(row, kColName,    makeCell(s.name, bg, true));
        m_table->setItem(row, kColCmdSize, makeNumCell(s.cmdSize,  bg));
        m_table->setItem(row, kColFbSize,  makeNumCell(s.fbSize,   bg));
        m_table->setItem(row, kColSizeErr, makeSizeErrCell(s.sizeErr, bg));
        m_table->setItem(row, kColErrMax,  makeNumCell(s.errMax,   bg));
        m_table->setItem(row, kColErrMin,  makeNumCell(s.errMin,   bg));
        m_table->setItem(row, kColErrAvg,  makeNumCell(s.errAvg,   bg));
        m_table->setItem(row, kColErrStd,  makeNumCell(s.errStdDev, bg));
    }

    // Resize dialog to fit table content
    int contentHeight = m_table->horizontalHeader()->height()
                        + stats.size() * 34 + 8;
    setMinimumHeight(contentHeight + 60);
    resize(width(), contentHeight + 60);
}
