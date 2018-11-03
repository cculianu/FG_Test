#ifndef FAKEFRAMEGENERATOR_H
#define FAKEFRAMEGENERATOR_H

#include <QImage>
#include <QVector>
#include "WorkerThread.h"
#include "Util.h"

class QTimer;

class FakeFrameGenerator : public WorkerThread
{
    Q_OBJECT
public:
    FakeFrameGenerator(int width = 5056, int height = 2968, double fps = 60.0, int nUniqueFrames = 20);
    ~FakeFrameGenerator();

signals:
    void generatedFrame(QImage);
    void fps(double);

public slots:

private slots:
    void genFrame();

private:
    int w, h;
    QTimer *t;
    QVector<QImage> frames;
    double tLastFrame, tLastFpsStatus;
    Avg fpsAvg;
};

#endif // FAKEFRAMEGENERATOR_H
