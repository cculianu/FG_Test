#include "GLVideoWidget.h"
#include <QPainter>

GLVideoWidget::GLVideoWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

GLVideoWidget::~GLVideoWidget() {}

void GLVideoWidget::updateFrame(QImage inframe)
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
    glViewport(0, 0, w, h);
    glOrtho( 0., GLdouble(w), 0, GLdouble(h), -1., 1.);
}

void GLVideoWidget::paintGL()
{
    if (frame.isNull()) {
        glClearColor(0.0,0.0,0.0,1.0);
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(rect(), frame);
    }
}
