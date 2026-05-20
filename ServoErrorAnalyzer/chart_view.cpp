#include "chart_view.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QtMath>
#include <algorithm>
#include <cmath>

// --- helpers -----------------------------------------------------------

static QString formatDouble(double v, int prec)
{
    QString s = QString::number(v, 'f', prec);
    if (s.contains('.')) {
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
    }
    return s;
}

static double niceStep(double approx)
{
    double expo = std::pow(10, std::floor(std::log10(approx)));
    double mant = approx / expo;
    if      (mant <= 1.5) mant = 1.0;
    else if (mant <= 3.5) mant = 2.0;
    else if (mant <= 7.5) mant = 5.0;
    else                  { mant = 1.0; expo *= 10; }
    return mant * expo;
}

void ChartView::calcTicks(double vMin, double vMax, int maxTicks,
                          double &first, double &step, int &count)
{
    double range = vMax - vMin;
    if (range <= 0) { first = vMin; step = 1; count = 1; return; }
    step = niceStep(range / maxTicks);
    first = std::ceil(vMin / step) * step;
    count = static_cast<int>(std::floor(vMax / step) - std::ceil(vMin / step)) + 1;
    if (count < 1) count = 1;
    if (count > maxTicks * 2) count = maxTicks * 2;
}

// 12-color palette – first 3 are the canonical X/Y/C colours
static const QColor kPalette[] = {
    QColor(220, 30, 30),     // X — red
    QColor(0, 150, 0),       // Y — green
    QColor(0, 100, 255),     // C — blue
    QColor(240, 140, 0),     // orange
    QColor(140, 20, 200),    // purple
    QColor(0, 160, 160),     // teal
    QColor(200, 50, 120),    // magenta
    QColor(120, 100, 40),    // olive
    QColor(60, 120, 220),    // steel
    QColor(200, 80, 60),     // brick
    QColor(0, 130, 100),     // forest
    QColor(160, 100, 180),   // lavender
};
static constexpr int kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

QColor ChartView::axisColor(int idx)
{
    return kPalette[idx % kPaletteSize];
}

// --- ChartView ---------------------------------------------------------

ChartView::ChartView(QWidget *parent)
    : QWidget(parent)
    , m_cursorIdx(-1)
    , m_lastEmittedCursorIdx(-1)
    , m_cursorVisible(false)
    , m_rubberBand(false)
    , m_gridColor(220, 220, 220)
    , m_bgColor(252, 252, 252)
    , m_staticCacheDirty(true)
{
    setMouseTracking(true);
    setAcceptDrops(true);
    setMinimumSize(400, 250);
}

void ChartView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    double w = width();
    double h = height();
    m_plotArea = QRectF(ML, MT, w - ML - MR, h - MT - MB);
    invalidateStaticCache();
}

// --- coordinate transforms --------------------------------------------

QPointF ChartView::dataToWidget(double t, double v) const
{
    double rx = m_viewXMax - m_viewXMin;
    double ry = m_viewYMax - m_viewYMin;
    if (rx == 0) rx = 1;
    if (ry == 0) ry = 1;
    double x = m_plotArea.left() + (t - m_viewXMin) / rx * m_plotArea.width();
    double y = m_plotArea.bottom() - (v - m_viewYMin) / ry * m_plotArea.height();
    return QPointF(x, y);
}

double ChartView::widgetToTime(double wx) const
{
    double rx = m_viewXMax - m_viewXMin;
    if (rx == 0) rx = 1;
    return m_viewXMin + (wx - m_plotArea.left()) / m_plotArea.width() * rx;
}

double ChartView::widgetToValue(double wy) const
{
    double ry = m_viewYMax - m_viewYMin;
    if (ry == 0) ry = 1;
    return m_viewYMax - (wy - m_plotArea.top()) / m_plotArea.height() * ry;
}

int ChartView::nearestIndex(double time) const
{
    if (m_data.isEmpty()) return -1;
    const QVector<double> &t = m_data.time;
    auto it = std::lower_bound(t.begin(), t.end(), time);
    if (it == t.begin()) return 0;
    if (it == t.end())   return t.size() - 1;
    int idx = static_cast<int>(it - t.begin());
    if (time - t[idx - 1] < t[idx] - time)
        return idx - 1;
    return idx;
}

