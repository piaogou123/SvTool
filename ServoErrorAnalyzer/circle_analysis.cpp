#include "circle_analysis.h"
#include "direction_defs.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// 换向毛刺搜索窗口:换向点两侧各 5° 圆弧(随角速度自适应,
// 固定采样数窗口在小半径/高转速圆上会覆盖几十度弧段,
// 把普通跟随误差误报为换向毛刺)。上限 ±200 采样防止退化。
constexpr double kRevAngWinDeg = 5.0;
constexpr int    kRevWindowCap = 200;
}

void CircleAnalysis::reset()
{
    hasXY = false;
    circleFitOk = false;
    filtered = false;
    onCircle.clear();
    refCx = refCy = refR = 0;
    fbCx = fbCy = fbR = 0;
    minRadius = maxRadius = avgRadius = roundness = 0;
    maxAbsDev = 0;
    dataRange = 1.0;
    avgFeed = 0;
    arcCoverageDeg = 0;
    revs.clear();
    dirs.clear();
    dirStats.clear();
}

void CircleAnalysis::compute(const Dataset &data)
{
    reset();
    time.clear();
    cmdx.clear();  cmdy.clear();
    fx.clear();    fy.clear();
    cmdVx.clear(); cmdVy.clear();

    if (!data.axes.contains("X") || !data.axes.contains("Y"))
        return;

    const AxisChannel &chX = data.axes["X"];
    const AxisChannel &chY = data.axes["Y"];
    time  = data.time;
    cmdx  = chX.cmd;
    cmdy  = chY.cmd;
    fx    = chX.fb;
    fy    = chY.fb;
    cmdVx = chX.cmdVel;   // 可能为空
    cmdVy = chY.cmdVel;
    if (fx.size() < 2) return;

    hasXY = true;
    computeCore();
    detectReversals();
    computeFeed();
}

// Kasa 最小二乘圆拟合:解 2x2 正规方程(中心化坐标,数值稳定)
CircleAnalysis::CircleFit CircleAnalysis::fitCircle(
    const QVector<double> &xs, const QVector<double> &ys,
    const QVector<int> &idx)
{
    CircleFit fit;
    const int m = idx.size();
    if (m < 3) return fit;

    double mx = 0, my = 0;
    for (int i : idx) { mx += xs[i]; my += ys[i]; }
    mx /= m; my /= m;

    double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (int i : idx) {
        const double u = xs[i] - mx, v = ys[i] - my;
        suu  += u * u;   svv  += v * v;   suv  += u * v;
        suuu += u * u * u;   svvv += v * v * v;
        suvv += u * v * v;   svuu += v * u * u;
    }

    const double det = suu * svv - suv * suv;
    if (std::abs(det) < 1e-12) return fit;   // 共线退化

    const double a = 0.5 * (suuu + suvv);
    const double b = 0.5 * (svvv + svuu);
    const double uc = (a * svv - b * suv) / det;
    const double vc = (b * suu - a * suv) / det;

    fit.cx = mx + uc;
    fit.cy = my + vc;
    fit.r  = std::sqrt(uc * uc + vc * vc + (suu + svv) / m);
    fit.ok = fit.r > 1e-9;
    return fit;
}

