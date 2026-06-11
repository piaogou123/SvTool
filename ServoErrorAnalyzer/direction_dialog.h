#pragma once
#include <QDialog>
#include "data_loader.h"

class CircularityWidget;
class QTableWidget;
class QLabel;
class QComboBox;

class DirectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit DirectionDialog(QWidget *parent = nullptr);
    void updateData(const Dataset &data);

private:
    void setupUi();
    void buildTable(const QVector<DirectionStats> &stats);
    void updateMetricCards();

    // Metric card labels (top header)
    QLabel *m_cardRoundness   = nullptr;
    QLabel *m_cardAvgRadius   = nullptr;
    QLabel *m_cardRadiusRange = nullptr;
    QLabel *m_cardReversal    = nullptr;   // 最大换向毛刺

    CircularityWidget *m_chart = nullptr;
    QTableWidget      *m_table = nullptr;
    QLabel            *m_lblNoData = nullptr;
    QComboBox         *m_magCombo = nullptr;   // 误差放大倍数
};
