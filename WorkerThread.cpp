#include "WorkerThread.h"
#include <QSemaphore>
#include <QTimer>
#include <QEvent>
#include <QApplication>
#include <utility>

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
            postLambdaSync([this]{ moveToThread(nullptr); });
            thr.quit();
            thr.wait();
        }
    }
    return false;
}

struct LambdaEvent : QEvent {
    static const QEvent::Type typ = QEvent::Type(QEvent::User + 201);

    std::function<void(void)> lambda;

    LambdaEvent(const std::function<void(void)> & f) : QEvent(typ), lambda(f) {}
    LambdaEvent(std::function<void(void)> && f) : QEvent(typ), lambda(std::move(f)) {}
    ~LambdaEvent() override;
};

LambdaEvent::~LambdaEvent() {}

/// these can be called from any thread
void WorkerThread::postLambda(const std::function<void(void)> & lambda)
{
    QApplication::postEvent(this, new LambdaEvent(lambda));
}
void WorkerThread::postLambda(std::function<void(void)> && lambda)
{
    QApplication::postEvent(this, new LambdaEvent(std::move(lambda)));
}
void WorkerThread::postLambdaSync(const std::function<void(void)> & lambda)
{
    QSemaphore sem;
    postLambda([lambda,&sem]{
        lambda();
        sem.release();
    });
    sem.acquire();
}
// this is called in the thread
void WorkerThread::customEvent(QEvent *e)
{
    if (e->type() == LambdaEvent::typ) {
        LambdaEvent *le = dynamic_cast<LambdaEvent *>(e);
        if (le) {
            le->lambda(); // call the function!
            e->accept();
            return;
        }
    }
    QObject::customEvent(e);
}
