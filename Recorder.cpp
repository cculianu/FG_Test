#include "Recorder.h"
#include "Settings.h"
#include "Util.h"
#include "quazip/quazip.h"
#include "quazip/quazipfile.h"
#include <QDir>
#include <QDateTime>
#include <QThreadPool>
#include <QByteArray>
#include <QBuffer>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <atomic>

struct Recorder::Pvt
{
    Pvt(const QString &o, Settings::Fmt f) : dest(o), format(f) {
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
        QTimer *t = new QTimer(&perSec);
        connect(t, &QTimer::timeout, &perSec, [this]{
            const auto bytes = wroteBytes.exchange(0LL);
            perSec.mark(double(bytes)/1e6);
        });
        t->start(333);
    }
    ~Pvt() {
        if (zipFile) { if (zipFile->isOpen()) zipFile->close(); delete zipFile; zipFile = nullptr; }
        if (zip) { if (zip->isOpen()) zip->close(); delete zip; zip = nullptr; }
    }
    QThreadPool pool;
    QString dest;
    const Settings::Fmt format;
    bool isZip = false;
    QuaZip *zip = nullptr;
    QuaZipFile *zipFile = nullptr;
    QMutex zipMut;
    PerSec perSec;
    std::atomic<qint64> wroteBytes;
};

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    // this is so our ThreadPool thread can stop recording by posting this signal to the main thread.
    connect(this, SIGNAL(stopLater()), this, SLOT(stop()));
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

    QString outDir = QString("%1%2")
            .arg(settings.savePrefix.isEmpty() ? "" : QString("%1_").arg(settings.savePrefix))
            .arg(QDateTime::currentDateTime().toString("yyMMdd_HHmmss"));
    if (settings.zipEmbed) outDir += ".zip";
    else {
        if (!d.mkdir(QString(outDir)))
            return "Error creating output directory.";
    }
    outDir = settings.saveDir + QDir::separator() + outDir;
    p = new Pvt(outDir, settings.format);
    if (saveLocation) *saveLocation = outDir;
    connect(&p->perSec, SIGNAL(perSec(double)), this, SIGNAL(dataRate(double)));
    emit started(outDir);
    return QString();
}

void Recorder::saveFrame(const Frame &f_in)
{
    if (!isRecording()) return;
    Frame f(f_in);
    auto r = new LambdaRunnable([this, f] { saveFrame_InAThread(f); });
    if (!p->pool.tryStart(r)) {
        delete r; r = nullptr;
        Warning() << "Frame " << f.num << " dropped";
        emit frameDropped(f.num);
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
            QMutexLocker ml(&p->zipMut); // only 1 thread at a time can modify the QuaZipFile object...
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
