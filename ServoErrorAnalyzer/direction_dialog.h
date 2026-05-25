#pragma once
#include <QDialog>
#include "data_loader.h"

class CircularityWidget;
class QTableWidget;
class QLabel;
class QSplitter;

class DirectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit DirectionDialog(QWidget *parent = nullptr);
    void updateData(const Dataset &data);

private:
    void setupUi();
    void buildTable(const QVector<DirectionStats> &stats);

    CircularityWidget *m_chart;
    QTableWidget      *m_table;
    QLabel            *m_lblNoData;
};
