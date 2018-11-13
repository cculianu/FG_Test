#include "Util.h"
#include "App.h"
#include <QGuiApplication>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QDateTime>
#include <QThread>
#include <QThreadPool>
#include <QSemaphore>
#include <QMessageBox>
#include <QProcess>
#include <QDir>
#include <QTimer>
#include <QCryptographicHash>
#include <QStandardPaths>
#if defined(Q_OS_DARWIN)
#  include <sys/types.h>
#  include <sys/sysctl.h>
#  include <mach/mach_time.h>
#elif defined(Q_OS_WIN)
#  include <Windows.h>
#else
#  include <thread>
#endif
#include <cstdlib>
#include <iostream>
#include <utility>
#include <math.h>
#include <atomic>
#include <chrono>
#include <stdarg.h>



namespace Util
{
#ifdef Q_OS_DARWIN
    qint64 getTime()
    {
        static bool did_init = false;
        static struct mach_timebase_info info;
        if (!did_init) {
            mach_timebase_info(&info);
            did_init = true;
        }
        quint64 abs = mach_absolute_time();
        return static_cast<qint64>((qint64(abs) * (static_cast<int64_t>(info.numer) / static_cast<int64_t>(info.denom) )) / 1000000LL);
    }

    qint64 getTimeNS()
    {
        static bool did_init = false;
        static struct mach_timebase_info info;
        if (!did_init) {
            mach_timebase_info(&info);
            did_init = true;
        }
        quint64 abs = mach_absolute_time();
        return static_cast<qint64>((qint64(abs) * (static_cast<int64_t>(info.numer) / static_cast<int64_t>(info.denom) )) );
    }

    double getTimeSecs()
    {
        return double(getTimeNS() / 1000LL)/1e6;
    }

    double machAbs2Secs(uint64_t abs)
    {
        static bool did_init = false;
        static struct mach_timebase_info info;
        if (!did_init) {
            mach_timebase_info(&info);
            did_init = true;
        }
        return (static_cast<double>(abs) * (static_cast<double>(info.numer) / static_cast<double>(info.denom) )) * 1e-9;
    }
    // a = (b*(c/d))*1e-9
    uint64_t secs2MachAbs(double secs)
    {
        static bool did_init = false;
        static struct mach_timebase_info info;
        if (!did_init) {
            mach_timebase_info(&info);
            did_init = true;
        }
        uint64_t nanos = uint64_t(secs*1e9);
        return nanos * static_cast<uint64_t>(info.denom) / static_cast<uint64_t>(info.numer);
    }

    uint64_t machAbs2Nanos(uint64_t abs)
    {
        static bool did_init = false;
        static struct mach_timebase_info info;
        if (!did_init) {
            mach_timebase_info(&info);
            did_init = true;
        }
        return static_cast<quint64>(abs * (static_cast<uint64_t>(info.numer) / static_cast<uint64_t>(info.denom) ));
    }

    void osSpecificFixups() {}

    unsigned getNVirtualProcessors()
    {
        static unsigned nVProcs = 0;
        if (!nVProcs) {
            int a = 0;
            size_t b = sizeof(a);
            if (0 == sysctlbyname("hw.ncpu",&a, &b, nullptr, 0)) {
                nVProcs = unsigned(a); // this returns virtual CPUs which isn't what we want..
            }
        }
        return nVProcs ? nVProcs : 1;
    }

    unsigned getNPhysicalProcessors()
    {
        static unsigned nProcs = 0;
        if (!nProcs) {
            int a = 0;
            size_t b = sizeof(a);
            if (0 == sysctlbyname("hw.physicalcpu",&a,&b,nullptr,0)) {
                nProcs = unsigned(a);
            }
            //Debug() << "nProcs = " << nProcs;//  << " a:" << a << "  b:" << b;
        }
        return nProcs ? nProcs : 1;
    }
#else
#  ifdef Q_OS_WIN
    unsigned getNPhysicalProcessors() {
        static unsigned nProcs = 0;
        if (!nProcs) {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            nProcs = unsigned(sysinfo.dwNumberOfProcessors);
        }
        return nProcs ? nProcs : 1;
    }
#  else
    unsigned getNPhysicalProcessors() { return 4; } // not supported on this platform yet...
#  endif

    unsigned getNVirtualProcessors() {
        return std::thread::hardware_concurrency();
    }

