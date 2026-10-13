#include "VideoPlayer.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace {

QImage toQImage(const cv::Mat& bgr)
{
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    // copy() so the QImage owns its pixels (rgb goes out of scope)
    return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888).copy();
}

} // namespace

VideoPlayer::VideoPlayer(QObject* parent)
    : QObject(parent)
{
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &VideoPlayer::tick);
}

bool VideoPlayer::open(const QString& path, QString& error)
{
    close();
    cap_.open(path.toLocal8Bit().toStdString());
    if (!cap_.isOpened()) {
        error = "The player could not open this video file.";
        return false;
    }
    fps_ = cap_.get(cv::CAP_PROP_FPS);
    if (!(fps_ > 1.0 && fps_ < 500.0)) {
        fps_ = 25.0;
    }
    totalFrames_ = static_cast<long long>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
    open_ = true;
    timer_.setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / fps_))));
    if (!showFrameAt(0)) {
        error = "The first frame of this video could not be decoded.";
        close();
        return false;
    }
    return true;
}

void VideoPlayer::close()
{
    timer_.stop();
    cap_.release();
    open_ = false;
    totalFrames_ = 0;
    currentFrame_ = 0;
    emit playbackStateChanged(false);
}

bool VideoPlayer::showFrameAt(long long index)
{
    if (!open_) {
        return false;
    }
    if (totalFrames_ > 0) {
        index = std::min(index, totalFrames_ - 1);
    }
    index = std::max(0LL, index);
    // Only jump when we are not already at the right place (jumping is slower than reading on).
    if (index != currentFrame_ + 1 || index == 0) {
        cap_.set(cv::CAP_PROP_POS_FRAMES, static_cast<double>(index));
    }
    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
        return false;
    }
    currentFrame_ = index;
    emit frameReady(toQImage(frame));
    emit positionChanged(positionSec());
    return true;
}

void VideoPlayer::play()
{
    if (!open_) {
        return;
    }
    if (totalFrames_ > 0 && currentFrame_ >= totalFrames_ - 1) {
        showFrameAt(0);
    }
    timer_.start();
    emit playbackStateChanged(true);
}

void VideoPlayer::pause()
{
    timer_.stop();
    emit playbackStateChanged(false);
}

void VideoPlayer::stop()
{
    timer_.stop();
    showFrameAt(0);
    emit playbackStateChanged(false);
}

void VideoPlayer::seek(double seconds)
{
    if (!open_) {
        return;
    }
    const long long index = static_cast<long long>(std::llround(std::max(0.0, seconds) * fps_));
    if (!showFrameAt(index)) {
        // Some codecs cannot land exactly; fall back to the start instead of showing nothing.
        showFrameAt(0);
    }
}

void VideoPlayer::tick()
{
    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
        timer_.stop();
        emit playbackStateChanged(false);
        emit reachedEnd();
        return;
    }
    ++currentFrame_;
    emit frameReady(toQImage(frame));
    emit positionChanged(positionSec());
}
