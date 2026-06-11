#pragma once
#include <QWidget>
#include <QMap>
#include <QPixmap>
#include <QStringList>
#include "data_loader.h"

class ChartView : public QWidget {
    Q_OBJECT
public:
    // 显示内容:位置误差(cmd-fb)或速度误差(lv-fv)
    enum class Mode { PosError, VelError };

    explicit ChartView(QWidget *parent = nullptr);

    void setAxisData(const Dataset &data);
    void setSeriesVisible(const QString &axis, bool visible);
    bool isSeriesVisible(const QString &axis) const;
    QStringList axisNames() const { return m_axisNames; }
    void setDisplayMode(Mode mode);
    Mode displayMode() const { return m_mode; }
    void zoomReset();
    static QColor axisColor(int idx);

signals:
    void cursorMoved(double time, int idx, const QMap<QString, double> &values);
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

    // 当前模式下该轴是否有数据可画(速度模式要求速度列存在)
    bool axisAvailable(const AxisChannel &ch) const;
    // 当前模式对应的数据序列
    const QVector<double> &seriesFor(const AxisChannel &ch) const;
    // 根据可见轴与当前模式重算全量数据范围(含留白)
    void computeFullRange();

    void drawGrid(QPainter &p);
    void drawCurves(QPainter &p);
    void drawCursor(QPainter &p);
    void drawRubberBand(QPainter &p);
    void drawStaticContent(QPainter &p);
    void invalidateStaticCache();

    static void calcTicks(double vMin, double vMax, int maxTicks,
                          double &first, double &step, int &count);

    // Data
    Dataset   m_data;
    Mode      m_mode = Mode::PosError;

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

    // 用户是否已手动缩放(框选/滚轮)。
    // 切换轴可见性时:未缩放则跟随新数据范围,已缩放则保留当前视野。
    bool m_userZoomed = false;

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
    int    m_lastEmittedCursorIdx;
    bool   m_cursorVisible;

    // Rubber-band zoom
    bool   m_rubberBand;
    QPoint m_rubberStart;
    QPoint m_rubberEnd;

    // Colors
    QColor m_gridColor;
    QColor m_bgColor;

    // Cached chart without the cursor/rubber band. Large CSV files are costly
    // to redraw, so mouse moves should reuse this layer.
    QPixmap m_staticCache;
    bool    m_staticCacheDirty;
};
