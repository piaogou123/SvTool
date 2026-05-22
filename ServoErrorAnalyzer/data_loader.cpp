#include "data_loader.h"
#include <QFile>
#include <QTextStream>
#include <QStringList>
#include <QRegularExpression>
#include <algorithm>

bool DataLoader::loadCsv(const QString &filePath, Dataset &out, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QString("Cannot open file: %1").arg(filePath);
        return false;
    }

    QTextStream stream(&file);
    QString headerLine = stream.readLine();

    if (headerLine.isEmpty()) {
        if (error) *error = "File is empty or missing header.";
        return false;
    }

    // --- Parse header to build column map -------------------------------
    // Pattern: axes.mach.<l|f>.p[<axisName>]
    static const QRegularExpression axisRe(
        R"(axes\.mach\.([lf])\.p\[(\w+)\])");

    QStringList headers = headerLine.split(',');
    int timeCol = -1;

    enum ColRole { RoleCmd, RoleFb };
    struct ColInfo { QString axis; ColRole role; };
    QMap<int, ColInfo> colMap;   // column index → (axis, cmd|fb)

    for (int ci = 0; ci < headers.size(); ++ci) {
        QString h = headers.at(ci).trimmed();
        auto m = axisRe.match(h);
        if (m.hasMatch()) {
            QString axisName = m.captured(2);          // e.g. "X", "A", "Z"
            bool isCmd = (m.captured(1) == "l");    // "l" = command, "f" = feedback
            colMap[ci] = { axisName, isCmd ? RoleCmd : RoleFb };
        } else if (h.compare("FINISHED", Qt::CaseInsensitive) == 0
                   || h.compare("TIME", Qt::CaseInsensitive) == 0) {
            timeCol = ci;
        }
    }

    // Fallback: if no axes.mach.* columns found, assume fixed 8-column
    // X/Y/C format: FINISHED(1), time(1), xCmd(2), yCmd(3), cCmd(4), xFb(5), yFb(6), cFb(7)
    bool useFallback = colMap.isEmpty();
    if (useFallback) {
        timeCol = 1;
        colMap[2] = { "X", RoleCmd };
        colMap[3] = { "Y", RoleCmd };
        colMap[4] = { "C", RoleCmd };
        colMap[5] = { "X", RoleFb };
        colMap[6] = { "Y", RoleFb };
        colMap[7] = { "C", RoleFb };
    }

    if (timeCol < 0) {
        if (error) *error = "Cannot find time column (FINISHED) in header.";
        return false;
    }

    // --- Initialise dataset ---------------------------------------------
    out = Dataset();
    QMap<QString, AxisChannel> &axes = out.axes;
    QStringList &axisOrder = out.axisOrder;

    // Discover axes in header order
    for (int ci = 0; ci < headers.size(); ++ci) {
        auto it = colMap.find(ci);
        if (it != colMap.end() && it->role == RoleCmd) {
            const QString &name = it->axis;
            if (!axes.contains(name)) {
                axes[name] = AxisChannel();
                axisOrder.append(name);
            }
        }
    }

    int estRows = 10000;
    out.time.reserve(estRows);
    for (auto &name : axisOrder) {
        AxisChannel &ch = axes[name];
        ch.cmd.reserve(estRows);
        ch.fb.reserve(estRows);
        ch.err.reserve(estRows);
    }

    // --- Read data rows -------------------------------------------------
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = line.split(',');
        if (fields.size() <= timeCol || fields.size() <= colMap.lastKey()) {
            continue;
        }

        bool ok = true;
        double t = fields.at(timeCol).trimmed().toDouble(&ok);
        if (!ok) { continue; }

        out.time.append(t);

        // Read per-axis cmd/fb via column map
        for (auto it = colMap.begin(); it != colMap.end(); ++it) {
            int ci = it.key();
            const ColInfo &info = it.value();
            if (!axes.contains(info.axis)) continue;
            double val = fields.at(ci).trimmed().toDouble(&ok);
            if (!ok) break;
            if (info.role == RoleCmd)
                axes[info.axis].cmd.append(val);
            else
                axes[info.axis].fb.append(val);
        }
        if (!ok) {
            // Back out this row if a field failed
            out.time.removeLast();
            for (auto &name : axisOrder) {
                AxisChannel &ch = axes[name];
                if (ch.cmd.size() > out.time.size()) ch.cmd.removeLast();
                if (ch.fb.size() > out.time.size()) ch.fb.removeLast();
            }
            continue;
        }

    }

    if (out.isEmpty()) {
        if (error) *error = "No valid data rows found in file.";
        return false;
    }

    // Compute errors
    for (auto &name : axisOrder) {
        AxisChannel &ch = axes[name];
        int n = ch.cmd.size();
        ch.err.resize(n);
        for (int i = 0; i < n; ++i)
            ch.err[i] = ch.cmd[i] - ch.fb[i];
    }

    computeResponseTime(out);
    return true;
}

// --- Response-time computation -----------------------------------------

