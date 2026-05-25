#include "circularity_widget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>

// Direction definitions: angle (rad), color, label
static const double   kAngles[] = { 0.0, M_PI/2.0, M_PI/4.0, 3.0*M_PI/4.0 };
static const QColor   kDirClr[] = {
    QColor(220,  30,  30),   // X  — red
    QColor(  0, 150,   0),   // Y  — green
    QColor(240, 140,   0),   // D1 — orange
    QColor(140,  20, 200),   // D2 — purple
};
// labels embedded as UTF-8 escape sequences (built at runtime)
static QString makeDirLabel(int i)
{
    switch (i) {
    case 0: return QString::fromUtf8("X (0\xc2\xb0\xe2\x86\x94""180\xc2\xb0)");
    case 1: return QString::fromUtf8("Y (90\xc2\xb0\xe2\x86\x94\xe2\x88\x92""90\xc2\xb0)");
    case 2: return QString::fromUtf8(
                "\xe5\xaf\xb9\xe8\xa7\x92\xe7\xba\xbf""1 (45\xc2\xb0\xe2\x86\x94\xe2\x88\x92""135\xc2\xb0)");
    case 3: return QString::fromUtf8(
                "\xe5\xaf\xb9\xe8\xa7\x92\xe7\xba\xbf""2 (135\xc2\xb0\xe2\x86\x94\xe2\x88\x92""45\xc2\xb0)");
    default: return QString();
    }
}

// -------------------------------------------------------------------------

CircularityWidget::CircularityWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setMouseTracking(true);
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(false);
}

void CircularityWidget::setData(const Dataset &data)
{
    m_hasData = false;
    m_fx.clear();
    m_fy.clear();
    m_dirs.clear();

    if (!data.axes.contains("X") || !data.axes.contains("Y")) {
        update();
        return;
    }

    m_fx = data.axes["X"].fb;
    m_fy = data.axes["Y"].fb;
    const int n = m_fx.size();
    if (n < 2) { update(); return; }

    // Centroid
    double sumX = 0, sumY = 0;
    for (int i = 0; i < n; ++i) { sumX += m_fx[i]; sumY += m_fy[i]; }
    m_cx = sumX / n;
    m_cy = sumY / n;

    // Radii from centroid
    m_maxRadius = 0;
    m_minRadius = std::numeric_limits<double>::max();
    double sumR = 0;
    for (int i = 0; i < n; ++i) {
        double dx = m_fx[i] - m_cx, dy = m_fy[i] - m_cy;
        double r = std::sqrt(dx*dx + dy*dy);
        m_maxRadius = std::max(m_maxRadius, r);
        m_minRadius = std::min(m_minRadius, r);
        sumR += r;
    }
    m_avgRadius = sumR / n;
    m_roundness = m_maxRadius - m_minRadius;

    // Half-range for fit: use max projection extent across all directions + 20%
    double maxExt = 0;
    for (int d = 0; d < 4; ++d) {
        double ca = std::cos(kAngles[d]), sa = std::sin(kAngles[d]);
        double pMin = std::numeric_limits<double>::max();
        double pMax = -pMin;
        for (int i = 0; i < n; ++i) {
            double p = (m_fx[i] - m_cx) * ca + (m_fy[i] - m_cy) * sa;
            pMin = std::min(pMin, p);
            pMax = std::max(pMax, p);
        }
        DirExtent de;
        de.angle   = kAngles[d];
        de.projMin = pMin;
        de.projMax = pMax;
        de.color   = kDirClr[d];
        de.label   = makeDirLabel(d);
        m_dirs.append(de);
        maxExt = std::max(maxExt, std::max(std::abs(pMin), std::abs(pMax)));
    }
    m_dataRange = (maxExt > 0) ? maxExt * 1.2 : 1.0;

    m_hasData = true;
    resetView();
    update();
}

// -------------------------------------------------------------------------
// View helpers
// -------------------------------------------------------------------------

QRect CircularityWidget::plotRect() const
{
    // Square plot area (equal aspect ratio crucial for circular display)
    int side = std::min(width() - 2*kM, height() - 2*kM);
    if (side < 10) side = 10;
    int ox = (width()  - side) / 2;
    int oy = (height() - side) / 2;
    return QRect(ox, oy, side, side);
}

void CircularityWidget::resetView()
{
    QRect plot = plotRect();
    m_fitScale = (m_dataRange > 0) ? (plot.width() * 0.5) / m_dataRange : 1.0;
    m_zoom = 1.0;
    m_pan  = QPointF(0, 0);
}

