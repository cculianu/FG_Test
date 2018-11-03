#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMap>

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

private:
    QMap<QString, QAction *> tbActs;
    void setupToolBar();
    Ui::MainWindow *ui;
    FakeFrameGenerator *fgen = nullptr;
};

#endif // MAINWINDOW_H
