#include "FFmpegEncoder.h"
#include "Settings.h"
#include "Util.h"
#include "Frame.h"


// AVCODEC STUFF
#include <math.h>
extern "C" {
#include "libavutil/opt.h"
#include "libavcodec/avcodec.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/imgutils.h"
#include "libavutil/mathematics.h"
#include "libavutil/samplefmt.h"
#include "libswscale/swscale.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libavdevice/avdevice.h"
#include "libavfilter/avfilter.h"
}
// /AVCODEC STUFF

#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QReadLocker>
#include <QWriteLocker>
#include <atomic>
#include <deque>
#include <list>
#include <QThreadPool>

#ifdef __GNUC__
#pragma GCC diagnostic push
//#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wpadded" // TODO: find out why new clang issues these annoying useless warnings...!
#pragma GCC diagnostic ignored "-Wold-style-cast" /* FFmpeg macros use C-style casts, so we want to ignore these warnings. */
#endif

namespace {

    struct Converter;

    AVPixelFormat pixelFormatForCodecId(AVCodecID codec);
    AVPixelFormat qimgfmt2avcodecfmt(QImage::Format fmt);
    AVCodecID fmt2CodecId(int fmtFromSettingsClass);

    struct Q
    {
        QString name = "Q";

        std::deque<Frame> frames; ///< buffered video frames.  this list never exceeds maxImgs in size. guarded by mut below.

        static const int maxFrames = qMax(3,int(Frame::DefaultFPS())); ///< max number of video frames to buffer: 1 second worth of frames or 3 minimum.

        mutable QMutex mut; ///< to synchronize access to frames member above
        QSemaphore semReadyForEncode; ///< signal video frames are proccessed by converters and ready for encode. typically sem.available() should be 1 or 0, but may reach maxFrames.

        Q() {}

        ~Q() {
            if (const auto ctv = frames.size()) {
                Warning("%s: ~Q still had %d frames in Q (all were safely released)", name.toUtf8().constData(), int(ctv));
            } else {
                Debug("%s: ~Q deleted (and was empty).", name.toUtf8().constData());
            }
        }

        // returns false if queue was full, in which case the frame wasn't added
        bool enqueue(const Frame & frame, QString *err = nullptr) {
            if (err) *err = "";
            QMutexLocker l(&mut);
            if (frames.size() >= maxFrames) {
                if (err) *err = QString("FFmpegEncoder::enqueue -- queue full, dropping frame %1").arg(frame.num);
                return false;
            }
            frames.push_back(frame);
            return true;
        }

        int size() const { QMutexLocker l(&mut); return int(frames.size()); }

        // unconditionally put back a frame because FFmpeg gave us EGAIN when we tried to process it.
        // Called from FFmpegEncoder::doEncode() (Encoder thread). Will release 1 semaphore resource.
        void putBackFrame(Frame &&frame) {
            QMutexLocker l(&mut);
            frames.emplace_front(std::move(frame));
            semReadyForEncode.release(1);
        }

        // Called from Conversion thread(s)
        Frame *findFirstNeedsAVFrame() {
            QMutexLocker l(&mut);
            for (auto it = frames.begin(); it != frames.end(); ++it) {
                if (!it->avframe && it->flag == 0) {
                    it->flag = 1; // mark it as "being processed"
                    return &*it;
                }
            }
            return nullptr;
        }

        // Called from Conversion thread(s) when a frame's conversion is complete. Will release 1 semaphore resource.
        void markFrameReadyForEncode(Frame *f) {
            if (f && f->avframe && f->flag == 1) {
                f->flag = 2;
                semReadyForEncode.release(1);
            }
        }

        // Called from Encoder thread to query for any frames available to encode. Will return a null frame if none available.
        Frame getFirstIfReadyForEncode() {
            Frame ret;
            QMutexLocker l(&mut);
            if (!frames.empty() && frames.front().avframe && frames.front().flag == 2) {
                ret = std::move(frames.front());
                frames.pop_front();
            }
            return ret;
        }

    };

    /// A simple converter to convert from QImage -> AVFrame.
    /// It can handle converting between pixel formats, but resizing/scaling is not implemented.
    struct Converter {
        bool isOk = false;
        int w=0, h=0; ///< width,height of the incoming image
        AVPixelFormat av_pix_fmt_in; ///< the format of the incoming QImages.. usually RGB0
        AVPixelFormat av_pix_fmt_out;
        SwsContext *ctx = nullptr;

