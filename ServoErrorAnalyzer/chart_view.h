#pragma once
#include <QWidget>
#include <QMap>
#include <QStringList>
#include "data_loader.h"

class ChartView : public QWidget {
    Q_OBJECT
public:
    explicit ChartView(QWidget *parent = nullptr);

    void setAxisData(const Dataset &data);
    void setSeriesVisible(const QString &axis, bool visible);
    bool isSeriesVisible(const QString &axis) const;
    QStringList axisNames() const { return m_axisNames; }
    void zoomReset();

signals:
    void cursorMoved(double time, int idx, const QMap<QString, double> &errors);
    void fileDropped(const QString &filePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QPointF dataToWidget(double t, double v) const;
    double  widgetToTime(double wx) const;
    double  widgetToValue(double wy) const;
    int     nearestIndex(double time) const;

    void drawGrid(QPainter &p);
    void drawCurves(QPainter &p);
    void drawCursor(QPainter &p);
    void drawRubberBand(QPainter &p);

    static void calcTicks(double vMin, double vMax, int maxTicks,
                          double &first, double &step, int &count);
    static QColor axisColor(int idx);

    // Data
    Dataset   m_data;

    // Per-axis state
    QStringList    m_axisNames;
    QMap<QString, bool>  m_visible;
    QMap<QString, QColor> m_color;

    // Full data range
    double m_fullXMin, m_fullXMax;
    double m_fullYMin, m_fullYMax;

    // Current view range
    double m_viewXMin, m_viewXMax;
    double m_viewYMin, m_viewYMax;

    // Plot area in widget pixels
    QRectF m_plotArea;

    // Margins
    static constexpr int ML = 70;
    static constexpr int MR = 25;
    static constexpr int MT = 25;
    static constexpr int MB = 45;

    // Cursor state
    QPoint m_mousePos;
    int    m_cursorIdx;
    bool   m_cursorVisible;

    // Rubber-band zoom
    bool   m_rubberBand;
    QPoint m_rubberStart;
    QPoint m_rubberEnd;

    // Colors
    QColor m_gridColor;
    QColor m_bgColor;
};