void CircleAnalysis::computeCore()
{
    const int n = fx.size();

    QVector<int> all(n);
    for (int i = 0; i < n; ++i) all[i] = i;

    // 按 "到圆心距离与半径之差" 过滤采样下标(以指令位置判定)
    auto filterByCircle = [this](const QVector<int> &src, double cx, double cy,
                                 double r, double tol) {
        QVector<int> out;
        out.reserve(src.size());
        for (int i : src) {
            const double dx = cmdx[i] - cx, dy = cmdy[i] - cy;
            if (std::abs(std::sqrt(dx * dx + dy * dy) - r) <= tol)
                out.append(i);
        }
        return out;
    };

    // --- 1) 指令轨迹圆拟合(迭代剔除进/退刀段) -------------------------
    // 三遍流水线:粗过滤 → 拟合 → 细过滤 → 再拟合 → 最终筛选。
    // 初值中心不可靠时第一遍环带会丢掉真圆弧,因此用两个候选初值
    // (包围盒中点:对部分圆弧更稳;坐标中位数:对进/退刀直线段更稳),
    // 各自跑完流水线后取保留圆弧点更多的结果。
    struct PipeResult {
        bool ok = false;
        CircleFit fit;
        QVector<int> final;
    };
    auto runPipeline = [&](double c0x, double c0y) {
        PipeResult res;
        QVector<double> radii(n);
        for (int i = 0; i < n; ++i) {
            const double dx = cmdx[i] - c0x, dy = cmdy[i] - c0y;
            radii[i] = std::sqrt(dx * dx + dy * dy);
        }
        std::nth_element(radii.begin(), radii.begin() + n / 2, radii.end());
        const double r0 = radii[n / 2];
        if (r0 <= 1e-6) return res;

        QVector<int> idx = filterByCircle(all, c0x, c0y, r0,
                                          std::max(0.05 * r0, 0.2));
        CircleFit fit = fitCircle(cmdx, cmdy, idx);
        if (!fit.ok) return res;

        idx = filterByCircle(all, fit.cx, fit.cy, fit.r,
                             std::max(1e-3 * fit.r, 0.02));
        const CircleFit fit2 = fitCircle(cmdx, cmdy, idx);
        if (fit2.ok) fit = fit2;

        QVector<int> final = filterByCircle(all, fit.cx, fit.cy, fit.r,
                                            std::max(5e-4 * fit.r, 0.01));
        if (final.size() >= 10 && final.size() >= n / 20) {
            res.ok = true;
            res.fit = fit;
            res.final = final;
        }
        return res;
    };

    // 候选 1:包围盒中点
    double minX = cmdx[0], maxX = minX, minY = cmdy[0], maxY = minY;
    for (int i = 1; i < n; ++i) {
        minX = std::min(minX, cmdx[i]); maxX = std::max(maxX, cmdx[i]);
        minY = std::min(minY, cmdy[i]); maxY = std::max(maxY, cmdy[i]);
    }
    PipeResult best = runPipeline((minX + maxX) / 2.0, (minY + maxY) / 2.0);

    // 候选 2:坐标中位数(进/退刀为少数样本时几乎不受其影响)
    {
        QVector<double> xs = cmdx, ys = cmdy;
        std::nth_element(xs.begin(), xs.begin() + n / 2, xs.end());
        std::nth_element(ys.begin(), ys.begin() + n / 2, ys.end());
        PipeResult alt = runPipeline(xs[n / 2], ys[n / 2]);
        if (alt.ok && (!best.ok || alt.final.size() > best.final.size()))
            best = alt;
    }

    bool fitOk = best.ok;
    if (fitOk) {
        refCx = best.fit.cx;
        refCy = best.fit.cy;
        refR  = best.fit.r;
        onCircle = best.final;
        filtered = best.final.size() < n;
    }
    circleFitOk = fitOk;

    if (!fitOk) {
        // 非圆轨迹回退:反馈包围盒中点 + 平均半径,所有点参与
        onCircle = all;
        filtered = false;
        double fMinX = fx[0], fMaxX = fMinX, fMinY = fy[0], fMaxY = fMinY;
        for (int i = 1; i < n; ++i) {
            fMinX = std::min(fMinX, fx[i]); fMaxX = std::max(fMaxX, fx[i]);
            fMinY = std::min(fMinY, fy[i]); fMaxY = std::max(fMaxY, fy[i]);
        }
        refCx = (fMinX + fMaxX) / 2.0;
        refCy = (fMinY + fMaxY) / 2.0;
        double sumR = 0;
        for (int i = 0; i < n; ++i) {
            const double dx = fx[i] - refCx, dy = fy[i] - refCy;
            sumR += std::sqrt(dx * dx + dy * dy);
        }
        refR = sumR / n;
    }

    // --- 2) 反馈轨迹 LSC 拟合 → 真圆度指标 -------------------------------
    const CircleFit fbFit = fitCircle(fx, fy, onCircle);
    fbCx = fbFit.ok ? fbFit.cx : refCx;
    fbCy = fbFit.ok ? fbFit.cy : refCy;

    maxRadius = 0;
    minRadius = std::numeric_limits<double>::max();
    double sumR = 0;
    maxAbsDev = 0;
    for (int i : onCircle) {
        const double dx = fx[i] - fbCx, dy = fy[i] - fbCy;
        const double r = std::sqrt(dx * dx + dy * dy);
        maxRadius = std::max(maxRadius, r);
        minRadius = std::min(minRadius, r);
        sumR += r;

        const double rdx = fx[i] - refCx, rdy = fy[i] - refCy;
        const double dev = std::abs(std::sqrt(rdx * rdx + rdy * rdy) - refR);
        maxAbsDev = std::max(maxAbsDev, dev);
    }
    avgRadius = sumR / onCircle.size();
    roundness = maxRadius - minRadius;
    fbR = fbFit.ok ? fbFit.r : avgRadius;

    // 圆弧角度覆盖(10° 分箱):部分圆弧的半径拟合误差敏感,
    // 收缩法等下游分析需要据此判断结果可信度
    if (circleFitOk) {
        bool bins[36] = { false };
        for (int i : onCircle) {
            const double ang = std::atan2(cmdy[i] - refCy, cmdx[i] - refCx);
            int b = static_cast<int>((ang + dir_defs::kPi)
                                     / (2.0 * dir_defs::kPi) * 36.0);
            if (b < 0) b = 0;
            if (b > 35) b = 35;
            bins[b] = true;
        }
        int cnt = 0;
        for (bool occupied : bins)
            if (occupied) ++cnt;
        arcCoverageDeg = cnt * 10.0;
    }

    // --- 3) 四方向卡尺尺寸(指令/反馈,基于圆弧段) -----------------------
    for (int d = 0; d < dir_defs::kCount; ++d) {
        const double ca = std::cos(dir_defs::angle(d));
        const double sa = std::sin(dir_defs::angle(d));
        double cMin = std::numeric_limits<double>::max(), cMax = -cMin;
        double fMin = cMin, fMax = -cMin;
        for (int i : onCircle) {
            const double pc = (cmdx[i] - refCx) * ca + (cmdy[i] - refCy) * sa;
            const double pf = (fx[i] - refCx) * ca + (fy[i] - refCy) * sa;
            cMin = std::min(cMin, pc); cMax = std::max(cMax, pc);
            fMin = std::min(fMin, pf); fMax = std::max(fMax, pf);
        }
        DirExtent de;
        de.angle   = dir_defs::angle(d);
        de.projMin = fMin;
        de.projMax = fMax;
        dirs.append(de);

        DirectionStats s;
        s.name    = dir_defs::label(d);
        s.cmdSize = cMax - cMin;
        s.fbSize  = fMax - fMin;
        s.sizeErr = s.fbSize - s.cmdSize;
        s.valid   = true;
        dirStats.append(s);
    }

    // --- 4) 原始数据范围(绘图初始视野,含进退刀) -----------------------
    double maxExt = 0;
    for (int i = 0; i < n; ++i) {
        const double fdx = fx[i] - refCx, fdy = fy[i] - refCy;
        const double cdx = cmdx[i] - refCx, cdy = cmdy[i] - refCy;
        maxExt = std::max(maxExt, std::sqrt(fdx * fdx + fdy * fdy));
        maxExt = std::max(maxExt, std::sqrt(cdx * cdx + cdy * cdy));
    }
    dataRange = (maxExt > 0) ? maxExt * 1.2 : 1.0;
}