    void osSpecificFixups() {}

    qint64 getTime() {
        QDateTime d(QDateTime::currentDateTime());
        return d.currentMSecsSinceEpoch();
    }

    qint64 getTimeNS() {
        return getTime() * 1000000LL;
    }

    double getTimeSecs() {
        return double(getTime()) / 1000.0;
    }
#endif

    QString rectToString(const QRect &r) {
        return r.isNull() ? QString("<null rect>") : QString().sprintf("%dx%d@%d,%d",r.width(),r.height(),r.x(),r.y());
    }

    QRect stringToRect(const QString &s) {
        QRegExp re("\\s*(\\d+)\\s*[xX]\\s*(\\d+)\\s*[@](\\d+)\\s*,\\s*(\\d+)\\s*");
        QRect r;
        if (re.exactMatch(s)) {
            r = QRect(re.cap(3).toInt(),re.cap(4).toInt(),re.cap(1).toInt(),re.cap(2).toInt());
        }
        return r;
    }

    QRect stringToRect(const QString &);


    App *app() { return dynamic_cast<App *>(qApp); }

    void Connect(QObject *srco, const QString & src, QObject *desto, const QString & dest)
    {
        if (!QObject::connect(srco, src.toUtf8(), desto, dest.toUtf8())) {
            QString tmp;
            QString msg = QString("Error connecting %1::%2 to %3::%4").arg( (tmp = srco->objectName()).isNull() ? "(unnamed)" : tmp ).arg(src.mid(1)).arg( (tmp = desto->objectName()).isNull() ? "(unnamed)" : tmp ).arg(dest.mid(1));
            if (QThread::currentThread() == qApp->thread())
                QMessageBox::critical(nullptr, "Signal connection error", msg);
            else {
                qCritical("Signal Connection Error: %s", msg.toUtf8().data());
                Error() <<  "Signal Connection Error: " << msg;
                double t0 = getTime();
                while (getTime()-t0 < 2.0)
                    QThread::yieldCurrentThread();
            }
            QApplication::exit(1);
            // only reached if event loop is not running
            std::exit(1);
        }
    }

    QString prettyFormatTime(double tsecs, bool noHHIfZero, bool bfracs, int prec)
    {
        if (prec < 0) prec = 0;
        if (!prec) bfracs = false;
        double precpow = prec ? pow(10.0,prec) : 1.0;
        const int hours = int(tsecs)/(60*60), mins = (int(tsecs)/60) % 60,
                  fracs = int(tsecs*precpow)%qRound(precpow);
        int secs = int(tsecs) % 60;
        if (!bfracs) { secs = qRound(tsecs)%60; }
        if (bfracs) {
            QString fracStr = (prec > 1 ? QString().sprintf((QString("%0")+QString::number(prec)+"d").toLatin1().constData(),fracs) : QString::number(fracs));
            if (!hours && noHHIfZero)
                return QString("").sprintf("%02d:%02d.%s",mins,secs,fracStr.toLatin1().constData());
            else {
                return QString("").sprintf("%02d:%02d:%02d.%s",hours,mins,secs,fracStr.toLatin1().constData());
            }
        } else if (!hours && noHHIfZero) { //!bfracs
            return QString("").sprintf("%02d:%02d",mins,secs);
        }
        return QString("").sprintf("%02d:%02d:%02d",hours,mins,secs);
    }

