#ifndef APP_H
#define APP_H

#include <QApplication>
#include <QColor>
#include <QList>
#include <QPair>
#include "Settings.h"
#include <atomic>

class DebugWindow;
class QSystemTrayIcon;
class QMenu;

class App : public QApplication
{
    Q_OBJECT
public:
    App(int argc, char **argv);
    ~App() override;

    Settings settings;

    void logLine(const QString & line, const QColor & color = QColor());
    void setSBString(const QString &text, int timeout_msecs=0);
    void sysTrayMsg(const QString & msg, int timeout_msecs=0, bool iserror=false);
    bool isConsoleHidden() const { return false; }
    bool isVerboseDebugMode() const { return settings.other.verbosity > 0; }

public slots:
    void setVerboseDebugMode(bool b) { settings.other.verbosity = b ? 2 : 0;  settings.save(); }
    void showRaiseDebugWin();

protected:
    bool event(QEvent *) override;

private slots:
    void clearBacklog();

private:
    std::atomic_bool destructing;
    DebugWindow *debugWin;
    QSystemTrayIcon *sysTray;
    QMenu *trayMenu;
    QList<QPair<QString, QColor> > backlog;
};

#endif // APP_H