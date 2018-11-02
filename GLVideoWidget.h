#ifndef GLVIDEOWIDGET_H
#define GLVIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QImage>

class GLVideoWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit GLVideoWidget(QWidget *parent = nullptr);
    ~GLVideoWidget() override;

signals:

public slots:
    void updateFrame(QImage);

protected:
    void paintGL() override;

private:
    QImage frame;
};

#endif // GLVIDEOWIDGET_H
