#pragma once
#include <QDialog>
#include "data_loader.h"

class CircularityWidget;
class QTableWidget;
class QLabel;

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
    QLabel *m_cardRoundness = nullptr;
    QLabel *m_cardAvgRadius = nullptr;
    QLabel *m_cardRadiusRange = nullptr;

    CircularityWidget *m_chart = nullptr;
    QTableWidget      *m_table = nullptr;
    QLabel            *m_lblNoData = nullptr;
};
