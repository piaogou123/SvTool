#pragma once
#include <QDialog>
#include "data_loader.h"

class QTableWidget;
class QLabel;

class DirectionDialog : public QDialog {
    Q_OBJECT
public:
    explicit DirectionDialog(QWidget *parent = nullptr);
    void updateData(const Dataset &data);

private:
    void setupUi();

    QTableWidget *m_table;
    QLabel       *m_lblNoData;
};
