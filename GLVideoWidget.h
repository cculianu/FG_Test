#ifndef GLVIDEOWIDGET_H
#define GLVIDEOWIDGET_H

#include <QOpenGLWidget>
#include "Frame.h"
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
    QOpenGLPaintDevice *pd = nullptr;
};

#endif // GLVIDEOWIDGET_H
