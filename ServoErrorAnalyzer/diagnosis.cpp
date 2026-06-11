#include "diagnosis.h"

#include <QDateTime>
#include <QStringList>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// 诊断阈值(全部集中定义,报告"方法与局限性"一节中向用户公开)
// ---------------------------------------------------------------------------
namespace {

// 回归估计:参与回归的最小样本数 / R² 可信下限 / R² 有效下限
constexpr int    kMinRegSamples = 100;
constexpr double kR2Reliable    = 0.7;
constexpr double kR2Min         = 0.5;
// 回归只取 |v| ≥ 10% vmax 的样本(避开换向/静止段的摩擦影响)
constexpr double kVelCutRatio   = 0.10;
// 两估计器一致性判定:相对差 < 30%
constexpr double kEstimateAgree = 0.30;
// 双轴等效增益失配阈值
constexpr double kMismatchWarn   = 1.3;
constexpr double kMismatchSevere = 2.0;
// 录入参数与实测等效偏差阈值
constexpr double kExpectMismatch = 0.40;
// 半径收缩:有效判定下限 / 测量噪声下限(0.5 µm)
constexpr double kShrinkMinMm   = 0.002;
constexpr double kShrinkFloorMm = 0.0005;
// 收缩法要求的最小圆弧覆盖角(部分圆弧的 LSC 半径拟合误差敏感)
constexpr double kMinArcCoverageDeg = 300.0;
// 收缩法与回归法交叉校验:允许的最大倍数偏差
constexpr double kCrossCheckFactor = 2.0;
// 换向毛刺判定阈值(5 µm)
constexpr double kRevSpikeMm = 0.005;
// 方向尺寸/对角线差判定阈值(5 µm)
constexpr double kSizeErrMm = 0.005;
// 无响应点比例警告阈值
constexpr double kNoRespRatio = 0.10;

QString num(double v, int prec = 1)
{
    return QString::number(v, 'f', prec);
}

QString um(double mm)   // mm → µm 显示
{
    return QString::number(mm * 1000.0, 'f', 1);
}

// 诊断条目:level 0=信息(蓝) 1=建议(橙) 2=重要(红)
QString findingHtml(int level, const QString &title, const QString &body)
{
    const char *bg;
    const char *tagClr;
    QString tag;
    switch (level) {
    case 2:  bg = "#fdecea"; tagClr = "#c0392b"; tag = QString::fromUtf8("重要"); break;
    case 1:  bg = "#fff4e5"; tagClr = "#b9770e"; tag = QString::fromUtf8("建议"); break;
    default: bg = "#eef4fb"; tagClr = "#2471a3"; tag = QString::fromUtf8("信息"); break;
    }
    return QString(
        "<table width='100%' cellpadding='6' cellspacing='0' bgcolor='%1' "
        "style='margin-top:6px;'><tr><td>"
        "<b><span style='color:%2;'>[%3]</span> %4</b><br>%5"
        "</td></tr></table>")
        .arg(bg).arg(tagClr).arg(tag).arg(title).arg(body);
}

// 调增益类建议统一附带的安全提示
QString gainSafetyNote()
{
    return QString::fromUtf8(
        "<br><span style='color:#888;'>安全:每步调整不超过 20–30%,"
        "调整后空载复测;出现振动、异响立即回退。</span>");
}

} // namespace

// ---------------------------------------------------------------------------
// 估计器 1+2:误差-速度回归 / 响应时滞
// ---------------------------------------------------------------------------

