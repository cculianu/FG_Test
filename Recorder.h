#ifndef RECORDER_H
#define RECORDER_H

#include <QObject>
#include "Frame.h"
struct Settings;

// TODO: much optimization
class Recorder : public QObject
{
    Q_OBJECT
public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    QString start(const Settings &, QString *saveLocation = nullptr); ///< on success, returns an empty QString. on failure returns an error message.
    bool isRecording() const;

signals:
    void started(QString location);
    void stopped();
    void error(QString); ///< emitted during recording iff error occurs.
    void wroteFrame(quint64 frameNum);
    void frameDropped(quint64);
    void stopLater();
    void dataRate(double mbPerSec); ///< emitted periodically to inform calling code about the MB/sec data rate written to disk

public slots:
    void stop();
    void saveFrame(const Frame &);


private:
    void saveFrame_InAThread(const Frame &);

    struct Pvt;
    Pvt *p = nullptr;
};

#endif // RECORDER_H