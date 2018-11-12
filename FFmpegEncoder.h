#ifndef FFMPEGENCODER_H
#define FFMPEGENCODER_H

#include <QString>
#include <QObject>

struct Frame;
struct AVPacket;

/// A parallelizing Frame encoder for writing Video frames.  Supports various formats. Is pretty fast and nimble.
/// Note that the input Frame pixel data may be in any format FFmpeg groks, and conversion is one in Converter threads
/// in parallel with Encoding.  Encoding is done in only 1 thread (however FFmpeg itself uses multiple threads when
/// encoding behind the scenes for most codecs).
class FFmpegEncoder : public QObject
{
    Q_OBJECT
public:
    FFmpegEncoder(const QString & outFile, double fps, qint64 bitrate, int fmt, unsigned numFFmpegEncodingThreads);
    ~FFmpegEncoder() override; ///< stop encoding session if running and gracefully close output movie file. May take a while to complete (on the order of milliseconds to seconds).

    /// Call this from your data grabbing thread (or main thread) to enqueue a video frame.
    /// Will kick off the rest of the pipeline in other threads behind the scenes.
    /// (Deleting this instance stops the encoding and writes trailers to the file).
    bool enqueue(const Frame &, QString *errMsg = nullptr);

    bool wroteHeader() const; ///< Returns true iff the header has been written to the output file (it's a sign things are going well!).

signals:
    // Note: The below signals are auto-disconnected right before the cleanup/file trailer code runs in the d'tor
    // However they may still be received in a Queued connection after this instance has died.
    // Guard against this situation in slots!

    void error(QString); ///< A low level critical error occurred during encoding. Client code is advised to stop the encoding session if this ever fires.
    void frameDropped(quint64 fnum); ///< Inform interested code that a frame was dropped due to full queues (encoding couldn't keep up with incoming frames).
    void wroteFrame(quint64 fnum); ///< Inform interested code that an AVFrame was successfully submitted to the avcodec API's queues. If it hasn't been already, the frame will soon be written to disk.
    void wroteBytes(qint64 bytes); ///< Emitted after every AVPacket write to indicate the number of bytes written.

private:
    struct Priv;

    Priv *p = nullptr;

    bool setupP(int w, int h, int av_pix_fmt, QString *err = nullptr); ///< Critical error if false is returned. Called by Encoder thread.
    int encode(Frame &, QString *errMsg = nullptr); ///< called by Encoder thread only
    bool flushEncoder(QString *errMsg = nullptr); ///< called from d'tor to clean up avcodec's internal queue
    int write_video_frame(AVPacket *pkt); ///< called by Encoder thread
    quint64 bytesWritten() const; ///< call this from Encoder thread only.


    QString outFile;
    double fps = 0.0;
    qint64 bitrate=0;
    int fmt=0,num_threads=0;

    void doConversion(); ///< Short-lived Conversion thread function -- these run in parallel and populate Frame.avframe. Runs once per Frame (but in parallel for all frames in queue).
    void doConversionLater(); ///< Tell Conversion thread pool to fire up a thread if it has any idle threads waiting.
    void doEncode(); ///< Encoder Thread's function.  Runs indefinitely until instance destruction. Only one of these is ever extant at once.
    void doEncodeLater(); ///< This is called to fire up the initial Encoder thread and is a noop once the Encoder thread is running.
};

#endif // FFMPEGENCODER_H