    QString prettyFormatBytes(quint64 bytes, bool rounded, bool isbits)
    {
        double fs = double(bytes);
        QString ret = "";
        if (!isbits) {
            static const QStringList ul = { "bytes", "KB", "MB", "GB", "TB" };
            int i = 0;
            while (i < ul.size() && fs/1024.0 >= 1.0) {
                fs /= 1024.0;
                ++i;
            }
            if (i >= ul.size()) i = ul.size()-1;
            if (rounded || !bytes) {
                ret = QString("").sprintf("%d ",qRound(fs)) + ul[i];
            } else {
                ret = QString("").sprintf("%1.2f ",fs) + ul[i];
            }
        } else {
            static const QStringList ul = { "bit", "Kbit", "Mbit", "Gbit", "Tbit" };
            int i = 0;
            while (i < ul.size() && fs/1000.0 >= 1.0) {
                fs /= 1000.0;
                ++i;
            }
            if (i >= ul.size()) i = ul.size()-1;
            if (rounded || !bytes) {
                ret = QString("").sprintf("%d ",qRound(fs)) + ul[i];
            } else {
                ret = QString("").sprintf("%1.2f ",fs) + ul[i];
            }
        }
        return ret;
    }
    void showInGraphicalShell(const QString &pathIn)
    {
        QWidget *parent = nullptr;
        // Mac, Windows support folder or file.
#if defined(Q_OS_WIN)
        const QString explorer = QStandardPaths::findExecutable("explorer");
        if (explorer.isEmpty()) {
            QMessageBox::warning(parent,
                                 QApplication::tr("Launching Windows Explorer failed"),
                                 QApplication::tr("Could not find explorer.exe in path to launch Windows Explorer."));
            return;
        }
        QString param;
        if (!QFileInfo(pathIn).isDir())
            param = "/select,";
        param += QDir::toNativeSeparators(pathIn);
        QString command = explorer + " " + param;
        QProcess::startDetached(command);
#elif defined(Q_OS_MAC)
        Q_UNUSED(parent)
        QStringList scriptArgs;
        scriptArgs << QLatin1String("-e")
                   << QString::fromLatin1("tell application \"Finder\" to reveal POSIX file \"%1\"")
                                         .arg(pathIn);
        QProcess::execute(QLatin1String("/usr/bin/osascript"), scriptArgs);
        scriptArgs.clear();
        scriptArgs << QLatin1String("-e")
                   << QLatin1String("tell application \"Finder\" to activate");
        QProcess::execute("/usr/bin/osascript", scriptArgs);
#else
#   error showInGraphicalShell() not implemented for this platform!
//        Q_UNUSED(parent)
//        // we cannot select a file here, because no file browser really supports it...
//        const QFileInfo fileInfo(pathIn);
//        const QString folder = fileInfo.absoluteFilePath();
//        const QString app = Utils::UnixUtils::fileBrowser(Core::ICore::instance()->settings());
//        QProcess browserProc;
//        const QString browserArgs = Utils::UnixUtils::substituteFileBrowserParameters(app, folder);
//        if (debug)
//            qDebug() <<  browserArgs;
//        bool success = browserProc.startDetached(browserArgs);
//        const QString error = QString::fromLocal8Bit(browserProc.readAllStandardError());
//        success = success && error.isEmpty();
//        if (!success)
//            showGraphicalShellError(app, error);
#endif
    }

    QPixmap multAlpha(const QPixmap &pm_in, float alpha)
    {
        if (pm_in.isNull()) return pm_in;
        QPixmap pm(pm_in);
        QImage img(pm.toImage());
        if (img.hasAlphaChannel()) {
            QPainter p(&img);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.fillRect(img.rect(),QColor(0,0,0,int(alpha*255.0f)));
            p.end();
            pm = QPixmap::fromImage(img);
        }
        return pm;
    }

    static QMap<int, QString> KeyMap{
      { 9 , "Tab" }, { 25, "Untab"}, {  27, "Esc" }, { 13, "Ent"}, { 127, "Del" }, { 32, "Space"},
      { 63232, "↑" }, { 63233, "↓"}, { 63234, "←" }, { 63235, "→" },
      { 63236, "F1" }, { 63237, "F2" }, { 63238, "F3" }, { 63239, "F4" }, { 63240, "F5" },
        { 63241, "F6" }, { 63242, "F7" }, { 63243, "F8" }, { 63244, "F9" }, { 63245, "F10" }, { 63246, "F11" }, { 63247, "F12" },
    };

    // used by functions that convert a key to a string (such as Settings::keyEvent2String)
    ushort KeyForName(const QString &name)
    {
        QString n(name.trimmed());
        for(QMap<int,QString>::const_iterator it = KeyMap.begin(); it != KeyMap.end(); ++it) {
            if (it.value().startsWith(n,Qt::CaseInsensitive)) return ushort(it.key());
        }
        return 0;
    }

    QString NameForKey(ushort key)
    {
        if (KeyMap.contains(key)) return KeyMap[key];
        return QString();
    }

