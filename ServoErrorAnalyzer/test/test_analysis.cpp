// 冒烟 + 精度验证测试(离屏):
//  1) 合成数据(已知真值 Kv/前馈)验证三个等效增益估计器的精度
//  2) 参数 sidecar JSON 读写往返
//  3) 真实 CSV:解析/统计/圆分析/诊断/渲染 无崩溃
// 退出码 = 失败断言数(0 = 全部通过)
#include <QApplication>
#include <QFile>
#include <cmath>
#include <cstdio>
#include "../data_loader.h"
#include "../chart_view.h"
#include "../circle_analysis.h"
#include "../circularity_widget.h"
#include "../diagnosis.h"
#include "../diagnosis_dialog.h"
#include "../servo_params.h"

static int g_failures = 0;

static void check(bool ok, const char *what, double got, double expect,
                  double tolRel)
{
    if (ok) {
        std::printf("  PASS  %-42s got=%.4f expect=%.4f (tol %.0f%%)\n",
                    what, got, expect, tolRel * 100);
    } else {
        std::printf("X FAIL  %-42s got=%.4f expect=%.4f (tol %.0f%%)\n",
                    what, got, expect, tolRel * 100);
        ++g_failures;
    }
}

static void checkRel(const char *what, double got, double expect, double tolRel)
{
    const bool ok = expect != 0
        && std::abs(got - expect) / std::abs(expect) <= tolRel;
    check(ok, what, got, expect, tolRel);
}

static void checkRange(const char *what, double got, double lo, double hi)
{
    const bool ok = got >= lo && got <= hi;
    if (ok)
        std::printf("  PASS  %-42s got=%.4f in [%.3f, %.3f]\n", what, got, lo, hi);
    else {
        std::printf("X FAIL  %-42s got=%.4f NOT in [%.3f, %.3f]\n", what, got, lo, hi);
        ++g_failures;
    }
}

// ---------------------------------------------------------------------------
// 合成数据:圆轨迹 + 每轴一阶位置环(增益 Kv、速度前馈 ff)仿真。
// 内部以 50µs 子步长积分(离散化偏差 < 0.1%),按 1ms 采样输出。
// ---------------------------------------------------------------------------
struct SynthSpec {
    double kvx = 30, kvy = 30;   // 1/s
    double ffx = 0,  ffy = 0;    // 0..1
    double R = 10.0;             // mm
    double feed = 1200.0;        // mm/min
    double cx = 50.0, cy = 30.0;
    double revs = 2.0;           // 记录的圈数
    double warmupRevs = 0.0;     // 先空转(不记录),消除起步瞬态
    bool   leadIn = false;       // 径向直线进刀段(测试进退刀剔除)
};

static Dataset makeSynth(const SynthSpec &s)
{
    const double dt = 0.001;
    const int    kSub = 20;                  // 50µs 子步
    const double dts = dt / kSub;
    const double v = s.feed / 60.0;          // mm/s
    const double om = v / s.R;               // rad/s

    const double leadLen = 5.0;              // mm
    const double leadDur = s.leadIn ? leadLen / v : 0.0;
    const double warmDur = s.warmupRevs * 2.0 * M_PI / om;
    const double recDur  = s.revs * 2.0 * M_PI / om;

    // 指令轨迹(解析):lead-in 径向逼近圆起点 → 逆时针圆
    auto cmdAt = [&](double t, double *x, double *y, double *vx, double *vy) {
        if (t < leadDur) {
            *x = s.cx + s.R + leadLen - v * t;
            *y = s.cy;
            *vx = -v; *vy = 0;
        } else {
            const double th = om * (t - leadDur);
            *x = s.cx + s.R * std::cos(th);
            *y = s.cy + s.R * std::sin(th);
            *vx = -s.R * om * std::sin(th);
            *vy =  s.R * om * std::cos(th);
        }
    };

    Dataset ds;
    AxisChannel chx, chy;

    double fx, fy, dummyVx, dummyVy;
    cmdAt(0, &fx, &fy, &dummyVx, &dummyVy);   // fb 从指令起点出发

    const double total = leadDur + warmDur + recDur;
    const double recStart = leadDur > 0 ? 0.0 : warmDur;  // lead-in 模式全程记录
    double t = 0;
    double rec = 0;
    while (t < total - 1e-9) {
        // 记录采样(1ms 栅格)
        if (t >= recStart - 1e-9 && t >= rec - 1e-9) {
            double cx0, cy0, cvx, cvy;
            cmdAt(t, &cx0, &cy0, &cvx, &cvy);
            ds.time.append(t);
            chx.cmd.append(cx0);  chy.cmd.append(cy0);
            chx.fb.append(fx);    chy.fb.append(fy);
            chx.cmdVel.append(cvx * 60.0);  // mm/s → mm/min
            chy.cmdVel.append(cvy * 60.0);
            rec = t + dt;
        }
        // 子步积分:fb' = Kv(cmd-fb) + ff*vcmd
        for (int k = 0; k < kSub; ++k) {
            double cx0, cy0, cvx, cvy;
            cmdAt(t + k * dts, &cx0, &cy0, &cvx, &cvy);
            fx += dts * (s.kvx * (cx0 - fx) + s.ffx * cvx);
            fy += dts * (s.kvy * (cy0 - fy) + s.ffy * cvy);
        }
        t += dt;
    }

    const int n = ds.time.size();
    chx.err.resize(n); chy.err.resize(n);
    chx.fbVel.resize(n); chy.fbVel.resize(n);
    chx.velErr.resize(n); chy.velErr.resize(n);
    for (int i = 0; i < n; ++i) {
        chx.err[i] = chx.cmd[i] - chx.fb[i];
        chy.err[i] = chy.cmd[i] - chy.fb[i];
        const int a = std::max(0, i - 1), b = std::min(n - 1, i + 1);
        const double dtw = ds.time[b] - ds.time[a];
        chx.fbVel[i] = dtw > 0 ? (chx.fb[b] - chx.fb[a]) / dtw * 60.0 : 0;
        chy.fbVel[i] = dtw > 0 ? (chy.fb[b] - chy.fb[a]) / dtw * 60.0 : 0;
        chx.velErr[i] = chx.cmdVel[i] - chx.fbVel[i];
        chy.velErr[i] = chy.cmdVel[i] - chy.fbVel[i];
    }

    ds.axes["X"] = chx;
    ds.axes["Y"] = chy;
    ds.axisOrder << "X" << "Y";
    DataLoader::computeResponseTime(ds);
    return ds;
}

