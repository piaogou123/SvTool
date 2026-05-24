#pragma once
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>
#include <algorithm>
#include <cmath>

struct ResponseStats {
    double avg    = 0;
    double max    = 0;
    double min    = 0;
    double median = 0;
    double stdDev = 0;
};

struct DirectionStats {
    QString name;          // e.g. "X (0°↔180°)"
    double  size     = 0;  // stroke: max(cmd_proj) - min(cmd_proj)
    double  errMax   = 0;  // max signed error (cmd - fb)
    double  errMin   = 0;  // min signed error
    double  errAvg   = 0;  // mean signed error
    double  errStdDev = 0; // std dev of error
    bool    valid    = false;
};

struct AxisChannel {
    QVector<double> cmd, fb, err;
    QVector<double> respLag;
    QVector<int>    bestIdx;
    ResponseStats   stats;
};

struct Dataset {
    QVector<double> time;
    QMap<QString, AxisChannel> axes;   // key = axis name (e.g. "X", "Y", "C", "A", "Z")
    QStringList axisOrder;             // discovery order from CSV header

    bool isEmpty() const { return time.isEmpty(); }
    int size() const { return time.size(); }
};

class DataLoader {
public:
    static bool loadCsv(const QString &filePath, Dataset &out, QString *error = nullptr);
    static void computeResponseTime(Dataset &data, int maxLookaheadSamples = 500);
    static QVector<DirectionStats> computeDirectionStats(const Dataset &data);
};