    /// if filename is /path/file X.ext or /path/file.ext, returns first non-existing file /path/file N.ext (or /path/file.ext if /path/file.ext !exists)
    QString getIncrementedFileName(const QString & fp)
    {
        QFileInfo fi(fp);

        QString path = fi.path(), ext = fi.completeSuffix(), bn = fi.baseName();
        static const QRegExp re ("^(.+) (\\d+)$");
        int start = 1;
        if (re.exactMatch(bn)) {
            bool ok;
            int n = re.cap(2).toInt(&ok);
            if (ok && n > 1) {
                start = n;
            }
            bn = re.cap(1);
        }
        QString fn;
        int i = start;
        do {
            fn = path + QDir::separator() + bn + (i <= start ? QString("") : QString(" ") + QString::number(i)) + (ext.isEmpty()?QString(""):QString(".") + ext);
            ++i;
        } while (QFile::exists(fn));
        return fn;
    }

    /// computes the sha256 hash of the contents of a file and returns the hash string
    /// if nBytes > 0, then only read the first nBytes and last nBytes of the file (for fast imperfect hashing)
    /// if concatMetaData is true, then add the fileSize in bytes and the fileName and mtime to the hash data
    QString sha256HashOfFile(const QString &fileName, qint64 nBytes, bool concatMetadata)
    {
        QFileInfo fi(fileName);
        if (!fi.exists() || !fi.isReadable()) return QString();
        QFile f(fileName);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (nBytes <= 0 || nBytes > fi.size()) nBytes = fi.size();
        if (concatMetadata) {
            QString meta = QString().sprintf("%s %lld %lld",fileName.toLatin1().constData(),fi.size(),fi.lastModified().toMSecsSinceEpoch());
            h.addData(meta.toLatin1());
        }
        if (nBytes >= fi.size())
            h.addData(&f);
        else {
            static const qint64 chunkSize = 64*1024;
            auto doRead = [&f,&h,nBytes]() -> bool {
                qint64 nLeft = nBytes;
                while (nLeft > 0) {
                    qint64 n2read = nLeft > chunkSize ? chunkSize : nLeft;
                    QByteArray a = f.read(n2read);
                    if (a.isEmpty()) break;
                    h.addData(a);
                    nLeft -= qint64(a.size());
                }
                return nLeft == 0;
            };
            if (!f.seek(0) || !doRead() || !f.seek(f.size()-nBytes) || !doRead()) {
                return QString();
            }
        }
        return QString::fromLatin1(h.result().toHex());
    }

    Settings &settings() { return app()->settings; }

    void renameAllPoolThreads(QThreadPool & pool, const QString & prefix) {
        QSemaphore sem;
        int i;
        bool ok;
        for (i = 0, ok = true; ok; ++i) {
            // rename threads
            QString name(QString("%1 %2").arg(prefix).arg(i+1));
            ok = LambdaRunnable::tryStart(pool, [name,&sem]{ QThread::currentThread()->setObjectName(name); Debug() << "Set name to: " << name; sem.release(1); });
        }
        sem.acquire(i-1); // wait for threads to run.
    }

} // end namespace Util

using namespace Util;

Log::Log()
    : doprt(true), colorOverridden(false), str(""), s(&str, QIODevice::WriteOnly)
{
    color = QColor(255,194,0,255);//Qt::darkGreen;
}

Log::Log(const char *fmt...)
    :  doprt(true), colorOverridden(false), str(""), s()
{
    color = QColor(255,194,0,255); //Qt::darkGreen;
    va_list ap;
    va_start(ap,fmt);
    str = QString::vasprintf(fmt,ap);
    va_end(ap);
    s.setString(&str, QIODevice::WriteOnly|QIODevice::Append);
}

Log::~Log()
{
    if (doprt) {
        s.flush(); // does nothing probably..
        QString dateStr = QString("[") + QDateTime::currentDateTime().toString("yyyy.MM.dd hh:mm:ss.zzz") + QString("] ");
        QString thrdStr = "";

        if (QThread *th = QThread::currentThread(); th && qApp && th != qApp->thread()) {
            QString thrdName = th->objectName();
            if (thrdName.trimmed().isEmpty()) thrdName = QString::asprintf("%p", reinterpret_cast<void *>(QThread::currentThreadId()));
            thrdStr = QString("<Thread: %1> ").arg(thrdName);
        }

        QString theString = dateStr + thrdStr + str;

        if (app()) {
            app()->logLine(theString, color);
        } else {
            // just print to console for now..
            std::cerr << theString.toUtf8().constData() << std::endl;
        }
    }
}