// 换向毛刺检测:指令速度过零(无速度数据时用指令位置差分)
void CircleAnalysis::detectReversals()
{
    // 非圆轨迹(拟合失败)时换向毛刺指标无意义,直接跳过;
    // 部分圆弧(覆盖 < 300°)上反馈拟合圆的半径/圆心误差敏感,
    // 以其为基准的毛刺数值不可靠,同样跳过
    if (!circleFitOk) return;
    if (arcCoverageDeg < 300.0) return;

    const int n = fx.size();
    QVector<bool> mask(n, false);
    for (int i : onCircle) mask[i] = true;

    for (int axis = 0; axis < 2; ++axis) {
        const QVector<double> &pos = (axis == 0) ? cmdx : cmdy;
        const QVector<double> *vel = (axis == 0) ? &cmdVx : &cmdVy;

        QVector<double> derived;
        if (vel->size() != n) {
            // 无速度列:用指令位置中心差分代替
            derived.resize(n);
            for (int i = 0; i < n; ++i) {
                const int a = std::max(0, i - 1), b = std::min(n - 1, i + 1);
                derived[i] = pos[b] - pos[a];
            }
            vel = &derived;
        }

        double vmax = 0;
        for (int i : onCircle)
            vmax = std::max(vmax, std::abs((*vel)[i]));
        if (vmax <= 0) continue;
        const double eps = vmax * 0.02;

        int lastSign = 0;
        for (int i = 0; i < n; ++i) {
            if (!mask[i]) continue;
            const double v = (*vel)[i];
            const int s = (v > eps) ? 1 : ((v < -eps) ? -1 : 0);
            if (s == 0) continue;
            if (lastSign != 0 && s != lastSign) {
                // 换向点 ±5° 圆弧内找最大径向偏差。
                // 基准 = 反馈 LSC 拟合圆(消除圆心偏移/尺寸误差,
                // 只度量局部毛刺);按指令位置的圆心角筛选,
                // 窗口随角速度自适应。
                const double ang0 =
                    std::atan2(cmdy[i] - refCy, cmdx[i] - refCx);
                const double angWin =
                    kRevAngWinDeg * dir_defs::kPi / 180.0;
                const int lo = std::max(0, i - kRevWindowCap);
                const int hi = std::min(n - 1, i + kRevWindowCap);
                int best = -1;
                double bestDev = 0;
                for (int j = lo; j <= hi; ++j) {
                    if (!mask[j]) continue;
                    const double aj =
                        std::atan2(cmdy[j] - refCy, cmdx[j] - refCx);
                    const double da = std::abs(std::atan2(
                        std::sin(aj - ang0), std::cos(aj - ang0)));
                    if (da > angWin) continue;
                    const double dx = fx[j] - fbCx;
                    const double dy = fy[j] - fbCy;
                    const double dev = std::sqrt(dx * dx + dy * dy) - fbR;
                    if (best < 0 || std::abs(dev) > std::abs(bestDev)) {
                        best = j;
                        bestDev = dev;
                    }
                }
                if (best >= 0)
                    revs.append({ best, axis, bestDev });
            }
            lastSign = s;
        }
    }
}

