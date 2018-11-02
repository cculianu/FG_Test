#include "WorkerThread.h"
#include <QSemaphore>
#include <QTimer>

WorkerThread::WorkerThread()
    : QObject(nullptr)
{
    static int ct = 0;
    moveToThread(&thr);
    thr.setObjectName(QString("WorkerThread ") + QString::number(++ct));
    thr.start();
}


WorkerThread::~WorkerThread()
{
    stop();
}

bool WorkerThread::stop()
{
    if (thr.isRunning()) {
        if (QThread::currentThread() == this->thread()) {
            qFatal("WorkerThread cannot stop itself from within its own thread!\n");
        } else {
            /* The below is an "elegant hack".
               In order for us to be able to destruct and delete sub-objects, we must move back to the main
               thread (QObjects can only be deleted in the thread they belong to).
               However, we can only call moveToThread in the the 'thr' thread, and wait for it to complete.
               Thus, this waits until we are moved, then it proceeds.
               Calling d'tors should explicitly call stop() before deleting any objects they created in their threads
               and which are owned by this object. */
            QSemaphore sem;
            QTimer::singleShot(1, this, [&]{moveToThread(nullptr); sem.release();});
            sem.acquire();
            thr.quit();
            thr.wait();
        }
    }
    return false;
}
