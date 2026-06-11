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

// 刻度标签:固定显示 3 位小数;放大后刻度步长小于 0.001 时
// 自动增加小数位,保证相邻刻度值始终可区分
static QString formatTick(double v, double step)
{
    int dec = 3;
    if (step > 0) {
        const int need =
            static_cast<int>(std::ceil(-std::log10(step)));
        if (need > dec) dec = need;
    }
    return QString::number(v, 'f', dec);
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

// QWheelEvent 取坐标的版本兼容封装(Qt 5.14 起才有 position())
static QPointF wheelPos(const QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return event->position();
#else
    return event->posF();
#endif
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

// 12 色调色板 — 单轴文件依次取 红/绿/蓝,与 AKD 示波器配色一致
static const QColor kPalette[] = {
    QColor(220, 30, 30),     // red
    QColor(0, 150, 0),       // green
    QColor(0, 100, 255),     // blue
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

static QString seriesKey(const QString &axis, int type)
{
    return axis + QLatin1Char(':') + QString::number(type);
}

// 曲线图例文本:轴名 + 类型 + 单位
static QString seriesLabel(const QString &axis, int type)
{
    const bool deg = (axis == "C" || axis == "A");
    switch (type) {
    case ChartView::kErr:
        return axis + QString::fromUtf8(" 位置误差 [%1]")
                          .arg(deg ? "deg" : "mm");
    case ChartView::kCmdVel:
        return axis + QString::fromUtf8(" 速度指令 [%1]")
                          .arg(deg ? "deg/min" : "mm/min");
    default:
        return axis + QString::fromUtf8(" 速度反馈 [%1]")
                          .arg(deg ? "deg/min" : "mm/min");
    }
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
    updatePlotArea();
    invalidateStaticCache();
}

// --- series enumeration -------------------------------------------------

bool ChartView::dataHasVelocity() const
{
    for (const auto &name : m_axisNames) {
        auto it = m_data.axes.constFind(name);
        if (it != m_data.axes.constEnd() && it->hasVelocity())
            return true;
    }
    return false;
}

QColor ChartView::errColorFor(const QString &axis) const
{
    return m_seriesColor.value(seriesKey(axis, kErr), QColor(Qt::black));
}

QVector<ChartView::SeriesRef> ChartView::visibleSeries() const
{
    QVector<SeriesRef> out;
    for (const QString &name : m_axisNames) {
        if (!m_visible.value(name, true)) continue;
        auto it = m_data.axes.constFind(name);
        if (it == m_data.axes.constEnd()) continue;
        // 注意:必须取 map 节点的稳定引用,SeriesRef 存其数据指针
        const AxisChannel &ch = it.value();

        if (m_showErr && !ch.err.isEmpty()) {
            out.append({ name, kErr, &ch.err,
                         m_seriesColor.value(seriesKey(name, kErr)),
                         seriesLabel(name, kErr), false });
        }
        if (ch.hasVelocity()) {
            if (m_showCmdVel) {
                out.append({ name, kCmdVel, &ch.cmdVel,
                             m_seriesColor.value(seriesKey(name, kCmdVel)),
                             seriesLabel(name, kCmdVel), true });
            }
            if (m_showFbVel) {
                out.append({ name, kFbVel, &ch.fbVel,
                             m_seriesColor.value(seriesKey(name, kFbVel)),
                             seriesLabel(name, kFbVel), true });
            }
        }
    }
    return out;
}

// --- coordinate transforms --------------------------------------------

double ChartView::xToPx(double t) const
{
    double rx = m_viewXMax - m_viewXMin;
    if (rx == 0) rx = 1;
    return m_plotArea.left() + (t - m_viewXMin) / rx * m_plotArea.width();
}

double ChartView::posToPx(double v) const
{
    double ry = m_viewPosMax - m_viewPosMin;
    if (ry == 0) ry = 1;
    return m_plotArea.bottom() - (v - m_viewPosMin) / ry * m_plotArea.height();
}

double ChartView::velToPx(double v) const
{
    double ry = m_viewVelMax - m_viewVelMin;
    if (ry == 0) ry = 1;
    return m_plotArea.bottom() - (v - m_viewVelMin) / ry * m_plotArea.height();
}

double ChartView::pxToTime(double wx) const
{
    double rx = m_viewXMax - m_viewXMin;
    if (rx == 0) rx = 1;
    return m_viewXMin + (wx - m_plotArea.left()) / m_plotArea.width() * rx;
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

    // 按存在的曲线顺序分配颜色(轴优先):
    // 单轴含速度 → 位置误差红 / 速度指令绿 / 速度反馈蓝(同 AKD)
    m_seriesColor.clear();
    m_visible.clear();
    int colorIdx = 0;
    for (const QString &name : m_axisNames) {
        m_visible[name] = true;
        const AxisChannel &ch = m_data.axes[name];
        m_seriesColor[seriesKey(name, kErr)] = axisColor(colorIdx++);
        if (ch.hasVelocity()) {
            m_seriesColor[seriesKey(name, kCmdVel)] = axisColor(colorIdx++);
            m_seriesColor[seriesKey(name, kFbVel)]  = axisColor(colorIdx++);
        }
    }

    computeFullRange();
    updatePlotArea();
    zoomReset();
}

void ChartView::computeFullRange()
{
    if (m_data.isEmpty()) {
        m_fullXMin = 0; m_fullXMax = 1;
        m_fullPosMin = -1; m_fullPosMax = 1;
        m_fullVelMin = -1; m_fullVelMax = 1;
    } else {
        m_fullXMin = m_data.time.first();
        m_fullXMax = m_data.time.last();

        // 位置/速度范围跟随可见轴(与类型开关无关,保持坐标轴稳定)
        bool firstPos = true, firstVel = true;
        m_fullPosMin = -1; m_fullPosMax = 1;
        m_fullVelMin = -1; m_fullVelMax = 1;
        for (const auto &name : m_axisNames) {
            if (!m_visible.value(name, true)) continue;
            const AxisChannel &ch = m_data.axes[name];
            for (double v : ch.err) {
                if (firstPos) { m_fullPosMin = m_fullPosMax = v; firstPos = false; }
                else {
                    if (v < m_fullPosMin) m_fullPosMin = v;
                    if (v > m_fullPosMax) m_fullPosMax = v;
                }
            }
            if (ch.hasVelocity()) {
                for (const QVector<double> *vec : { &ch.cmdVel, &ch.fbVel }) {
                    for (double v : *vec) {
                        if (firstVel) { m_fullVelMin = m_fullVelMax = v; firstVel = false; }
                        else {
                            if (v < m_fullVelMin) m_fullVelMin = v;
                            if (v > m_fullVelMax) m_fullVelMax = v;
                        }
                    }
                }
            }
        }
    }

    double xPad = (m_fullXMax - m_fullXMin) * 0.02;
    if (xPad == 0) xPad = 0.01;
    m_fullXMin -= xPad;
    m_fullXMax += xPad;

    double posPad = (m_fullPosMax - m_fullPosMin) * 0.08;
    if (posPad == 0) posPad = 0.001;
    m_fullPosMin -= posPad;
    m_fullPosMax += posPad;

    double velPad = (m_fullVelMax - m_fullVelMin) * 0.08;
    if (velPad == 0) velPad = 0.001;
    m_fullVelMin -= velPad;
    m_fullVelMax += velPad;
}

void ChartView::updatePlotArea()
{
    const int ml = dataHasVelocity() ? kMLDual : kMLPosOnly;

    // 图例流式布局行数(与 drawLegend 的排布逻辑一致)
    QFont f = font();
    f.setPixelSize(11);
    const QFontMetrics fm(f);
    const double availW = width() - ml - MR;
    int rows = 1;
    double x = 0;
    const auto series = visibleSeries();
    for (const SeriesRef &s : series) {
        const double entryW = 22 + fm.horizontalAdvance(s.label) + 18;
        if (x > 0 && x + entryW > availW) { ++rows; x = 0; }
        x += entryW;
    }
    if (series.isEmpty()) rows = 1;
    m_legendRows = rows;

    const double top = 8 + rows * 18 + 4;
    m_plotArea = QRectF(ml, top,
                        width() - ml - MR, height() - top - MB);
}

void ChartView::setSeriesVisible(const QString &axis, bool visible)
{
    if (!m_visible.contains(axis)) return;
    m_visible[axis] = visible;

    // 范围跟随可见轴;若用户已手动缩放则保留当前视野
    computeFullRange();
    updatePlotArea();
    if (!m_userZoomed) {
        zoomReset();
    } else {
        invalidateStaticCache();
        update();
    }
}

bool ChartView::isSeriesVisible(const QString &axis) const
{
    return m_visible.value(axis, false);
}

void ChartView::setShowPosError(bool on)
{
    if (m_showErr == on) return;
    m_showErr = on;
    updatePlotArea();
    invalidateStaticCache();
    update();
}

void ChartView::setShowCmdVel(bool on)
{
    if (m_showCmdVel == on) return;
    m_showCmdVel = on;
    updatePlotArea();
    invalidateStaticCache();
    update();
}

void ChartView::setShowFbVel(bool on)
{
    if (m_showFbVel == on) return;
    m_showFbVel = on;
    updatePlotArea();
    invalidateStaticCache();
    update();
}

void ChartView::zoomReset()
{
    m_viewXMin = m_fullXMin;
    m_viewXMax = m_fullXMax;
    m_viewPosMin = m_fullPosMin;
    m_viewPosMax = m_fullPosMax;
    m_viewVelMin = m_fullVelMin;
    m_viewVelMax = m_fullVelMax;
    m_userZoomed = false;
    invalidateStaticCache();
    update();
}

// --- drawing -----------------------------------------------------------

void ChartView::paintEvent(QPaintEvent *)
{
    if (m_plotArea.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QSize pixmapSize = size() * devicePixelRatio();
    if (m_staticCacheDirty || m_staticCache.size() != pixmapSize) {
        m_staticCache = QPixmap(pixmapSize);
        m_staticCache.setDevicePixelRatio(devicePixelRatio());

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

    drawLegend(p);
    drawGrid(p);
    drawCurves(p);
}

void ChartView::invalidateStaticCache()
{
    m_staticCacheDirty = true;
}

// 顶部横排图例(AKD 风格):线样 + 文本,超宽自动换行
void ChartView::drawLegend(QPainter &p)
{
    const auto series = visibleSeries();
    if (series.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const QFontMetrics fm(f);

    const double availW = m_plotArea.width();
    double x = m_plotArea.left();
    double y = 8;
    for (const SeriesRef &s : series) {
        const double entryW = 22 + fm.horizontalAdvance(s.label) + 18;
        if (x > m_plotArea.left() && x + entryW > m_plotArea.left() + availW) {
            x = m_plotArea.left();
            y += 18;
        }
        p.setPen(QPen(s.color, 2));
        p.drawLine(QPointF(x, y + 7), QPointF(x + 18, y + 7));
        p.setPen(Qt::black);
        p.drawText(QPointF(x + 22, y + 12), s.label);
        x += entryW;
    }
    p.restore();
}

void ChartView::drawGrid(QPainter &p)
{
    p.save();

    const bool dual = dataHasVelocity();
    const double ml = m_plotArea.left();

    QPen pen(m_gridColor, 1);
    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);

    // --- X 轴刻度(底部,毫秒显示)----------------------------------------
    {
        // 刻度按毫秒栅格计算,数据时间(秒)换算后显示
        double first, step; int count;
        calcTicks(m_viewXMin * 1000.0, m_viewXMax * 1000.0, 8,
                  first, step, count);

        for (int i = 0; i < count; ++i) {
            double valMs = first + i * step;
            double val = valMs / 1000.0;
            if (val < m_viewXMin || val > m_viewXMax) continue;
            const double x = xToPx(val);
            // 网格线裁剪在绘图区内;数字标签画在绘图区下方,不裁剪
            p.save();
            p.setClipRect(m_plotArea);
            p.setPen(pen);
            p.drawLine(QPointF(x, m_plotArea.top()),
                       QPointF(x, m_plotArea.bottom()));
            p.restore();
            p.setPen(Qt::black);
            QRectF labelRect(x - 40, m_plotArea.bottom() + 2, 80, 16);
            p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop,
                       formatTick(valMs, step));
        }

        QRectF xLabel(m_plotArea.left(), m_plotArea.bottom() + 22,
                      m_plotArea.width(), 16);
        p.setPen(Qt::black);
        font.setPixelSize(11);
        p.setFont(font);
        p.drawText(xLabel, Qt::AlignHCenter, QString::fromUtf8("时间 [ms]"));
        font.setPixelSize(10);
        p.setFont(font);
    }

    // --- 位置误差轴(内侧):横向网格线 + 蓝色刻度值 -----------------------
    {
        double first, step; int count;
        calcTicks(m_viewPosMin, m_viewPosMax, 7, first, step, count);

        const QColor posLabelClr(30, 70, 180);
        for (int i = 0; i < count; ++i) {
            double val = first + i * step;
            if (val < m_viewPosMin || val > m_viewPosMax) continue;
            const double y = posToPx(val);
            p.setPen(pen);
            p.save();
            p.setClipRect(m_plotArea);
            p.drawLine(QPointF(m_plotArea.left(), y),
                       QPointF(m_plotArea.right(), y));
            p.restore();
            p.setPen(posLabelClr);
            QRectF labelRect(ml - 60, y - 8, 56, 16);
            p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                       formatTick(val, step));
        }

        // 轴标题(竖排)
        p.setPen(posLabelClr);
        font.setPixelSize(11);
        p.setFont(font);
        p.save();
        p.translate(ml - 62, m_plotArea.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-60, -10, 120, 20), Qt::AlignCenter,
                   QString::fromUtf8("位置误差 [mm]"));
        p.restore();
        font.setPixelSize(10);
        p.setFont(font);
    }

    // --- 速度轴(外侧,仅当数据含速度列)-----------------------------------
    if (dual) {
        const double axisX = ml - 82;   // 速度轴竖线位置
        p.setPen(QPen(QColor(120, 120, 120), 1));
        p.drawLine(QPointF(axisX, m_plotArea.top()),
                   QPointF(axisX, m_plotArea.bottom()));

        double first, step; int count;
        calcTicks(m_viewVelMin, m_viewVelMax, 7, first, step, count);

        for (int i = 0; i < count; ++i) {
            double val = first + i * step;
            if (val < m_viewVelMin || val > m_viewVelMax) continue;
            const double y = velToPx(val);
            p.setPen(QPen(QColor(120, 120, 120), 1));
            p.drawLine(QPointF(axisX - 3, y), QPointF(axisX + 3, y));
            p.setPen(Qt::black);
            QRectF labelRect(axisX - 70, y - 8, 64, 16);
            p.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                       formatTick(val, step));
        }

        p.setPen(Qt::black);
        font.setPixelSize(11);
        p.setFont(font);
        p.save();
        p.translate(12, m_plotArea.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-70, -10, 140, 20), Qt::AlignCenter,
                   QString::fromUtf8("速度 [mm/min]"));
        p.restore();
        font.setPixelSize(10);
        p.setFont(font);
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

    for (const SeriesRef &s : visibleSeries()) {
        p.setPen(QPen(s.color, 1));

        QPolygonF poly;
        poly.reserve(visCount);
        for (int i = iBeg; i < iEnd; ++i) {
            const double v = (*s.data)[i];
            poly.append(QPointF(xToPx(t[i]),
                                s.velScale ? velToPx(v) : posToPx(v)));
        }

        if (poly.size() >= 2)
            p.drawPolyline(poly);
        else if (poly.size() == 1)
            p.drawPoint(poly[0]);
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

    const double t = m_data.time[m_cursorIdx];
    const double x = xToPx(t);

    QPen linePen(QColor(100, 100, 100), 1, Qt::DashLine);
    p.setPen(linePen);
    p.drawLine(QPointF(x, m_plotArea.top()), QPointF(x, m_plotArea.bottom()));

    for (const SeriesRef &s : visibleSeries()) {
        const double v = (*s.data)[m_cursorIdx];
        QPointF pt(x, s.velScale ? velToPx(v) : posToPx(v));
        p.setPen(QPen(s.color.darker(150), 2));
        p.setBrush(s.color);
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
    double t = pxToTime(event->pos().x());
    m_cursorIdx = nearestIndex(t);

    if (m_cursorIdx != m_lastEmittedCursorIdx) {
        emit cursorMoved(m_data.time[m_cursorIdx], m_cursorIdx);
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
            const double x1 = pxToTime(r.left());
            const double x2 = pxToTime(r.right());
            m_viewXMin = std::min(x1, x2);
            m_viewXMax = std::max(x1, x2);

            // 双 Y 轴:按选框在绘图区的高度比例同步缩放两个纵轴
            const double h = m_plotArea.height();
            const double fTop = (r.top() - m_plotArea.top()) / h;
            const double fBot = (r.bottom() - m_plotArea.top()) / h;

            const double posRange = m_viewPosMax - m_viewPosMin;
            const double posMaxOld = m_viewPosMax;
            m_viewPosMax = posMaxOld - fTop * posRange;
            m_viewPosMin = posMaxOld - fBot * posRange;

            const double velRange = m_viewVelMax - m_viewVelMin;
            const double velMaxOld = m_viewVelMax;
            m_viewVelMax = velMaxOld - fTop * velRange;
            m_viewVelMin = velMaxOld - fBot * velRange;

            m_userZoomed = true;
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
    const QPointF pos = wheelPos(event);

    // Zoom toward cursor: anchor the data value under the cursor so it stays
    // in the same widget pixel.  Save old extents first — both endpoints must
    // be computed from the *original* values, not the already-mutated ones.
    double cx = pxToTime(pos.x());
    double oldXMin = m_viewXMin, oldXMax = m_viewXMax;
    m_viewXMin = cx - (cx - oldXMin) * factor;
    m_viewXMax = cx + (oldXMax - cx) * factor;

    // 双 Y 轴:以光标像素高度比例为锚点,同步缩放两个纵轴
    const double fy = (pos.y() - m_plotArea.top()) / m_plotArea.height();
    {
        const double range = m_viewPosMax - m_viewPosMin;
        const double anchor = m_viewPosMax - fy * range;
        const double newRange = range * factor;
        m_viewPosMax = anchor + fy * newRange;
        m_viewPosMin = m_viewPosMax - newRange;
    }
    {
        const double range = m_viewVelMax - m_viewVelMin;
        const double anchor = m_viewVelMax - fy * range;
        const double newRange = range * factor;
        m_viewVelMax = anchor + fy * newRange;
        m_viewVelMin = m_viewVelMax - newRange;
    }

    // 限幅:不超出全量范围外 2 倍
    const double xMargin = (m_fullXMax - m_fullXMin) * 2;
    if (m_viewXMin < m_fullXMin - xMargin) m_viewXMin = m_fullXMin - xMargin;
    if (m_viewXMax > m_fullXMax + xMargin) m_viewXMax = m_fullXMax + xMargin;
    const double posMargin = (m_fullPosMax - m_fullPosMin) * 2;
    if (m_viewPosMin < m_fullPosMin - posMargin) m_viewPosMin = m_fullPosMin - posMargin;
    if (m_viewPosMax > m_fullPosMax + posMargin) m_viewPosMax = m_fullPosMax + posMargin;
    const double velMargin = (m_fullVelMax - m_fullVelMin) * 2;
    if (m_viewVelMin < m_fullVelMin - velMargin) m_viewVelMin = m_fullVelMin - velMargin;
    if (m_viewVelMax > m_fullVelMax + velMargin) m_viewVelMax = m_fullVelMax + velMargin;

    m_userZoomed = true;
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
