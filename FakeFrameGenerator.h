#ifndef FAKEFRAMEGENERATOR_H
#define FAKEFRAMEGENERATOR_H

#include <QImage>
#include <QVector>
#include "WorkerThread.h"
#include "Frame.h"

class QTimer;
class PerSec;

class FakeFrameGenerator : public WorkerThread
{
    Q_OBJECT
public:
    FakeFrameGenerator(int width = 5056, int height = 2968, double fps = 10.0, int nUniqueFrames = 20);
    ~FakeFrameGenerator();

signals:
    void generatedFrame(const Frame &);
    void fps(double);

public slots:

private slots:
    void genFrame();

private:
    int w, h;
    quint64 frameNum = 0ULL;
    QTimer *t = nullptr;
    QVector<QImage> frames;
    PerSec *ps = nullptr;
};

#endif // FAKEFRAMEGENERATOR_H
