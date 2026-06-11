#include "data_loader.h"

#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringList>
#include <QVarLengthArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();
}

bool DataLoader::loadCsv(const QString &filePath, Dataset &out, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString::fromUtf8("无法打开文件:%1").arg(filePath);
        return false;
    }

    QTextStream stream(&file);
    QString headerLine = stream.readLine();
    // 去掉可能存在的 UTF-8 BOM
    if (!headerLine.isEmpty() && headerLine.at(0) == QChar(0xFEFF))
        headerLine.remove(0, 1);

    if (headerLine.isEmpty()) {
        if (error) *error = QString::fromUtf8("文件为空或缺少标题行。");
        return false;
    }

    // --- 解析标题行,建立列映射 ------------------------------------------
    // 匹配 axes.mach.<l|f>.<p|v>[<轴名>]
    //   l = 指令(command), f = 反馈(feedback)
    //   p = 位置,          v = 速度
    static const QRegularExpression axisRe(
        R"(axes\.mach\.([lf])\.([pv])\[(\w+)\])");

    QStringList headers = headerLine.split(',');
    int timeCol = -1;

    struct ColInfo { QString axis; bool isCmd; bool isVel; };
    QMap<int, ColInfo> colMap;   // 列号 → (轴名, 指令/反馈, 位置/速度)

    for (int ci = 0; ci < headers.size(); ++ci) {
        QString h = headers.at(ci).trimmed();
        auto m = axisRe.match(h);
        if (m.hasMatch()) {
            colMap[ci] = { m.captured(3),
                           m.captured(1) == QLatin1String("l"),
                           m.captured(2) == QLatin1String("v") };
        } else if (h.compare(QLatin1String("FINISHED"), Qt::CaseInsensitive) == 0
                   || h.compare(QLatin1String("TIME"), Qt::CaseInsensitive) == 0) {
            timeCol = ci;
        }
    }

    // 兼容格式:未找到 axes.mach.* 列时按固定 8 列 X/Y/C 结构处理
    if (colMap.isEmpty()) {
        timeCol = 1;
        colMap[2] = { "X", true,  false };
        colMap[3] = { "Y", true,  false };
        colMap[4] = { "C", true,  false };
        colMap[5] = { "X", false, false };
        colMap[6] = { "Y", false, false };
        colMap[7] = { "C", false, false };
    }

    if (timeCol < 0) {
        if (error) *error = QString::fromUtf8(
            "在标题行中未找到时间列(TIME / FINISHED)。");
        return false;
    }

    // --- 初始化数据集 ------------------------------------------------------
    out = Dataset();

    // 按位置指令列的出现顺序发现轴
    for (auto it = colMap.begin(); it != colMap.end(); ++it) {
        const ColInfo &info = it.value();
        if (info.isCmd && !info.isVel && !out.axes.contains(info.axis)) {
            out.axes[info.axis] = AxisChannel();
            out.axisOrder.append(info.axis);
        }
    }
    if (out.axisOrder.isEmpty()) {
        if (error) *error = QString::fromUtf8(
            "标题行中未找到任何轴的位置指令列。");
        return false;
    }

    // 列号 → 目标向量指针表:建一次表,逐行解析时零查找开销。
    // QMap 节点地址稳定,此后不再插入新轴,取指针安全。
    struct ColTarget { int col; QVector<double> *vec; };
    QVector<ColTarget> targets;
    targets.reserve(colMap.size());
    int maxCol = timeCol;
    for (auto it = colMap.begin(); it != colMap.end(); ++it) {
        const ColInfo &info = it.value();
        auto axIt = out.axes.find(info.axis);
        if (axIt == out.axes.end()) continue;   // 只有速度列、没有位置指令列的轴
        AxisChannel &ch = axIt.value();
        QVector<double> *vec = info.isVel
            ? (info.isCmd ? &ch.cmdVel : &ch.fbVel)
            : (info.isCmd ? &ch.cmd    : &ch.fb);
        targets.append({ it.key(), vec });
        maxCol = std::max(maxCol, it.key());
    }

    // 按文件大小估算行数,减少扩容次数
    const int estRows = static_cast<int>(file.size() / 80) + 256;
    out.time.reserve(estRows);
    for (const ColTarget &t : targets)
        t.vec->reserve(estRows);

    // --- 逐行读取数据 -------------------------------------------------------
    QVarLengthArray<double, 32> rowVals(targets.size());
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty()) continue;

        const QVector<QStringRef> fields = line.splitRef(',');
        if (fields.size() <= maxCol) continue;

        // 先把整行解析到临时缓冲,全部成功才提交,避免逐列回退
        bool ok = false;
        double t = fields.at(timeCol).trimmed().toDouble(&ok);
        if (!ok) continue;     // 单位行 / 非数据行

        for (int k = 0; k < targets.size(); ++k) {
            rowVals[k] = fields.at(targets[k].col).trimmed().toDouble(&ok);
            if (!ok) break;
        }
        if (!ok) continue;

        out.time.append(t);
        for (int k = 0; k < targets.size(); ++k)
            targets[k].vec->append(rowVals[k]);
    }

    if (out.isEmpty()) {
        if (error) *error = QString::fromUtf8("文件中未找到有效数据行。");
        return false;
    }

    // --- 计算误差 ------------------------------------------------------------
    const int n = out.time.size();
    for (auto &name : out.axisOrder) {
        AxisChannel &ch = out.axes[name];
        ch.err.resize(n);
        for (int i = 0; i < n; ++i)
            ch.err[i] = ch.cmd[i] - ch.fb[i];

        // 速度误差:指令/反馈速度列齐全时才计算
        if (ch.cmdVel.size() == n && ch.fbVel.size() == n) {
            ch.velErr.resize(n);
            for (int i = 0; i < n; ++i)
                ch.velErr[i] = ch.cmdVel[i] - ch.fbVel[i];
        } else {
            ch.cmdVel.clear();
            ch.fbVel.clear();
            ch.velErr.clear();
        }
    }

    computeResponseTime(out);
    return true;
}

