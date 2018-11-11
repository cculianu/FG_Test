#ifndef UTIL_H
#define UTIL_H

#include <QTextStream>
#include <QString>
#include <atomic>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <functional>
#include <QRunnable>

struct Settings;
class App;
class QTimer;

namespace Util {

    qint64 getTime(); ///< returns a timestamp in milliseconds
    qint64 getTimeNS(); ///< returns a timestamp in nanoseconds (on OSX is basically mach_abs_time)
    double getTimeSecs(); ///< returns a timestamp in seconds (on OSX it's mach_abs_time / 1e9 )

    /// safely connect objects using enqueued messages, printing errors and aborting app if connection fails
    void Connect(QObject *srco, const QString & src, QObject *desto, const QString & dest);

    /// returns the App instance (QApplication subclass)
    App *app();

    /// returns the app-global settings object
    Settings &settings();

    /// misc platform-specific fixups called at app startup.
    void osSpecificFixups();

    /// Return the user's home directory
    QString getUserDirectory();

    /// returns the number of physical (real) processors on the system
    unsigned getNPhysicalProcessors();

    /// returns the number of virtual processors on the system
    unsigned getNVirtualProcessors();

    // returns an HH:MM:SS string for secs
    QString prettyFormatTime(double secs, bool noHHIfZero = false, bool appendSecondFractions = false, int frac_precision = 2 /* only used iff appendSecondFractions = true*/);
    // returns "XX.YY KB" or "XX.YY MB" (or GB or TB) etc...
    QString prettyFormatBytes(quint64 bytes, bool rounded = false, bool isBits = false);

    /// Does "reveal in finder" on Mac or "Show in Explorer" on windows..
    void showInGraphicalShell(const QString & file);

    /// if filename is /path/file X.ext or /path/file.ext, returns first non-existing file /path/file N.ext (or /path/file.ext if /path/file.ext !exists)
    QString getIncrementedFileName(const QString & filePathPlusExt);


    QString rectToString(const QRect &);
    QRect stringToRect(const QString &);

    // returns pm with its alpha channel multiplied by alpha
    QPixmap multAlpha(const QPixmap &pm, float alpha);

    // used by functions that convert a key to a string (such as Settings::keyEvent2String)
    ushort KeyForName(const QString &nameCaseInsensitive);
    QString NameForKey(ushort key);

    /// computes the sha256 hash of the contents of a file and returns the hash string
    /// if nBytes > 0, then only read the first nBytes and last nBytes of the file (for fast imperfect hashing)
    /// if concatMetaData is true, then add the fileSize in bytes and the fileName and mtime to the hash data
    QString sha256HashOfFile(const QString &fileName, qint64 nBytes = 0LL, bool concatMetadata = false);

} // end namespace Util

/// Super class of Debug, Warning, Error classes.
class Log
{
public:
    bool doprt;

    explicit Log(const char *fmt...);
    Log();
    virtual ~Log();

    template <class T> Log & operator<<(const T & t) {  s << t; return *this;  }

    // the following specialization sets the color:
    //  template <> Log & operator<<(const QColor &c);

    void setColor(const QColor &c) { color = c; colorOverridden = true; }
    const QColor & getColor() const { return color; }

protected:
    bool colorOverridden;
    QColor color;
    QString str;
    QTextStream s;
};

// specialization to set the color.
template <> Log & Log::operator<<(const QColor &c);

/** \brief Stream-like class to print a debug message to the app's console window
    Example:
   \code
        Debug() << "This is a debug message"; // would print a debug message to the console window
   \endcode
 */
class Debug : public Log
{
public:
    Debug() : Log() {}
    explicit Debug(const char *fmt...);
    virtual ~Debug();
};

/** \brief Stream-like class to print an error message to the app's console window
    Example:
   \code
        Error() << "This is an ERROR message!!"; // would print an error message to the console window
   \endcode
 */
class Error : public Log
{
public:
    Error() : Log() {}
    explicit Error(const char *fmt...);
    virtual ~Error();
};

/** \brief Stream-like class to print a warning message to the app's console window

    Example:
  \code
        Warning() << "This is a warning message..."; // would print a warning message to the console window
   \endcode
*/
class Warning : public Log
{
public:
    Warning() : Log() {}
    explicit Warning(const char *fmt...);
    virtual ~Warning();
};

/// Stream-like class to print a message to the app's status bar
class Status
{
public:
    Status(int timeout = 0);
    virtual ~Status();

    template <class T> Status & operator<<(const T & t) {  s << t; return *this;  }
protected:
    int to;
    QString str;
    QTextStream s;
};

class Systray : public Status
{
public:
    Systray(bool iserror = false, int timeout = 0);
    virtual ~Systray();
protected:
    bool isError;
};

class Avg {
    std::atomic<double> avg;
    std::atomic<unsigned> navg;
    unsigned nlim;
public:
    Avg(unsigned n=10) { reset(n); }
    void reset(unsigned n=10) { avg = 0.0; navg = 0; setN(n); }
    void setN(unsigned n) { if (!n) n = 1; nlim = n; if (navg >= nlim) navg = nlim; }
    unsigned N() const { return nlim; }
    double operator()(double x) {
        double av = avg;
        if (navg >= nlim && navg) {
            av = av - av/double(navg);
            navg = nlim-1;
        }
        av = av + x/double(++navg);
        return avg=av;
    }
    double operator()() const { return avg; }
};

/// reentrant version of above Avg class
class Avg_R : public Avg {
    QMutex mut;
public:
    Avg_R(unsigned n=10) : Avg(n) {}
    double operator()(double x) { QMutexLocker l(&mut); return Avg::operator()(x); Q_UNUSED(l); }
    double operator()() const { return Avg::operator()(); }
};

/// Useful for calculating FPS, etc...
class PerSec : public QObject {
    Q_OBJECT
public:
    explicit PerSec(QObject *parent = nullptr, unsigned numAvg = 20U) : QObject(parent), avg(numAvg) {}

    double emitTimeoutSecs = 1.0;

    void mark(double multiplier = 1.0); ///< record the current time. May emit perSec() periodically.

signals:
    void perSec(double);

private:
    double tLast = -1.0, tLastEmit = 0.0;
    Avg avg;
};

/// Performs a function with at most frequency hz.
/// If Throttler is called too quickly, it enqueues at most 1 call to a timer to be executed in the future.
class Throttler
{
public:
    typedef std::function<void(void)> VoidFunc;

    Throttler(const VoidFunc &);
    Throttler(VoidFunc &&);
    ~Throttler();

    void operator ()();

    double hz() const { return hz_; }
    void setHz(double h) { if (hz_ > 0.0) hz_ = h; }

private:
    VoidFunc func;
    double hz_ = 4.0, tLast = 0.0;
    QTimer *t = nullptr;
};

class LambdaRunnable : public QRunnable
{
public:
    typedef std::function<void(void)> VoidFunc;
    LambdaRunnable(const VoidFunc &);
    LambdaRunnable(VoidFunc &&);
    ~LambdaRunnable() override;
    void run() override;
private:
    VoidFunc func;
};

class SpinLock {
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (locked.test_and_set(std::memory_order_acquire)) { ; }
    }
    bool tryLock() {
        return !locked.test_and_set(std::memory_order_acquire);
    }
    void unlock() {
        locked.clear(std::memory_order_release);
    }
};

#define qs2cstr(s) (s.toUtf8().constData())

#endif // UTIL_H
