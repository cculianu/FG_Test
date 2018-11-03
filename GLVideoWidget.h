#ifndef GLVIDEOWIDGET_H
#define GLVIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QImage>
#include "Util.h"

class QOpenGLPaintDevice;

class GLVideoWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit GLVideoWidget(QWidget *parent = nullptr);
    ~GLVideoWidget() override;

signals:
    void fps(double);

public slots:
    void updateFrame(QImage);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    QImage frame;
    PerSec ps;
    QOpenGLPaintDevice *pd = nullptr;
};

#endif // GLVIDEOWIDGET_H
