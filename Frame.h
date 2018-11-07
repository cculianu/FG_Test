#ifndef FRAME_H
#define FRAME_H

#include <QImage>

struct Frame
{
    QImage img;
    quint64 num = 0ULL;
};

Q_DECLARE_METATYPE(Frame);

#endif // FRAME_H
