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
    QString unitsLine  = stream.readLine();

    if (headerLine.isEmpty()) {
        if (error) *error = "File is empty or missing header.";
        return false;
    }

    // Detect if second row is a units row
    bool hasUnitsRow = unitsLine.startsWith("units,") || unitsLine.startsWith("units\t");
    int dataStartLine = hasUnitsRow ? 2 : 1;

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
        } else if (h.compare("FINISHED", Qt::CaseInsensitive) == 0) {
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
    int lineNum = dataStartLine;
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;

        QStringList fields = line.split(',');
        if (fields.size() <= timeCol || fields.size() <= colMap.lastKey()) {
            ++lineNum;
            continue;
        }

        bool ok = true;
        double t = fields.at(timeCol).trimmed().toDouble(&ok);
        if (!ok) { ++lineNum; continue; }

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
            ++lineNum;
            continue;
        }

        ++lineNum;
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
// The returned indices are the extrema — the last point before reversal.
static QVector<int> findTurningPoints(const QVector<double> &cmd, double eps)
{
    QVector<int> turns;
    int n = cmd.size();
    if (n < 3) return turns;

    int state = 0;               // 0=unknown, 1=up, -1=down
    double extremum = cmd[0];
    int extremumIdx = 0;

    for (int i = 1; i < n; ++i) {
        double step = cmd[i] - cmd[i - 1];

        if (step > eps) {
            if (state == -1) {
                turns.append(extremumIdx);
                extremum = cmd[i];
                extremumIdx = i;
            } else if (cmd[i] > extremum) {
                extremum = cmd[i];
                extremumIdx = i;
            }
            state = 1;
        } else if (step < -eps) {
            if (state == 1) {
                turns.append(extremumIdx);
                extremum = cmd[i];
                extremumIdx = i;
            } else if (cmd[i] < extremum) {
                extremum = cmd[i];
                extremumIdx = i;
            }
            state = -1;
        }
    }

    return turns;
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

    // Pre-compute turning points as hard segment boundaries.
    // Each point's search is confined to [i+1, nextTurningPoint].
    QVector<int> turns = findTurningPoints(cmd, eps);
    int tpCursor = 0;  // index into turns

    for (int i = 0; i < n; ++i) {
        double target = cmd[i];
        double bestDiff = std::abs(fb[i] - target);
        int bestJ = i;

        // Advance cursor so turns[tpCursor] is the first turn strictly after i
        while (tpCursor < turns.size() && turns[tpCursor] <= i)
            ++tpCursor;

        int segEnd = (tpCursor < turns.size()) ? turns[tpCursor] : n;
        int jEnd = std::min({i + maxAhead, segEnd, n});

        for (int j = i + 1; j < jEnd; ++j) {
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

    QVector<double> sorted = lagOut;
    std::sort(sorted.begin(), sorted.end());

    statsOut.min = sorted.first();
    statsOut.max = sorted.last();

    if (n % 2 == 1)
        statsOut.median = sorted[n / 2];
    else
        statsOut.median = (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;

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