// --- public API --------------------------------------------------------

void ChartView::setAxisData(const Dataset &data)
{
    m_data = data;
    m_cursorIdx = -1;
    m_lastEmittedCursorIdx = -1;
    m_cursorVisible = false;

    m_axisNames = data.axisOrder;

    // Assign colours
    m_color.clear();
    m_visible.clear();
    for (int i = 0; i < m_axisNames.size(); ++i) {
        const QString &name = m_axisNames[i];
        m_color[name] = axisColor(i);
        m_visible[name] = true;
    }

    if (data.isEmpty()) {
        m_fullXMin = 0; m_fullXMax = 1;
        m_fullYMin = -1; m_fullYMax = 1;
    } else {
        m_fullXMin = data.time.first();
        m_fullXMax = data.time.last();

        bool first = true;
        for (const auto &name : m_axisNames) {
            const QVector<double> &err = data.axes[name].err;
            for (double v : err) {
                if (first) { m_fullYMin = m_fullYMax = v; first = false; }
                else { if (v < m_fullYMin) m_fullYMin = v; if (v > m_fullYMax) m_fullYMax = v; }
            }
        }
        if (first) { m_fullYMin = -1; m_fullYMax = 1; }
    }

    double xPad = (m_fullXMax - m_fullXMin) * 0.02;
    if (xPad == 0) xPad = 0.01;
    double yPad = (m_fullYMax - m_fullYMin) * 0.08;
    if (yPad == 0) yPad = 0.001;

    m_fullXMin -= xPad;
    m_fullXMax += xPad;
    m_fullYMin -= yPad;
    m_fullYMax += yPad;

    zoomReset();
}

void ChartView::setSeriesVisible(const QString &axis, bool visible)
{
    if (m_visible.contains(axis)) {
        m_visible[axis] = visible;
        invalidateStaticCache();
    }
    update();
}

bool ChartView::isSeriesVisible(const QString &axis) const
{
    return m_visible.value(axis, false);
}

void ChartView::zoomReset()
{
    m_viewXMin = m_fullXMin;
    m_viewXMax = m_fullXMax;
    m_viewYMin = m_fullYMin;
    m_viewYMax = m_fullYMax;
    invalidateStaticCache();
    update();
}

// --- drawing -----------------------------------------------------------

void ChartView::paintEvent(QPaintEvent *)
{
    if (m_plotArea.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QSize pixmapSize = size() * devicePixelRatioF();
    if (m_staticCacheDirty || m_staticCache.size() != pixmapSize) {
        m_staticCache = QPixmap(pixmapSize);
        m_staticCache.setDevicePixelRatio(devicePixelRatioF());

        QPainter cachePainter(&m_staticCache);
        cachePainter.setRenderHint(QPainter::Antialiasing, true);
        drawStaticContent(cachePainter);
        m_staticCacheDirty = false;
    }

    p.drawPixmap(0, 0, m_staticCache);
    drawCursor(p);
    drawRubberBand(p);
}

void ChartView::drawStaticContent(QPainter &p)
{
    p.fillRect(rect(), m_bgColor);
    p.fillRect(m_plotArea, Qt::white);

    drawGrid(p);
    drawCurves(p);
}

void ChartView::invalidateStaticCache()
{
    m_staticCacheDirty = true;
}

void ChartView::drawGrid(QPainter &p)
{
    p.save();
    p.setClipRect(m_plotArea);

    QPen pen(m_gridColor, 1);
    p.setPen(pen);
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);

    // X ticks
    {
        double first, step; int count;
        calcTicks(m_viewXMin, m_viewXMax, 8, first, step, count);

        for (int i = 0; i < count; ++i) {
            double val = first + i * step;
            if (val < m_viewXMin || val > m_viewXMax) continue;
            QPointF pt = dataToWidget(val, 0);
            p.drawLine(QPointF(pt.x(), m_plotArea.top()),
                       QPointF(pt.x(), m_plotArea.bottom()));
            QString label = formatDouble(val, 3);
            QRectF labelRect(pt.x() - 30, m_plotArea.bottom() + 2, 60, 16);
            p.setPen(Qt::black);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, label);
            p.setPen(pen);
        }

        QRectF xLabel(m_plotArea.left(), m_plotArea.bottom() + 22,
                      m_plotArea.width(), 16);
        p.setPen(Qt::black);
        font.setPixelSize(11);
        p.setFont(font);
        p.drawText(xLabel, Qt::AlignHCenter, "Time (s)");
        font.setPixelSize(10);
        p.setFont(font);
    }

    // Y ticks
    {
        double first, step; int count;
        calcTicks(m_viewYMin, m_viewYMax, 7, first, step, count);

        p.setPen(pen);
        for (int i = 0; i < count; ++i) {
            double val = first + i * step;
            if (val < m_viewYMin || val > m_viewYMax) continue;
            QPointF pt = dataToWidget(0, val);
            p.drawLine(QPointF(m_plotArea.left(), pt.y()),
                       QPointF(m_plotArea.right(), pt.y()));
            QString label = formatDouble(val, 4);
            QRectF labelRect(m_plotArea.left() - 66, pt.y() - 8, 62, 16);
            p.setPen(Qt::black);
            p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
            p.setPen(pen);
        }

        p.setPen(Qt::black);
        font.setPixelSize(11);
        p.setFont(font);
        p.save();
        p.translate(12, m_plotArea.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-50, -10, 100, 20), Qt::AlignCenter, "Error");
        p.restore();
    }

    // Plot area border
    p.setPen(QPen(QColor(180, 180, 180), 1));
    p.drawRect(m_plotArea);

    p.restore();
}

