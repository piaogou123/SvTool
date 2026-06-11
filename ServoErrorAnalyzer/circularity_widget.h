#pragma once
#include <QWidget>
#include <QVector>
#include <QPixmap>
#include "circle_analysis.h"
#include "data_loader.h"

// XY 双轴真圆度图(显示层;分析计算见 circle_analysis.h):
//  - 指令轨迹最小二乘圆拟合(LSC)作为基准圆,自动剔除进刀/退刀段
//  - 反馈轨迹径向误差可放大显示(圆度仪式显示)
//  - 换向点(象限过渡)毛刺标注
//  - 四方向卡尺尺寸标注
class CircularityWidget : public QWidget {
    Q_OBJECT
public:
    explicit CircularityWidget(QWidget *parent = nullptr);
    void setData(const Dataset &data);

    // 显示开关(由对话框工具栏勾选项驱动)
    void setCommandVisible(bool on);   // 指令(位置)轨迹
    void setFeedbackVisible(bool on);  // 反馈轨迹
    void setReferenceVisible(bool on); // 基准圆(指令拟合圆)
    void setReversalVisible(bool on);  // 换向毛刺标记
    // 方向尺寸标注: dir 0=X, 1=Y, 2=对角线1, 3=对角线2
    void setSizeVisible(int dir, bool on);
    // 径向误差放大倍数(≥1;>1 时仅绘制圆弧段)
    void setMagnification(double mag);
    double magnification() const { return m_mag; }

    // 指标访问(基于剔除进退刀后的反馈轨迹)
    bool   hasData()    const { return m_hasData; }
    double roundness()  const { return m_an.roundness; }
    double avgRadius()  const { return m_an.avgRadius; }
    double minRadius()  const { return m_an.minRadius; }
    double maxRadius()  const { return m_an.maxRadius; }
    // 换向毛刺:所有换向点中最大径向偏差绝对值(mm);无换向点返回 -1
    double maxReversalDev() const { return m_an.maxReversalDev(); }
    // 四个方向的指令/反馈尺寸(供对话框表格显示)
    QVector<DirectionStats> directionStats() const { return m_an.dirStats; }
    // 完整分析结果(诊断报告使用,与图上显示同源)
    const CircleAnalysis &analysis() const { return m_an; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // 径向误差放大:以基准圆为参考,把 (x,y) 的径向偏差放大 m_mag 倍
    QPointF magnified(double x, double y) const;
    // 数据坐标(mm)→ 控件像素
    QPointF toWidget(double dx, double dy) const;
    QRect   plotRect() const;
    void    resetView();

    // 绘制子步骤
    void drawBackground(QPainter &p);
    void drawReferenceCircle(QPainter &p);
    QPainterPath buildPath(const QVector<double> &xs,
                           const QVector<double> &ys) const;
    void drawCommandTrajectory(QPainter &p);
    void drawTrajectory(QPainter &p);
    void drawReversalMarks(QPainter &p);
    void drawSizeBars(QPainter &p);    // 几何(裁剪)
    void drawSizeLabels(QPainter &p);  // 文本(不裁剪)
    void drawOverlay(QPainter &p);

    // 分析结果(含数据副本)
    CircleAnalysis m_an;
    bool m_hasData = false;

    // 显示开关
    bool m_showCommand  = true;
    bool m_showFeedback = true;
    bool m_showRef      = true;
    bool m_showRev      = true;
    bool m_showSize[4]  = { true, true, true, true };
    double m_mag = 1.0;

    // 视图变换
    double m_fitScale = 1.0;  // px/mm at zoom=1
    double m_zoom = 1.0;
    QPointF m_pan;

    // 平移拖动:拖动期间直接平移缓存帧,松开后再完整重绘
    bool m_dragging = false;
    QPoint m_dragStart;
    QPointF m_panAtDrag;
    QPixmap m_dragCache;

    static constexpr int kM = 50; // 绘图区四周留白(px)
};
