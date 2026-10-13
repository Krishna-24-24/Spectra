#include "VideoView.hpp"

#include <QPainter>

VideoView::VideoView(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoView::setMessage(const QString& text)
{
    message_ = text;
    update();
}

void VideoView::setFrame(const QImage& image)
{
    image_ = image;
    update();
}

void VideoView::clear()
{
    image_ = QImage();
    update();
}

void VideoView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (image_.isNull()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter, message_);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QSize target = image_.size();
    target.scale(size(), Qt::KeepAspectRatio);
    const QRect r(QPoint((width() - target.width()) / 2, (height() - target.height()) / 2), target);
    p.drawImage(r, image_);
}