// ---------------------------------------------------------------------------

static void testSynthetic()
{
    std::printf("=== T1 合成: Kv=30/40, ff=0, 带进刀段 ===\n");
    {
        SynthSpec sp;
        sp.kvx = 30; sp.kvy = 40;
        sp.leadIn = true;
        const Dataset ds = makeSynth(sp);

        CircleAnalysis ca;
        ca.compute(ds);
        check(ca.circleFitOk, "circleFitOk", ca.circleFitOk, 1, 0);
        check(ca.filtered, "filtered(进刀段被剔除)", ca.filtered, 1, 0);
        checkRel("refR", ca.refR, sp.R, 0.002);
        checkRel("avgFeed", ca.avgFeed, sp.feed, 0.01);

        const AxisKvEstimate ex =
            Diagnosis::estimateAxisKv("X", ds, ds.axes["X"]);
        const AxisKvEstimate ey =
            Diagnosis::estimateAxisKv("Y", ds, ds.axes["Y"]);
        check(ex.regValid, "X regValid", ex.regValid, 1, 0);
        checkRel("X kvReg", ex.kvReg, 30.0, 0.03);
        checkRange("X r2", ex.r2, 0.98, 1.0);
        checkRel("Y kvReg", ey.kvReg, 40.0, 0.03);
        // 时滞法:理论滞后 atan(ω/Kv)/ω
        checkRel("X kvLag", ex.kvLag, 30.0, 0.12);
        checkRel("Y kvLag", ey.kvLag, 40.0, 0.12);
        check(ex.consistent, "X 两法一致", ex.consistent, 1, 0);
    }

    std::printf("=== T2 合成: Kv=35/35, ff=0(收缩法精度)===\n");
    {
        SynthSpec sp;
        sp.kvx = sp.kvy = 35;
        sp.warmupRevs = 1.0;
        const Dataset ds = makeSynth(sp);

        CircleAnalysis ca;
        ca.compute(ds);
        const ShrinkEstimate se = Diagnosis::estimateFromShrink(ca);
        check(se.valid && !se.lowerBoundOnly, "shrink valid", se.valid, 1, 0);
        checkRange("arcCoverageDeg(整圆)", ca.arcCoverageDeg, 350, 360);
        // 理论:ΔR = R(1 − Kv/√(Kv²+ω²)),ω = v/R = 2 rad/s
        const double om = (sp.feed / 60.0) / sp.R;
        const double dRtheory =
            sp.R * (1.0 - sp.kvx / std::sqrt(sp.kvx * sp.kvx + om * om));
        checkRel("deltaR vs 理论", se.deltaR, dRtheory, 0.05);
        checkRel("kv(收缩法)", se.kv, 35.0, 0.03);
    }

    std::printf("=== T3 合成: Kv=30/30, ff=50%%(前馈下的等效值)===\n");
    {
        SynthSpec sp;
        sp.kvx = sp.kvy = 30;
        sp.ffx = sp.ffy = 0.5;
        sp.warmupRevs = 1.0;
        const Dataset ds = makeSynth(sp);

        const AxisKvEstimate ex =
            Diagnosis::estimateAxisKv("X", ds, ds.axes["X"]);
        // 回归等效 = Kv/(1−ff) = 60
        checkRel("X kvReg (=Kv/(1-ff))", ex.kvReg, 60.0, 0.03);

        CircleAnalysis ca;
        ca.compute(ds);
        const ShrinkEstimate se = Diagnosis::estimateFromShrink(ca);
        // 收缩法按无前馈模型反解:理论 = Kv/√(1−ff²) = 30/√0.75 = 34.64
        const double expect = 30.0 / std::sqrt(1.0 - 0.5 * 0.5);
        checkRel("kv(收缩法, =Kv/sqrt(1-ff^2))", se.kv, expect, 0.05);
    }

    std::printf("=== T4 合成: Kv=30/40 混合(收缩法给双轴平均)===\n");
    {
        SynthSpec sp;
        sp.kvx = 30; sp.kvy = 40;
        sp.warmupRevs = 1.0;
        const Dataset ds = makeSynth(sp);
        CircleAnalysis ca;
        ca.compute(ds);
        const ShrinkEstimate se = Diagnosis::estimateFromShrink(ca);
        checkRange("kv(收缩法) 介于两轴之间", se.kv, 30.0 * 0.97, 40.0 * 1.03);
    }

    std::printf("=== T4b 合成: 1/4 圆弧(收缩法必须被拒绝)===\n");
    {
        SynthSpec sp;
        sp.kvx = sp.kvy = 35;
        sp.revs = 0.25;
        const Dataset ds = makeSynth(sp);
        CircleAnalysis ca;
        ca.compute(ds);
        check(ca.circleFitOk, "circleFitOk(1/4 圆弧可拟合)",
              ca.circleFitOk, 1, 0);
        checkRange("arcCoverageDeg(约 90°)", ca.arcCoverageDeg, 60, 130);
        const ShrinkEstimate se = Diagnosis::estimateFromShrink(ca);
        check(!se.valid, "shrink 被覆盖角防线拒绝", se.valid, 0, 0);
    }
}

