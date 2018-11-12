#include "Recorder.h"
#include "Settings.h"
#include "Util.h"
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"
#include "FFmpegEncoder.h"
#include <QDir>
#include <QDateTime>
#include <QThreadPool>
#include <QByteArray>
#include <QBuffer>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <atomic>
#include <chrono>

struct Recorder::Pvt
{
    Pvt(const QString &o, Settings::Fmt f, double fps) : dest(o), format(f) {
        using namespace std::chrono;
        auto pollBytesTimer = 333ms;
        if (Settings::FFmpegFormats.count(format)) {
            unsigned n = Util::getNPhysicalProcessors();
            if (n < 1) n = 1;
            pool.setMaxThreadCount(1); // only 1 processing thread. multiple threads happen in the encoder itself.
            isZip = false;
            ff = new FFmpegEncoder(dest, fps, qint64(1e6*60)/*qint64(Frame::DefaultWidth())*qint64(Frame::DefaultHeight())*2LL*8LL*qint64(fps)*/, format, n);
            pollBytesTimer = 1s;
        } else {
            int n = QThread::idealThreadCount()-1;
            if (n < 1) n = 1;
            pool.setMaxThreadCount(n);
            if (dest.endsWith(".zip")) {
                isZip = true;
                zip = new QuaZip(dest);
                if (!zip->open(QuaZip::mdCreate)) {
                    Error() << "Error opening zip";
                    delete zip; zip = nullptr;
                    return;
                }
                zip->setZip64Enabled(true);
                zipFile = new QuaZipFile(zip);
            }
        }
        QTimer *t = new QTimer(&perSecMB);
        connect(t, &QTimer::timeout, &perSecMB, [this]{
            const auto bytes = wroteBytes.exchange(0LL);
            perSecMB.mark(double(bytes)/1e6);
        });
        t->start(pollBytesTimer);
    }
    ~Pvt() {
        if (zipFile) { if (zipFile->isOpen()) zipFile->close(); delete zipFile; zipFile = nullptr; }
        if (zip) { if (zip->isOpen()) zip->close(); delete zip; zip = nullptr; }
        if (ff) { delete ff; ff = nullptr; }
    }
    QThreadPool pool;
    QString dest;
    const Settings::Fmt format;
    bool isZip = false;
    QuaZip *zip = nullptr;
    QuaZipFile *zipFile = nullptr;
    QMutex mut;
    PerSec perSecMB, perSecFrames;
    std::atomic<qint64> wroteBytes;

    FFmpegEncoder *ff = nullptr;
};

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    // this is so our ThreadPool thread can stop recording by posting this signal to the main thread.
    connect(this, SIGNAL(stopLater()), this, SLOT(stop()));
    connect(this, SIGNAL(wroteFrame(quint64)), this, SLOT(didWriteFrame()));
}

Recorder::~Recorder()
{
    if (isRecording()) stop(); // implicitly deletes p, if non-null
}

void Recorder::stop()
{
    if (p) {
        if (p->pool.activeThreadCount()) p->pool.waitForDone();
        delete p; p = nullptr;
        emit stopped();
    }
}

bool Recorder::isRecording() const { return !!p; }

QString Recorder::start(const Settings &settings, QString *saveLocation)
{
    if (isRecording()) return "Recording already running!";
    QDir d(settings.saveDir);
    if (!d.exists()) return "Save directory invalid.";

    QString dest = QString("%1%2")
            .arg(settings.savePrefix.isEmpty() ? "" : QString("%1_").arg(settings.savePrefix))
            .arg(QDateTime::currentDateTime().toString("yyMMdd_HHmmss"));
    if (Settings::FFmpegFormats.count(settings.format)) dest += ".avi";
    else if (settings.zipEmbed) dest += ".zip";
    else {
        if (!d.mkdir(QString(dest)))
            return "Error creating output directory.";
    }
    dest = settings.saveDir + QDir::separator() + dest;
    p = new Pvt(dest, settings.format, settings.fps);
    if (saveLocation) *saveLocation = dest;
    connect(&p->perSecMB, SIGNAL(perSec(double)), this, SIGNAL(dataRate(double)));
    connect(&p->perSecFrames, SIGNAL(perSec(double)), this, SIGNAL(fps(double)));
    if (p->ff) {
        connect(p->ff, SIGNAL(error(QString)), this, SIGNAL(error(QString)));
        connect(p->ff, SIGNAL(error(QString)), this, SLOT(stop()));
        connect(p->ff, SIGNAL(frameDropped(quint64)), this, SIGNAL(frameDropped(quint64)));
        connect(p->ff, SIGNAL(wroteFrame(quint64)), this, SIGNAL(wroteFrame(quint64)));
        connect(p->ff, &FFmpegEncoder::wroteBytes, this, [this](qint64 nb){
            if (p) p->wroteBytes += nb;
        });
    }
    emit started(dest);
    return QString();
}

