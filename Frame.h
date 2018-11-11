#ifndef FRAME_H
#define FRAME_H

#include <QImage>

struct AVFrame;

/// Note that copying this struct around and passing it around by value should be very cheap and fast, because all
/// data (including the avframe) is shallow-copied.
struct Frame
{
    QImage img;
    quint64 num = 0ULL;
    AVFrame *avframe = nullptr; ///< may be null. if non-nullptr, contains referenced AVFrame, suitable for passing to avcodec_send_frame(). (be sure to set avframe->pts before using). Will be freed in d'tor with av_frame_free



    Frame() {}
    Frame(const QImage &img, quint64 num) : img(img), num(num) {}

    Frame(const Frame &other);
    Frame(Frame &&other);
    ~Frame(); ///< cleans up all resources, including avframe

    Frame &operator=(const Frame &); // copy assign
    Frame &operator=(Frame &&); // move assign

    bool isNull() const { return img.isNull(); }
    void nullify() { img = QImage(); } ///< cleans up just the image. avframe is left alone.

    ///< Convenience function: cleans up just the avframe, setting it to nullptr and freeing its resources and unreferencing any referenced buffers
    void destroyAVFrame() { destruct(true); }

    static constexpr double DefaultFPS() { return 10.0; }
    static constexpr int DefaultWidth() { return 5056; }
    static constexpr int DefaultHeight() { return 2968; }

private:
    void destruct(bool justavframe = false);
};

Q_DECLARE_METATYPE(Frame);

#endif // FRAME_H
