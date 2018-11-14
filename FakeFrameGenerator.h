#ifndef FAKEFRAMEGENERATOR_H
#define FAKEFRAMEGENERATOR_H

#include <QImage>
#include <QVector>
#include "Frame.h"
#include "FrameGenerator.h"

class QTimer;

/// Currently generates random static.  Used for testing.
class FakeFrameGenerator : public FrameGenerator
{
    Q_OBJECT
public:
    FakeFrameGenerator(int width = Frame::DefaultWidth(), int height = Frame::DefaultHeight(),
                       double fps = Frame::DefaultFPS(), int nUniqueFrames = 16);
    ~FakeFrameGenerator() override;

    double requestedFPS() const { return reqfps; }

   /* INHERITED signals:
    *     void generatedFrame(const Frame &);
    *     void fps(double); */

private slots:
    void genFrame();

private:
    int w, h;
    double reqfps;
    quint64 frameNum = 0ULL;
    QTimer *t = nullptr;
    QVector<QImage> frames;
    int lastPick = -999;
};

#endif // FAKEFRAMEGENERATOR_H
