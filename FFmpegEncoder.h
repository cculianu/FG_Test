#ifndef FFMPEGENCODER_H
#define FFMPEGENCODER_H

#include <QString>
#include <QObject>

struct Frame;

class FFmpegEncoder : public QObject
{
    Q_OBJECT
public:
    FFmpegEncoder(const QString & outFile, double fps, qint64 bitrate, int fmt, unsigned numFFmpegEncodingThreads);
    ~FFmpegEncoder() override;

    /// call this from data grabbing thread or main thread to enqueue a video frame.
    /// will kick of the rest of the pipeline in other threads behind the scenes.
    bool enqueue(const Frame &, QString *errMsg = nullptr);

    quint64 bytesWritten() const;

    bool wroteHeader() const;

signals:
    void error(QString);
    void frameDropped(quint64 fnum);
    void wroteFrame(quint64 fnum);
    void wroteBytes(qint64 bytes);

private:
    struct Priv;

    Priv *p = nullptr;

    bool setupP(int w, int h, int av_pix_fmt, QString *err = nullptr); //< sets error if false
    int encode(const Frame &, QString *errMsg = nullptr);
    bool flushEncoder(QString *errMsg = nullptr);
    /// called from cleanup code to process frames still in queue before destruction
    /// returns the frame number if processed a frame, 0 if timed out, or negative value on error, in which case
    /// *errMsg will contain an error string.
    /// (encode_time_ms is the time in milliseconds it took to encode the frame.)
    qint64 processOneVideoFrame(int timeout_ms, QString *errMsg = nullptr, int *encode_time_ms = nullptr);


    QString outFile;
    double fps = 0.0;
    qint64 bitrate=0;
    int fmt=0,num_threads=0;

    void doConversion();
    void doConversionLater();
    void doEncode();
    void doEncodeLater();
};

#endif // FFMPEGENCODER_H