        /// on success, returns a newly allocated frame which must be freed with av_frame_free(&frame).
        /// on error, returns nullptr and sets errMsg
        AVFrame *convert(const QImage &, QString &errMsg);

        /// Returns a newly allocated frame like convert above. There is no "conversion" done and it simply copies
        /// the pixel data from QImage into a referenced AVFrame, and returns it.
        static AVFrame *trivial(const QImage &, AVPixelFormat fmt, QString& errMsg);

        ~Converter();
        Converter(int w, int h, AVPixelFormat src_fmt, AVPixelFormat dest_fmt);
    private:
        /// Just allocates a new AVFrame for the data in img. Only call this if fmt_in == fmt_out
        AVFrame *trivial(const QImage &, QString &errMsg);

        /// this is a work-alike to AVPicture. AVPicture itself was deprecated. Used internally.
        struct Picture {
            uint8_t *data[AV_NUM_DATA_POINTERS] = {nullptr};    ///< pointers to the image data planes
            int linesize[AV_NUM_DATA_POINTERS]  = {0};          ///< number of bytes per line
        };
    };

    Converter::~Converter()
    {
        if (ctx) { sws_freeContext(ctx); ctx = nullptr; Debug("Deleted a non-trivial converter"); }
    }

    Converter::Converter(int width, int height, AVPixelFormat pxfmt_in, AVPixelFormat pxfmt_out)
    {
        w = width; h = height;
        av_pix_fmt_in = pxfmt_in;
        av_pix_fmt_out = pxfmt_out;
        isOk = w > 0 && h > 0 && av_pix_fmt_in >= 0 && av_pix_fmt_out >= 0;
        if (!isOk) {
            Error("FFmpegEncoder bad args!");
            return;
        }
        if (av_pix_fmt_in != av_pix_fmt_out) {
            Debug() << "Img format != Codec format; using a converter";

            //create the conversion context.  you only need to do this once if
            //you are going to do the same conversion multiple times.
            ctx = sws_getContext(w,
                                 h,
                                 av_pix_fmt_in,
                                 w,
                                 h,
                                 av_pix_fmt_out,
                                 SWS_FAST_BILINEAR,
                                 nullptr, nullptr, nullptr);

            if (QString(av_get_pix_fmt_name(av_pix_fmt_out)).toLower().contains("yuvj")
                    && ! QString(av_get_pix_fmt_name(av_pix_fmt_in)).toLower().contains("yuvj") ) {
                // Make sure to maintain the same colorspace from input -> output for RGB -> YUVJ
                // Not doing the below caused brightness glitches on MJPEG videos being too dark.
                // (I think it's because Sws was using JPEG mode for YUVJ when really we are recording a movie).
                // So.. the below voodoo fixes that.
                int *table, *inv_table,srcRange,dstRange,brightness,contrast,saturation;
                sws_getColorspaceDetails(ctx,&inv_table,&srcRange,&table,&dstRange,&brightness,&contrast,&saturation);
                if (srcRange != dstRange) {
                    constexpr int override = 0;
                    Debug("yuvj output: colorspace voodoo applied to sws context: src: %d->%d dest: %d->%d (bri: %d cont: %d sat:%d left untouched)",srcRange,override,dstRange,override,brightness,contrast,saturation);
                    sws_setColorspaceDetails(ctx, inv_table, override, table, override, brightness, contrast, saturation);
                }
            }
        }
    }

    /* static */
    AVFrame *
    Converter::trivial(const QImage &img, AVPixelFormat fmt, QString & errMsg)
    {
        if (img.isNull()) { errMsg = "Null image passed to converter"; return nullptr; }

        AVFrame *frame = av_frame_alloc(); // allocate frame struct, initializing to default values
        try {
            if (!frame) throw QString("Could not allocate AVFrame");
            frame->format = fmt;
            frame->width = img.width();
            frame->height = img.height();
            if (av_frame_get_buffer(frame, 0))
                throw QString("Could not allocate AVFrame buffer");
            Picture inpic;
            if (av_image_fill_arrays(inpic.data, inpic.linesize, img.bits(), fmt, img.width(), img.height(), 32/*QImages use align=32*/) < 0)
                throw QString("Could not fill arrays");
            av_image_copy(frame->data, frame->linesize, const_cast<const uint8_t **>(inpic.data), inpic.linesize, fmt, img.width(), img.height());
        } catch (const QString & e) {
            errMsg = e;
            av_frame_free(&frame);
        }
        return frame;
    }
    AVFrame *
    Converter::trivial(const QImage &img, QString & errMsg)
    {
        if (av_pix_fmt_in != av_pix_fmt_out) {
            errMsg = "Do not call trivial() unless fmt_in == fmt_out!";
            return nullptr;
        }
        return trivial(img, av_pix_fmt_out, errMsg);
    }

