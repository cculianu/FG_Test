#ifndef FRAME_H
#define FRAME_H

#include <QImage>

struct Frame
{
    QImage img;
    quint64 num = 0ULL;

    bool isNull() const { return img.isNull(); }
    void nullify() { img = QImage(); num=0ULL; }

    static constexpr double DefaultFPS() { return 10.0; }
    static constexpr int DefaultWidth() { return 5056; }
    static constexpr int DefaultHeight() { return 2968; }
};

Q_DECLARE_METATYPE(Frame);

#endif // FRAME_H
