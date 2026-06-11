#pragma once
#include <QWidget>
#include <QMap>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include "data_loader.h"

// AKD 示波器式误差曲线画布:
//  - 同一画布显示各轴 位置误差 / 速度指令 / 速度反馈
//  - 左侧双 Y 轴:外侧速度刻度,内侧位置误差刻度(无速度数据时只有位置轴)
//  - 顶部横排图例(仅列出当前可见的曲线)
class ChartView : public QWidget {
    Q_OBJECT
public:
    // 曲线类型
    enum SeriesType { kErr = 0, kCmdVel = 1, kFbVel = 2 };

    explicit ChartView(QWidget *parent = nullptr);

    void setAxisData(const Dataset &data);
    void setSeriesVisible(const QString &axis, bool visible);   // 整轴开关
    bool isSeriesVisible(const QString &axis) const;
    // 曲线类型开关(作用于所有轴)
    void setShowPosError(bool on);
    void setShowCmdVel(bool on);
    void setShowFbVel(bool on);
    bool dataHasVelocity() const;   // 数据中是否存在速度列
    // 某轴位置误差曲线的颜色(供工具栏/面板取色保持一致)
    QColor errColorFor(const QString &axis) const;
    QStringList axisNames() const { return m_axisNames; }
    void zoomReset();
    static QColor axisColor(int idx);

signals:
    // 光标移动:主窗口用 idx 自行读取数据集组装面板
    void cursorMoved(double time, int idx);
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
    // 一条可绘制曲线的引用(数据归 m_data 所有)
    struct SeriesRef {
        QString axis;
        int     type;                    // SeriesType
        const QVector<double> *data;
        QColor  color;
        QString label;                   // 图例文本,如 "X 位置误差 [mm]"
        bool    velScale;                // true=速度轴, false=位置轴
    };
    QVector<SeriesRef> visibleSeries() const;

    // 坐标变换
    double xToPx(double t) const;
    double posToPx(double v) const;
    double velToPx(double v) const;
    double pxToTime(double wx) const;
    int    nearestIndex(double time) const;

    void computeFullRange();   // 按可见曲线重算 X/位置/速度全量范围
    void updatePlotArea();     // 依据图例行数与速度轴有无计算绘图区

    void drawLegend(QPainter &p);
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

    // Per-axis state
    QStringList    m_axisNames;
    QMap<QString, bool> m_visible;
    // 曲线颜色:键 = 轴名*3 + 类型,载入时按存在的曲线顺序分配
    QMap<QString, QColor> m_seriesColor;

    // 曲线类型开关
    bool m_showErr    = true;
    bool m_showCmdVel = true;
    bool m_showFbVel  = true;

    // Full data range
    double m_fullXMin = 0,  m_fullXMax = 1;
    double m_fullPosMin = -1, m_fullPosMax = 1;
    double m_fullVelMin = -1, m_fullVelMax = 1;

    // Current view range
    double m_viewXMin = 0,  m_viewXMax = 1;
    double m_viewPosMin = -1, m_viewPosMax = 1;
    double m_viewVelMin = -1, m_viewVelMax = 1;

    // 用户是否已手动缩放(框选/滚轮)。
    // 切换可见性时:未缩放则跟随新数据范围,已缩放则保留当前视野。
    bool m_userZoomed = false;

    // Plot area in widget pixels
    QRectF m_plotArea;
    int    m_legendRows = 1;

    // Margins(ML 依据有无速度轴动态决定,见 updatePlotArea)
    static constexpr int kMLPosOnly = 76;   // 仅位置轴
    static constexpr int kMLDual    = 168;  // 速度轴 + 位置轴
    static constexpr int MR = 25;
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