    AVFrame *
    Converter::convert(const QImage &img, QString & errMsg)
    {
        if (!isOk || img.isNull()) {
            errMsg = "Bad arguments given to FFmpegEncoder::Converter!";
            return nullptr;
        }
        if (av_pix_fmt_in == av_pix_fmt_out)
            return trivial(img, errMsg);
        if (!ctx) {
            errMsg = "Could not allocate a SwsContext!";
            return nullptr;
        }
        if (img.width() != w || img.height() != h) {
            errMsg = "img.width or img.height changed!";
            return nullptr;
        }

        AVFrame *frame = av_frame_alloc(); // allocate frame struct

        try {
            if (!frame)
                throw QString("av_frame_alloc returned NULL!");
            // req struct fields need to be set for av_frame_get_buffer to work ok
            frame->width = w;
            frame->height = h;
            frame->format = av_pix_fmt_out;
            // allocate buffers and reference frame. (av_frame_free also unreferences frame->buf before deleting frame struct)
            if (int res = av_frame_get_buffer(frame, 0 /* <-- docs say to pass 0 *//*32*/);
                    res != 0) {
                throw QString("av_frame_get_buffer returned %1").arg(res);
            }

            Picture inpic;

            // NB: this does a shallow copy -- just fills pointers to image planes...
            if (int size = av_image_fill_arrays(inpic.data,
                                                inpic.linesize,
                                                img.bits(),
                                                av_pix_fmt_in, w, h, 32 /* align=32 for QImages, from Qt docs*/);
                    size < 0) {
                throw QString("av_image_fill_arrays returned %1").arg(size);
            } else if (size > img.bytesPerLine()*h) {
                throw QString("av_image_fill_arrays size is greater than img size in bytes!");
            }

            //perform the conversion
            if (int res =
                    sws_scale(ctx,
                              inpic.data,
                              inpic.linesize,
                              0,
                              h,
                              frame->data,
                              frame->linesize);
                    res < 0) {
                throw QString("sws_scale returned %1").arg(res);
            }
            errMsg = "";
        } catch (const QString &s) {
            errMsg = s;
            av_frame_free(&frame); // implicitly sets frame to nullptr. calling av_frame_free with NULL frame is ok.
        }
        return frame;
    }

    class ConverterMgr {
        std::list<Converter *> convs;
        QMutex mut;
    public:
        Converter *take(int w, int h, int pix_fmt_in, int pix_fmt_out);
        void put(Converter *&); ///< writes nullptr to passed-in arg after it's done putting the converter back in the list.
        ~ConverterMgr();
    };

    Converter *ConverterMgr::take(int w, int h, int pxin, int pxout) {
        {
            QMutexLocker l(&mut);
            for (auto it = convs.begin(); it != convs.end(); ++it) {
                if (auto conv = *it; conv->w == w && conv->h == h && conv->av_pix_fmt_in == pxin && conv->av_pix_fmt_out == pxout) {
                    convs.erase(it);
                    return conv;
                }
            }
        }
        return new Converter(w, h, AVPixelFormat(pxin), AVPixelFormat(pxout));
    }
    void ConverterMgr::put(Converter *&conv) {
        QMutexLocker l(&mut);
        convs.push_front(conv);
        conv = nullptr;
    }
    ConverterMgr::~ConverterMgr() { for (auto conv : convs) delete conv; }

} // end anonymous namespace

struct FFmpegEncoder::Priv {
    AVCodec *codec = nullptr;
    AVCodecContext *c = nullptr;
    AVFormatContext *oc = nullptr;
    AVStream *video_st = nullptr;
    AVPacket pkt;
    std::atomic_uint framesProcessed = 0U; ///< used to determine if we need to flush encoder
    AVPixelFormat codec_pix_fmt = AV_PIX_FMT_NONE;

    qint64 firstFrameNum = -1; ///< used to calculate frame->pts

    Q *queue = nullptr;

    bool wroteHeader = false;

    QThreadPool poolConv, poolEnc;
    ConverterMgr converters;
    std::atomic_bool stopEncFlag = false;

    Priv();
    ~Priv();
};

