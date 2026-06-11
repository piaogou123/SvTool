#include "circularity_widget.h"
#include "direction_defs.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {

// 轨迹绘制抽稀上限:超过该点数按步长跳点,保证重绘流畅
constexpr int kMaxPts = 8000;

// QWheelEvent 取坐标的版本兼容封装(Qt 5.14 起才有 position())
QPointF wheelPos(const QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return event->position();
#else
    return event->posF();
#endif
}

} // namespace

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
    m_an.compute(data);
    m_hasData = m_an.hasXY;
    if (m_hasData)
        resetView();
    update();
}

// -------------------------------------------------------------------------
// 显示开关
// -------------------------------------------------------------------------

void CircularityWidget::setCommandVisible(bool on)
{
    if (m_showCommand == on) return;
    m_showCommand = on;
    update();
}

void CircularityWidget::setFeedbackVisible(bool on)
{
    if (m_showFeedback == on) return;
    m_showFeedback = on;
    update();
}

void CircularityWidget::setReferenceVisible(bool on)
{
    if (m_showRef == on) return;
    m_showRef = on;
    update();
}

void CircularityWidget::setReversalVisible(bool on)
{
    if (m_showRev == on) return;
    m_showRev = on;
    update();
}

void CircularityWidget::setSizeVisible(int dir, bool on)
{
    if (dir < 0 || dir >= 4 || m_showSize[dir] == on) return;
    m_showSize[dir] = on;
    update();
}

void CircularityWidget::setMagnification(double mag)
{
    mag = std::max(1.0, mag);
    if (mag == m_mag) return;
    m_mag = mag;
    resetView();
    update();
}

// -------------------------------------------------------------------------
// 视图辅助
// -------------------------------------------------------------------------

QRect CircularityWidget::plotRect() const
{
    // Rectangular plot: full widget width, height = min(w,h) - 2*kM.
    int pw = width() - 2 * kM;
    int ph = std::min(width() - 2 * kM, height() - 2 * kM);
    if (pw < 10) pw = 10;
    if (ph < 10) ph = 10;
    int ox = kM;
    int oy = (height() - ph) / 2;
    return QRect(ox, oy, pw, ph);
}

void CircularityWidget::resetView()
{
    QRect plot = plotRect();
    int minSide = std::min(plot.width(), plot.height());

    // 放大模式下视野按 "基准半径 + 放大后的最大偏差" 计算,
    // 否则按原始数据范围(含进退刀)
    double range = m_an.dataRange;
    if (m_mag > 1.0)
        range = (m_an.refR + std::max(m_an.maxAbsDev * m_mag, 1e-9)) * 1.08;

    m_fitScale = (range > 0) ? (minSide * 0.5) / range : 1.0;
    m_zoom = 1.0;
    m_pan  = QPointF(0, 0);
}

// 径向误差放大:点到基准圆心距离 r 映射为 refR + (r-refR)*mag
QPointF CircularityWidget::magnified(double x, double y) const
{
    if (m_mag == 1.0) return QPointF(x, y);
    const double dx = x - m_an.refCx, dy = y - m_an.refCy;
    const double r = std::sqrt(dx * dx + dy * dy);
    if (r < 1e-12) return QPointF(x, y);
    double rm = m_an.refR + (r - m_an.refR) * m_mag;
    if (rm < 0) rm = 0;
    const double f = rm / r;
    return QPointF(m_an.refCx + dx * f, m_an.refCy + dy * f);
}

// data (dx, dy) in mm absolute → widget pixel position
QPointF CircularityWidget::toWidget(double dx, double dy) const
{
    QRect plot = plotRect();
    double scale = m_fitScale * m_zoom;
    return QPointF(
        plot.center().x() + m_pan.x() + (dx - m_an.refCx) * scale,
        plot.center().y() + m_pan.y() - (dy - m_an.refCy) * scale  // Y flipped
    );
}

// -------------------------------------------------------------------------
// Paint
// -------------------------------------------------------------------------