static void testParamsJson()
{
    std::printf("=== T5 参数 sidecar JSON 往返 ===\n");
    ServoParams p;
    p.brandIndex = servo_brands::indexOfId("fanuc");
    AxisParams ax;
    ax.posLoopGain = 3000; ax.hasGain = true;       // FANUC 0.01/s → 30/s
    ax.velFF = 50;        ax.hasFF = true;
    ax.frictionComp = 12; ax.hasFrictionComp = true;
    p.axes["X"] = ax;

    const QString path = "._test_params.json";
    QString err;
    check(p.saveTo(path, &err), "saveTo", 1, 1, 0);

    ServoParams q;
    check(q.loadFrom(path), "loadFrom", 1, 1, 0);
    check(q.brandIndex == p.brandIndex, "brand 往返", q.brandIndex,
          p.brandIndex, 0);
    checkRel("gainSI (3000 x 0.01)", q.gainSI("X"), 30.0, 1e-9);
    check(q.axes["X"].hasFF && q.axes["X"].velFF == 50, "velFF 往返",
          q.axes["X"].velFF, 50, 0);
    check(q.axes["X"].hasFrictionComp, "frictionComp 往返",
          q.axes["X"].frictionComp, 12, 0);
    check(!q.axes["X"].hasBacklashComp, "未录入字段不被虚构",
          q.axes["X"].hasBacklashComp, 0, 0);
    QFile::remove(path);

    // 品牌单位换算(依据各厂商手册,见 servo_params.cpp 注释)
    auto gainOf = [](const char *brand, double v) {
        ServoParams sp;
        sp.brandIndex = servo_brands::indexOfId(brand);
        AxisParams a;
        a.posLoopGain = v;
        a.hasGain = true;
        sp.axes["X"] = a;
        return sp.gainSI("X");
    };
    checkRel("汇川 H08-02 64.0Hz → 64/s", gainOf("inovance", 64.0),
             64.0, 1e-9);
    checkRel("AKD PL.KP 100 → 100/s", gainOf("akd", 100.0), 100.0, 1e-9);
    checkRel("AKD2G PL.KP 10Hz → 62.83/s", gainOf("akd2g", 10.0),
             62.8319, 1e-4);
    checkRel("安川 Pn102 400 → 40/s", gainOf("yaskawa", 400.0), 40.0, 1e-9);
    checkRel("SIEMENS Kv 1 → 16.67/s", gainOf("siemens", 1.0),
             16.6667, 1e-4);
    check(servo_brands::indexOfId("unknown-id") == 0,
          "未知品牌回退到通用", servo_brands::indexOfId("unknown-id"), 0, 0);
}