void Recorder::saveFrame(const Frame &f_in)
{
    if (!isRecording()) return;
    if (!p->ff) {
        // no FFmpegEncoder, use "img save"
        Frame f(f_in);
        if (!LambdaRunnable::tryStart(p->pool, [this, f] { saveFrame_InAThread(f); })) {
            Warning() << "Frame " << f.num << " dropped";
            emit frameDropped(f.num);
        }
    } else {
        // use FFmpegEncoder
        if (QString err; ! p->ff->enqueue(f_in, &err) )
            Warning() << err;
    }
}

void Recorder::saveFrame_InAThread(const Frame &f)
{
    if (!p) {
        // defensive programming.  this check is not going to ever be true (unless we change this class around and forget to update this code).
        QString err("INTERNAL ERROR: 'p' ptr is null but we are still saving in saveFrame_InAThread_NoZip!");
        Error() << err; emit error(err); return;
    }

    struct Err { QString err; };

    try {
        if (p->isZip && (!p->zip || !p->zipFile))
            throw Err{"Zip File could not be opened. Check the destination directory."};
        QString ext = Settings::fmt2String(p->format).toLower();

        QIODevice *out = nullptr;
        QByteArray outbytes;
        QBuffer outbuf(&outbytes);
        const QString fname = QString("Frame_%1.%2").arg(f.num,6,10,QChar('0')).arg(ext);
        QFile outf(p->dest + QDir::separator() + fname);
        qint64 wroteBytes = 0LL;
        if (p->isZip)
            out = &outbuf;
        else
            out = &outf;
        if (!out->open(QFile::WriteOnly|QFile::NewOnly))
            throw Err{out->errorString()};
        if (p->format == Settings::Fmt_RAW) {
            // not zip file, write to file
            const qint64 len = f.img.bytesPerLine()*f.img.height();
            if (p->isZip) {
                // zip file.. skip writing to buffer.. instear "point" buffer at img data. This usage ensures no extra copying
                outbytes = QByteArray::fromRawData(reinterpret_cast<const char *>(f.img.constBits()), int(len));
            } else {
                // not a zip file. write to output file.
                if (const qint64 res = out->write(reinterpret_cast<const char *>(f.img.constBits()), len); res < 0LL)
                    throw Err{out->errorString()};
                else if (res != len)
                    throw Err{"Short write"};
            }
        } else if (p->format == Settings::Fmt_PNG || p->format == Settings::Fmt_JPG) {
            // JPG/PNG needs conversion so this usage does the conversion. In the zip file case we are writing to outbytes.
            // In the non zip file case we are writing to a disk file here.
            if (!f.img.save(out, ext.toUpper().toUtf8().constData()))
                throw Err{QString("Error writing %1 image").arg(ext.toUpper())};
        } else
            throw Err{"Invalid format"};
        if (p->isZip) {
            QuaZipNewInfo inf(fname);
            inf.setPermissions(QFile::Permissions(0x6666));
            QMutexLocker ml(&p->mut); // only 1 thread at a time can modify the QuaZipFile object...
            if (!p->zipFile->open(QuaZipFile::WriteOnly|QuaZipFile::NewOnly, inf, nullptr, 0, Z_DEFLATED, Z_NO_COMPRESSION)) {
                throw Err{p->zipFile->errorString()};
            }
            if (qint64 len = p->zipFile->write(outbytes); len != outbytes.length()) {
                throw Err{p->zipFile->errorString()};
            } else
                wroteBytes = len;
            p->zipFile->close();
            if (p->zipFile->getZipError() != Z_OK) {
                throw Err{"Error on close within zip file"};
            }
        } else
            wroteBytes = out->pos();
        p->wroteBytes += wroteBytes;
        emit wroteFrame(f.num);
    } catch (const Err & e) {
        emit error(e.err);
        emit stopLater();
        return;
    }
}

void Recorder::didWriteFrame()
{
    if (p) p->perSecFrames.mark();
}
