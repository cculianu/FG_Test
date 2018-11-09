#include "FFmpegEncoder.h"
#include "Settings.h"
#include "Util.h"
#include "Frame.h"


// AVCODEC STUFF
#include <math.h>
extern "C" {
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
}
// /AVCODEC STUFF

#include <QMutex>
#include <QMutexLocker>
#include <QSemaphore>
#include <QReadLocker>
#include <QWriteLocker>
#include <atomic>
#include <deque>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wpadded"
#endif

namespace {
    bool doAvCodecInit() {
        avcodec_register_all();
        avfilter_register_all();
        av_register_all();

        return true;
    }

    const bool avcodecIsInitted = doAvCodecInit();

    AVPixelFormat pixelFormatForCodecId(AVCodecID codec);
    AVPixelFormat qimgfmt2avcodecfmt(QImage::Format fmt);
    AVCodecID fmt2CodecId(int fmtFromSettingsClass);

    struct Q
    {
        std::deque<Frame> frames; ///< buffered video frames.  this list never exceeds maxImgs in size

        static const int maxFrames = qMax(3,int(Frame::DefaultFPS())); ///< max number of video frames to buffer

        QMutex mut; ///< to synchronize access to video frames
        QSemaphore sem; ///< signals video frames are ready.. sem.available() never exceeds maxImgs, and should always equal the imgs size

        Q() : sem(0) {}

        ~Q() {
            if (const auto ctv = frames.size()) {
                Warning("Q::~Q still had %d frames in Q (all were safely released)", int(ctv));
            } else {
                Debug("Q::~Q deleted (and was empty).");
            }
        }

        // deep-copies img
        // always succeeds but returns false if queue was full and old img frames were dropped as a result of enqueue
        bool enqueue(const Frame & frame, QString *err = nullptr) {
            bool ret = true;
            QMutexLocker l(&mut);
            if (err) *err = "";
            if (frames.size() >= maxFrames) {
                ret = false;
                const auto fdropped = frames.front().num;
                frames.pop_front();
                if (err)
                    *err = QString("FFmpegEncoder::enqueue -- queue full, dropping frame %1").arg(fdropped);
            }
            frames.push_back(frame);
            if (sem.available() < int(frames.size())) {
                sem.release(int(frames.size()) - sem.available());
            }
            return ret;
        }

        // returns a non-null frame if we have an actual img frame. If no frame was available, the returned frame is null.
        Frame dequeueOneFrame(int timeout_ms) {
            Frame frame;

            bool ok = sem.tryAcquire(1,timeout_ms);
            mut.lock();
            if (ok) { // todo: check for race condition here?
                if (!frames.empty()) {
                    // there appears to be a race condition here where sometimes we get signalled that the
                    // sem is available but imgs.isEmpty().  I am not sure *why* this would ever happen with
                    // exactly 1 reader and 1 writer -- but it lead to crashes.  So we need this redundant
                    // check..
                    frame = frames.front();
                    frames.pop_front();
                }
            }
            mut.unlock();
            return frame;
        }
    };


} // end anonymous namespace

struct FFmpegEncoder::Priv {
    AVCodec *codec = nullptr;
    AVCodecContext *c = nullptr;
    AVFormatContext *oc = nullptr;
    AVStream *video_st = nullptr;
    AVFrame *frame = nullptr;
    AVPacket pkt;
    unsigned framesProcessed = 0; ///< used to determine if we need to flush encoder
    int64_t last_pts = 0LL;
    AVPixelFormat codec_pix_fmt = AV_PIX_FMT_NONE;

    std::atomic<quint64> lastfNumProcd = 0;
    qint64 firstFrameNum = -1;

    Q *queue = nullptr;

    bool wroteHeader = false;

    Priv();
    ~Priv();
};


struct FFmpegEncoder::Converter {
    AVPicture inpic, outpic;
    bool avPictureOk = false;
    int w=0, h=0; ///< width,height of the incoming image
    AVPixelFormat av_pix_fmt_in; ///< the format of the incoming QImages.. usually RGB0
    AVPixelFormat av_pix_fmt_out;
    SwsContext *ctx = nullptr;

    const AVPicture *convert(const QImage &, QString &errMsg);

    ~Converter();
    Converter(int w, int h, AVPixelFormat src_fmt, AVPixelFormat dest_fmt);
};

