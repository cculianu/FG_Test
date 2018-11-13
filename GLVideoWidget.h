#ifndef GLVIDEOWIDGET_H
#define GLVIDEOWIDGET_H

#include <QOpenGLWidget>
#include "Frame.h"
#include "Util.h"

class QOpenGLPaintDevice;
class QOpenGLShaderProgram;
class QOpenGLTexture;

class GLVideoWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit GLVideoWidget(QWidget *parent = nullptr);
    ~GLVideoWidget() override;

signals:
    void fps(double);
    void displayedFrame(quint64 num);

public slots:
    void updateFrame(const Frame &);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    Frame frame;
    PerSec ps;
    QOpenGLPaintDevice *pd = nullptr; // fallback to QPainter-based painting -- this will go away if we transition away from QImage for pixel data
    QOpenGLShaderProgram *prog = nullptr;
    QOpenGLTexture *tex = nullptr;
    GLsizei pixWidth=0, pixHeight=0;

    // PBO-related stuff. Note we only use these fields if PBOs are available, otherwise the fallback is a slower pixel transfer method.
    static constexpr int NPBOS = 2;
    GLuint pbos[NPBOS] = {0};
    int index = 0;
    Frame pboFrames[NPBOS]; // we buffer the pbo data in memory so pixel transfers can happen to the GPU in the background
};

#endif // GLVIDEOWIDGET_H
