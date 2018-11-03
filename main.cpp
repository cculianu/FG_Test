#include "MainWindow.h"
#include "App.h"
#include <QTimer>

int main(int argc, char *argv[])
{
    App a(argc, argv);
    MainWindow w;

    // NB: the below delayed-show is required because otherwise our window has some glitches on both macOS and Windows
    QTimer::singleShot(250, &w, [&w]{
        w.show();
    });

    return a.exec();
}
