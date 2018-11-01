#include "MainWindow.h"
#include "App.h"

int main(int argc, char *argv[])
{
    App a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