// data (dx, dy) in mm absolute → widget pixel position
QPointF CircularityWidget::toWidget(double dx, double dy) const
{
    QRect plot = plotRect();
    double scale = m_fitScale * m_zoom;
    return QPointF(
        plot.center().x() + m_pan.x() + (dx - m_cx) * scale,
        plot.center().y() + m_pan.y() - (dy - m_cy) * scale  // Y flipped
    );
}

// -------------------------------------------------------------------------
// Paint
// -------------------------------------------------------------------------

void CircularityWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(245, 247, 250));

    if (!m_hasData) {
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont("Arial", 11));
        p.drawText(rect(), Qt::AlignCenter,
            // 请先加载包含 X/Y 轴的 CSV 文件
            QString::fromUtf8(
                "\xe8\xaf\xb7\xe5\x85\x88\xe5\x8a\xa0\xe8\xbd\xbd"
                "\xe5\x8c\x85\xe5\x90\xab X/Y \xe8\xbd\xb4\xe7\x9a\x84"
                " CSV \xe6\x96\x87\xe4\xbb\xb6"));
        return;
    }

    QRect plot = plotRect();

    p.setClipRect(plot);
    drawBackground(p);
    drawDirectionLines(p);
    drawReferenceCircles(p);
    drawTrajectory(p);
    p.setClipping(false);

    // Size bars and labels drawn outside clip so text isn't cut off
    drawSizeBars(p);
    drawOverlay(p);
}

void CircularityWidget::drawBackground(QPainter &p)
{
    QRect plot = plotRect();
    p.fillRect(plot, Qt::white);

    // Light grid (fixed 30px spacing in widget space)
    p.setPen(QPen(QColor(230, 230, 230), 1));
    for (int y = plot.top(); y <= plot.bottom(); y += 30)
        p.drawLine(plot.left(), y, plot.right(), y);
    for (int x = plot.left(); x <= plot.right(); x += 30)
        p.drawLine(x, plot.top(), x, plot.bottom());

    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(plot);
}