FFmpegEncoder::FFmpegEncoder(const QString &fn, double fps, qint64 br, int fmt, unsigned n_thr)
    : outFile(fn), fps(fps), bitrate(br), fmt(fmt), num_threads(int(n_thr))
{
    p = new Priv;
    p->queue = new Q; p->queue->name = "Frame Q";
    p->poolConv.setMaxThreadCount(num_threads);
    p->poolEnc.setMaxThreadCount(1);

    Util::renameAllPoolThreads(p->poolConv, "Conversion");
    Util::renameAllPoolThreads(p->poolEnc, "Encoding");
}

FFmpegEncoder::~FFmpegEncoder()
{
    disconnect(); // we don't want threads still running to continue to emit signals as we are destructing.

    p->poolConv.waitForDone(); // allow conversion threads to finish
    p->queue->semReadyForEncode.release(p->queue->size()); // make sure encoder thread runs until it drains all frames
    p->stopEncFlag = true; // stop flag indicates encoder thread should exit after it drains queue.
    p->poolEnc.waitForDone(); // allow encoder thread to finish

    QString error;
    if (!flushEncoder(&error)) {
        Warning("Encoder flush returned error: %s",error.toUtf8().constData());
    }

    if (p->queue) { delete p->queue; p->queue = nullptr; }
    delete p; p = nullptr; // should write trailer for us...
}

FFmpegEncoder::Priv::Priv()
{
    memset(&pkt, 0, sizeof(pkt));
    av_init_packet(&pkt);
}

FFmpegEncoder::Priv::~Priv()
{
    if (video_st)
        // video_st should be freed by avformat_free_context call below...
        video_st = nullptr;
    if (oc) {
        if (oc->pb) {
            av_write_trailer(oc); // needed to properly close file...
            avio_flush(oc->pb);
            avio_closep(&oc->pb);
        }
        avformat_free_context(oc); oc = nullptr;
    }
    if (c) { avcodec_close(c); av_free(c); c = nullptr; }
//    Debug("Priv deleted.");
}

bool FFmpegEncoder::enqueue(const Frame &frame, QString *errMsg)
{
    bool ret = p->queue->enqueue(frame, errMsg);
    if (!ret) emit frameDropped(frame.num);
    doConversionLater();
    if (!p->framesProcessed)
        doEncodeLater(); // on first run fire up the encode thread
    return ret;
}

void FFmpegEncoder::doConversionLater()
{
    if (!LambdaRunnable::tryStart(p->poolConv, [this]{ doConversion(); })) {
        // pool busy..
        //Debug() << "FFmpegEncoder queue->enqueue -- pool busy";
    }
}
void FFmpegEncoder::doEncodeLater()
{
    if (!LambdaRunnable::tryStart(p->poolEnc, [this]{ doEncode(); })) {
        // pool busy..
        //Debug() << "FFmpegEncoder ready frame->enqueue -- pool busy";
    }
}

void FFmpegEncoder::doConversion()
{
    Frame *frame = p->queue->findFirstNeedsAVFrame();
    if (frame) {
        const QImage & img(frame->img);
        const AVPixelFormat img_pix_fmt = qimgfmt2avcodecfmt(img.format());
        const AVPixelFormat codec_pix_fmt = pixelFormatForCodecId(fmt2CodecId(fmt));
        if (!frame->avframe && int(frame->flag)==1) {
            auto t0 = Util::getTime();
            Converter *conv = p->converters.take(img.width(), img.height(), img_pix_fmt, codec_pix_fmt);
            QString err;
            frame->avframe = conv->convert(img, err);
            p->converters.put(conv);
            if (!frame->avframe)
                emit error(err);
            p->queue->markFrameReadyForEncode(frame); // mark it as "processed"
            Debug() << "convert " << frame->num << " took: " << (Util::getTime()-t0) << " ms";
            doConversionLater(); // re-enqueue another conversion thread when we are done. may be noop if all threads are busy.
        } else {
            Warning() << "POSSIBLE RACE CONDITION: Two converter threads got given the same frame!";
        }
    } else {
        //Debug() << "doConversion -- got a null frame";
    }
}

