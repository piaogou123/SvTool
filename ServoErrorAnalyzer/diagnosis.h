#pragma once
#include <QString>
#include <QVector>

#include "data_loader.h"
#include "circle_analysis.h"
#include "servo_params.h"

// 单轴等效位置环增益估计。
// "等效"指包含速度前馈与速度环动态的综合效果,并非驱动器参数本身;
// 两个独立估计器互相校验,不一致时在报告中明确提示。
struct AxisKvEstimate {
    QString axis;

    // 估计器 1:误差-速度回归(err = tau * v,过原点最小二乘)
    bool   regValid = false;   // 样本足够且 R² ≥ 下限
    double kvReg = 0;          // 1/s
    double tauMs = 0;          // 回归斜率(等效时滞,ms)
    double r2 = 0;             // 决定系数(过原点定义)
    int    samples = 0;        // 参与回归的采样数

    // 估计器 2:响应时滞(Kv ≈ 1/中位响应延迟)
    bool   lagValid = false;
    double kvLag = 0;          // 1/s
    double medianLagMs = 0;

    // 两估计器一致性(相对差 < 30%)
    bool   consistent = false;

    // 录入参数对比(已换算 SI)
    bool   hasEntered = false;
    double kvEntered = 0;      // 1/s
    double ffEntered = 0;      // %(未录入按 0)
    bool   hasExpected = false;
    double kvExpected = 0;     // 期望等效 = kvEntered/(1-ff)

    // 推荐使用的实测等效增益(优先回归,其次时滞);0 = 无法估计
    double best() const
    {
        if (regValid) return kvReg;
        if (lagValid) return kvLag;
        return 0;
    }
};

// 圆半径收缩反推等效增益(估计器 3)。
// 理论依据:一阶位置环 G(s)=Kv/(s+Kv) 跟踪角速度 ω 的圆,
// 反馈半径 fbR = refR·Kv/√(Kv²+ω²),反解 Kv = ω·ρ/√(1−ρ²),ρ=fbR/refR。
struct ShrinkEstimate {
    bool   valid = false;
    bool   lowerBoundOnly = false; // 收缩量低于测量下限,只能给出增益下界
    double kv = 0;                 // 1/s(lowerBoundOnly 时为下界值)
    double deltaR = 0;             // refR - fbR (mm)
    double feedMmMin = 0;          // 圆弧段平均进给
    double omega = 0;              // rad/s
    double coverageDeg = 0;        // 圆弧角度覆盖(部分圆弧时拟合敏感)
    QString note;                  // 无法估计时的原因说明
};

struct DiagnosisResult {
    QVector<AxisKvEstimate> axes;
    ShrinkEstimate shrink;
    QString html;   // 完整报告(含安全须知、数据、估计、规则诊断)
};

class Diagnosis {
public:
    // data: 已加载数据;circle: 与真圆度图同源的圆分析结果;
    // params: 用户录入的驱动器参数(可为空,报告按"未录入"降级)
    static DiagnosisResult run(const Dataset &data,
                               const CircleAnalysis &circle,
                               const ServoParams &params,
                               const QString &fileName);

    // 独立估计器(测试用例直接调用验证精度)
    static AxisKvEstimate estimateAxisKv(const QString &name,
                                         const Dataset &data,
                                         const AxisChannel &ch);
    static ShrinkEstimate estimateFromShrink(const CircleAnalysis &circle);
};