// 圆弧段平均合成进给速度(mm/min):优先速度指令列,否则位置差分。
// 只统计运动中的采样(速度 ≥ 5% 峰值)—— 圆弧上的暂停/驻留不能
// 稀释平均进给,否则半径收缩反推的 ω 会严重偏小。
void CircleAnalysis::computeFeed()
{
    const int n = fx.size();
    if (onCircle.isEmpty()) return;

    const bool hasVel = (cmdVx.size() == n && cmdVy.size() == n);
    auto speedAt = [&](int i) -> double {
        if (hasVel)
            return std::sqrt(cmdVx[i] * cmdVx[i] + cmdVy[i] * cmdVy[i]);
        if (i > 0 && i < n - 1) {
            const double dt = time[i + 1] - time[i - 1];
            if (dt <= 0) return 0;
            const double dx = cmdx[i + 1] - cmdx[i - 1];
            const double dy = cmdy[i + 1] - cmdy[i - 1];
            return std::sqrt(dx * dx + dy * dy) / dt * 60.0;  // mm/s → mm/min
        }
        return 0;
    };

    double vmax = 0;
    for (int i : onCircle)
        vmax = std::max(vmax, speedAt(i));
    if (vmax <= 0) return;

    const double vmin = 0.05 * vmax;
    double sum = 0;
    int cnt = 0;
    for (int i : onCircle) {
        const double v = speedAt(i);
        if (v < vmin) continue;
        sum += v;
        ++cnt;
    }
    avgFeed = (cnt > 0) ? sum / cnt : 0;
}

double CircleAnalysis::maxReversalDev() const
{
    if (revs.isEmpty()) return -1.0;
    double maxDev = 0;
    for (const ReversalMark &r : revs)
        maxDev = std::max(maxDev, std::abs(r.dev));
    return maxDev;
}