template <> Log & Log::operator<<(const QColor &c) { setColor(c); return *this; }

Debug::Debug(const char *fmt...)
    : Log()
{
    va_list ap;
    va_start(ap,fmt);
    str = QString::vasprintf(fmt,ap);
    va_end(ap);
    s.setString(&str, QIODevice::WriteOnly|QIODevice::Append);
}

Debug::~Debug()
{
    if (!app() || !app()->isVerboseDebugMode())
        doprt = false;
    if (!colorOverridden) color = QColor(128,128,128,255);//Qt::cyan;
}


Error::Error(const char *fmt...)
    :  Log()
{
    va_list ap;
    va_start(ap,fmt);
    str = QString::vasprintf(fmt,ap);
    va_end(ap);
    s.setString(&str, QIODevice::WriteOnly|QIODevice::Append);
}



Error::~Error()
{
    if (!colorOverridden) color = QColor(255,100,100,255); // slightly-light red
    if (app() && app()->isConsoleHidden())
        Systray(true) << str; /// also echo to system tray!
}

Warning::Warning(const char *fmt...)
    :  Log()
{
    va_list ap;
    va_start(ap,fmt);
    str = QString::vasprintf(fmt,ap);
    va_end(ap);
    s.setString(&str, QIODevice::WriteOnly|QIODevice::Append);
}

Warning::~Warning()
{
    if (!colorOverridden) color = QColor(235,74,215,255);
    if (app() && app()->isConsoleHidden())
        Systray(true) << str; /// also echo to system tray!
}


Status::Status(int to)
    : to(to), str(""), s(&str, QIODevice::WriteOnly)
{
    s.setRealNumberNotation(QTextStream::FixedNotation);
    s.setRealNumberPrecision(2);
}

Status::~Status()
{
    if (!str.length()) return;
    if (app()) app()->setSBString(str, to);
    else {
        std::cerr << "STATUSMSG: " << str.toUtf8().constData() << "\n";
    }
}

Systray::Systray(bool err, int to)
    : Status(to), isError(err)
{
}

Systray::~Systray()
{
    if (app()) app()->sysTrayMsg(str, to, isError);
    else {
        std::cerr << "SYSTRAYMSG: " << str.toUtf8().constData() << "\n";
    }
    str = ""; // clear it for superclass d'tor
}


void PerSec::mark(double mul)
{
    const double tNow = Util::getTimeSecs();
    if (tLast >= 0.0) {
        // compute per sec Avg
        double result = mul/(tNow-tLast);
        if (qIsNaN(result) || qIsInf(result)) result = 0.;
        avg(result);
        if (tNow-tLastEmit >= emitTimeoutSecs) {
            emit perSec(avg());
            tLastEmit = tNow;
        }
    }
    tLast = tNow;
}

Throttler::Throttler(VoidFunc &&f): func(std::move(f)) {}
Throttler::Throttler(const VoidFunc &f): func(f) {}
Throttler::~Throttler()
{
    if (t) { delete t; t = nullptr; }
}
void Throttler::operator()()
{
    if (t && t->isActive()) return;

    if (const double tDiff = getTimeSecs() - tLast, period = 1.0/hz_; tDiff < period) {
        if (!t) {
            t = new QTimer;
            QObject::connect(t, &QTimer::timeout, [this]{
                t->stop();
                func();
                tLast = getTimeSecs();
            });
        }
        t->start(std::chrono::milliseconds(int((period- tDiff)*1e3)));
    } else {
        func();
        tLast = getTimeSecs();
    }
}

LambdaRunnable::LambdaRunnable(const VoidFunc &f) : func(f) {}
LambdaRunnable::LambdaRunnable(VoidFunc &&f) : func(std::move(f)) {}
LambdaRunnable::~LambdaRunnable() {}
void LambdaRunnable::run() { func(); }
/*static*/
bool LambdaRunnable::tryStart(QThreadPool &pool, const VoidFunc &f)
{
    auto lr = new LambdaRunnable(f);
    if (!pool.tryStart(lr)) {
        delete lr;
        return false;
    }
    return true;
}
/*static*/
bool LambdaRunnable::tryStart(QThreadPool &pool, VoidFunc &&f)
{
    auto lr = new LambdaRunnable(std::move(f));
    if (!pool.tryStart(lr)) {
        delete lr;
        return false;
    }
    return true;
}
