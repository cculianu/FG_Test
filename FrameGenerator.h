#ifndef FRAMEGENERATOR_H
#define FRAMEGENERATOR_H
#include "WorkerThread.h"

class PerSec;
struct Frame;

/// Base class of all frame generators: "Fake" as well as Framegrabber-based 'real' (yet to be implemented)
class FrameGenerator : public WorkerThread
{
    Q_OBJECT
protected:
    FrameGenerator(); /// prevent client code from constructing an instance of this base class

public:
    virtual ~FrameGenerator();

signals:
    void generatedFrame(const Frame &); ///< subclasses should emit this to publish generated frames to client code
    void fps(double);  ///< automatically emitted based on frequency of calls to generatedFrame()

private:
    PerSec *ps = nullptr;
};

#endif // FRAMEGENERATOR_H
