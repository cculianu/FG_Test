#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <QObject>
#include <QThread>
#include <functional>

// Qt Event-Based Worker.  Process events in another thread.
// Subclasses can inherit from this class and postEvents and send signals/slots and they will be processed
// in another thread.
class WorkerThread : public QObject
{
    Q_OBJECT
public:
    // Will create itself with parent=nullptr and move itself to its own member thr, starting the QThread.
    WorkerThread();
    // Implicitly calls stop(). Derived classes are encouraged to call stop explicitly, if they own dependent QObjects, however.
    ~WorkerThread() override;

    /// Stop QThread, but first will move itself to the main thread. Blocks until complete.
    /// Returns true on success or false on failure or if we were already stopped.
    ///
    /// Derived classes should explicitly call this method from their destructors if part of their destruction
    /// sequence involves the destruction of child QObjects!
    virtual bool stop();

    /// Executes lambda by posting it as an event to the WorkerThread's event queue. Returns immediately.
    void postLambda(const std::function<void(void)> & lambda);
    void postLambda(std::function<void(void)> && lambda);
    /// Synchronous (blocking) version of the above. Waits for lambda() to be called then returns.
    void postLambdaSync(const std::function<void(void)> & lambda);
signals:

public slots:

protected:
    void customEvent(QEvent *) override;

    QThread thr;
};

#endif // WORKERTHREAD_H
