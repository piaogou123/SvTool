#pragma once
#include <QDialog>
#include "data_loader.h"
#include "servo_params.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

// 伺服诊断对话框:
//  - 驱动器品牌 + 每轴参数录入(位置环增益/前馈/速度环/补偿值)
//  - 参数随 CSV 存 sidecar JSON(<csv>.params.json),自动加载
//  - 生成规则诊断报告(等效增益估计、圆度诊断、参数建议),可导出 HTML
class DiagnosisDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiagnosisDialog(QWidget *parent = nullptr);
    void updateData(const Dataset &data, const QString &csvPath);

private slots:
    void onGenerate();
    void onExport();
    void onBrandChanged(int index);
    void onParamEdited();

private:
    void setupUi();
    void rebuildParamTable();                  // 按当前数据的轴重建行
    void populateTable(const ServoParams &p);  // 把参数填入表格
    ServoParams collectParams() const;         // 从表格读取参数
    void refreshGainSI();                      // 重算 "≈1/s" 列
    void refreshBrandHint();

    Dataset m_data;
    QString m_csvPath;
    QString m_lastHtml;
    bool    m_updating = false;   // 防止程序填表触发 itemChanged

    QComboBox    *m_brandCombo = nullptr;
    QLabel       *m_brandHint  = nullptr;
    QTableWidget *m_paramTable = nullptr;
    QPushButton  *m_btnGenerate = nullptr;
    QPushButton  *m_btnExport   = nullptr;
    QTextBrowser *m_report      = nullptr;
};