void FFmpegEncoder::doEncode()
{
    int iterct_outer = 0;
    while (!p->stopEncFlag) {
        int iterct = 0;
        while (p->queue->semReadyForEncode.tryAcquire(1, 100)) {
            QString err;
            Frame frame = p->queue->getFirstIfReadyForEncode();
            if (!frame.isNull()) {
                //    if (res) qDebug("dequeue got img frame %llu (%d x %d)", frame.num, frame.img.width(), frame.img.height());
                if (const int res = encode(frame, &err); res == 0) {
                    // got EAGAIN from avcodec
                    Debug() << "Got EAGAIN from avcodec_send_frame, re-enqueing frame...";
                    p->queue->putBackFrame(std::move(frame));
                } else if (res < 0) {
                    emit error(err);
                } else if (res > 0) {
                    emit wroteFrame(frame.num);
                }
                // note Frame may be invalidated after this line because of putBackFrame() call above.
            } else {
                //Debug("Frame was null...");
            }
            ++iterct;
        }
        if (!iterct) {
            //Debug("Spurious wakeup...");
        }
        ++iterct_outer;
    }
    Debug("doEncode exiting after %d semaphore timeouts and %u frames processed", iterct_outer, unsigned(p->framesProcessed));
}

quint64 FFmpegEncoder::bytesWritten() const
{
    if (p && p->c && p->oc && p->oc->pb) {
        qint64 r = avio_size(p->oc->pb);
        if (r >= 0) return quint64(r);
    }
    return 0ULL;
}

