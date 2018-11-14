#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifdef XXX__QT_CORE_LIB__XXX
// emualte the windows classes so that FG-specific code doesn't depend on Qt
#include <QMutex>
#include <QSemaphore>
#include <QThread>
#ifndef INFINITE_TO
#define INFINITE_TO 0xffffffff
#endif

class Mutex
{
public:
    Mutex() {}
    ~Mutex() {}

    bool lock(unsigned timeout_ms = INFINITE_TO) { if (timeout_ms == INFINITE_TO) { mut.lock(); return true; } else return mut.tryLock(timeout_ms); }
    void unlock() { mut.unlock(); }

private:
    QMutex mut;
};

class Semaphore
{
public:
    Semaphore(unsigned max_count, unsigned current_count = 0)
        : s(current_count) { (void)max_count; }
    ~Semaphore() {}

    bool acquire(unsigned timeout_ms = INFINITE_TO) ///< acquire 1. negative timeout means wait forever
    { return s.tryAcquire(1, timeout_ms == INFINITE_TO ? -1 : timeout_ms); }
    void release(int ct = 1) { s.release(ct); }

private:
    QSemaphore s;
};

class Thread : protected QThread
{
public:
    Thread() {}
    virtual ~Thread() {}

    bool isRunning() const { return QThread::isRunning();  }
    bool start() { QThread::start(); return true; }
    bool wait(unsigned timeout_ms) { return QThread::wait(timeout_ms); }
    void kill() { QThread::terminate(); }
    int id() { return static_cast<int>(reinterpret_cast<long>(this)); }

protected:
    void run() { threadFunc(); } /* from QThread */
    virtual void threadFunc() = 0;
};
#else
#include "Thread.h"
#endif


#endif // THREAD_COMPAT_H
