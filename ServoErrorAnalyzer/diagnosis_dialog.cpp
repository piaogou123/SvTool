#include "diagnosis_dialog.h"
#include "circle_analysis.h"
#include "diagnosis.h"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

enum Col {
    kColGain    = 0,   // 位置环增益(品牌单位)
    kColGainSI  = 1,   // ≈ 1/s(只读,自动换算)
    kColFF      = 2,   // 速度前馈 %
    kColVelGain = 3,   // 速度环增益(驱动器单位)
    kColVelInt  = 4,   // 速度环积分时间 ms
    kColFric    = 5,   // 摩擦/象限突起补偿
    kColBack    = 6,   // 反向间隙补偿
    kColCount   = 7
};

// 读取单元格数值;空白或非数字返回 false
bool cellValue(const QTableWidget *t, int row, int col, double *out)
{
    const QTableWidgetItem *item = t->item(row, col);
    if (!item) return false;
    const QString s = item->text().trimmed();
    if (s.isEmpty()) return false;
    bool ok = false;
    const double v = s.toDouble(&ok);
    if (ok) *out = v;
    return ok;
}

void setCell(QTableWidget *t, int row, int col, const QString &text)
{
    QTableWidgetItem *item = t->item(row, col);
    if (!item) {
        item = new QTableWidgetItem();
        item->setTextAlignment(Qt::AlignCenter);
        t->setItem(row, col, item);
    }
    item->setText(text);
}

} // namespace

// -------------------------------------------------------------------------

DiagnosisDialog::DiagnosisDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
    setWindowTitle(QString::fromUtf8("伺服诊断报告"));
    resize(1000, 920);
    setMinimumSize(720, 600);
}

void DiagnosisDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // ---- 品牌行 ----------------------------------------------------------
    QHBoxLayout *brandRow = new QHBoxLayout();
    QLabel *brandLbl = new QLabel(QString::fromUtf8("驱动器品牌:"), this);
    brandLbl->setStyleSheet("font-weight: bold; color: #555;");
    brandRow->addWidget(brandLbl);

    m_brandCombo = new QComboBox(this);
    for (int i = 0; i < servo_brands::count(); ++i)
        m_brandCombo->addItem(servo_brands::at(i).name);
    connect(m_brandCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DiagnosisDialog::onBrandChanged);
    brandRow->addWidget(m_brandCombo);

    m_brandHint = new QLabel(this);
    m_brandHint->setStyleSheet("color: #888; font-size: 12px;");
    brandRow->addWidget(m_brandHint, 1);
    root->addLayout(brandRow);

    // ---- 参数表 ----------------------------------------------------------
    m_paramTable = new QTableWidget(this);
    m_paramTable->setColumnCount(kColCount);
    m_paramTable->verticalHeader()->setVisible(true);
    m_paramTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_paramTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_paramTable, &QTableWidget::itemChanged,
            this, &DiagnosisDialog::onParamEdited);
    root->addWidget(m_paramTable);

    QLabel *tblHint = new QLabel(QString::fromUtf8(
        "留空 = 未录入(对应诊断自动降级);速度环增益/积分/补偿值按驱动器"
        "自身单位填写,仅在报告中引用,不做换算。参数号请以驱动器手册核对。"),
        this);
    tblHint->setStyleSheet("color: #888; font-size: 11px;");
    tblHint->setWordWrap(true);
    root->addWidget(tblHint);

    // ---- 按钮行 ----------------------------------------------------------
    QHBoxLayout *btnRow = new QHBoxLayout();
    m_btnGenerate = new QPushButton(QString::fromUtf8("生成诊断报告"), this);
    m_btnGenerate->setStyleSheet("font-weight: bold; padding: 6px 18px;");
    connect(m_btnGenerate, &QPushButton::clicked,
            this, &DiagnosisDialog::onGenerate);
    btnRow->addWidget(m_btnGenerate);

    m_btnExport = new QPushButton(QString::fromUtf8("导出报告..."), this);
    m_btnExport->setEnabled(false);
    connect(m_btnExport, &QPushButton::clicked,
            this, &DiagnosisDialog::onExport);
    btnRow->addWidget(m_btnExport);

    QLabel *saveHint = new QLabel(QString::fromUtf8(
        "生成报告时参数自动保存为 <CSV 文件名>.params.json"), this);
    saveHint->setStyleSheet("color: #888; font-size: 11px;");
    btnRow->addWidget(saveHint);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    // ---- 报告 ------------------------------------------------------------
    m_report = new QTextBrowser(this);
    m_report->setOpenExternalLinks(false);
    m_report->setHtml(QString::fromUtf8(
        "<div style='color:#999;'>加载 CSV 后点击\"生成诊断报告\"。</div>"));
    root->addWidget(m_report, 1);

    refreshBrandHint();
}

