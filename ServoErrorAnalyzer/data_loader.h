#pragma once
#include <QVector>
#include <QString>
#include <QStringList>
#include <QMap>

// 单轴全程统计(载入文件时计算一次)
struct ResponseStats {
    // 响应延迟统计(仅统计指令运动中且找到有效响应的采样点,单位:秒)
    double avg    = 0;
    double max    = 0;
    double min    = 0;
    double median = 0;
    double stdDev = 0;
    int    movingCount = 0;   // 参与统计的运动采样点数
    int    noRespCount = 0;   // 未找到有效响应的运动采样点数(lag = NaN)
    // 位置误差统计
    double maxAbsErr = 0;     // max |cmd - fb|
    double rmsErr    = 0;     // RMS(cmd - fb)
    bool   valid = false;     // movingCount > 0
};

struct DirectionStats {
    QString name;           // 例如 "X (0°↔180°)"
    double  cmdSize  = 0;   // 指令尺寸: max(cmd_proj) - min(cmd_proj)
    double  fbSize   = 0;   // 反馈尺寸: max(fb_proj)  - min(fb_proj)
    double  sizeErr  = 0;   // 尺寸偏差: fbSize - cmdSize
    bool    valid    = false;
};

struct AxisChannel {
    QVector<double> cmd, fb, err;           // 位置指令 / 反馈 / 误差
    QVector<double> cmdVel, fbVel, velErr;  // 速度指令 / 反馈 / 误差(文件无速度列时为空)
    QVector<double> respLag;   // 响应延迟(秒);NaN = 运动中但未找到有效响应
    QVector<int>    bestIdx;
    ResponseStats   stats;

    bool hasVelocity() const { return !velErr.isEmpty(); }
};

struct Dataset {
    QVector<double> time;
    QMap<QString, AxisChannel> axes;   // key = 轴名(如 "X", "Y", "C", "A", "Z")
    QStringList axisOrder;             // CSV 标题行中的出现顺序

    bool isEmpty() const { return time.isEmpty(); }
    int size() const { return time.size(); }
};

class DataLoader {
public:
    static bool loadCsv(const QString &filePath, Dataset &out, QString *error = nullptr);
    static void computeResponseTime(Dataset &data, int maxLookaheadSamples = 500);
};
