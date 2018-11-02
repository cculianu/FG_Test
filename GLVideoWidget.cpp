#include "GLVideoWidget.h"
#include <QPainter>

GLVideoWidget::GLVideoWidget(QWidget *parent) : QOpenGLWidget(parent)
{
}

GLVideoWidget::~GLVideoWidget() {}

void GLVideoWidget::updateFrame(QImage inframe)
{
    frame = inframe;
    update();
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
