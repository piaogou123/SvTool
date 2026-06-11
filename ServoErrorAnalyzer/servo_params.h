#pragma once
#include <QMap>
#include <QString>

// 驱动器品牌表:只做两件事 —— 显示该品牌位置环增益的参数名/单位,
// 以及把录入值换算为 SI 单位(1/s)供诊断计算。
// 注意:参数号以各驱动器手册为准,软件 UI 与报告中均有提示。
namespace servo_brands {

struct Brand {
    QString id;          // 存档用(英文,不随 UI 语言变)
    QString name;        // 显示名
    QString gainParam;   // 位置环增益参数名
    QString gainUnit;    // 录入单位
    double  gainToSI;    // 录入值 × 系数 = 1/s
};

int count();
const Brand &at(int i);
int indexOfId(const QString &id);   // 找不到返回 0(通用)

}  // namespace servo_brands

// 单轴伺服参数(用户录入)。
// posLoopGain/velFF 参与诊断计算;其余仅记录、在报告中引用,
// 单位随驱动器品牌不同,软件不做换算。
struct AxisParams {
    double posLoopGain  = 0;   // 位置环增益(按所选品牌单位)
    double velFF        = 0;   // 速度前馈(%)
    double velLoopGain  = 0;   // 速度环增益(驱动器单位,记录用)
    double velLoopIntMs = 0;   // 速度环积分时间 ms(记录用)
    double frictionComp = 0;   // 摩擦/象限突起补偿(驱动器单位,记录用)
    double backlashComp = 0;   // 反向间隙补偿(驱动器单位,记录用)
    bool   hasGain = false;    // 对应字段是否已录入
    bool   hasFF   = false;
    bool   hasVelLoopGain  = false;
    bool   hasVelLoopIntMs = false;
    bool   hasFrictionComp = false;
    bool   hasBacklashComp = false;
};

// 全部轴的参数 + 品牌,随数据文件存为 sidecar JSON
// (<csv 文件名>.params.json),调参前后各次采集自带当时的参数快照。
struct ServoParams {
    int brandIndex = 0;
    QMap<QString, AxisParams> axes;

    // 位置环增益换算为 1/s;未录入返回 0
    double gainSI(const QString &axis) const;

    bool saveTo(const QString &path, QString *err = nullptr) const;
    bool loadFrom(const QString &path);
    static QString sidecarPath(const QString &csvPath);
};