void CircularityWidget::drawDirectionLines(QPainter &p)
{
    QRect plot = plotRect();
    QPointF cw = toWidget(m_cx, m_cy);  // centroid in widget space
    double len = std::hypot(plot.width(), plot.height());

    for (int d = 0; d < m_dirs.size(); ++d) {
        double ca = std::cos(m_dirs[d].angle);
        double sa = std::sin(m_dirs[d].angle);
        QPointF dir(ca * len, -sa * len);  // y-flipped in widget
        QPen pen(m_dirs[d].color, 1.0, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(cw - dir, cw + dir);
    }
}

void CircularityWidget::drawReferenceCircles(QPainter &p)
{
    if (m_avgRadius <= 0) return;
    double scale = m_fitScale * m_zoom;
    QPointF cw = toWidget(m_cx, m_cy);

    // Min radius circle (inner bound)
    p.setPen(QPen(QColor(200, 200, 240), 1.2, Qt::DotLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(cw, m_minRadius * scale, m_minRadius * scale);

    // Max radius circle (outer bound)
    p.setPen(QPen(QColor(240, 200, 200), 1.2, Qt::DotLine));
    p.drawEllipse(cw, m_maxRadius * scale, m_maxRadius * scale);

    // Average radius (nominal circle)
    p.setPen(QPen(QColor(160, 160, 210), 1.5, Qt::SolidLine));
    p.drawEllipse(cw, m_avgRadius * scale, m_avgRadius * scale);
}

void CircularityWidget::drawTrajectory(QPainter &p)
{
    if (m_fx.isEmpty()) return;

    // Subsample for very large datasets to keep paint fast
    const int kMaxPts = 8000;
    int step = std::max(1, m_fx.size() / kMaxPts);

    QPainterPath path;
    path.moveTo(toWidget(m_fx[0], m_fy[0]));
    for (int i = step; i < m_fx.size(); i += step)
        path.lineTo(toWidget(m_fx[i], m_fy[i]));
    // Close back to start for round-trip paths
    path.lineTo(toWidget(m_fx[0], m_fy[0]));

    p.setPen(QPen(QColor(0, 80, 200), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void CircularityWidget::drawSizeBars(QPainter &p)
{
    // For each direction, draw a thick tick at the two projection extremes
    // and annotate with the feedback size.
    static const QPointF kLabelOffset[] = {
        QPointF( 6, -4),   // X: right of max
        QPointF( 4, -14),  // Y: above max
        QPointF( 6, -6),   // D1: upper-right
        QPointF(-100, -6), // D2: upper-left (negative x to go left)
    };

    for (int d = 0; d < m_dirs.size(); ++d) {
        const DirExtent &de = m_dirs[d];
        double ca = std::cos(de.angle), sa = std::sin(de.angle);

        // Widget coords of the two extent endpoints
        QPointF wMin = toWidget(m_cx + de.projMin * ca,
                                m_cy + de.projMin * sa);
        QPointF wMax = toWidget(m_cx + de.projMax * ca,
                                m_cy + de.projMax * sa);

        // Perpendicular tick length (widget pixels)
        const double kTick = 7.0;
        QPointF perp(-sa * kTick, -ca * kTick); // y-flipped perp

        p.setPen(QPen(de.color, 2.0));
        p.drawLine(wMin + perp, wMin - perp);
        p.drawLine(wMax + perp, wMax - perp);
        // Light line connecting them
        p.setPen(QPen(de.color, 1.0, Qt::DotLine));
        p.drawLine(wMin, wMax);

        // Size label near the max endpoint
        double sizeVal = de.projMax - de.projMin;
        QString txt = QString("%1mm").arg(sizeVal, 0, 'f', 4);
        p.setFont(QFont("Arial", 9));
        p.setPen(de.color);
        QPointF offset = kLabelOffset[d % 4];
        p.drawText(wMax + offset, txt);
    }
}

void CircularityWidget::drawOverlay(QPainter &p)
{
    QRect plot = plotRect();

    // 真圆度 label — top-left corner of plot
    p.setFont(QFont("Arial", 10, QFont::Bold));
    p.setPen(QColor(50, 50, 50));
    QString roundStr =
        // 真圆度:
        QString::fromUtf8("\xe7\x9c\x9f\xe5\x9c\x86\xe5\xba\xa6: ")
        + QString::number(m_roundness, 'f', 4) + " mm";
    p.drawText(plot.adjusted(6, 4, 0, 0),
               Qt::AlignTop | Qt::AlignLeft, roundStr);

    // Zoom factor — top-right
    p.setFont(QFont("Arial", 10));
    p.setPen(QColor(100, 100, 100));
    QString zoomStr = QString::fromUtf8("\xc3\x97")  // ×
                      + QString::number(m_zoom, 'f', 1);
    p.drawText(plot.adjusted(0, 4, -4, 0),
               Qt::AlignTop | Qt::AlignRight, zoomStr);

    // Hint — bottom-right
    p.setFont(QFont("Arial", 8));
    p.setPen(QColor(160, 160, 160));
    // 滚轮缩放 | 拖动平移 | 双击复位
    QString hint = QString::fromUtf8(
        "\xe6\xbb\x9a\xe8\xbd\xae\xe7\xbc\xa9\xe6\x94\xbe | "
        "\xe6\x8b\x96\xe5\x8a\xa8\xe5\xb9\xb3\xe7\xa7\xbb | "
        "\xe5\x8f\x8c\xe5\x87\xbb\xe5\xa4\x8d\xe4\xbd\x8d");
    p.drawText(plot.adjusted(0, 0, -4, -4),
               Qt::AlignBottom | Qt::AlignRight, hint);
}

// -------------------------------------------------------------------------
// Interaction
// -------------------------------------------------------------------------

void CircularityWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_hasData) { event->ignore(); return; }

    double factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);
    // Zoom toward cursor, keeping the point under the cursor stationary
    QPointF cursor(event->pos());
    QPointF plotCtr(plotRect().center());
    // pan_new = (cursor - plotCtr) * (1 - factor) + pan_old * factor
    m_pan = (cursor - plotCtr) * (1.0 - factor) + m_pan * factor;
    m_zoom = std::max(0.05, std::min(500.0, m_zoom * factor));
    update();
    event->accept();
}

void CircularityWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging    = true;
        m_dragStart   = event->pos();
        m_panAtDrag   = m_pan;
        setCursor(Qt::ClosedHandCursor);
    }
}

void CircularityWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_pan = m_panAtDrag + QPointF(event->pos() - m_dragStart);
        update();
    }
}

void CircularityWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
    }
}

void CircularityWidget::mouseDoubleClickEvent(QMouseEvent *)
{
    resetView();
    update();
}

void CircularityWidget::resizeEvent(QResizeEvent *)
{
    if (m_hasData) resetView();
}
