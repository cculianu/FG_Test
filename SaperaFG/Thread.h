#ifndef THREAD_H
#define THREAD_H

#include <QThread>

// A slightly different API to QThread.
class Thread : protected QThread
{
public:
    Thread() {}
    virtual ~Thread() override;

    bool isRunning() const { return QThread::isRunning();  }
    bool start() { QThread::start(); return true; }
    bool wait(unsigned timeout_ms = ULONG_MAX) { return QThread::wait(timeout_ms); }
    void kill() { QThread::terminate(); }
    int id() { return static_cast<int>(reinterpret_cast<long long>(this)); }

protected:
    void run() override { threadFunc(); } /* from QThread */
    virtual void threadFunc() = 0;
};

#endif // THREAD_H
