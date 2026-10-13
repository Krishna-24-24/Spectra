#include "ChartWidget.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <set>

ChartWidget::ChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setPalette(pal);
}

void ChartWidget::setTitle(const QString& title)
{
    title_ = title;
    update();
}

void ChartWidget::setAxisTitles(const QString& xTitle, const QString& yTitle)
{
    xTitle_ = xTitle;
    yTitle_ = yTitle;
    update();
}

void ChartWidget::setSeries(const QVector<ChartSeries>& series)
{
    series_ = series;
    update();
}

void ChartWidget::clear()
{
    series_.clear();
    update();
}

bool ChartWidget::hasData() const
{
    for (const auto& s : series_) {
        if (!s.points.isEmpty()) {
            return true;
        }
    }
    return false;
}

namespace {

// Round a maximum up to a "nice" number (1, 2, 2.5, 5, 10 x 10^n) for the y axis.
double niceCeil(double v)
{
    if (v <= 0.0) {
        return 1.0;
    }
    const double exponent = std::floor(std::log10(v));
    const double base = std::pow(10.0, exponent);
    const double f = v / base;
    double nice;
    if (f <= 1.0) nice = 1.0;
    else if (f <= 2.0) nice = 2.0;
    else if (f <= 2.5) nice = 2.5;
    else if (f <= 5.0) nice = 5.0;
    else nice = 10.0;
    return nice * base;
}

} // namespace

void ChartWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), Qt::white);

    // Title
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(font().pointSizeF() + 1.0);
    p.setFont(titleFont);
    p.setPen(QColor(40, 40, 40));
    p.drawText(QRect(0, 4, width(), 22), Qt::AlignCenter, title_);
    p.setFont(font());

    const QRect plot(58, 34, std::max(10, width() - 58 - 16), std::max(10, height() - 34 - 50));

    if (!hasData()) {
        p.setPen(QColor(130, 130, 130));
        p.drawText(rect(), Qt::AlignCenter, emptyText_);
        p.setPen(QPen(QColor(210, 210, 210), 1));
        p.drawRect(plot);
        return;
    }

    // Distinct x values and the maximum y.
    std::set<double> xs;
    double maxY = 0.0;
    for (const auto& s : series_) {
        for (const auto& pt : s.points) {
            xs.insert(pt.x());
            maxY = std::max(maxY, pt.y());
        }
    }
    const std::vector<double> xv(xs.begin(), xs.end());
    const double yTop = niceCeil(maxY * 1.08);

    auto xPos = [&](double x) {
        const auto it = std::find(xv.begin(), xv.end(), x);
        const double idx = static_cast<double>(it - xv.begin());
        if (xv.size() == 1) {
            return plot.left() + plot.width() / 2.0;
        }
        const double pad = plot.width() * 0.06;
        return plot.left() + pad + idx * (plot.width() - 2 * pad) / static_cast<double>(xv.size() - 1);
    };
    auto yPos = [&](double y) { return plot.bottom() - (y / yTop) * plot.height(); };

    // Grid + y labels
    const int gridLines = 5;
    QFontMetrics fm(font());
    for (int i = 0; i <= gridLines; ++i) {
        const double value = yTop * i / gridLines;
        const double y = yPos(value);
        p.setPen(QPen(QColor(228, 228, 228), 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(QColor(90, 90, 90));
        const QString label = QString::number(value, 'f', yTop >= 10 ? 0 : (yTop >= 1 ? 1 : 2));
        p.drawText(QRectF(0, y - 9, plot.left() - 6, 18), Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // Axes
    p.setPen(QPen(QColor(90, 90, 90), 1.2));
    p.drawLine(plot.bottomLeft(), plot.bottomRight());
    p.drawLine(plot.bottomLeft(), plot.topLeft());

    // x ticks
    for (double x : xv) {
        const double px = xPos(x);
        p.setPen(QPen(QColor(90, 90, 90), 1));
        p.drawLine(QPointF(px, plot.bottom()), QPointF(px, plot.bottom() + 4));
        p.drawText(QRectF(px - 20, plot.bottom() + 5, 40, 16), Qt::AlignCenter, QString::number(x, 'g', 6));
    }

    // Axis titles
    p.setPen(QColor(60, 60, 60));
    p.drawText(QRect(plot.left(), height() - 22, plot.width(), 18), Qt::AlignCenter, xTitle_);
    p.save();
    p.translate(12, plot.center().y());
    p.rotate(-90);
    p.drawText(QRect(-plot.height() / 2, -10, plot.height(), 20), Qt::AlignCenter, yTitle_);
    p.restore();

    // Series
    for (const auto& s : series_) {
        if (s.points.isEmpty()) {
            continue;
        }
        QVector<QPointF> pts = s.points;
        std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });

        QPen pen(s.color, s.dashed ? 1.6 : 2.4);
        if (s.dashed) {
            pen.setStyle(Qt::DashLine);
        }
        p.setPen(pen);
        QPainterPath path;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF pt(xPos(pts[i].x()), yPos(pts[i].y()));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        if (s.markers) {
            p.setPen(QPen(s.color.darker(130), 1.2));
            p.setBrush(s.color);
            for (const auto& pt : pts) {
                p.drawEllipse(QPointF(xPos(pt.x()), yPos(pt.y())), 4.0, 4.0);
            }
        }
        if (s.valueLabels) {
            p.setPen(s.color.darker(150));
            for (const auto& pt : pts) {
                const QString text = QString::number(pt.y(), 'f', decimals_);
                p.drawText(QRectF(xPos(pt.x()) - 30, yPos(pt.y()) - 22, 60, 16), Qt::AlignCenter, text);
            }
        }
    }

    // Legend (only when more than one series)
    if (series_.size() > 1) {
        int ly = plot.top() + 6;
        for (const auto& s : series_) {
            const int textW = fm.horizontalAdvance(s.name);
            const int lx = plot.right() - textW - 40;
            QPen pen(s.color, 2);
            if (s.dashed) pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(lx, ly + 7, lx + 24, ly + 7);
            p.setPen(QColor(60, 60, 60));
            p.drawText(lx + 30, ly, textW + 4, 14, Qt::AlignLeft | Qt::AlignVCenter, s.name);
            ly += 16;
        }
    }
}
