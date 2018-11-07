#include "Recorder.h"
#include "Settings.h"
#include "Util.h"
#include <QDir>
#include <QDateTime>
#include <QThreadPool>

struct Recorder::Pvt
{
    Pvt(const QString &o, Settings::Fmt f) : outDir(o), format(f) {
        int n = QThread::idealThreadCount();
        if (n < 1) n = 1;
        pool.setMaxThreadCount(n);
    }
    QThreadPool pool;
    QString outDir;
    Settings::Fmt format;
};

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    connect(this, SIGNAL(stopLater()), this, SLOT(stop())); // this is so our ThreadPool thread can stop recording by posting signal to main thread.
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
    if (settings.zipEmbed) return "ZIP Embed currently not supported!";
    QDir d(settings.saveDir);
    if (!d.exists()) return "Save directory invalid.";

    QString outDir = QString("%1%2")
            .arg(settings.savePrefix.isEmpty() ? "" : QString("%1_").arg(settings.savePrefix))
            .arg(QDateTime::currentDateTime().toString("yyMMdd_HHmmss"));
    if (!d.mkdir(QString(outDir)))
        return "Error creating output directory.";

    outDir = settings.saveDir + QDir::separator() + outDir;
    p = new Pvt(outDir, settings.format);
    if (saveLocation) *saveLocation = outDir;

    emit started(outDir);
    return QString();
}

void Recorder::saveFrame(const Frame &f_in)
{
    if (!isRecording()) return;
    Frame f(f_in);
    auto r = new LambdaRunnable([this, f] {
        QString ext = Settings::fmt2String(p->format).toLower();

        QFile out(p->outDir + QDir::separator() + QString("Frame_%1.%2").arg(f.num,6,10,QChar('0')).arg(ext));
        if (!out.open(QFile::WriteOnly|QFile::NewOnly)) {
            emit error(out.errorString());
            emit stopLater();
            return;
        }
        if (p->format == Settings::Fmt_RAW) {
            qint64 len = f.img.bytesPerLine()*f.img.height();
            if (qint64 res = out.write(reinterpret_cast<const char *>(f.img.constBits()), len); res < 0LL) {
                emit error(out.errorString());
                emit stopLater();
                return;
            } else if (res != len) {
                emit error("Short write.");
                emit stopLater();
                return;
            }
        } else if (p->format == Settings::Fmt_PNG || p->format == Settings::Fmt_JPG) {
            if (!f.img.save(&out, ext.toUpper().toUtf8().constData())) {
                emit error(QString("Error writing %1 image").arg(ext.toUpper()));
                emit stopLater();
                return;
            }
        } else {
            emit error("Invalid format");
            emit stopLater();
            return;
        }
        emit wroteFrame(f.num);
    });
    if (!p->pool.tryStart(r)) {
        delete r; r = nullptr;
        Warning() << "Frame " << f.num << " dropped";
        emit frameDropped(f.num);
    }
}