void ChartView::drawCurves(QPainter &p)
{
    if (m_data.isEmpty()) return;

    p.save();
    p.setClipRect(m_plotArea.adjusted(-1, -1, 1, 1));

    const QVector<double> &t = m_data.time;

    // Find visible index range (avoid iterating all points on every paint)
    int iBeg = static_cast<int>(
        std::lower_bound(t.begin(), t.end(), m_viewXMin) - t.begin());
    if (iBeg > 0) iBeg--;
    int iEnd = static_cast<int>(
        std::upper_bound(t.begin() + iBeg, t.end(), m_viewXMax) - t.begin()) + 1;
    if (iEnd > m_data.size()) iEnd = m_data.size();
    int visCount = iEnd - iBeg;
    if (visCount <= 0) { p.restore(); return; }

    for (const QString &name : m_axisNames) {
        if (!m_visible.value(name, true)) continue;
        const AxisChannel &ch = m_data.axes[name];

        QPen pen(m_color[name], 1);
        p.setPen(pen);

        QPolygonF poly;
        poly.reserve(visCount);

        for (int i = iBeg; i < iEnd; ++i)
            poly.append(dataToWidget(t[i], ch.err[i]));

        if (poly.size() >= 2)
            p.drawPolyline(poly);
        else if (poly.size() == 1)
            p.drawPoint(poly[0]);
    }

    p.restore();

    // Legend — dynamic height
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont f = p.font();
    f.setPixelSize(12);
    p.setFont(f);
    double lx = m_plotArea.right() - 150;
    double ly = m_plotArea.top() + 5;
    for (const QString &name : m_axisNames) {
        QColor c = m_color[name];
        p.setPen(c);
        p.setBrush(c);
        p.drawRect(QRectF(lx, ly, 16, 3));
        p.setPen(Qt::black);
        p.drawText(QRectF(lx + 20, ly - 7, 110, 16), Qt::AlignLeft,
                   QString("%1 Error").arg(name));
        ly += 18;
    }
    p.restore();
}

void ChartView::drawCursor(QPainter &p)
{
    if (!m_cursorVisible || m_cursorIdx < 0 || m_cursorIdx >= m_data.size())
        return;
    if (m_rubberBand) return;

    p.save();
    p.setClipRect(m_plotArea);

    double t = m_data.time[m_cursorIdx];
    double yTop = m_viewYMax;
    double yBot = m_viewYMin;

    QPointF top = dataToWidget(t, yTop);
    QPointF bot = dataToWidget(t, yBot);

    QPen linePen(QColor(100, 100, 100), 1, Qt::DashLine);
    p.setPen(linePen);
    p.drawLine(top, bot);

    for (const QString &name : m_axisNames) {
        if (!m_visible.value(name, true)) continue;
        double err = m_data.axes[name].err[m_cursorIdx];
        QPointF pt = dataToWidget(t, err);

        QColor c = m_color[name];
        p.setPen(QPen(c.darker(150), 2));
        p.setBrush(c);
        p.drawEllipse(pt, 3, 3);
    }

    p.restore();
}