bool FFmpegEncoder::setupP(int width, int height, int av_pix_fmt, QString *err_out)
{
    bool retVal = true;
    QString dummy, &error(err_out ? *err_out : dummy);
    error = "";

    do {
        if (!p) {
            error = "INTERNAL ERROR: P ptr is NULL!";
            retVal = false;
            break;
        }

        AVCodecID codec_id = fmt2CodecId(fmt);
        if (codec_id == AV_CODEC_ID_NONE) {
            error = "Invalid format to FFPmegEncoder class";
            retVal = false;
            break;
        }
        p->codec = avcodec_find_encoder(codec_id);

        if (!p->codec) {
            error = QString("Error #1: NULL codec id for ") + QString::number(int(codec_id));
            retVal = false;
            break;
        }

        p->codec_pix_fmt = AVPixelFormat(av_pix_fmt);

        p->c = avcodec_alloc_context3(p->codec);
        if (!p->c) {
            error = "Error #2: Could not allocate context!";
            retVal = false;
            break;
        }

        if (avformat_alloc_output_context2(&p->oc, nullptr, nullptr, outFile.toUtf8().constData()) < 0 || !p->oc) {
            error = QString("Error #3: Failed output context for file: ") + outFile;
            retVal = false;
            break;
        }

        p->c->bit_rate = bitrate;
        p->c->width = width; p->c->height = height;
        AVRational rat; rat.num = 1000; rat.den = int(fps*1000.0);
        if (rat.den <= 0) rat.den = 1;
        if (rat.num == rat.den) rat.num = rat.den = 1;
        p->c->time_base = rat;

        // the below need tuning and/or possibly coming from ui params
        //p->c->gop_size = fps < 5.0 ? 0 : static_cast<int>(ceil(fps/3.0)); // tune this? todo: from UI?
        //p->c->max_b_frames = fps < 5.0 ? 0 : qRound(fps/5.0);
        p->c->gop_size = 1; //fps > 10.0 ? 10 : int(ceil(fps));
        p->c->max_b_frames = codec_id == AV_CODEC_ID_MPEG2VIDEO && fps >= 5.0 ? 2 : 1;
 /*       if (codec_id == AV_CODEC_ID_MJPEG || codec_id == AV_CODEC_ID_APNG || codec_id == AV_CODEC_ID_GIF
                || codec_id == AV_CODEC_ID_JPEG2000 || codec_id == AV_CODEC_ID_LJPEG
                || codec_id == AV_CODEC_ID_FFV1)
            p->c->max_b_frames = 0, p->c->gop_size = 1;
*/

        // TESTING DEBUG FIXME COME FROM UI?!
        switch(codec_id) {
        case AV_CODEC_ID_FFV1:
            //p->c->compression_level = 0;
            p->c->max_b_frames = 0;
            p->c->gop_size = 1;
            p->c->thread_count = num_threads;
            p->c->thread_type = FF_THREAD_SLICE;
            // uncomment the below to suppress warnings from ffv1 -- at the expense of losing >8 bits per sample codecs!
            // for now we leave this commented-out as it appears to generate better, more compatible files to not have the below enabled
            // but, if the warnings annoy you -- you can suppress them with this.
            //p->c->level = 4; p->c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; // request version 4 FFV1 Codec... this means it won't work by default in VLC but works with ffplay :/

            // NB: slices= seems to break movie files (frozen frames) for long periods??
            //p->c->slices=9;
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
        case AV_CODEC_ID_MPEG4:
            p->c->thread_count = num_threads;
            p->c->thread_type = FF_THREAD_SLICE;
            break;
        case AV_CODEC_ID_MJPEG:
            p->c->max_b_frames = 0;
            p->c->gop_size = 1;
            //p->c->thread_count = 1; // <-- orig viking
            //p->c->thread_type = FF_THREAD_FRAME;
            p->c->thread_count = num_threads;
            p->c->thread_type = FF_THREAD_SLICE;
            break;
        case AV_CODEC_ID_LJPEG:
            p->c->max_b_frames = 0;
            p->c->gop_size = 1;
            p->c->thread_type = FF_THREAD_FRAME;
            p->c->thread_count = num_threads; // /* orig viking-->*/ 1;//num_threads > 2 ? 2 : num_threads;
            break;
        case AV_CODEC_ID_GIF:
        case AV_CODEC_ID_APNG:
            p->c->max_b_frames = 0;
            p->c->gop_size = 1;
        //    p->c->compression_level = 0;
            break;
        default:
            (void)0; // nothing?
        }
        // rest are auto or none?


       // if ( (codec_id == AV_CODEC_ID_LJPEG
       //       || codec_id == AV_CODEC_ID_MJPEG) && fps >= 5.0) p->c->gop_size = 3;

        if (codec_id == AV_CODEC_ID_JPEG2000)
            //p->c->prediction_method = 1; // set to lossless?
            av_opt_set_int(p->c->priv_data, "pred", 1, 0); // settings pred=1 sets it to lossless

        p->c->pix_fmt = p->codec_pix_fmt;

        if (codec_id == AV_CODEC_ID_H264) {
            // todo: have this come from the UI
            av_opt_set(p->c->priv_data, "preset", "ultrafast", AV_OPT_SEARCH_CHILDREN);
            av_opt_set(p->c->priv_data, "tune", "zerolatency", AV_OPT_SEARCH_CHILDREN);
        }


        if (codec_id == AV_CODEC_ID_MPEG1VIDEO){
             /* Needed to avoid using macroblocks in which some coeffs overflow.
                This does not happen with normal video, it just happens here as
                the motion of the chroma plane does not match the luma plane. */
             p->c->mb_decision=2;
         }

         // some formats want stream headers to be separate
         if(p->oc->oformat->flags & AVFMT_GLOBALHEADER)
             p->c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;


        if (avcodec_open2(p->c, p->codec, nullptr) < 0) {
            error = "Error #4: Could not open codec";
            retVal = false;
            break;
        }

        p->video_st = avformat_new_stream(p->oc, p->codec);
        if (!p->video_st) {
            error = "Error #5: Could not open video output stream";
            retVal = false;
            break;
        }
        {
            // setup some vars
            //AVCodecContext *cd = p->video_st->codec;
            //cd->bit_rate = p->c->bit_rate;
            //cd->width = p->c->width;
            //cd->height = p->c->height;
            //cd->time_base = p->c->time_base;
            //cd->gop_size = p->c->gop_size;
            //cd->max_b_frames = p->c->max_b_frames;
            //cd->mb_decision = p->c->mb_decision;
            //cd->pix_fmt = p->c->pix_fmt;
            //cd->flags = p->c->flags;
            //if (codec_id == AV_CODEC_ID_H264)
                // todo: have this come from the UI
            //    av_opt_set(cd->priv_data, "preset", "slow", 0);
            /*AVCodecParameters *cp = p->video_st->codecpar;
            cp->width = p->c->width;
            cp->height = p->c->height;
            cp->bit_rate = p->c->bit_rate;
            cp->codec_id = p->c->codec_id;
            cp->codec_type = p->c->codec_type;
            cp->codec_tag = p->c->codec_tag;
            cp->format = p->c->pix_fmt;
            AVRational r; r.num = p->c->width; r.den = p->c->height;
            cp->sample_aspect_ratio = r;
            p->video_st->time_base = p->c->time_base;
            cd->time_base = p->c->time_base;
            */
            p->video_st->time_base = p->c->time_base;
            avcodec_parameters_from_context(p->video_st->codecpar,p->c);
            //cd->time_base = p->c->time_base;
            //todo: avformat_write_header
        }

        if (avio_open(&p->oc->pb, outFile.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
               error = "Error #7: Open failed on file " + outFile;
               retVal = false;
               break;
        }
        if (avformat_write_header(p->oc, nullptr) < 0) {
            error = "Error #8: Could not write header";
            if (p->oc->pb && p->oc->pb->error) error += QString(": ") + strerror(abs(p->oc->pb->error));
            retVal = false;
            break;
        }

        p->wroteHeader = true;

    } while(0);

    return retVal;
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{

    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;


    int ret = av_interleaved_write_frame(fmt_ctx, pkt);
    av_packet_unref(pkt);
    return ret;
}

int FFmpegEncoder::write_video_frame(AVPacket *pkt)
{
    const quint64 b0 = bytesWritten();
    const int ret = ::write_frame(p->oc, &p->c->time_base, p->video_st, pkt);
    if (0==ret) emit wroteBytes(qint64(bytesWritten()-b0));
    return ret;
}

bool FFmpegEncoder::wroteHeader() const { return p->wroteHeader; }

bool FFmpegEncoder::flushEncoder(QString *errMsg)
{
    if (p && p->framesProcessed && p->c && p->oc && p->video_st && p->oc->pb && !p->oc->pb->error) {
        if (p->c->codec->capabilities & AV_CODEC_CAP_DELAY) {
            Debug("Flushing video frames from codec buffers...");
            // delayed frames.. get and save
            int iter_ctr = 0, res = 0;

            // indicate end by sending null frame
            res = avcodec_send_frame(p->c, nullptr);

            if (res == AVERROR(EAGAIN)) res = 0;
            else if (res < 0) {
                if (errMsg) *errMsg = "Error flushing stream";
                return false;
            }

            // now loop until no more packets are read
            while (0 == res)
            {
                // this normally returns 0 until end of stream, then it returns AFERROR_EOF
                res = avcodec_receive_packet(p->c, &p->pkt);

                if (0 == res)
                    // if we got a packet, write it to file
                    res = write_video_frame(&p->pkt); // this automatically unreferences and clears the packet

                if (res && res != AVERROR_EOF) {
                    // res != 0 and res != AVERROR_EOF means we got some error above...
                    if (errMsg) {
                        *errMsg = "Error from inner write_frame()";
                        if (p->oc && p->oc->pb && p->oc->pb->error)
                            *errMsg += QString(": ") + strerror(abs(p->oc->pb->error));
                    }
                    return false;
                }
                ++iter_ctr;
            }
            Debug("Did %d extra video 'receive_packet' iterations...",iter_ctr);
        }
    }
    return true;
}

int FFmpegEncoder::encode(Frame & frame, QString *errMsg)
{
    const QImage & img(frame.img);
    qint64 t0 = Util::getTime();

    if (!p || !p->codec || !p->c || !p->oc || !p->oc->pb) {
        if (!setupP(img.width(), img.height(), pixelFormatForCodecId(fmt2CodecId(fmt)), errMsg)) {
            return -1;
        }
    }

    AVFrame *outFrame = frame.avframe;

    int retVal = 1;

    try {
        if (p->c->width != img.width())
            throw QString("Unexpected image size change: Did you resize the screen?");
        if (!outFrame)
            throw QString("In-line conversion in Encoder thread no longer supported. FIXME!");

        if (p->firstFrameNum < 0LL) p->firstFrameNum = qint64(frame.num); // remember "first frame" number seen for proper pts below...
        const qint64 fnum = qint64(frame.num) - p->firstFrameNum; // this is really an offset from start of recording

        outFrame->pts = fnum;

        int res = avcodec_send_frame(p->c, outFrame); // will ref this frame's buf (shallow copy it)

        if (AVERROR(EAGAIN) == res) {
            // new API: avcodec_send_frame() may return EAGAIN when its buffers are full.
            // indicate this to calling code to re-enqueue the frame later. Then proceed to read packets
            // off codec and save them to disk.
            retVal = 0;
        } else if (res < 0) {
            // new API same as old here -- negative return that is NOT EAGAIN means error.
            throw QString("Error encoding frame");
        } else
            p->framesProcessed++;

        while ((res = avcodec_receive_packet(p->c, &p->pkt)) == 0) { // keep looping until we get -EAGAIN or some error
            if (write_video_frame(&p->pkt)) { // this automatically unreferences and inits the packet
                QString error = "Error #11: Could not write frame";
                if (p->oc && p->oc->pb && p->oc->pb->error)
                    error += QString(": ") + strerror(qAbs(p->oc->pb->error));
                throw error;
            }
        }
        if (res != AVERROR(EAGAIN) && res != 0) { // check while() condition above didn't get an error
           QString error = "Error #12: avcodec_write_packet returned an error";
           if (p->oc && p->oc->pb && p->oc->pb->error)
               error += QString(": ") + strerror(qAbs(p->oc->pb->error));
           throw error;
        }
    } catch (const QString & e) {
        retVal = -1;
        if (errMsg) *errMsg = e;
    }

    Debug() << "encode " << frame.num << " took: " << (Util::getTime()-t0) << " ms";
    return retVal;
}


namespace { // Anonymous

    AVCodecID fmt2CodecId(int settings_fmt)
    {
        switch(settings_fmt) {
        case Settings::Fmt_Mpeg4: return AV_CODEC_ID_MPEG4;
        case Settings::Fmt_Mpeg2: return AV_CODEC_ID_MPEG2VIDEO;
        case Settings::Fmt_H264:  return AV_CODEC_ID_H264;
        case Settings::Fmt_FFV1:  return AV_CODEC_ID_FFV1;
        case Settings::Fmt_LJPEG: return AV_CODEC_ID_LJPEG;
        case Settings::Fmt_APNG:  return AV_CODEC_ID_APNG; ///< APNG, animated PNG
        case Settings::Fmt_MJPEG: return AV_CODEC_ID_MJPEG; ///< MJPEG, motion jpeg
        case Settings::Fmt_GIF:   return AV_CODEC_ID_GIF; ///< animated GIF
            // add new supported formats here
        }
        return AV_CODEC_ID_NONE;
    }

    AVPixelFormat pixelFormatForCodecId(AVCodecID codec)
    {
        switch (int(codec)) {
        case AV_CODEC_ID_GIF:
            return AV_PIX_FMT_RGB8;
        case AV_CODEC_ID_APNG:
            return AV_PIX_FMT_RGB24;
            //case AV_CODEC_ID_X:
            //    return AV_PIX_FMT_BGR24;
        /*case AV_CODEC_ID_FFV1:
        case AV_CODEC_ID_LJPEG: // accepts bgr0
            return AV_PIX_FMT_BGR0;*/
            //return AV_PIX_FMT_BGRA;
        case AV_CODEC_ID_FFV1: // <-- seems to be faster when using this pix fmt.
            return AV_PIX_FMT_YUV420P;
        case AV_CODEC_ID_LJPEG: // <--- MUCH faster when using yuvj420p
        case AV_CODEC_ID_MJPEG:
            return AV_PIX_FMT_YUVJ420P;
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_MPEG2VIDEO:
        case AV_CODEC_ID_MPEG4:
        default:
            (void)0; // fall thru...
        }
        return AV_PIX_FMT_YUV420P;
    }


    AVPixelFormat qimgfmt2avcodecfmt(QImage::Format fmt) {
        switch (fmt) {
        case QImage::Format_Mono: return AV_PIX_FMT_MONOWHITE;
        case QImage::Format_RGBA8888_Premultiplied:
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBX8888:
        case QImage::Format_RGB32: return AV_PIX_FMT_BGR0;
        case QImage::Format_ARGB32: return AV_PIX_FMT_BGRA;
        case QImage::Format_ARGB32_Premultiplied: return AV_PIX_FMT_BGR0;
        case QImage::Format_RGB16: return AV_PIX_FMT_BGR565LE;
        case QImage::Format_RGB555: return AV_PIX_FMT_BGR555LE;
        case QImage::Format_RGB888: return AV_PIX_FMT_BGR24;
        case QImage::Format_RGB444: return AV_PIX_FMT_RGB444LE;//AV_PIX_FMT_BGR444LE;
        case QImage::Format_Grayscale8: return AV_PIX_FMT_GRAY8;

            // unsupported formats by avcodec
        case QImage::Format_Alpha8:
        case QImage::Format_BGR30:
        case QImage::Format_A2BGR30_Premultiplied:
        case QImage::Format_RGB30:
        case QImage::Format_A2RGB30_Premultiplied:
        case QImage::Format_ARGB4444_Premultiplied:
        case QImage::Format_ARGB8565_Premultiplied:
        case QImage::Format_RGB666:
        case QImage::Format_ARGB6666_Premultiplied:
        case QImage::Format_ARGB8555_Premultiplied:
        case QImage::Format_MonoLSB:   // unsupported by avcodec
        case QImage::Format_Indexed8:  // unsupported by avcodec
        default:
            (void)0; /// fall thru
        }
        return AV_PIX_FMT_NONE;
    }

} // end anonymous namespace

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
