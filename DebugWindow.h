#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QTextBrowser;

namespace Ui {
class DebugWindow;
}

struct Settings;

class DebugWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DebugWindow(QWidget *parent = nullptr);
    ~DebugWindow();

    QTextBrowser *console();

public slots:
    void printSettings(const Settings &);
    void clearLog(); ///< clears the debug/console log

private:
    Ui::DebugWindow *ui;
};

#endif // DEBUGWINDOW_H
