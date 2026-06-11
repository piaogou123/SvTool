#pragma once
#include <QColor>
#include <QString>

// 四个测量方向(X / Y / 两条对角线)的统一定义,
// 供 circularity_widget 与 direction_dialog 共用,避免颜色/文案多处重复。
namespace dir_defs {

constexpr int kCount = 4;
constexpr double kPi = 3.14159265358979323846;

// 方向角(弧度): X=0°, Y=90°, 对角线1=45°, 对角线2=135°
inline double angle(int i)
{
    static const double a[kCount] = { 0.0, kPi / 2.0, kPi / 4.0, 3.0 * kPi / 4.0 };
    return (i >= 0 && i < kCount) ? a[i] : 0.0;
}

inline QColor color(int i)
{
    static const QColor c[kCount] = {
        QColor(220,  30,  30),   // X — 红
        QColor(  0, 150,   0),   // Y — 绿
        QColor(240, 140,   0),   // 对角线1 — 橙
        QColor(140,  20, 200),   // 对角线2 — 紫
    };
    return (i >= 0 && i < kCount) ? c[i] : QColor(Qt::black);
}

inline QString label(int i)
{
    switch (i) {
    case 0: return QString::fromUtf8("X (0°↔180°)");
    case 1: return QString::fromUtf8("Y (90°↔−90°)");
    case 2: return QString::fromUtf8("对角线1 (45°↔−135°)");
    case 3: return QString::fromUtf8("对角线2 (135°↔−45°)");
    default: return QString();
    }
}

// 朝白色方向混合 pct%(0=原色,100=纯白),用于浅色背景/淡化线条
inline QColor blendWhite(const QColor &c, int pct)
{
    return QColor(c.red()   + (255 - c.red())   * pct / 100,
                  c.green() + (255 - c.green()) * pct / 100,
                  c.blue()  + (255 - c.blue())  * pct / 100);
}

}  // namespace dir_defs
