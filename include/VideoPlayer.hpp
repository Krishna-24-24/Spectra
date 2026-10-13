#pragma once
//
// VideoPlayer
// -----------
// The "Video Player Module". Plays the video in the GUI thread using a QTimer and
// its OWN cv::VideoCapture, completely separate from the analysis engine. So the
// user can keep watching / seeking while the analysis runs on other threads.

#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>

#include <opencv2/videoio.hpp>

class VideoPlayer : public QObject {
    Q_OBJECT
public:
    explicit VideoPlayer(QObject* parent = nullptr);

    bool open(const QString& path, QString& error);
    void close();

    bool isOpen() const { return open_; }
    bool isPlaying() const { return timer_.isActive(); }
    double durationSec() const { return fps_ > 0 ? totalFrames_ / fps_ : 0.0; }
    double positionSec() const { return fps_ > 0 ? currentFrame_ / fps_ : 0.0; }

signals:
    void frameReady(const QImage& image);
    void positionChanged(double seconds);
    void playbackStateChanged(bool playing);
    void reachedEnd();

public slots:
    void play();
    void pause();
    void stop();               // pause and go back to the first frame
    void seek(double seconds); // jump (works while playing or paused)

private slots:
    void tick();

private:
    bool showFrameAt(long long index);

    cv::VideoCapture cap_;
    QTimer timer_;
    bool open_ = false;
    double fps_ = 25.0;
    long long totalFrames_ = 0;
    long long currentFrame_ = 0;   // index of the frame currently shown
};
