#pragma once
//
// ChartWidget
// -----------
// A small line chart drawn with QPainter (no Qt Charts library needed).
// The x axis is "categorical": every distinct x value (1, 2, 4, 8 threads) gets an
// equally spaced tick, which keeps the benchmark graphs easy to read.

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

struct ChartSeries {
    QString name;
    QVector<QPointF> points;   // x = thread count, y = value
    QColor color = QColor(30, 110, 200);
    bool dashed = false;       // used for the "ideal" reference line
    bool markers = true;
    bool valueLabels = true;
};

class ChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChartWidget(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setAxisTitles(const QString& xTitle, const QString& yTitle);
    void setDecimals(int decimals) { decimals_ = decimals; update(); }
    void setEmptyText(const QString& text) { emptyText_ = text; update(); }
    void setSeries(const QVector<ChartSeries>& series);
    void clear();

    int seriesCount() const { return series_.size(); }
    bool hasData() const;

    QSize minimumSizeHint() const override { return QSize(260, 180); }
    QSize sizeHint() const override { return QSize(380, 240); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString title_;
    QString xTitle_;
    QString yTitle_;
    QString emptyText_ = "Run the benchmark to see this graph";
    int decimals_ = 2;
    QVector<ChartSeries> series_;
};