// -------------------------------------------------------------------------

void DiagnosisDialog::updateData(const Dataset &data, const QString &csvPath)
{
    m_data = data;
    m_csvPath = csvPath;

    rebuildParamTable();

    // 自动加载随文件保存的参数
    ServoParams p;
    if (!csvPath.isEmpty() && p.loadFrom(ServoParams::sidecarPath(csvPath))) {
        m_updating = true;
        m_brandCombo->setCurrentIndex(p.brandIndex);
        m_updating = false;
        populateTable(p);
    }
    refreshBrandHint();

    // 立即生成一次(未录入参数时报告按"未录入"降级,实测估计仍然有效)
    if (!m_data.isEmpty())
        onGenerate();
}

void DiagnosisDialog::rebuildParamTable()
{
    m_updating = true;
    m_paramTable->clear();
    m_paramTable->setColumnCount(kColCount);

    const servo_brands::Brand &b = servo_brands::at(m_brandCombo->currentIndex());
    QStringList hdrs;
    hdrs << QString::fromUtf8("%1\n(%2)").arg(b.gainParam, b.gainUnit);
    hdrs << QString::fromUtf8("≈ 1/s");
    hdrs << QString::fromUtf8("速度前馈\n(%)");
    hdrs << QString::fromUtf8("速度环增益\n(驱动器单位)");
    hdrs << QString::fromUtf8("速度环积分\n(ms)");
    hdrs << QString::fromUtf8("摩擦补偿\n(驱动器单位)");
    hdrs << QString::fromUtf8("反向间隙补偿\n(驱动器单位)");
    m_paramTable->setHorizontalHeaderLabels(hdrs);

    m_paramTable->setRowCount(m_data.axisOrder.size());
    QStringList rowNames;
    for (int r = 0; r < m_data.axisOrder.size(); ++r) {
        rowNames << m_data.axisOrder[r];
        for (int c = 0; c < kColCount; ++c) {
            QTableWidgetItem *item = new QTableWidgetItem();
            item->setTextAlignment(Qt::AlignCenter);
            if (c == kColGainSI) {   // 只读换算列
                item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                item->setBackground(QColor(245, 245, 247));
                item->setForeground(QColor(120, 120, 125));
            }
            m_paramTable->setItem(r, c, item);
        }
    }
    m_paramTable->setVerticalHeaderLabels(rowNames);

    const int rows = std::max(1, m_data.axisOrder.size());
    m_paramTable->setFixedHeight(
        m_paramTable->horizontalHeader()->height() + rows * 30 + 6);
    for (int r = 0; r < m_paramTable->rowCount(); ++r)
        m_paramTable->setRowHeight(r, 30);

    m_updating = false;
}

void DiagnosisDialog::populateTable(const ServoParams &p)
{
    m_updating = true;
    for (int r = 0; r < m_data.axisOrder.size(); ++r) {
        const QString &name = m_data.axisOrder[r];
        if (!p.axes.contains(name)) continue;
        const AxisParams &a = p.axes[name];
        if (a.hasGain)          setCell(m_paramTable, r, kColGain,    QString::number(a.posLoopGain));
        if (a.hasFF)            setCell(m_paramTable, r, kColFF,      QString::number(a.velFF));
        if (a.hasVelLoopGain)   setCell(m_paramTable, r, kColVelGain, QString::number(a.velLoopGain));
        if (a.hasVelLoopIntMs)  setCell(m_paramTable, r, kColVelInt,  QString::number(a.velLoopIntMs));
        if (a.hasFrictionComp)  setCell(m_paramTable, r, kColFric,    QString::number(a.frictionComp));
        if (a.hasBacklashComp)  setCell(m_paramTable, r, kColBack,    QString::number(a.backlashComp));
    }
    m_updating = false;
    refreshGainSI();
}

