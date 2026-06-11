#include "servo_params.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace servo_brands {

// 单位换算依据(均为各厂商手册的标准单位约定):
//  - FANUC No.1825 伺服环增益:单位 0.01/s(如 3000 → 30/s)
//  - SIEMENS Kv 系数:单位 (m/min)/mm,1 (m/min)/mm = 1000/60 ≈ 16.667/s
//  - 安川 Σ 系列 Pn102 位置环增益:单位 0.1/s(如 400 → 40/s)
//  - 三菱 MR-J4/J5 PB08 位置环增益:单位 rad/s(数值上等同 1/s)
//  - 汇川 SV660/IS620P H08-02 位置环增益:手册标注 Hz,范围 0.0~2000.0,
//    默认 64.0;数值语义即 1/s(跟随误差 = v/H08-02;默认 64 与
//    速度环 40Hz 的经典整定关系 Kp≈1.6×fv 吻合,若按 ×2π 解释会得到
//    402/s 的荒谬默认值),系数 1.0
//  - 科尔摩根 AKD PL.KP:单位 (rev/s)/rev(官方手册 25.29.12 节,
//    默认 100),即 1/s,系数 1.0
//  - 科尔摩根 AKD2G PL.KP:单位 Hz(官方文档:AKD 一代值 = AKD2G 值
//    × 2π),换算 1/s 需 ×2π
static const Brand kBrands[] = {
    { "generic",    QString::fromUtf8("通用 / 其他"),
      QString::fromUtf8("位置环增益"),            "1/s",       1.0 },
    { "fanuc",      QString::fromUtf8("FANUC"),
      QString::fromUtf8("No.1825 伺服环增益"),     "0.01/s",    0.01 },
    { "siemens",    QString::fromUtf8("SIEMENS"),
      QString::fromUtf8("Kv 系数"),               "(m/min)/mm", 1000.0 / 60.0 },
    { "yaskawa",    QString::fromUtf8("安川 Σ 系列"),
      QString::fromUtf8("Pn102 位置环增益"),       "0.1/s",     0.1 },
    { "mitsubishi", QString::fromUtf8("三菱 MR-J4/J5"),
      QString::fromUtf8("PB08 位置环增益"),        "rad/s",     1.0 },
    { "inovance",   QString::fromUtf8("汇川 SV660/IS620P"),
      QString::fromUtf8("H08-02 位置环增益"),
      QString::fromUtf8("Hz(数值≈1/s)"),         1.0 },
    { "akd",        QString::fromUtf8("科尔摩根 AKD"),
      QString::fromUtf8("PL.KP 位置环增益"),       "(rev/s)/rev", 1.0 },
    { "akd2g",      QString::fromUtf8("科尔摩根 AKD2G"),
      QString::fromUtf8("PL.KP 位置环增益"),
      QString::fromUtf8("Hz(×2π → 1/s)"),
      2.0 * 3.14159265358979323846 },
};
static constexpr int kBrandCount = sizeof(kBrands) / sizeof(kBrands[0]);

int count() { return kBrandCount; }

const Brand &at(int i)
{
    if (i < 0 || i >= kBrandCount) i = 0;
    return kBrands[i];
}

int indexOfId(const QString &id)
{
    for (int i = 0; i < kBrandCount; ++i)
        if (kBrands[i].id == id) return i;
    return 0;
}

}  // namespace servo_brands

// -------------------------------------------------------------------------

double ServoParams::gainSI(const QString &axis) const
{
    auto it = axes.find(axis);
    if (it == axes.end() || !it->hasGain) return 0;
    return it->posLoopGain * servo_brands::at(brandIndex).gainToSI;
}

QString ServoParams::sidecarPath(const QString &csvPath)
{
    return csvPath + ".params.json";
}

bool ServoParams::saveTo(const QString &path, QString *err) const
{
    QJsonObject root;
    root["brand"] = servo_brands::at(brandIndex).id;

    QJsonObject axesObj;
    for (auto it = axes.begin(); it != axes.end(); ++it) {
        const AxisParams &p = it.value();
        QJsonObject o;
        if (p.hasGain)          o["posLoopGain"]  = p.posLoopGain;
        if (p.hasFF)            o["velFF"]        = p.velFF;
        if (p.hasVelLoopGain)   o["velLoopGain"]  = p.velLoopGain;
        if (p.hasVelLoopIntMs)  o["velLoopIntMs"] = p.velLoopIntMs;
        if (p.hasFrictionComp)  o["frictionComp"] = p.frictionComp;
        if (p.hasBacklashComp)  o["backlashComp"] = p.backlashComp;
        if (!o.isEmpty())
            axesObj[it.key()] = o;
    }
    root["axes"] = axesObj;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QString::fromUtf8("无法写入参数文件:%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ServoParams::loadFrom(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    brandIndex = servo_brands::indexOfId(root["brand"].toString());
    axes.clear();

    const QJsonObject axesObj = root["axes"].toObject();
    for (auto it = axesObj.begin(); it != axesObj.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        AxisParams p;
        if (o.contains("posLoopGain")) {
            p.posLoopGain = o["posLoopGain"].toDouble();
            p.hasGain = true;
        }
        if (o.contains("velFF")) {
            p.velFF = o["velFF"].toDouble();
            p.hasFF = true;
        }
        if (o.contains("velLoopGain")) {
            p.velLoopGain = o["velLoopGain"].toDouble();
            p.hasVelLoopGain = true;
        }
        if (o.contains("velLoopIntMs")) {
            p.velLoopIntMs = o["velLoopIntMs"].toDouble();
            p.hasVelLoopIntMs = true;
        }
        if (o.contains("frictionComp")) {
            p.frictionComp = o["frictionComp"].toDouble();
            p.hasFrictionComp = true;
        }
        if (o.contains("backlashComp")) {
            p.backlashComp = o["backlashComp"].toDouble();
            p.hasBacklashComp = true;
        }
        axes[it.key()] = p;
    }
    return true;
}
