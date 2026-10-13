#pragma once
// VideoView: a widget that simply paints the current video frame, scaled to fit
// (with black bars so the picture is never stretched).

#include <QImage>
#include <QString>
#include <QWidget>

class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void setMessage(const QString& text);   // shown when there is no picture
    QSize minimumSizeHint() const override { return QSize(320, 180); }
    QSize sizeHint() const override { return QSize(480, 270); }

public slots:
    void setFrame(const QImage& image);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage image_;
    QString message_ = "No video loaded";
};