ServoParams DiagnosisDialog::collectParams() const
{
    ServoParams p;
    p.brandIndex = m_brandCombo->currentIndex();
    for (int r = 0; r < m_data.axisOrder.size(); ++r) {
        AxisParams a;
        double v;
        if (cellValue(m_paramTable, r, kColGain, &v))    { a.posLoopGain = v;  a.hasGain = true; }
        if (cellValue(m_paramTable, r, kColFF, &v))      { a.velFF = v;        a.hasFF = true; }
        if (cellValue(m_paramTable, r, kColVelGain, &v)) { a.velLoopGain = v;  a.hasVelLoopGain = true; }
        if (cellValue(m_paramTable, r, kColVelInt, &v))  { a.velLoopIntMs = v; a.hasVelLoopIntMs = true; }
        if (cellValue(m_paramTable, r, kColFric, &v))    { a.frictionComp = v; a.hasFrictionComp = true; }
        if (cellValue(m_paramTable, r, kColBack, &v))    { a.backlashComp = v; a.hasBacklashComp = true; }
        if (a.hasGain || a.hasFF || a.hasVelLoopGain || a.hasVelLoopIntMs
            || a.hasFrictionComp || a.hasBacklashComp)
            p.axes[m_data.axisOrder[r]] = a;
    }
    return p;
}

void DiagnosisDialog::refreshGainSI()
{
    m_updating = true;
    const double toSI = servo_brands::at(m_brandCombo->currentIndex()).gainToSI;
    for (int r = 0; r < m_paramTable->rowCount(); ++r) {
        double v;
        setCell(m_paramTable, r, kColGainSI,
                cellValue(m_paramTable, r, kColGain, &v)
                    ? QString::number(v * toSI, 'f', 1)
                    : QString());
    }
    m_updating = false;
}

void DiagnosisDialog::refreshBrandHint()
{
    const servo_brands::Brand &b = servo_brands::at(m_brandCombo->currentIndex());
    m_brandHint->setText(QString::fromUtf8(
        "位置环增益按 \"%1\" 单位 %2 录入,自动换算为 1/s 参与诊断")
        .arg(b.gainParam, b.gainUnit));
}

// -------------------------------------------------------------------------

void DiagnosisDialog::onBrandChanged(int)
{
    if (m_updating) return;
    // 更新增益列表头与换算列
    const servo_brands::Brand &b = servo_brands::at(m_brandCombo->currentIndex());
    QTableWidgetItem *hdr = m_paramTable->horizontalHeaderItem(kColGain);
    if (hdr)
        hdr->setText(QString::fromUtf8("%1\n(%2)").arg(b.gainParam, b.gainUnit));
    refreshGainSI();
    refreshBrandHint();
}

void DiagnosisDialog::onParamEdited()
{
    if (m_updating) return;
    refreshGainSI();
}

void DiagnosisDialog::onGenerate()
{
    if (m_data.isEmpty()) {
        m_report->setHtml(QString::fromUtf8(
            "<div style='color:#999;'>请先加载 CSV 文件。</div>"));
        return;
    }

    const ServoParams params = collectParams();

    // 参数随数据文件保存(调参前后各次采集自带当时的参数快照)
    if (!m_csvPath.isEmpty()) {
        QString err;
        if (!params.saveTo(ServoParams::sidecarPath(m_csvPath), &err))
            QMessageBox::warning(this, QString::fromUtf8("保存参数"), err);
    }

    CircleAnalysis circle;
    circle.compute(m_data);

    const DiagnosisResult res = Diagnosis::run(
        m_data, circle, params, QFileInfo(m_csvPath).fileName());
    m_lastHtml = res.html;
    m_report->setHtml(res.html);
    m_btnExport->setEnabled(true);
}

void DiagnosisDialog::onExport()
{
    if (m_lastHtml.isEmpty()) return;

    QString defName = m_csvPath.isEmpty()
        ? QString::fromUtf8("伺服诊断报告.html")
        : m_csvPath + QString::fromUtf8(".诊断报告.html");
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出诊断报告"), defName,
        "HTML (*.html);;All Files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QString::fromUtf8("导出失败"),
            QString::fromUtf8("无法写入文件:%1").arg(path));
        return;
    }
    f.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body>");
    f.write(m_lastHtml.toUtf8());
    f.write("</body></html>");
}
