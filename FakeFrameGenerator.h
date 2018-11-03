#ifndef FAKEFRAMEGENERATOR_H
#define FAKEFRAMEGENERATOR_H

#include "WorkerThread.h"
#include <QImage>
#include <QVector>

class QTimer;

class FakeFrameGenerator : public WorkerThread
{
    Q_OBJECT
public:
    FakeFrameGenerator(int width = 5056, int height = 2968, double fps = 60.0);
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
    double lastTime;
};

#endif // FAKEFRAMEGENERATOR_H
