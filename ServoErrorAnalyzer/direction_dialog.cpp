#include "direction_dialog.h"

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>

// Row background colors (semi-transparent tints matching chart axis colors)
static const QColor kRowColors[] = {
    QColor(255, 220, 220),   // X  — light red
    QColor(210, 240, 210),   // Y  — light green
    QColor(255, 240, 205),   // D1 — light orange
    QColor(230, 215, 250),   // D2 — light purple
};

DirectionDialog::DirectionDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowTitle(QString::fromUtf8(
        "\xe6\x96\xb9\xe5\x90\x91\xe5\xb0\xba\xe5\xaf\xb8\xe5\x88\x86\xe6\x9e\x90"));
    resize(780, 200);
}

void DirectionDialog::setupUi()
{
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);

    m_lblNoData = new QLabel(
        QString::fromUtf8("\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd CSV \xe6\x96\x87\xe4\xbb\xb6"));
    m_lblNoData->setAlignment(Qt::AlignCenter);
    m_lblNoData->setStyleSheet("color: #888; font-size: 14px;");
    lay->addWidget(m_lblNoData);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);

    QStringList headers;
    headers << QString::fromUtf8("\xe6\x96\xb9\xe5\x90\x91")           // 方向
            << QString::fromUtf8("\xe5\xb0\xba\xe5\xaf\xb8 (mm)")      // 尺寸
            << QString::fromUtf8("\xe6\x9c\x80\xe5\xa4\xa7\xe5\x81\x8f\xe5\xb7\xae (mm)") // 最大偏差
            << QString::fromUtf8("\xe6\x9c\x80\xe5\xb0\x8f\xe5\x81\x8f\xe5\xb7\xae (mm)") // 最小偏差
            << QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\x81\x8f\xe5\xb7\xae (mm)") // 平均偏差
            << QString::fromUtf8("\xe6\xa0\x87\xe5\x87\x86\xe5\xb7\xae (mm)");             // 标准差
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

static QTableWidgetItem *makeCell(const QString &text, const QColor &bg)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setBackground(bg);
    return item;
}

static QTableWidgetItem *makeNumberCell(double val, const QColor &bg, int prec = 4)
{
    return makeCell(QString::number(val, 'f', prec), bg);
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

    // Adjust row height for readability
    for (int row = 0; row < stats.size(); ++row)
        m_table->setRowHeight(row, 34);

    for (int row = 0; row < stats.size(); ++row) {
        const DirectionStats &s = stats[row];
        const QColor bg = kRowColors[row % 4];

        m_table->setItem(row, 0, makeCell(s.name, bg));
        m_table->setItem(row, 1, makeNumberCell(s.size,      bg));
        m_table->setItem(row, 2, makeNumberCell(s.errMax,    bg));
        m_table->setItem(row, 3, makeNumberCell(s.errMin,    bg));
        m_table->setItem(row, 4, makeNumberCell(s.errAvg,    bg));
        m_table->setItem(row, 5, makeNumberCell(s.errStdDev, bg));
    }

    // Shrink dialog height to fit content
    int contentHeight = m_table->horizontalHeader()->height()
                        + stats.size() * 34 + 8;
    setMinimumHeight(contentHeight + 60);
    resize(width(), contentHeight + 60);
}
