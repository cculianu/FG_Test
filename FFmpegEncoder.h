#ifndef FFMPEGENCODER_H
#define FFMPEGENCODER_H

#include <QString>
#include <QReadWriteLock>

struct Frame;

class FFmpegEncoder
{
public:
    FFmpegEncoder(const QString & outFile, double fps, int bitrate, int fmt, unsigned numFFmpegEncodingThreads);
    ~FFmpegEncoder();

    // call this from data grabbing thread to enqueue a video frame
    bool enqueue(const Frame &, QString *errMsg = nullptr);
    // call this from encoding thread to encode 1 video frame
    // returns the frame number if processed a frame, 0 if timed out, or negative value on error, in which case
    // *errMsg will contain an error string.
    // (encode_time_ms is the time in milliseconds it took to encode the frame.)
    qint64 processOneVideoFrame(int timeout_ms, QString *errMsg = nullptr, int *encode_time_ms = nullptr);

    bool encode(const Frame &, QString *errMsg = nullptr);
    quint64 bytesWritten() const;

    bool wroteHeader() const;

private:
    struct Priv;
    struct Converter;

    Priv *p = nullptr;
    Converter *conv = nullptr;
    QReadWriteLock plock;

    bool setupP(int w, int h, int av_pix_fmt); //< sets error if false
    bool flushEncoder(QString *errMsg = nullptr);


    QString outFile;
    double fps = 0.0;
    int bitrate=0,fmt=0,num_threads=0;

    QString error;
};

#endif // FFMPEGENCODER_H