FFmpegEncoder::Converter::~Converter()
{
    if (ctx) { sws_freeContext(ctx); ctx = nullptr; }
    if (avPictureOk) {
        av_freep(&outpic.data[0]);
        memset(&outpic, 0, sizeof(outpic));
    }
}

FFmpegEncoder::Converter::Converter(int width, int height, AVPixelFormat pxfmt_in, AVPixelFormat pxfmt_out)
{
    w = width; h = height;
    av_pix_fmt_in = pxfmt_in;
    av_pix_fmt_out = pxfmt_out;
    memset(&inpic, 0, sizeof(inpic));
    memset(&outpic,0,sizeof(outpic));
    avPictureOk = av_image_alloc(outpic.data, outpic.linesize, w, h, av_pix_fmt_out, 32 /*force 32-byte alignment good for all codecs*/) > 0;
    if (!avPictureOk) {
        Debug("FFmpegEncoder::Could not allocate output picture buffer!");
    }
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

}

const AVPicture *
FFmpegEncoder::Converter::convert(const QImage &img, QString & errMsg)
{
    if (!ctx || !avPictureOk) {
        errMsg = "Required pointer is NULL in Converter::convert()!";
        return nullptr;
    }
    if (img.width() != w || img.height() != h) {
        errMsg = "img.width or img.height changed!";
        return nullptr;
    }

    // NB: this does a shallow copy -- just fills pointers to image planes...
    avpicture_fill(&inpic,
                   img.bits(),
                   av_pix_fmt_in,
                   w,
                   h);

    inpic.linesize[0] = img.bytesPerLine();

    //perform the conversion
    sws_scale(ctx,
              inpic.data,
              inpic.linesize,
              0,
              h,
              outpic.data,
              outpic.linesize);
    errMsg = "";
    return &outpic;
}


FFmpegEncoder::FFmpegEncoder(const QString &fn, double fps, int br, int fmt, unsigned n_thr)
    : plock(QReadWriteLock::NonRecursive),
      outFile(fn), fps(fps), bitrate(br), fmt(fmt), num_threads(int(n_thr)),
      error("")
{
    p = new Priv;
    p->queue = new Q;
}

FFmpegEncoder::~FFmpegEncoder()
{
    if (p && p->queue && p->oc && p->oc->pb && !p->oc->pb->error) {
        while (p->queue->frames.size())
            if (processOneVideoFrame(10) <= 0) // dequeue leftovers...
                break;
    }

    if (!flushEncoder(&error)) {
        Warning("Encoder flush returned error: %s",error.toUtf8().constData());
    }

    if (p && p->queue) { delete p->queue; p->queue = nullptr; }
    delete conv; conv = nullptr;
    delete p; p = nullptr; // should write trailer for us...
}

FFmpegEncoder::Priv::Priv()
{
    memset(&pkt, 0, sizeof(pkt));
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
    if (frame) {
        //  below av_freep() call is NOT NEEDED SINCE WE DON'T USE OUR OWN BUFFER BUT RATHER USE INCOMING DATA
        //  av_freep(&frame->data[0]);
        av_frame_free(&frame);
        frame = nullptr;
    }
//    Debug("Priv deleted.");
}


quint64 FFmpegEncoder::bytesWritten() const
{
    if (p && p->c && p->oc && p->oc->pb) {
        qint64 r = avio_size(p->oc->pb);
        if (r >= 0) return quint64(r);
    }
    return 0ULL;
}