void CircularityWidget::paintEvent(QPaintEvent *)
{
    // 拖动中:直接平移缓存帧,避免每帧重建大轨迹路径
    if (m_dragging && !m_dragCache.isNull()) {
        QPainter p(this);
        p.fillRect(rect(), QColor(245, 247, 250));
        p.drawPixmap(m_pan - m_panAtDrag, m_dragCache);
        return;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(245, 247, 250));

    if (!m_hasData) {
        p.setPen(QColor(150, 150, 150));
        p.setFont(QFont("Arial", 11));
        p.drawText(rect(), Qt::AlignCenter,
                   QString::fromUtf8("请先加载包含 X/Y 轴的 CSV 文件"));
        return;
    }

    QRect plot = plotRect();

    // All geometry strictly clipped to plot area
    p.setClipRect(plot);
    drawBackground(p);
    drawReferenceCircle(p);
    drawCommandTrajectory(p);   // command path drawn first (below feedback)
    drawTrajectory(p);          // feedback path on top
    drawSizeBars(p);            // lines clipped; labels drawn separately below
    drawReversalMarks(p);
    p.setClipping(false);

    // Text overlays: not clipped so they are never cut off at the edge
    drawSizeLabels(p);
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

void CircularityWidget::drawReferenceCircle(QPainter &p)
{
    if (!m_showRef || m_an.refR <= 0) return;

    const QPointF c = toWidget(m_an.refCx, m_an.refCy);
    const double rad = m_an.refR * m_fitScale * m_zoom;

    QPen pen(QColor(150, 150, 155), 1.0, Qt::DashLine);
    pen.setDashPattern({2, 4});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, rad, rad);

    // 圆心十字
    p.drawLine(c + QPointF(-6, 0), c + QPointF(6, 0));
    p.drawLine(c + QPointF(0, -6), c + QPointF(0, 6));
}

// 构建轨迹路径:mag=1 时画全部点(含进退刀);
// 放大模式只画圆弧段(进退刀点的放大偏差会冲出视野),按下标断点分段
QPainterPath CircularityWidget::buildPath(const QVector<double> &xs,
                                          const QVector<double> &ys) const
{
    QPainterPath path;
    if (m_mag == 1.0) {
        const int n = xs.size();
        const int step = std::max(1, n / kMaxPts);
        path.moveTo(toWidget(xs[0], ys[0]));
        int last = 0;
        for (int i = step; i < n; i += step) {
            path.lineTo(toWidget(xs[i], ys[i]));
            last = i;
        }
        if (last != n - 1)
            path.lineTo(toWidget(xs[n - 1], ys[n - 1]));
    } else {
        const int m = m_an.onCircle.size();
        if (m == 0) return path;
        const int step = std::max(1, m / kMaxPts);
        bool started = false;
        int prevIdx = -1;
        for (int k = 0; k < m; k += step) {
            const int i = m_an.onCircle[k];
            const QPointF d = magnified(xs[i], ys[i]);
            const QPointF w = toWidget(d.x(), d.y());
            if (!started || i - prevIdx > 5 * step + 5) {
                path.moveTo(w);
                started = true;
            } else {
                path.lineTo(w);
            }
            prevIdx = i;
        }
    }
    return path;
}

void CircularityWidget::drawCommandTrajectory(QPainter &p)
{
    if (!m_showCommand || m_an.cmdx.isEmpty()) return;

    QPen pen(QColor(0, 160, 150), 1.5, Qt::DashLine);
    pen.setDashPattern({6, 4});
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(buildPath(m_an.cmdx, m_an.cmdy));
}