AxisKvEstimate Diagnosis::estimateAxisKv(const QString &name,
                                         const Dataset &data,
                                         const AxisChannel &ch)
{
    AxisKvEstimate est;
    est.axis = name;
    const int n = data.size();
    if (n < 3) return est;

    // 速度序列(单位/s):优先指令速度列(单位/min → /60),否则位置中心差分
    QVector<double> v(n, 0.0);
    if (ch.cmdVel.size() == n) {
        for (int i = 0; i < n; ++i)
            v[i] = ch.cmdVel[i] / 60.0;
    } else {
        for (int i = 1; i < n - 1; ++i) {
            const double dt = data.time[i + 1] - data.time[i - 1];
            if (dt > 0)
                v[i] = (ch.cmd[i + 1] - ch.cmd[i - 1]) / dt;
        }
    }

    double vmax = 0;
    for (double x : v) vmax = std::max(vmax, std::abs(x));

    // 该轴基本静止,无法估计
    if (vmax > 0.01) {
        // 过原点最小二乘:err = tau * v
        // 只取 |v| ≥ 10% vmax 的样本,避开换向与静止段(摩擦主导,非线性)
        const double vmin = kVelCutRatio * vmax;
        double svv = 0, sve = 0, see = 0;
        int m = 0;
        for (int i = 0; i < n; ++i) {
            if (std::abs(v[i]) < vmin) continue;
            const double e = ch.err[i];
            svv += v[i] * v[i];
            sve += v[i] * e;
            see += e * e;
            ++m;
        }
        if (m >= kMinRegSamples && svv > 0) {
            const double tau = sve / svv;
            if (tau > 1e-6) {
                const double ssRes = see - 2 * tau * sve + tau * tau * svv;
                const double r2 = (see > 0) ? 1.0 - ssRes / see : 0.0;
                est.tauMs = tau * 1000.0;
                est.kvReg = 1.0 / tau;
                est.r2 = r2;
                est.samples = m;
                est.regValid = (r2 >= kR2Min);
            }
        }
    }

    // 时滞法:Kv ≈ 1/中位响应延迟(中位数对毛刺鲁棒)
    if (ch.stats.valid && ch.stats.median > 1e-5) {
        est.medianLagMs = ch.stats.median * 1000.0;
        est.kvLag = 1.0 / ch.stats.median;
        est.lagValid = true;
    }

    if (est.regValid && est.lagValid) {
        const double hi = std::max(est.kvReg, est.kvLag);
        est.consistent = (std::abs(est.kvReg - est.kvLag) / hi) < kEstimateAgree;
    }
    return est;
}

// ---------------------------------------------------------------------------
// 估计器 3:圆半径收缩反推
// ---------------------------------------------------------------------------

ShrinkEstimate Diagnosis::estimateFromShrink(const CircleAnalysis &circle)
{
    ShrinkEstimate s;
    if (!circle.hasXY) {
        s.note = QString::fromUtf8("数据不含 X/Y 双轴");
        return s;
    }
    if (!circle.circleFitOk) {
        s.note = QString::fromUtf8("非圆轨迹,无法使用半径收缩法");
        return s;
    }
    if (circle.avgFeed <= 0 || circle.refR <= 0) {
        s.note = QString::fromUtf8("无法确定圆弧段进给速度");
        return s;
    }
    s.coverageDeg = circle.arcCoverageDeg;
    if (circle.arcCoverageDeg < kMinArcCoverageDeg) {
        // 部分圆弧上 LSC 半径与圆心强耦合,微小扰动即可造成
        // 数百 µm 的虚假"收缩",必须整圆才允许使用该方法
        s.note = QString::fromUtf8(
            "圆弧覆盖不足(%1°,需要 ≥ %2°):部分圆弧的半径拟合"
            "误差敏感,收缩法不可用")
            .arg(QString::number(circle.arcCoverageDeg, 'f', 0))
            .arg(QString::number(kMinArcCoverageDeg, 'f', 0));
        return s;
    }

    s.deltaR = circle.refR - circle.fbR;
    s.feedMmMin = circle.avgFeed;
    const double vMmS = circle.avgFeed / 60.0;
    s.omega = vMmS / circle.refR;

    if (s.deltaR < 0) {
        s.note = QString::fromUtf8(
            "反馈半径大于指令半径(可能为超调或机械因素),该方法不适用");
        return s;
    }

    // 收缩量小于测量下限:只能给出增益下界(按下限收缩量计算)
    const double effDelta = std::max(s.deltaR, kShrinkFloorMm);
    const double rho = (circle.refR - effDelta) / circle.refR;
    if (rho <= 0 || rho >= 1) {
        s.note = QString::fromUtf8("收缩量超出可解算范围");
        return s;
    }

    s.kv = s.omega * rho / std::sqrt(1.0 - rho * rho);
    s.lowerBoundOnly = (s.deltaR < kShrinkFloorMm);
    s.valid = true;
    return s;
}

// ---------------------------------------------------------------------------
// 报告生成
// ---------------------------------------------------------------------------

