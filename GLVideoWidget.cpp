#include "GLVideoWidget.h"
#include <QPainter>
#include <QOpenGLPaintDevice>

GLVideoWidget::GLVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent), ps(this)
{
    connect(&ps, SIGNAL(perSec(double)), this, SIGNAL(fps(double)));
}

GLVideoWidget::~GLVideoWidget()
{
    delete pd; pd = nullptr;
}

void GLVideoWidget::updateFrame(const Frame & inframe)
{
    frame = inframe;
    update();
}

void GLVideoWidget::initializeGL()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void GLVideoWidget::resizeGL(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const qreal retinaScale = devicePixelRatio();
    const GLsizei pixWidth = GLsizei(w * retinaScale), pixHeight = GLsizei(h * retinaScale);
    glViewport(0, 0, pixWidth, pixHeight);
    glOrtho( 0., GLdouble(pixWidth), 0, GLdouble(pixHeight), -1., 1.);
    if (pd) delete pd;
    pd = new QOpenGLPaintDevice(pixWidth, pixHeight);
}

void GLVideoWidget::paintGL()
{
    if (frame.isNull()) {
        glClearColor(0.0,0.0,0.0,1.0);
        glClear(GL_COLOR_BUFFER_BIT);
    } else if (pd) {
        const QRect r(QPoint(), pd->size());
        QPainter p(pd);
        p.setRenderHint(QPainter::SmoothPixmapTransform, /*set to false for now.. true*/false);
        p.drawImage(r, frame.img);
        emit displayedFrame(frame.num);
    }
    ps.mark();
}
