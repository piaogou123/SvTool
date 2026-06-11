#pragma once
#include <QVector>
#include "data_loader.h"

// 双轴切圆分析核心(纯数据计算,不依赖 UI):
// 指令轨迹 LSC 圆拟合、进退刀剔除、真圆度指标、四方向卡尺尺寸、
// 换向毛刺检测、圆弧段平均进给速度。
// CircularityWidget(显示)与 Diagnosis(诊断报告)共用同一份实现,
// 保证图上显示的数值与报告中的数值完全一致。
struct CircleAnalysis {
    struct ReversalMark {
        int    idx;   // 毛刺峰值采样下标
        int    axis;  // 0=X 换向, 1=Y 换向
        // 对"反馈 LSC 拟合圆"的径向偏差(mm,正=外凸)。
        // 用反馈自身的拟合圆为基准可消除圆心偏移与尺寸误差的影响,
        // 只度量换向点的局部毛刺(与圆度仪/球杆仪一致);
        // 圆心偏移、尺寸误差由独立指标另行报告。
        double dev;
    };
    struct DirExtent { double angle; double projMin; double projMax; };

    // --- 输入副本(Qt 隐式共享,代价低) ---------------------------------
    QVector<double> time;
    QVector<double> cmdx, cmdy;     // 位置指令
    QVector<double> fx, fy;         // 位置反馈
    QVector<double> cmdVx, cmdVy;   // 速度指令(可能为空)

    // --- 结果 -------------------------------------------------------------
    bool hasXY = false;        // 数据含 X/Y 且样本数足够
    bool circleFitOk = false;  // 指令圆拟合成功(失败 = 非圆轨迹)
    bool filtered = false;     // 是否剔除了进/退刀采样

    QVector<int> onCircle;     // 位于指令圆弧上的采样下标
    double refCx = 0, refCy = 0, refR = 0;   // 基准圆:指令 LSC 拟合
    double fbCx = 0, fbCy = 0, fbR = 0;      // 反馈 LSC 拟合圆
    double minRadius = 0;      // 对反馈 LSC 圆心
    double maxRadius = 0;
    double avgRadius = 0;
    double roundness = 0;      // maxRadius - minRadius
    double maxAbsDev = 0;      // max |r_fb - refR|(对基准圆)
    double dataRange = 1.0;    // 原始数据半视野(绘图用)
    double avgFeed = 0;        // 圆弧段平均合成进给速度(mm/min,仅运动段)
    double arcCoverageDeg = 0; // 圆弧角度覆盖(10° 分箱统计,满圆 = 360)

    QVector<ReversalMark> revs;
    QVector<DirExtent> dirs;          // 四方向反馈投影范围(对基准圆心)
    QVector<DirectionStats> dirStats; // 四方向指令/反馈尺寸

    void compute(const Dataset &data);
    double maxReversalDev() const;    // 无换向点返回 -1

private:
    struct CircleFit { double cx = 0, cy = 0, r = 0; bool ok = false; };
    static CircleFit fitCircle(const QVector<double> &xs,
                               const QVector<double> &ys,
                               const QVector<int> &idx);
    void reset();
    void computeCore();
    void detectReversals();
    void computeFeed();
};