// --- 响应时间计算 -----------------------------------------------------------

static bool commandIsStaticAt(const QVector<double> &cmd, int i, double eps)
{
    const int n = cmd.size();
    const bool sameAsPrev = (i == 0) || (std::abs(cmd[i] - cmd[i - 1]) <= eps);
    const bool sameAsNext = (i == n - 1) || (std::abs(cmd[i + 1] - cmd[i]) <= eps);
    return sameAsPrev && sameAsNext;
}

// Local direction at each point: sign of change over a short backward
// window.  Uses simple slope (not cumulative hysteresis) so direction is
// responsive at reversal boundaries and won't report a stale state.
static QVector<int> localDirection(const QVector<double> &signal,
                                   double eps, int window)
{
    int n = signal.size();
    QVector<int> dir(n, 0);
    for (int i = window; i < n; ++i) {
        double diff = signal[i] - signal[i - window];
        if (diff > eps) dir[i] = 1;
        else if (diff < -eps) dir[i] = -1;
    }
    return dir;
}

static void computeOneAxis(const QVector<double> &time,
                           const QVector<double> &cmd,
                           const QVector<double> &fb,
                           QVector<double> &lagOut,
                           QVector<int>    &idxOut,
                           ResponseStats   &statsOut,
                           int maxAhead)
{
    int n = time.size();
    lagOut.resize(n);
    idxOut.resize(n);

    // Compute command range to derive a reversal-detection threshold.
    double cmdMin = cmd[0], cmdMax = cmd[0];
    for (int i = 1; i < n; ++i) {
        if (cmd[i] < cmdMin) cmdMin = cmd[i];
        if (cmd[i] > cmdMax) cmdMax = cmd[i];
    }
    double cmdRange = cmdMax - cmdMin;
    // Direction-detection threshold: scales with axis range so that the
    // same relative motion amplitude is recognised regardless of units.
    // Used in localDirection() and all direction-fallback comparisons.
    double eps = std::max(cmdRange * 0.0001, 0.005);
    // Tracking-exit threshold: how far fb must retreat from its running
    // extremum before the forward search is abandoned.  Fixed at 0.005 mm
    // so that large-range files (where eps would be >> 0.005) still get a
    // tight exit and cannot scan past a genuine reversal into the arc
    // return segment (root cause of spurious 100+ ms lag values).
    const double kTrackEps = 0.005;
    double staticEps = std::max(cmdRange * 1e-9, 1e-9);

    // Per-point direction so forward search only considers fb samples
    // that are already moving in the same direction as cmd.
    // Window of 2 samples keeps the check responsive at reversals.
    QVector<int> cmdDir = localDirection(cmd, eps, 2);
    QVector<int> fbDir = localDirection(fb, eps, 2);
    // Wider window (8 samples) supplements fbDir when per-step changes are
    // too small for the 2-sample window to detect — the wide window
    // accumulates enough displacement to recognise slow drifts.
    QVector<int> fbDirWide = localDirection(fb, eps, 8);

    // 运动点的有效响应延迟集合(用于统计;静止点不参与)
    QVector<double> validLags;
    validLags.reserve(n);
    int noResp = 0;

    for (int i = 0; i < n; ++i) {
        if (commandIsStaticAt(cmd, i, staticEps)) {
            lagOut[i] = 0.0;
            idxOut[i] = i;
            continue;
        }

        double target = cmd[i];
        int jEnd = std::min(i + maxAhead, n);
        bool foundAny = false;

        int dir = cmdDir[i];
        // When cmd is moving too slowly for per-sample direction detection,
        // fall back to fb's direction so the search is still constrained.
        if (dir == 0)
            dir = fbDir[i];
        // When both per-sample directions are 0 (slow movement below the
        // 2-sample eps threshold), try progressively wider windows to
        // determine an overall trend.  Prefer cmd — it encodes the
        // intended direction of motion.
        if (dir == 0) {
            for (int w : {4, 8, 16, 32}) {
                if (i >= w) {
                    double d = cmd[i] - cmd[i - w];
                    if (d > eps) { dir = 1; break; }
                    if (d < -eps) { dir = -1; break; }
                }
            }
        }
        if (dir == 0) {
            for (int w : {4, 8, 16, 32}) {
                if (i >= w) {
                    double d = fb[i] - fb[i - w];
                    if (d > eps) { dir = 1; break; }
                    if (d < -eps) { dir = -1; break; }
                }
            }
        }
        // Forward-looking cmd fallback: at the static→moving boundary,
        // every backward window falls inside the static segment and returns
        // diff = 0.  Look ahead in cmd (all data is already available) to
        // detect the direction of the upcoming motion.  This prevents
        // dir = 0 from leaving the forward search unconstrained, which
        // would allow a spurious arc-return match hundreds of ms later.
        if (dir == 0) {
            for (int w : {4, 8, 16, 32}) {
                if (i + w < n) {
                    double d = cmd[i + w] - cmd[i];
                    if (d > eps) { dir = 1; break; }
                    if (d < -eps) { dir = -1; break; }
                }
            }
        }

        // When fb is already past the target in the direction of motion
        // (overshoot from the previous segment), there is no meaningful
        // forward response to find.  Return lag = 0.
        if (dir != 0) {
            bool fb_past = (dir == 1 && fb[i] >= target) ||
                           (dir == -1 && fb[i] <= target);
            if (fb_past) {
                lagOut[i] = 0.0;
                idxOut[i] = i;
                validLags.append(0.0);
                continue;
            }
        }

        // When fb at position i is moving opposite to cmd, the numerical
        // proximity is coincidental rather than a true response.  Reject
        // j=i as a candidate so the forward search finds the actual
        // response after fb reverses direction.
        int fbDirAtI = fbDir[i];
        if (fbDirAtI == 0)
            fbDirAtI = fbDirWide[i];
        bool fbAtIOpposite =
            (dir != 0 && fbDirAtI != 0 && fbDirAtI != dir);

        double bestDiff;
        int bestJ;
        if (fbAtIOpposite) {
            bestDiff = std::numeric_limits<double>::max();
            bestJ = i;  // fallback if forward search finds nothing
        } else {
            bestDiff = std::abs(fb[i] - target);
            bestJ = i;
        }

        bool tracking = false;
        double fbExt = 0;
        bool reachedTarget = false;

        for (int j = i + 1; j < jEnd; ++j) {
            // Resolve fb's effective direction: prefer the responsive
            // 2-sample window, but fall back to the 8-sample window when
            // the narrow window reports 0 (per-step change below eps).
            int fbEffDir = fbDir[j];
            if (fbEffDir == 0)
                fbEffDir = fbDirWide[j];

            // When cmd is rising but fb is still falling (or vice versa),
            // fb has not yet responded to the command — matching on value
            // alone would pick a spurious crossing point.
            if (dir != 0 && fbEffDir != 0 && fbEffDir != dir) {
                // fb direction has reversed relative to the original cmd
                // direction.  If we already found valid candidates (fb was
                // moving in the correct direction), this reversal marks the
                // end of the useful search window.
                if (foundAny)
                    break;
                continue;
            }

            foundAny = true;

            // Cumulative retreat from running extremum catches slow fb
            // reversals that the per-sample direction filter above misses
            // because per-step changes stay below eps over a 2-sample window.
            if (dir != 0) {
                if (!tracking) {
                    tracking = true;
                    fbExt = fb[j];
                } else if (dir == 1) {
                    if (fb[j] > fbExt)
                        fbExt = fb[j];
                    else if (fbExt - fb[j] > kTrackEps)
                        break;
                } else {  // dir == -1
                    if (fb[j] < fbExt)
                        fbExt = fb[j];
                    else if (fb[j] - fbExt > kTrackEps)
                        break;
                }
            }

            double diff = std::abs(fb[j] - target);
            if (diff < bestDiff) {
                bestDiff = diff;
                bestJ = j;
            }
            // Stop at the first crossing of the target in the commanded
            // direction.  After fb crosses, further oscillation can produce
            // a closer match on the return pass, which would be reported as
            // a spuriously longer lag.
            if (dir != 0 &&
                ((dir == 1 && fb[j] >= target) ||
                 (dir == -1 && fb[j] <= target))) {
                reachedTarget = true;
                break;
            }
        }

        // A response point must be an actual same-direction arrival at the
        // target.  Near a segment peak, fb can lag so far behind lp that it
        // reverses before ever reaching lp[i]; lag is NaN ("未找到响应")
        // instead of 0 so it cannot be confused with an instant response.
        if (dir != 0 && !reachedTarget) {
            lagOut[i] = kNaN;
            idxOut[i] = i;
            ++noResp;
            continue;
        }

        lagOut[i] = time[bestJ] - time[i];
        idxOut[i] = bestJ;
        validLags.append(lagOut[i]);
    }

    // --- 响应延迟统计(仅运动点) -----------------------------------------
    statsOut = ResponseStats();
    statsOut.noRespCount = noResp;
    const int m = validLags.size();
    statsOut.movingCount = m;
    if (m == 0)
        return;

    const auto mm = std::minmax_element(validLags.begin(), validLags.end());
    statsOut.min = *mm.first;
    statsOut.max = *mm.second;

    double sum = 0;
    for (double v : validLags) sum += v;
    statsOut.avg = sum / m;

    double sqSum = 0;
    for (double v : validLags) sqSum += (v - statsOut.avg) * (v - statsOut.avg);
    statsOut.stdDev = std::sqrt(sqSum / m);

    QVector<double> sorted = validLags;
    if (m % 2 == 1) {
        std::nth_element(sorted.begin(), sorted.begin() + m / 2, sorted.end());
        statsOut.median = sorted[m / 2];
    } else {
        std::nth_element(sorted.begin(), sorted.begin() + m / 2 - 1, sorted.end());
        const double lower = sorted[m / 2 - 1];
        const double upper = *std::min_element(sorted.begin() + m / 2,
                                               sorted.end());
        statsOut.median = (lower + upper) / 2.0;
    }
    statsOut.valid = true;
}

void DataLoader::computeResponseTime(Dataset &data, int maxLookaheadSamples)
{
    if (data.isEmpty()) return;
    if (maxLookaheadSamples < 1) maxLookaheadSamples = 1;

    for (auto &name : data.axisOrder) {
        AxisChannel &ch = data.axes[name];
        computeOneAxis(data.time, ch.cmd, ch.fb,
                       ch.respLag, ch.bestIdx, ch.stats, maxLookaheadSamples);

        // 位置误差统计(全程)
        double maxAbs = 0, sq = 0;
        for (double e : ch.err) {
            double a = std::abs(e);
            if (a > maxAbs) maxAbs = a;
            sq += e * e;
        }
        ch.stats.maxAbsErr = maxAbs;
        ch.stats.rmsErr = ch.err.isEmpty() ? 0.0
                                           : std::sqrt(sq / ch.err.size());
    }
}