static void testRealFile(const char *path)
{
    std::printf("=== 真实文件 %s ===\n", path);
    Dataset data;
    QString err;
    if (!DataLoader::loadCsv(QString::fromLocal8Bit(path), data, &err)) {
        std::printf("X FAIL  load: %s\n", qPrintable(err));
        ++g_failures;
        return;
    }
    std::printf("rows: %d  axes: %s\n", data.size(),
                qPrintable(data.axisOrder.join(",")));

    CircleAnalysis ca;
    ca.compute(data);
    if (ca.hasXY) {
        std::printf("circ: fitOk=%d roundness=%.5f refR=%.4f fbR=%.4f "
                    "feed=%.0f maxRev=%.5f coverage=%.0f\n",
                    ca.circleFitOk, ca.roundness, ca.refR, ca.fbR,
                    ca.avgFeed, ca.maxReversalDev(), ca.arcCoverageDeg);
        for (const CircleAnalysis::ReversalMark &r : ca.revs) {
            const double ang = std::atan2(ca.cmdy[r.idx] - ca.refCy,
                                          ca.cmdx[r.idx] - ca.refCx)
                               * 180.0 / 3.14159265358979;
            std::printf("  rev axis=%s idx=%d dev=%+.4f ang=%.1f deg "
                        "cmd=(%.4f,%.4f) fb=(%.4f,%.4f) t=%.3f\n",
                        r.axis == 0 ? "X" : "Y", r.idx, r.dev, ang,
                        ca.cmdx[r.idx], ca.cmdy[r.idx],
                        ca.fx[r.idx], ca.fy[r.idx], ca.time[r.idx]);
        }
    }

    const DiagnosisResult res =
        Diagnosis::run(data, ca, ServoParams(), "test");
    for (const AxisKvEstimate &e : res.axes)
        std::printf("kv[%s]: reg=%.1f(r2=%.2f,n=%d) lag=%.1f best=%.1f\n",
                    qPrintable(e.axis), e.kvReg, e.r2, e.samples,
                    e.kvLag, e.best());
    if (res.shrink.valid)
        std::printf("shrink: kv=%.1f dR=%.4f%s\n", res.shrink.kv,
                    res.shrink.deltaR,
                    res.shrink.lowerBoundOnly ? " (lower bound)" : "");
    else
        std::printf("shrink: n/a (%s)\n", qPrintable(res.shrink.note));
    check(res.html.size() > 2000, "报告 HTML 非空", res.html.size(), 2000, 0);

    // TEST_EXPORT=1 时导出报告供人工检查(含录入参数的完整路径)
    if (qEnvironmentVariableIsSet("TEST_EXPORT")) {
        ServoParams sp;
        sp.brandIndex = servo_brands::indexOfId("fanuc");
        AxisParams ax;
        ax.posLoopGain = 3000; ax.hasGain = true;   // 30/s
        ax.velFF = 0; ax.hasFF = true;
        ax.frictionComp = 10; ax.hasFrictionComp = true;
        sp.axes["X"] = ax;
        ax.velFF = 95; sp.axes["Y"] = ax;
        const DiagnosisResult res2 = Diagnosis::run(data, ca, sp, "export");
        QFile f("._report_export.html");
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                    "</head><body>");
            f.write(res2.html.toUtf8());
            f.write("</body></html>");
            std::printf("report exported: ._report_export.html\n");
        }
    }

    // 离屏渲染:圆度图 + 误差图 + 诊断对话框
    CircularityWidget w;
    w.resize(900, 700);
    w.setData(data);
    w.grab();
    w.setMagnification(100);
    w.grab();

    ChartView cv;
    cv.resize(1000, 600);
    cv.setAxisData(data);
    cv.grab();
    cv.setDisplayMode(ChartView::Mode::VelError);
    cv.grab();

    DiagnosisDialog dlg;
    dlg.resize(950, 850);
    dlg.updateData(data, QString());   // 空路径:测试中不写 sidecar
    dlg.grab();
    std::printf("render: ok\n");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    testSynthetic();
    testParamsJson();

    for (int i = 1; i < argc; ++i)
        testRealFile(argv[i]);

    std::printf("\n%s: %d failure(s)\n",
                g_failures == 0 ? "ALL PASS" : "FAILED", g_failures);
    std::fflush(stdout);
    return g_failures;
}
