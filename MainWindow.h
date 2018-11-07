#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Util.h"
#include <QMainWindow>
#include <QMap>
#include <QVector>

namespace Ui {
class MainWindow;
}

class FakeFrameGenerator;
class Recorder;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *) override;

private slots:

private:
    void setupToolBar();
    void updateToolBar();
    void updateStatusMessage();
    QMap<QString, QAction *> tbActs;
    Throttler updateStatusMessageThrottled;
    Ui::MainWindow *ui;
    FakeFrameGenerator *fgen = nullptr;

    enum StatusString { FPS1 = 0, FPS2, FrameNum, Dropped, FrameNumRec, Recording, Misc, NStatus };
    QVector<QString> statusStrings = QVector<QString>(NStatus);

    Recorder *rec = nullptr;

};

#endif // MAINWINDOW_H