// Find indices where the command signal changes direction (peaks/valleys).
// Uses cumulative retreat from the running extremum, not single-step size,
// so that gradual reversals are detected promptly.
static QVector<int> findTurningPoints(const QVector<double> &cmd, double eps)
{
    QVector<int> turns;
    int n = cmd.size();
    if (n < 3) return turns;

    int state = 0;               // 0=unknown, 1=up, -1=down
    double extremum = cmd[0];
    int extremumIdx = 0;
    double startVal = cmd[0];

    for (int i = 1; i < n; ++i) {
        if (state == 0) {
            // Use cumulative displacement from start to determine initial direction.
            // Single-step threshold would miss slow accelerations from standstill.
            double disp = cmd[i] - startVal;
            if (disp > eps) {
                state = 1;
                extremum = cmd[i];
                extremumIdx = i;
            } else if (disp < -eps) {
                state = -1;
                extremum = cmd[i];
                extremumIdx = i;
            }
        } else if (state == 1) {
            if (cmd[i] > extremum) {
                extremum = cmd[i];
                extremumIdx = i;
            } else if (extremum - cmd[i] > eps) {
                // Cumulative retreat from peak exceeds threshold → reversal
                turns.append(extremumIdx);
                state = -1;
                extremum = cmd[i];
                extremumIdx = i;
            }
        } else {  // state == -1
            if (cmd[i] < extremum) {
                extremum = cmd[i];
                extremumIdx = i;
            } else if (cmd[i] - extremum > eps) {
                // Cumulative rise from valley exceeds threshold → reversal
                turns.append(extremumIdx);
                state = 1;
                extremum = cmd[i];
                extremumIdx = i;
            }
        }
    }

    return turns;
}

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
static QVector<int> localDirection(const QVector<double>& signal,
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
                           ResponseStats &statsOut,
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
    double eps = std::max(cmdRange * 0.0001, 0.005);
    double staticEps = std::max(cmdRange * 1e-9, 1e-9);

    // Per-point direction so forward search only considers fb samples
    // that are already moving in the same direction as cmd.
    // Window of 2 samples keeps the check responsive at reversals.
    QVector<int> cmdDir = localDirection(cmd, eps, 2);
    QVector<int> fbDir = localDirection(fb, eps, 2);

    for (int i = 0; i < n; ++i) {
        if (commandIsStaticAt(cmd, i, staticEps)) {
            lagOut[i] = 0.0;
            idxOut[i] = i;
            continue;
        }

        double target = cmd[i];
        double bestDiff = std::abs(fb[i] - target);
        int bestJ = i;
        int jEnd = std::min(i + maxAhead, n);
        bool foundAny = false;

        int dir = cmdDir[i];
        // When cmd is moving too slowly for per-sample direction detection,
        // fall back to fb's direction so the search is still constrained.
        if (dir == 0)
            dir = fbDir[i];
        bool tracking = false;
        double fbExt = 0;

        for (int j = i + 1; j < jEnd; ++j) {
            // When cmd is rising but fb is still falling (or vice versa),
            // fb has not yet responded to the command — matching on value
            // alone would pick a spurious crossing point.
            if (dir != 0 && fbDir[j] != 0 && fbDir[j] != dir) {
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
                    else if (fbExt - fb[j] > eps)
                        break;
                } else {  // dir == -1
                    if (fb[j] < fbExt)
                        fbExt = fb[j];
                    else if (fb[j] - fbExt > eps)
                        break;
                }
            }

            double diff = std::abs(fb[j] - target);
            if (diff < bestDiff) {
                bestDiff = diff;
                bestJ = j;
            }
        }

        lagOut[i] = time[bestJ] - time[i];
        idxOut[i] = bestJ;
    }

    // Statistics
    if (n == 0) return;

    // Min/max via single O(n) pass
    const auto minMaxPair = std::minmax_element(lagOut.begin(), lagOut.end());
    statsOut.min = *minMaxPair.first;
    statsOut.max = *minMaxPair.second;

    // Median via std::nth_element (O(n) average) instead of full sort
    QVector<double> sorted = lagOut;
    if (n % 2 == 1) {
        std::nth_element(sorted.begin(), sorted.begin() + n / 2, sorted.end());
        statsOut.median = sorted[n / 2];
    } else {
        std::nth_element(sorted.begin(), sorted.begin() + n / 2 - 1, sorted.end());
        const double lower = sorted[n / 2 - 1];
        const double upper = *std::min_element(sorted.begin() + n / 2, sorted.end());
        statsOut.median = (lower + upper) / 2.0;
    }

    double sum = 0;
    for (double v : lagOut) sum += v;
    statsOut.avg = sum / n;

    double sqSum = 0;
    for (double v : lagOut) sqSum += (v - statsOut.avg) * (v - statsOut.avg);
    statsOut.stdDev = std::sqrt(sqSum / n);
}

void DataLoader::computeResponseTime(Dataset &data, int maxLookaheadSamples)
{
    if (data.isEmpty()) return;
    if (maxLookaheadSamples < 1) maxLookaheadSamples = 1;

    for (auto &name : data.axisOrder) {
        AxisChannel &ch = data.axes[name];
        computeOneAxis(data.time, ch.cmd, ch.fb,
                       ch.respLag, ch.bestIdx, ch.stats, maxLookaheadSamples);
    }
}
