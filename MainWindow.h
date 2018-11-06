#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>
#include <QVector>

namespace Ui {
class MainWindow;
}

class FakeFrameGenerator;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *) override;

private slots:
    void updateStatusMessage();

private:
    QMap<QString, QAction *> tbActs;
    void setupToolBar();
    Ui::MainWindow *ui;
    FakeFrameGenerator *fgen = nullptr;

    enum StatusString { FPS1 = 0, FPS2, Count, Dropped, Recording, Misc, NStatus };
    QVector<QString> statusStrings = QVector<QString>(NStatus);
};

#endif // MAINWINDOW_H