bool FFmpegEncoder::setupP(int width, int height, int av_pix_fmt)
{
    bool retVal = true;
    error = "";

    QWriteLocker wl (&plock);

    do {
        Q *q = p && p->queue ? p->queue : new Q;
        const qint64 ffn = p ? p->firstFrameNum : -1;
        delete p; p = nullptr;
        p = new Priv;
        p->queue = q;
        p->firstFrameNum = ffn;


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
        p->c->gop_size = fps > 10.0 ? 10 : int(ceil(fps));
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
            p->c->level = 4; p->c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL; // request version 4 FFV1 Codec... this means it won't work by default in VLC but works with ffplay :/

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
            p->c->prediction_method = 1; // set to lossless?

        p->c->pix_fmt = p->codec_pix_fmt;

        if (codec_id == AV_CODEC_ID_H264) {
            // todo: have this come from the UI
            av_opt_set(p->c->priv_data, "preset", "slow", 0);
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

/*        p->f = fopen(outFile.Utf8().constData(), "wb");
        if (!p->f) {
            QString estring = strerror(errno);
            error = QString("Error opening output file: ") + outFile + "; error was: " + estring;
            retVal = false;
            break;
        }
*/
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

        p->frame = av_frame_alloc();
        if (!p->frame) {
            error = "Error #9: Could not allocate video frame";
            retVal = false;
            break;
        }
        p->frame->format = p->c->pix_fmt;
        p->frame->width = p->c->width;
        p->frame->height = p->c->height;

        /*
         * Not needed since we encode the image data either from the incoming QImage directly
         * or from the YUV data output of our Convert::convert() call..
        */
        /*
        int res = av_image_alloc(p->frame->data, p->frame->linesize, p->c->width, p->c->height, p->c->pix_fmt, 32);
        if (res < 0) {
            error = "Error #10: Could not allocate raw buffer";
            retVal = false;
            break;
        }
        */

    } while(0);
    plock.unlock();
    plock.lockForRead();

    //qDebug("CODEC CONTEXT VERSION: %d BITS_PER_RAW_SAMPLE: %d", p->c->level, p->c->bits_per_raw_sample);
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

bool FFmpegEncoder::wroteHeader() const { return p->wroteHeader; }

bool FFmpegEncoder::flushEncoder(QString *errMsg)
{
    if (p && p->framesProcessed && p->c && p->oc && p->video_st && p->oc->pb && !p->oc->pb->error) {
        if (p->c->codec->capabilities & AV_CODEC_CAP_DELAY) {
            qDebug("Flushing delayed video frames...");
            // delayed frames.. get and save
            int iter_ctr = 0, got_output = 0, res;
            do {
                av_init_packet(&p->pkt);
                p->pkt.data = nullptr;
                p->pkt.size = 0;
                p->pkt.pts = p->last_pts+1;
                p->pkt.dts = p->pkt.pts;
                p->pkt.duration = 0;

                res = avcodec_encode_video2(p->c, &p->pkt, nullptr, &got_output);
                if (res < 0) {
                    if (errMsg) *errMsg = "Error encoding frame";
                    return false;
                }

                p->pkt.pts = p->last_pts+1;
                p->pkt.dts = p->pkt.pts;
                p->last_pts = p->pkt.pts;

                if (got_output) {
                    if (write_frame(p->oc, &p->c->time_base, p->video_st, &p->pkt)) { // this automatically unreferences the packet
                        if (errMsg) {
                            *errMsg = "Error from  inner write_frame()";
                            if (p->oc && p->oc->pb && p->oc->pb->error)
                                *errMsg += QString(": ") + strerror(abs(p->oc->pb->error));
                        }
                        return false;
                    }
                    ++iter_ctr;
                }
            } while (got_output);
            Debug("Did %d extra video 'got_input' iterations...",iter_ctr);
        }
    }
    return true;
}

bool FFmpegEncoder::encode(const Frame & frame, QString *errMsg)
{
    const QImage & img(frame.img);
    const uchar *imgData (img.bits());
    const AVPicture *imgYuvData = nullptr;
    AVPixelFormat img_pix_fmt = qimgfmt2avcodecfmt(img.format());
//    qDebug("img_pix_fmt is %d", img_pix_fmt);
    AVPixelFormat codec_pix_fmt = pixelFormatForCodecId(fmt2CodecId(fmt));

    if (img_pix_fmt != codec_pix_fmt) {
        if (!conv || conv->w != img.width() || conv->h != img.height() || conv->av_pix_fmt_in != img_pix_fmt
            || conv->av_pix_fmt_out != codec_pix_fmt
            || (p && p->codec_pix_fmt != codec_pix_fmt) )
        {
            if (conv) {
                delete conv; conv = nullptr;
                qDebug("Input image format or dimensions changed in FFmpegEncoder::encode()!");
            }
            conv = new Converter(img.width(), img.height(), img_pix_fmt, codec_pix_fmt);
            Debug() << "Img format != Codec format; using a converter";
        }
        imgData = nullptr;
        imgYuvData = conv->convert(img, error);
        if (!imgYuvData) {
            if (errMsg) *errMsg = error;
            return false;
        }
    }
    if (!p || !p->codec || !p->c || !p->oc || !p->frame || !p->oc->pb) {
        if (!setupP(img.width(), img.height(), codec_pix_fmt)) {
            if (errMsg) *errMsg = error;
            return false;
        }
    }
    if (p->c->width != img.width()) {
        if (errMsg) *errMsg = "Unexpected image size change: Did you resize the screen?";
        return false;
    }

    if (p->firstFrameNum < 0LL) p->firstFrameNum = qint64(frame.num); // remember "first frame" number seen for proper pts below...
    const qint64 fnum = qint64(frame.num) - p->firstFrameNum; // this is really an offset from start of recording

    av_init_packet(&p->pkt);
    p->pkt.data = nullptr;
    p->pkt.size = 0;
    p->frame->pts = fnum;

    p->framesProcessed++;

    //p->pkt.pts = AV_NOPTS_VALUE;
    //p->pkt.dts = p->frame->pts;
    p->pkt.pts = p->frame->pts; // is this needed?
    p->pkt.dts = p->pkt.pts; //p->last_pts ? p->last_pts : 0;
    p->last_pts = p->pkt.pts;
    p->pkt.duration = 0; // unknown duration ok?

    int got_output = 0, res;

    if (imgYuvData) {
        memcpy(p->frame->data, imgYuvData->data, sizeof(p->frame->data));
        memcpy(p->frame->linesize, imgYuvData->linesize, sizeof(p->frame->linesize));
    } else if (imgData) { // RGB32 data
        p->frame->data[0] = const_cast<uchar *>(imgData);
        p->frame->linesize[0] = img.bytesPerLine();
    }
    res = avcodec_encode_video2(p->c, &p->pkt, p->frame, &got_output);

    p->frame->data[0] = nullptr; // ensure we no longer point to data that will soon go away..
    p->frame->linesize[0] = 0;

    if (res < 0) {
        if (errMsg) *errMsg = "Error encoding frame";
        return false;
    }
    if (got_output) {
        p->pkt.pts = p->frame->pts; // redundant?!
        p->pkt.dts = p->pkt.pts;
        p->pkt.duration = 0;
        if (write_frame(p->oc, &p->c->time_base, p->video_st, &p->pkt)) { // this automatically unreferences the packet
            if (errMsg) {
                *errMsg = "Error #11: Could not write frame";
                if (p->oc && p->oc->pb && p->oc->pb->error)
                    *errMsg += QString(": ") + strerror(abs(p->oc->pb->error));
            }
            return false;
        }
        // todo maybe call flushEncoder() here? I called it and on some encoders it breaks..
        // todo2: see about looping until got_output = 0? (not sure if that's needed: do research!)
    }

    return true;
}

bool FFmpegEncoder::enqueue(const Frame &frame, QString *errMsg)
{
    QReadLocker rl(&plock);
    return p->queue->enqueue(frame, errMsg);
}

qint64 FFmpegEncoder::processOneVideoFrame(int timeout_ms, QString *errMsg, int *tEncode)
{
    using Util::getTime;

    qint64 t0 = getTime(), t1=t0, t2=t0, t3 = t0;
    if (errMsg) *errMsg = "";
    if (tEncode) *tEncode = 0;
    Q & q = *p->queue;
    qint64 retVal = 0;
    bool res = true;

    Frame frame = q.dequeueOneFrame(timeout_ms);
    t1 = getTime();
//    if (res) qDebug("dequeue got img frame %llu (%d x %d)", frame.num, frame.img.width(), frame.img.height());
    if (!frame.isNull()) {
        res = encode(frame, errMsg);
        p->lastfNumProcd = frame.num;
        retVal = res ? qint64(frame.num) : -1;
    } else {
        res = true;
        retVal = 0;
    }
    t2 = getTime();

    if (!res) {
        if (errMsg && errMsg->isEmpty()) *errMsg = "Error writing data.";
        retVal = -1;
    }

    t3 = getTime();

    if (res) {
        if (tEncode) *tEncode = int(t2-t1);
        //qDebug("frame %u (%d x %d, %u audio streams) took %lld ms waiting for a frame, %lld ms to encode, and %lld ms to encode %u audio frames, (%lld ms total encoding time), %lld ms total time",
        //       fNum, imgW, imgH, aqSize, t1-t0, t2-t1, t3-t2, nAudioFrames, t3-t1, getTime()-t0);
    }
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
        case AV_CODEC_ID_FFV1: // <-- seems to be much faster when using this pix fmt.
            return AV_PIX_FMT_YUV420P;
        case AV_CODEC_ID_LJPEG:  // <--- this works far better with yuvj, speed-wise
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
