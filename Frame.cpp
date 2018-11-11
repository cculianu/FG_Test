#include "Frame.h"
extern "C" {
#include "libavutil/frame.h"
}

#include <utility>

extern int FrameTypeId;
int FrameTypeId = qRegisterMetaType<Frame>(); ///< make sure Frame can be used in signals/slots


Frame::Frame(const Frame &other) {  *this = other; }
Frame::Frame(Frame &&o) { *this = std::move(o);  }

Frame::~Frame() { destruct(); }

Frame &Frame::operator=(const Frame &o)
{
    img = o.img;
    num = o.num;
    destroyAVFrame();
    if (o.avframe)
        avframe = av_frame_clone(o.avframe);
    return *this;
}

Frame &
Frame::operator=(Frame &&o)
{
    if (this != &o) {
        img = std::move(o.img);
        num = o.num;
        destroyAVFrame();
        avframe = o.avframe;
        o.avframe = nullptr;
        o.destruct();
    }
    return *this;
}

void Frame::destruct()
{
    img = QImage();
    num = 0;
    destroyAVFrame();
}

void Frame::destroyAVFrame() { av_frame_free(&avframe); /* <-- implicitly nulls pointer. passing-in &(NULL) is ok */ }



// Below is to performance test things.  it's pretty fast!
#if 0
#include <list>
#include "Util.h"
extern "C" {
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
}

void TEST_Frame()
{
    using namespace Util;
    Log() << "Running TEST_Frame";

    const int w = 1024, h = 768, n = 1000;
    Log() << "Generating " << n << " Random Frames, " << w << "x" << h << " pix each...";
    auto t0 = getTime();
    std::list<Frame> frames;
    for (int i=0; i < n; ++i) {
        frames.emplace_back(QImage(w, h, QImage::Format_RGB32), i+1);
        Frame & fr = frames.back();
        for(int r = 0; r < h; ++r) {
            const int sizeBytes = fr.img.bytesPerLine();
            auto bytes = fr.img.bits() + r*sizeBytes;
            for (int c = 0; c < sizeBytes; c+=4) {
                *reinterpret_cast<unsigned *>(bytes + c) = static_cast<unsigned>(qrand())*2;
            }
        }
        fr.avframe = av_frame_alloc();
        fr.avframe->format = AV_PIX_FMT_BGRA;
        fr.avframe->width = fr.img.width();
        fr.avframe->height = fr.img.height();
        av_frame_get_buffer(fr.avframe, 0);
        uint8_t *data[4];  int linesize[4];
        av_image_fill_arrays(data, linesize, fr.img.bits(), AV_PIX_FMT_BGRA, w, h, 32);
        av_image_copy(fr.avframe->data, fr.avframe->linesize, const_cast<const uint8_t **>(&data[0]), linesize, AV_PIX_FMT_BGRA, w, h);
    }
    auto t1 = getTime();
    Log() << "Done, took " << (t1-t0) << " msec";
    Log() << "Copying entire list..";
    qDebug("COPY BEGIN");
    auto t2 = getTime();
    std::list<Frame> frames2(frames);
    auto t3 = getTime();
    qDebug("COPY END");
    Log() << "Done, took " << (t3-t2) << " msec";

    Log() << "Moving entire list..";
    std::list<Frame> frames3;
    qDebug("MOVE BEGIN");
    auto t4 = getTime();
    for (auto & f : frames2) {
        frames3.emplace_back(std::move(f));
    }
    auto t5 = getTime();
    qDebug("MOVE END");
    Log() << "Done, took " << (t5-t4) << " msec";
}
#endif