void ChartView::drawRubberBand(QPainter &p)
{
    if (!m_rubberBand) return;

    QRect r = QRect(m_rubberStart, m_rubberEnd).normalized();
    r = r.intersected(m_plotArea.toRect());
    if (r.isEmpty()) return;

    p.save();
    p.setPen(QPen(QColor(0, 120, 215), 1, Qt::DashLine));
    p.setBrush(QColor(0, 120, 215, 40));
    p.drawRect(r);
    p.restore();
}

// --- mouse events ------------------------------------------------------

void ChartView::mouseMoveEvent(QMouseEvent *event)
{
    m_mousePos = event->pos();

    if (m_rubberBand) {
        m_rubberEnd = event->pos();
        update();
        return;
    }

    if (m_data.isEmpty()) return;

    bool onPlot = m_plotArea.contains(event->pos());
    if (!onPlot) {
        if (m_cursorVisible) {
            m_cursorVisible = false;
            m_cursorIdx = -1;
            update();
        }
        return;
    }

    m_cursorVisible = true;
    double t = widgetToTime(event->pos().x());
    m_cursorIdx = nearestIndex(t);

    if (m_cursorIdx != m_lastEmittedCursorIdx) {
        QMap<QString, double> errors;
        for (const QString &name : m_axisNames)
            errors[name] = m_data.axes[name].err[m_cursorIdx];

        emit cursorMoved(m_data.time[m_cursorIdx], m_cursorIdx, errors);
        m_lastEmittedCursorIdx = m_cursorIdx;
    }

    update();
}

void ChartView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        zoomReset();
        return;
    }

    if (event->button() == Qt::LeftButton && m_plotArea.contains(event->pos())) {
        m_rubberBand = true;
        m_rubberStart = event->pos();
        m_rubberEnd   = event->pos();
        m_cursorVisible = false;
        update();
    }
}

void ChartView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_rubberBand) {
        m_rubberBand = false;

        QRect r = QRect(m_rubberStart, m_rubberEnd).normalized();
        r = r.intersected(m_plotArea.toRect());

        if (r.width() > 10 && r.height() > 10) {
            double x1 = widgetToTime(r.left());
            double x2 = widgetToTime(r.right());
            double y1 = widgetToValue(r.bottom());
            double y2 = widgetToValue(r.top());

            m_viewXMin = std::min(x1, x2);
            m_viewXMax = std::max(x1, x2);
            m_viewYMin = std::min(y1, y2);
            m_viewYMax = std::max(y1, y2);
            invalidateStaticCache();
        }

        update();
        return;
    }
}

void ChartView::wheelEvent(QWheelEvent *event)
{
    if (!(event->modifiers() & Qt::ControlModifier)) return;

    double factor = (event->angleDelta().y() > 0) ? 0.85 : 1.0 / 0.85;

    double cx = widgetToTime(event->pos().x());
    m_viewXMin = cx - (cx - m_viewXMin) * factor;
    m_viewXMax = m_viewXMin + (m_viewXMax - m_viewXMin) * factor;

    double cy = widgetToValue(event->pos().y());
    m_viewYMin = cy - (cy - m_viewYMin) * factor;
    m_viewYMax = m_viewYMin + (m_viewYMax - m_viewYMin) * factor;

    double xMargin = (m_fullXMax - m_fullXMin) * 2;
    double yMargin = (m_fullYMax - m_fullYMin) * 2;
    if (m_viewXMin < m_fullXMin - xMargin) m_viewXMin = m_fullXMin - xMargin;
    if (m_viewXMax > m_fullXMax + xMargin) m_viewXMax = m_fullXMax + xMargin;
    if (m_viewYMin < m_fullYMin - yMargin) m_viewYMin = m_fullYMin - yMargin;
    if (m_viewYMax > m_fullYMax + yMargin) m_viewYMax = m_fullYMax + yMargin;

    invalidateStaticCache();
    update();
}

// --- drag & drop -------------------------------------------------------

void ChartView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ChartView::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString path = urls.first().toLocalFile();
        if (!path.isEmpty())
            emit fileDropped(path);
    }
}