static QString buildHtml(const Dataset &data,
                         const CircleAnalysis &circle,
                         const ServoParams &params,
                         const DiagnosisResult &res,
                         const QString &fileName)
{
    const servo_brands::Brand &brand = servo_brands::at(params.brandIndex);
    QString h;
    h += QString::fromUtf8(
        "<h2 style='margin-bottom:2px;'>伺服诊断报告</h2>"
        "<div style='color:#666;'>文件:%1 &nbsp;|&nbsp; 生成时间:%2 "
        "&nbsp;|&nbsp; 驱动器:%3</div>")
        .arg(fileName)
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"))
        .arg(brand.name);

    // ---- 安全须知 ---------------------------------------------------------
    h += QString::fromUtf8(
        "<table width='100%' cellpadding='8' cellspacing='0' bgcolor='#fff5f5' "
        "style='margin-top:8px;'><tr><td>"
        "<b style='color:#c0392b;'>⚠ 安全须知</b><br>"
        "本报告为采样数据的数学估计,<b>仅供调机参考,不替代驱动器手册与"
        "厂家指导</b>。调整伺服参数必须:由具备资质的人员进行;在无工件、"
        "低速、可随时急停的条件下进行;<b>每次只调一个参数,小步(≤20–30%)"
        "调整并复测</b>;提高增益过程中出现振动、异响、电机异常发热须立即"
        "回退。参数号请以驱动器手册核对。"
        "</td></tr></table>");

    // ---- 数据概况 ---------------------------------------------------------
    // 采样周期取中位数:dump 中可能存在时间跳变段,均值会被拉偏
    const int n = data.size();
    double dt = 0;
    if (n > 1) {
        QVector<double> dts;
        dts.reserve(n - 1);
        for (int i = 1; i < n; ++i) {
            const double d = data.time[i] - data.time[i - 1];
            if (d > 0) dts.append(d);
        }
        if (!dts.isEmpty()) {
            std::nth_element(dts.begin(), dts.begin() + dts.size() / 2,
                             dts.end());
            dt = dts[dts.size() / 2];
        }
    }

    h += QString::fromUtf8("<h3>1. 数据概况</h3>");
    QString circInfo;
    if (!circle.hasXY)
        circInfo = QString::fromUtf8("数据不含 X/Y 双轴,圆度诊断跳过");
    else if (!circle.circleFitOk)
        circInfo = QString::fromUtf8("指令轨迹非圆,圆度相关诊断跳过");
    else
        circInfo = QString::fromUtf8(
            "拟合成功:R = %1 mm,进给 ≈ %2 mm/min,圆弧段 %3 / %4 点")
            .arg(num(circle.refR, 4))
            .arg(num(circle.avgFeed, 0))
            .arg(circle.onCircle.size())
            .arg(n);
    h += QString::fromUtf8(
        "<div>数据点:%1(采样周期 ≈ %2 ms)&nbsp;|&nbsp; 轴:%3<br>"
        "指令圆:%4</div>")
        .arg(n)
        .arg(num(dt * 1000.0, 3))
        .arg(data.axisOrder.join(", "))
        .arg(circInfo);

    // ---- 各轴等效增益估计 ---------------------------------------------------
    h += QString::fromUtf8("<h3>2. 各轴等效位置环增益(实测)</h3>"
        "<div style='color:#666;font-size:12px;'>"
        "\"等效\"=包含速度前馈与速度环动态的综合响应,不等于驱动器参数本身。"
        "两个独立估计互相校验。</div>");

    h += QString::fromUtf8(
        "<table border='1' cellpadding='4' cellspacing='0' width='100%' "
        "style='margin-top:4px;'>"
        "<tr bgcolor='#f0f0f3'>"
        "<th>轴</th><th>录入增益<br>(换算 1/s)</th><th>前馈 %</th>"
        "<th>期望等效<br>(1/s)</th><th>实测等效·回归<br>(1/s)</th>"
        "<th>R² / 样本</th><th>实测等效·时滞<br>(1/s)</th><th>两法一致</th>"
        "</tr>");

    const QString dash = QString::fromUtf8("—");
    for (const AxisKvEstimate &e : res.axes) {
        QString entered = e.hasEntered ? num(e.kvEntered, 1) : dash;
        QString ff      = e.hasEntered ? num(e.ffEntered, 0) : dash;
        QString expect  = e.hasExpected ? num(e.kvExpected, 1)
                        : (e.hasEntered && e.ffEntered >= 99.5
                           ? QString::fromUtf8("—(前馈≈100%)") : dash);
        QString reg = e.regValid
            ? num(e.kvReg, 1)
            : (e.samples > 0 && e.r2 < kR2Min
               ? QString::fromUtf8("不可靠(R²=%1)").arg(num(e.r2, 2))
               : QString::fromUtf8("无法估计"));
        QString r2s = e.samples > 0
            ? QString("%1 / %2").arg(num(e.r2, 2)).arg(e.samples) : dash;
        QString lag = e.lagValid
            ? QString::fromUtf8("%1(中位 %2 ms)")
                  .arg(num(e.kvLag, 1)).arg(num(e.medianLagMs, 1))
            : QString::fromUtf8("无法估计");
        QString cons = (e.regValid && e.lagValid)
            ? (e.consistent ? QString::fromUtf8("✓")
                            : QString::fromUtf8("<b style='color:#c0392b;'>✗</b>"))
            : dash;

        h += QString("<tr align='center'><td><b>%1</b></td><td>%2</td>"
                     "<td>%3</td><td>%4</td><td><b>%5</b></td><td>%6</td>"
                     "<td>%7</td><td>%8</td></tr>")
                 .arg(e.axis, entered, ff, expect, reg, r2s, lag, cons);
    }
    h += "</table>";
    if (params.axes.isEmpty())
        h += QString::fromUtf8(
            "<div style='color:#888;font-size:12px;'>未录入驱动器参数,"
            "无法做\"设定 vs 实测\"对比 —— 在上方参数表中录入后重新生成。</div>");

    // 收缩法与回归法交叉校验:两者本应同量级(收缩法按无前馈模型,
    // 有前馈时会低于回归值,但不应超过 kCrossCheckFactor 倍偏差)。
    // 不一致说明收缩主要不是动态滞后造成,该值不可用于参数建议。
    bool shrinkCrossOk = true;
    {
        double regMean = 0;
        int regCnt = 0;
        for (const AxisKvEstimate &e : res.axes) {
            if ((e.axis == "X" || e.axis == "Y") && e.best() > 0) {
                regMean += e.best();
                ++regCnt;
            }
        }
        if (res.shrink.valid && !res.shrink.lowerBoundOnly && regCnt > 0) {
            regMean /= regCnt;
            // 前馈使两法理论比值 = √((1+ff)/(1−ff));已录入前馈时
            // 放宽允许倍数,避免高前馈被误判为不一致
            double ffMax = 0;
            for (const QString &ax : { QString("X"), QString("Y") }) {
                const AxisParams ap = params.axes.value(ax);
                if (ap.hasFF)
                    ffMax = std::max(ffMax, ap.velFF / 100.0);
            }
            double factor = kCrossCheckFactor;
            if (ffMax > 0 && ffMax < 0.99)
                factor = std::max(factor,
                    1.5 * std::sqrt((1.0 + ffMax) / (1.0 - ffMax)));
            shrinkCrossOk = res.shrink.kv > regMean / factor
                         && res.shrink.kv < regMean * factor;
        }
    }

    // ---- 圆度指标 ---------------------------------------------------------
    if (circle.hasXY && !circle.circleFitOk) {
        // 非圆轨迹:回退模式下的真圆度/收缩/尺寸数字没有意义,
        // 一律不显示,避免误读
        h += QString::fromUtf8(
            "<h3>3. 圆度指标</h3>"
            "<div>指令轨迹非圆(或无法拟合圆),圆度指标不适用,"
            "本报告仅含轴响应诊断。</div>");
    } else if (circle.hasXY) {
        h += QString::fromUtf8("<h3>3. 圆度指标</h3>");
        h += QString::fromUtf8(
            "<div>真圆度(LSC):<b>%1 mm</b> &nbsp;|&nbsp; "
            "指令半径 %2 mm → 反馈半径 %3 mm,收缩 ΔR = <b>%4 µm</b>"
            " &nbsp;|&nbsp; 圆弧覆盖 %5°</div>")
            .arg(num(circle.roundness, 4))
            .arg(num(circle.refR, 4))
            .arg(num(circle.fbR, 4))
            .arg(um(circle.refR - circle.fbR))
            .arg(num(circle.arcCoverageDeg, 0));

        const ShrinkEstimate &s = res.shrink;
        if (s.valid) {
            h += s.lowerBoundOnly
                ? QString::fromUtf8(
                      "<div>半径收缩反推:收缩低于测量下限(%1 µm),"
                      "等效增益 <b>≥ %2 /s</b>(ω = %3 rad/s)</div>")
                      .arg(um(kShrinkFloorMm)).arg(num(s.kv, 1))
                      .arg(num(s.omega, 2))
                : QString::fromUtf8(
                      "<div>半径收缩反推:双轴平均等效增益 ≈ <b>%1 /s</b>"
                      "(ω = %2 rad/s)</div>")
                      .arg(num(s.kv, 1)).arg(num(s.omega, 2));
            if (!shrinkCrossOk)
                h += QString::fromUtf8(
                    "<div style='color:#c0392b;'><b>⚠ 交叉校验不通过:</b>"
                    "该值与回归法估计严重不一致,说明半径收缩主要不是"
                    "动态滞后造成(可能为机械尺寸偏差、变速运行或测量"
                    "因素;若实际前馈比例很高,请录入前馈后重新生成),"
                    "<b>不可用于参数调整</b>。</div>");
        } else {
            h += QString::fromUtf8(
                "<div>半径收缩反推:%1</div>").arg(s.note);
        }

        // 方向尺寸表
        h += QString::fromUtf8(
            "<table border='1' cellpadding='4' cellspacing='0' "
            "style='margin-top:4px;'>"
            "<tr bgcolor='#f0f0f3'><th>方向</th><th>指令尺寸 mm</th>"
            "<th>反馈尺寸 mm</th><th>尺寸偏差 mm</th></tr>");
        for (const DirectionStats &d : circle.dirStats) {
            h += QString("<tr align='center'><td>%1</td><td>%2</td>"
                         "<td>%3</td><td>%4%5</td></tr>")
                     .arg(d.name)
                     .arg(num(d.cmdSize, 4))
                     .arg(num(d.fbSize, 4))
                     .arg(d.sizeErr >= 0 ? "+" : "")
                     .arg(num(d.sizeErr, 4));
        }
        h += "</table>";

        const double maxRev = circle.maxReversalDev();
        if (maxRev >= 0)
            h += QString::fromUtf8(
                "<div>最大换向毛刺(对反馈拟合圆,换向点 ±5° 弧段):"
                "<b>%1 µm</b>,共检测到 %2 个换向点</div>")
                .arg(um(maxRev)).arg(circle.revs.size());

        // 圆心偏移:反馈拟合圆心相对指令圆心(系统性单边偏差线索)
        if (circle.circleFitOk) {
            const double ox = circle.fbCx - circle.refCx;
            const double oy = circle.fbCy - circle.refCy;
            h += QString::fromUtf8(
                "<div>圆心偏移(反馈 − 指令):ΔX = %1 µm,ΔY = %2 µm</div>")
                .arg(um(ox)).arg(um(oy));
        }
    }

    // ---- 规则诊断 ---------------------------------------------------------
    h += QString::fromUtf8("<h3>4. 诊断与建议</h3>");
    QStringList findings;

    // 查表辅助
    auto findAxis = [&res](const QString &name) -> const AxisKvEstimate* {
        for (const AxisKvEstimate &e : res.axes)
            if (e.axis == name) return &e;
        return nullptr;
    };

    // R1: 双轴等效增益失配(切圆失圆的首要原因)
    const AxisKvEstimate *ex = findAxis("X");
    const AxisKvEstimate *ey = findAxis("Y");
    if (ex && ey && ex->best() > 0 && ey->best() > 0) {
        const double kx = ex->best(), ky = ey->best();
        const double ratio = std::max(kx, ky) / std::min(kx, ky);
        if (ratio > kMismatchWarn) {
            const QString slow = (kx < ky) ? "X" : "Y";
            const QString fast = (kx < ky) ? "Y" : "X";
            // 某轴回归不可靠、数值仅来自时滞法时,在标题中注明
            QString caveat;
            if (!ex->regValid || !ey->regValid)
                caveat = QString::fromUtf8(
                    "(其中 %1 轴回归不可靠,数值为时滞法估计,"
                    "仅供定性参考)")
                    .arg(!ex->regValid ? "X" : "Y");
            findings += findingHtml(ratio > kMismatchSevere ? 2 : 1,
                QString::fromUtf8("双轴动态失配:%1 轴等效增益 %2 /s,"
                                  "%3 轴 %4 /s(比值 %5)%6")
                    .arg(slow).arg(num(std::min(kx, ky), 1))
                    .arg(fast).arg(num(std::max(kx, ky), 1))
                    .arg(num(ratio, 2)).arg(caveat),
                QString::fromUtf8(
                    "失配会把圆切成 45° 方向的椭圆,是圆度的首要影响因素。"
                    "两条途径:<b>(a)</b> 提高 %1 轴位置环增益或速度前馈,"
                    "使其等效响应接近 %2 轴 —— 有振荡风险,须小步进行;"
                    "<b>(b)</b> 将 %2 轴增益/前馈降至与 %1 轴一致 —— 更安全,"
                    "但整体响应变慢。建议先用 (b) 验证圆度确实改善、"
                    "确认方向正确后,再考虑 (a) 逐步恢复响应。%3")
                    .arg(slow).arg(fast).arg(gainSafetyNote()));
        }
    }

    // R2: 录入设定与实测等效偏差过大(只采信回归法,时滞法单独
    // 存在时证据不足,不做设定对比以免误导)
    for (const AxisKvEstimate &e : res.axes) {
        if (!e.hasExpected || !e.regValid) continue;
        const double dev = (e.kvReg - e.kvExpected) / e.kvExpected;
        if (std::abs(dev) > kExpectMismatch) {
            findings += findingHtml(1,
                QString::fromUtf8("%1 轴实测等效增益(%2 /s)与设定预期"
                                  "(%3 /s)偏差 %4%")
                    .arg(e.axis).arg(num(e.kvReg, 1))
                    .arg(num(e.kvExpected, 1))
                    .arg(num(dev * 100.0, 0)),
                dev < 0
                    ? QString::fromUtf8(
                          "实际响应低于设定预期,优先检查:速度前馈是否实际"
                          "生效、前馈/指令平滑滤波时间常数是否过大、速度环"
                          "带宽是否不足、转矩是否饱和。不要为补偿差距而"
                          "直接大幅提高位置环增益。")
                    : QString::fromUtf8(
                          "实际响应高于设定预期,请核对录入的参数值与单位"
                          "(品牌单位换算见参数表),以及是否有自适应/模型"
                          "控制功能在起作用。"));
        }
    }

    // R3: 半径收缩明显(仅在与回归法交叉校验通过时给建议)
    if (res.shrink.valid && !res.shrink.lowerBoundOnly && shrinkCrossOk
        && res.shrink.deltaR > kShrinkMinMm) {
        findings += findingHtml(1,
            QString::fromUtf8("圆半径收缩 %1 µm(R=%2 mm, F≈%3 mm/min)")
                .arg(um(res.shrink.deltaR))
                .arg(num(circle.refR, 2))
                .arg(num(res.shrink.feedMmMin, 0)),
            QString::fromUtf8(
                "收缩量与进给平方成正比、与等效增益平方成反比"
                "(ΔR ≈ v²/(2·Kv²·R)):等效增益提高一倍,收缩约降至 1/4。"
                "<b>优先逐步增加速度前馈</b>(每步 10–20%)而不是提高位置环"
                "增益 —— 前馈不影响环路稳定性;但前馈过量会引起过冲"
                "(反馈半径大于指令、拐角过切),反馈半径接近指令半径即停。%1")
                .arg(gainSafetyNote()));
    }

    // R4: 换向毛刺(按轴分别取最大带符号偏差)
    if (circle.hasXY && circle.circleFitOk) {
        for (int axis = 0; axis < 2; ++axis) {
            double worst = 0;
            for (const CircleAnalysis::ReversalMark &r : circle.revs)
                if (r.axis == axis && std::abs(r.dev) > std::abs(worst))
                    worst = r.dev;
            if (std::abs(worst) <= kRevSpikeMm) continue;

            const QString axName = (axis == 0) ? "X" : "Y";
            const AxisParams ap = params.axes.value(axName);
            QString compInfo;
            if (ap.hasFrictionComp)
                compInfo = QString::fromUtf8(
                    "当前录入摩擦补偿值 %1(驱动器单位)。")
                    .arg(num(ap.frictionComp, 2));
            if (ap.hasBacklashComp)
                compInfo += QString::fromUtf8(
                    "当前录入反向间隙补偿 %1(驱动器单位)。")
                    .arg(num(ap.backlashComp, 2));

            if (worst > 0) {
                findings += findingHtml(1,
                    QString::fromUtf8("%1 轴换向处外凸毛刺 %2 µm")
                        .arg(axName).arg(um(worst)),
                    QString::fromUtf8(
                        "外凸尖峰通常为换向时摩擦/反向间隙引起的轴短暂停滞,"
                        "即象限毛刺。%1建议小步(每步约 10–20%)增大该轴摩擦"
                        "(象限突起)补偿并复测;若双反馈数据显示机械背隙大,"
                        "应优先处理机械。补偿过量会变成内凹,出现内凹即回退。")
                        .arg(compInfo));
            } else {
                // 内凹有两类成因,按该轴整体响应水平区分提示,
                // 避免把响应滞后误导成"补偿过量"
                const AxisKvEstimate *ae = findAxis(axName);
                const AxisKvEstimate *other =
                    findAxis(axis == 0 ? "Y" : "X");
                const bool axSlow = ae && other && ae->best() > 0
                    && other->best() > 0
                    && ae->best() * kMismatchWarn < other->best();
                findings += findingHtml(1,
                    QString::fromUtf8("%1 轴换向处内凹 %2 µm")
                        .arg(axName).arg(um(-worst)),
                    axSlow
                        ? QString::fromUtf8(
                              "该轴整体响应明显偏慢(见第 2 节),换向区"
                              "内凹更可能是响应滞后所致 —— 先解决双轴"
                              "匹配/响应问题,再评估摩擦补偿。%1")
                              .arg(compInfo)
                        : QString::fromUtf8(
                              "可能原因:摩擦补偿过量或补偿时机过早;"
                              "也可能是该轴换向区响应滞后。%1若已确认"
                              "响应正常,建议小步减小该轴摩擦补偿量"
                              "(或延后补偿时机)并复测。")
                              .arg(compInfo));
            }
        }
    }

    // R4b: 圆心偏移(系统性单边偏差,线性伺服动态不会产生圆心偏移)
    if (circle.hasXY && circle.circleFitOk) {
        const double ox = circle.fbCx - circle.refCx;
        const double oy = circle.fbCy - circle.refCy;
        const double off = std::sqrt(ox * ox + oy * oy);
        if (off > kSizeErrMm) {
            QString dirTxt;
            if (std::abs(ox) > 2 * std::abs(oy))
                dirTxt = QString::fromUtf8(",主要在 X 方向");
            else if (std::abs(oy) > 2 * std::abs(ox))
                dirTxt = QString::fromUtf8(",主要在 Y 方向");
            findings += findingHtml(1,
                QString::fromUtf8("反馈圆心相对指令圆心偏移 %1 µm"
                                  "(ΔX %2 / ΔY %3 µm%4)")
                    .arg(um(off)).arg(um(ox)).arg(um(oy)).arg(dirTxt),
                QString::fromUtf8(
                    "线性的伺服滞后不会使圆心偏移 —— 偏移说明该方向存在"
                    "<b>系统性单边因素</b>:摩擦/补偿不对称、采集期间的"
                    "位置漂移、或机械单边受力。建议正反两个方向各切一圆"
                    "复测:偏移随方向反号 → 摩擦/间隙类;偏移方向不变 → "
                    "检查机械与补偿偏置。该项不是提高增益能解决的问题。"));
        }
    }

    // R5: 对角线尺寸差(垂直度/失配)
    if (circle.hasXY && circle.circleFitOk && circle.dirStats.size() >= 4) {
        const double d1 = circle.dirStats[2].fbSize;
        const double d2 = circle.dirStats[3].fbSize;
        const double diff = std::abs(d1 - d2);
        const double thr = std::max(kSizeErrMm, circle.refR * 2e-4);
        if (diff > thr) {
            bool axesMatched = (ex && ey && ex->best() > 0 && ey->best() > 0
                && std::max(ex->best(), ey->best())
                       / std::min(ex->best(), ey->best()) <= kMismatchWarn);
            findings += findingHtml(axesMatched ? 1 : 0,
                QString::fromUtf8("两条对角线尺寸相差 %1 µm"
                                  "(对角线1 %2 mm / 对角线2 %3 mm)")
                    .arg(um(diff)).arg(num(d1, 4)).arg(num(d2, 4)),
                axesMatched
                    ? QString::fromUtf8(
                          "双轴动态已基本匹配的情况下,对角线差通常来自"
                          "<b>机械因素(X/Y 垂直度)</b>,伺服参数无法修正,"
                          "建议机械检测确认。")
                    : QString::fromUtf8(
                          "当前双轴动态尚未匹配(见上),失配本身就会造成"
                          "对角线差。先解决双轴匹配,再复测评估是否还有"
                          "垂直度问题。"));
        }
    }

    // R6: X/Y 方向尺寸偏差
    if (circle.hasXY && circle.circleFitOk && circle.dirStats.size() >= 2) {
        for (int d = 0; d < 2; ++d) {
            const DirectionStats &s = circle.dirStats[d];
            if (std::abs(s.sizeErr) <= kSizeErrMm) continue;
            const QString axName = (d == 0) ? "X" : "Y";
            findings += findingHtml(0,
                QString::fromUtf8("%1 方向尺寸偏差 %2%3 µm")
                    .arg(axName)
                    .arg(s.sizeErr >= 0 ? "+" : QString::fromUtf8("−"))
                    .arg(um(std::abs(s.sizeErr))),
                s.sizeErr < 0
                    ? QString::fromUtf8(
                          "反馈尺寸偏小:该轴动态滞后(增益/前馈不足)的"
                          "典型表现,与半径收缩/双轴匹配条目一并处理。")
                    : QString::fromUtf8(
                          "反馈尺寸偏大:可能为该轴过冲(前馈/增益过高、"
                          "或摩擦补偿过量),也可能是机械间隙,结合换向"
                          "毛刺条目判断。"));
        }
    }

    // R7: 数据质量警告
    for (const QString &name : data.axisOrder) {
        const AxisChannel &ch = data.axes[name];
        const int total = ch.stats.movingCount + ch.stats.noRespCount;
        if (total > 0
            && double(ch.stats.noRespCount) / total > kNoRespRatio) {
            findings += findingHtml(0,
                QString::fromUtf8("%1 轴有 %2% 的运动采样点未找到响应")
                    .arg(name)
                    .arg(num(100.0 * ch.stats.noRespCount / total, 0)),
                QString::fromUtf8(
                    "该轴跟随严重滞后或往复频繁(峰值附近反馈未到达指令即"
                    "反向)。此情形下时滞法估计偏乐观,以回归法为准。"));
        }
    }
    for (const AxisKvEstimate &e : res.axes) {
        if (e.regValid && e.r2 < kR2Reliable) {
            findings += findingHtml(0,
                QString::fromUtf8("%1 轴回归可信度偏低(R² = %2)")
                    .arg(e.axis).arg(num(e.r2, 2)),
                QString::fromUtf8(
                    "误差与速度的线性关系弱,可能存在明显摩擦、间隙或"
                    "加减速段占比过高。建议用恒速段更长的数据复测。"));
        }
        if (e.regValid && e.lagValid && !e.consistent) {
            findings += findingHtml(0,
                QString::fromUtf8("%1 轴两种估计不一致"
                                  "(回归 %2 /s vs 时滞 %3 /s)")
                    .arg(e.axis).arg(num(e.kvReg, 1)).arg(num(e.kvLag, 1)),
                QString::fromUtf8(
                    "提示存在非线性因素(摩擦、补偿动作、加减速),单一"
                    "数字不可全信。建议以多种进给速度分别采集复测,"
                    "误差随速度线性增长才能确认是增益/前馈问题。"));
        }
    }

    if (findings.isEmpty())
        h += QString::fromUtf8(
            "<div style='color:#1e8449;'>未发现超过阈值的异常。</div>");
    else
        h += findings.join("");

    // ---- 方法与局限性 -------------------------------------------------------
    h += QString::fromUtf8(
        "<h3>5. 方法与局限性</h3>"
        "<ul style='font-size:12px;color:#555;'>"
        "<li>回归法:err = τ·v 过原点最小二乘,样本取 |v|≥10%·vmax"
        "(避开换向/静止段),等效增益 = 1/τ;要求样本 ≥ %1、R² ≥ %2,"
        "R² &lt; %3 标注为低可信。</li>"
        "<li>时滞法:等效增益 = 1/中位响应延迟。</li>"
        "<li>换向毛刺:指令速度过零点两侧各 5° 圆弧内,反馈对"
        "<b>反馈自身 LSC 拟合圆</b>的最大径向偏差(正=外凸,负=内凹);"
        "以反馈拟合圆为基准可排除圆心偏移与尺寸误差的影响,"
        "只度量局部毛刺,与球杆仪习惯一致。</li>"
        "<li>收缩法:一阶模型 fbR = refR·Kv/√(Kv²+ω²) 精确反解,结果为"
        "<b>双轴平均</b>等效增益;收缩 &lt; %4 µm 时只给下界;要求圆弧"
        "覆盖 ≥ 300°(部分圆弧拟合敏感),且与回归法交叉校验一致才采信。"
        "若前馈已启用,收缩法值 = Kv/√(1−ff²),低于回归法值 = Kv/(1−ff),"
        "两者差异本身即前馈在起作用的证据。</li>"
        "<li>三种估计均为\"等效\"值:包含速度前馈与速度环动态,"
        "不等于驱动器参数本身;录入参数后才能做设定对比。</li>"
        "<li>单一工况无法区分电气与机械因素 —— 请用不同进给速度(至少"
        "2–3 档)、正反两个方向分别采集复测:随速度增长的误差为增益/"
        "前馈问题,速度无关的固定误差为机械问题。</li>"
        "<li>判定阈值:双轴失配 &gt;%5(严重 &gt;%6)、设定偏差 &gt;%7%、"
        "半径收缩 &gt;%8 µm、换向毛刺 &gt;%9 µm、尺寸/对角线差 &gt;%10 µm。</li>"
        "</ul>")
        .arg(kMinRegSamples)
        .arg(num(kR2Min, 1))
        .arg(num(kR2Reliable, 1))
        .arg(um(kShrinkFloorMm))
        .arg(num(kMismatchWarn, 1))
        .arg(num(kMismatchSevere, 1))
        .arg(num(kExpectMismatch * 100, 0))
        .arg(um(kShrinkMinMm))
        .arg(um(kRevSpikeMm))
        .arg(um(kSizeErrMm));

    return h;
}

DiagnosisResult Diagnosis::run(const Dataset &data,
                               const CircleAnalysis &circle,
                               const ServoParams &params,
                               const QString &fileName)
{
    DiagnosisResult res;

    for (const QString &name : data.axisOrder) {
        AxisKvEstimate e = estimateAxisKv(name, data, data.axes[name]);

        const double kvSet = params.gainSI(name);
        if (kvSet > 0) {
            e.hasEntered = true;
            e.kvEntered = kvSet;
            const AxisParams ap = params.axes.value(name);
            e.ffEntered = ap.hasFF ? ap.velFF : 0.0;
            const double ffFrac = e.ffEntered / 100.0;
            if (ffFrac < 0.995) {
                e.kvExpected = kvSet / (1.0 - ffFrac);
                e.hasExpected = true;
            }
        }
        res.axes.append(e);
    }

    res.shrink = estimateFromShrink(circle);
    res.html = buildHtml(data, circle, params, res, fileName);
    return res;
}
