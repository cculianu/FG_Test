#ifndef WORKERTHREAD_H
#define WORKERTHREAD_H

#include <QObject>
#include <QThread>

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

signals:

public slots:

protected:
    QThread thr;
};

#endif // WORKERTHREAD_H