void CircularityWidget::drawTrajectory(QPainter &p)
{
    if (!m_showFeedback || m_an.fx.isEmpty()) return;

    p.setPen(QPen(QColor(0, 80, 200), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(buildPath(m_an.fx, m_an.fy));
}

void CircularityWidget::drawReversalMarks(QPainter &p)
{
    if (!m_showRev || m_an.revs.isEmpty()) return;

    QFont f("Arial", 8, QFont::Bold);
    p.setFont(f);
    const QFontMetrics fm(f);

    for (const CircleAnalysis::ReversalMark &rev : m_an.revs) {
        const QPointF d = magnified(m_an.fx[rev.idx], m_an.fy[rev.idx]);
        const QPointF w = toWidget(d.x(), d.y());
        const QColor c = dir_defs::color(rev.axis);   // 0=X 红, 1=Y 绿

        p.setPen(QPen(c, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(w, 6, 6);

        // 标签:轴名 + 带符号偏差(正=外凸,负=内凹)
        const QString txt = QString("%1 %2%3")
                                .arg(rev.axis == 0 ? "X" : "Y")
                                .arg(rev.dev >= 0 ? "+" : "")
                                .arg(rev.dev, 0, 'f', 4);
        const QRect tr = fm.boundingRect(txt);
        const QPointF tp(w.x() + 9, w.y() - 9);
        p.fillRect(QRectF(tp.x() - 2, tp.y() - tr.height() + 2,
                          tr.width() + 5, tr.height() + 2),
                   QColor(255, 255, 255, 190));
        p.setPen(c.darker(110));
        p.drawText(tp, txt);
    }
}

void CircularityWidget::drawSizeBars(QPainter &p)
{
    for (int d = 0; d < m_an.dirs.size(); ++d) {
        if (d < 4 && !m_showSize[d]) continue;
        const CircleAnalysis::DirExtent &de = m_an.dirs[d];
        const double ca = std::cos(de.angle), sa = std::sin(de.angle);

        const QPointF dMin = magnified(m_an.refCx + de.projMin * ca,
                                       m_an.refCy + de.projMin * sa);
        const QPointF dMax = magnified(m_an.refCx + de.projMax * ca,
                                       m_an.refCy + de.projMax * sa);
        const QPointF wMin = toWidget(dMin.x(), dMin.y());
        const QPointF wMax = toWidget(dMax.x(), dMax.y());

        // Perpendicular unit vector (widget space, y-flipped)
        const QPointF perp(-sa, -ca);

        // Lightened color for the bar geometry (lines stay subtle)
        const QColor barClr = dir_defs::blendWhite(dir_defs::color(d), 30);
        const double kTickOuter = 9.0;

        p.setBrush(Qt::NoBrush);

        // --- Center measurement line: thin dotted, very light --------------
        QPen dashPen(barClr, 1.0, Qt::DashLine);
        dashPen.setDashPattern({4, 5});
        p.setPen(dashPen);
        p.drawLine(wMin, wMax);

        // --- Single-tier end caps: thin, same lightened color ---------------
        QPen tickPen(barClr, 1.2);
        p.setPen(tickPen);
        p.drawLine(wMin + perp * kTickOuter, wMin - perp * kTickOuter);
        p.drawLine(wMax + perp * kTickOuter, wMax - perp * kTickOuter);
    }
}

void CircularityWidget::drawSizeLabels(QPainter &p)
{
    // Labels only (called after clip is removed so text isn't cut off).
    QRect wr = rect().adjusted(kM, kM, -kM, -kM);

    for (int d = 0; d < m_an.dirs.size(); ++d) {
        if (d < 4 && !m_showSize[d]) continue;
        const CircleAnalysis::DirExtent &de = m_an.dirs[d];
        const double ca = std::cos(de.angle), sa = std::sin(de.angle);

        // The label is anchored at the *positive* extent of each direction
        const QPointF dMax = magnified(m_an.refCx + de.projMax * ca,
                                       m_an.refCy + de.projMax * sa);
        const QPointF wMax = toWidget(dMax.x(), dMax.y());

        const double sizeVal = de.projMax - de.projMin;
        const QString txt = QString("%1 mm").arg(sizeVal, 0, 'f', 4);
        QFont labelFont("Arial", 8, QFont::Bold);
        p.setFont(labelFont);

        // Measure actual text width so the clamp never cuts off the label
        QFontMetrics fm(labelFont);
        QRect textRect = fm.boundingRect(txt);
        int txtW = textRect.width();

        // Clamp into the plot area so label is always readable
        double lx = std::max((double)wr.left(),
                    std::min((double)wr.right() - txtW - 4, wMax.x() + 6));
        double ly = std::max((double)wr.top()  + 4,
                    std::min((double)wr.bottom() - 14, wMax.y() - 4));

        // Semi-transparent white pill behind the text for legibility
        QRectF bg(lx - 2, ly - textRect.height() + 1,
                  textRect.width() + 6, textRect.height() + 2);
        p.fillRect(bg, QColor(255, 255, 255, 185));

        // Text in the direction's lightened color (less glaring than full sat)
        p.setPen(dir_defs::blendWhite(dir_defs::color(d), 20));
        p.drawText(QPointF(lx + 1, ly), txt);
    }
}

void CircularityWidget::drawOverlay(QPainter &p)
{
    QRect plot = plotRect();

    // 缩放/放大倍数 — 右上角
    p.setFont(QFont("Arial", 10));
    p.setPen(QColor(100, 100, 100));
    QString zoomStr = QString::fromUtf8("×") + QString::number(m_zoom, 'f', 1);
    if (m_mag > 1.0)
        zoomStr += QString::fromUtf8("   误差放大 ×%1")
                       .arg(QString::number(m_mag, 'f', 0));
    p.drawText(plot.adjusted(0, 4, -4, 0),
               Qt::AlignTop | Qt::AlignRight, zoomStr);

    // 圆弧段过滤信息 — 左上角
    if (m_an.filtered) {
        p.setFont(QFont("Arial", 8));
        p.setPen(QColor(130, 130, 130));
        p.drawText(plot.adjusted(6, 4, 0, 0), Qt::AlignTop | Qt::AlignLeft,
                   QString::fromUtf8("圆弧段: %1 / %2 点(已剔除进退刀)")
                       .arg(m_an.onCircle.size()).arg(m_an.fx.size()));
    }

    // Hint — bottom-right
    p.setFont(QFont("Arial", 8));
    p.setPen(QColor(160, 160, 160));
    p.drawText(plot.adjusted(0, 0, -4, -4),
               Qt::AlignBottom | Qt::AlignRight,
               QString::fromUtf8("滚轮缩放 | 拖动平移 | 双击复位"));

    // Legend — bottom-left (only list trajectories that are visible)
    {
        int lx = plot.left() + 8;
        int ly = plot.bottom() - 50;
        int row = 0;
        p.setFont(QFont("Arial", 8, QFont::Bold));

        if (m_showCommand) {
            int y = ly + 5 + row * 15;
            QPen cmdPen(QColor(0, 160, 150), 1.5, Qt::DashLine);
            cmdPen.setDashPattern({6, 4});
            p.setPen(cmdPen);
            p.drawLine(lx, y, lx + 22, y);
            p.setPen(QColor(0, 160, 150));
            p.drawText(lx + 26, y + 4, QString::fromUtf8("指令"));
            ++row;
        }

        if (m_showFeedback) {
            int y = ly + 5 + row * 15;
            p.setPen(QPen(QColor(0, 80, 200), 1.5));
            p.drawLine(lx, y, lx + 22, y);
            p.setPen(QColor(0, 80, 200));
            p.drawText(lx + 26, y + 4, QString::fromUtf8("反馈"));
            ++row;
        }

        if (m_showRef) {
            int y = ly + 5 + row * 15;
            QPen refPen(QColor(150, 150, 155), 1.0, Qt::DashLine);
            refPen.setDashPattern({2, 4});
            p.setPen(refPen);
            p.drawLine(lx, y, lx + 22, y);
            p.setPen(QColor(120, 120, 125));
            p.drawText(lx + 26, y + 4, QString::fromUtf8("基准圆"));
            ++row;
        }
    }
}

// -------------------------------------------------------------------------
// Interaction
// -------------------------------------------------------------------------

void CircularityWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_hasData) { event->ignore(); return; }

    double factor = (event->angleDelta().y() > 0) ? 1.25 : (1.0 / 1.25);
    // Zoom toward cursor, keeping the point under the cursor stationary
    QPointF cursor = wheelPos(event);
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
        // 先抓当前帧作为拖动缓存,再进入拖动状态
        m_dragCache  = grab();
        m_dragging   = true;
        m_dragStart  = event->pos();
        m_panAtDrag  = m_pan;
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
        m_dragCache = QPixmap();
        setCursor(Qt::ArrowCursor);
        update();
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
