#pragma once
#include <QWidget>
#include <QVector>
#include "data_loader.h"

// XY feedback trajectory chart for roundness (真圆度) visualization.
// Equal-aspect-ratio plot: feedback positions drawn as a continuous line,
// with reference circles and per-direction size bars.
class CircularityWidget : public QWidget {
    Q_OBJECT
public:
    explicit CircularityWidget(QWidget *parent = nullptr);
    void setData(const Dataset &data);

    // Visibility toggles (driven by the dialog's checkbox toolbar).
    void setCommandVisible(bool on);   // 指令(位置)轨迹
    void setFeedbackVisible(bool on);  // 反馈轨迹
    // Per-direction size annotation: dir 0=X, 1=Y, 2=对角线1, 3=对角线2
    void setSizeVisible(int dir, bool on);

    // Metric accessors (computed from feedback trajectory)
    bool   hasData()    const { return m_hasData; }
    double roundness()  const { return m_roundness; }
    double avgRadius()  const { return m_avgRadius; }
    double minRadius()  const { return m_minRadius; }
    double maxRadius()  const { return m_maxRadius; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Coordinate mapping (data mm -> widget px)
    QPointF toWidget(double dx, double dy) const;
    QRect   plotRect() const;
    void    resetView();

    // Paint sub-steps
    void drawBackground(QPainter &p);
    void drawCommandTrajectory(QPainter &p);  // command path (orange dashed)
    void drawTrajectory(QPainter &p);         // feedback path (blue solid)
    void drawSizeBars(QPainter &p);    // geometry (clipped)
    void drawSizeLabels(QPainter &p); // text (unclipped)
    void drawOverlay(QPainter &p);

    // Command data (raw positions)
    QVector<double> m_cmdx, m_cmdy;

    // Feedback data (raw positions)
    QVector<double> m_fx, m_fy;
    double m_cx = 0, m_cy = 0;     // centroid of feedback
    double m_dataRange = 1.0;       // half-range for initial fit
    double m_avgRadius = 0;
    double m_maxRadius = 0;
    double m_minRadius = 0;
    double m_roundness = 0;         // maxR - minR

    // Per-direction extent (projection of feedback relative to centroid)
    struct DirExtent {
        double angle;   // radians
        double projMin; // min signed projection from centroid (mm)
        double projMax; // max signed projection from centroid (mm)
        QColor color;
        QString label;  // e.g. "X (0°↔180°)"
    };
    QVector<DirExtent> m_dirs;
    bool m_hasData = false;

    // Visibility flags
    bool m_showCommand  = true;
    bool m_showFeedback = true;
    bool m_showSize[4]  = { true, true, true, true };

    // View transform
    double m_fitScale = 1.0;  // px/mm at zoom=1 to fit data
    double m_zoom = 1.0;
    QPointF m_pan;

    // Pan drag
    bool m_dragging = false;
    QPoint m_dragStart;
    QPointF m_panAtDrag;

    static constexpr int kM = 50; // margin around plot area (px)
};
