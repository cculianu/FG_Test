#include <QEvent>
#include <QFileOpenEvent>
#include <QTimer>
#include <QThread>
#include <QTextBrowser>
#include <QStatusBar>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>

#include "App.h"
#include "Util.h"
#include "DebugWindow.h"
#include "ui_Prefs.h"

#include "Version.h"

namespace {
    class LogLineEvent : public QEvent
    {
    public:

        static const QEvent::Type myType = static_cast<QEvent::Type>(QEvent::User+10);

        LogLineEvent(const QString &str, const QColor & color)
            : QEvent(QEvent::Type(myType)), str(str), color(color) {}
        QString str;
        QColor color;
    };
}

App::App(int argc, char **argv)
    : QApplication(argc, argv), darkMode(settings.appearance.useDarkStyle)
{
    if (darkMode) {
        QFile f(":qdarkstyle/style.qss");
        if (!f.exists()) {
            Error("Unable to set stylesheet, file not found");
        } else {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            setStyleSheet(ts.readAll());
            Debug() << "Set Style to: Dark Style";
        }
    }
    setApplicationName(APPNAME);
    setApplicationVersion(VERSION_STR);
    setApplicationDisplayName(APPNAME_FULL);

    Util::osSpecificFixups();

    debugWin = new DebugWindow();
    debugWin->hide();

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        sysTray = new QSystemTrayIcon(QIcon(":/Img/tray_icon.png"), this);
        sysTray->setObjectName("System Tray Icon");
        sysTray->setContextMenu(trayMenu = new QMenu(nullptr));
        QAction *a;
        trayMenu->addSeparator();
        a = trayMenu->addAction(APPNAME);
        a->setDisabled(true);

        trayMenu->addSeparator();

        a = trayMenu->addAction(
#ifdef Q_OS_MAC
                    "Preferences"
#else
                    "Settings"
#endif
                    , this, SLOT(showPrefs()));

        a = new QAction("Quit", this);
        connect(a, &QAction::triggered, this, [this]{ this->quit();});
        trayMenu->addAction(a);


        sysTray->show();
    } else {
        QMessageBox::critical(nullptr, APPNAME_FULL, "System tray unavailable, exiting.", QMessageBox::Ok);
        quit();
    }

    debugWin->printSettings(settings);

    Log() << applicationDisplayName() << " Started";
}

App::~App() /* override */
{
    destructing = true;
    delete debugWin; debugWin = nullptr;
    delete trayMenu; trayMenu = nullptr;
    delete sysTray; sysTray = nullptr;
}

bool App::event(QEvent *e) /* override */
{
    const int t(e->type());

    switch (t) {
    case QEvent::FileOpen: {
        QFileOpenEvent *fe = dynamic_cast<QFileOpenEvent *>(e);
        if (fe) {
            // todo handle fileopen events here...
            Debug() << "QFileOpenEvent for url=" << fe->url().toString() << " (" << fe->file() << ")";
            return true;
        }
    }
        break;
    case LogLineEvent::myType: {
        LogLineEvent *le = dynamic_cast<LogLineEvent *>(e);
        if (le) {
            //           qDebug("received event for line: %s...",le->str.left(10).toLatin1().constData());
            logLine(le->str, le->color);
            le->accept();
            return true;
        }
    }
        break;
    }
    return QApplication::event(e);
}

void App::showRaiseDebugWin()
{
    if (debugWin) {
        debugWin->show();
        debugWin->activateWindow();
        debugWin->raise();
    }
}

void App::logLine(const QString &line, const QColor &color) {
    if (destructing) {
        qDebug("-APPEXIT LOG-: %s", qs2cstr(line));
        return;
    }
    if (!debugWin) {
        backlog.push_back(QPair<QString,QColor>(line,color));
        QTimer::singleShot(100, this, SLOT(clearBacklog()));
        return;
    }
    if (QThread::currentThread() == this->thread()) {
        QTextBrowser *tb = debugWin->console();
        QColor origColor = tb->textColor();
        if (color.isValid()) tb->setTextColor(color);
        tb->append(line);
        //te->moveCursor();
        if (color.isValid()) tb->setTextColor(origColor);
    } else {
        postEvent(this, new LogLineEvent(line, color));
        //qDebug("posted event for line: %s...",line.left(10).toLatin1().constData());
    }
}

void App::clearBacklog() {
    if (!backlog.empty()) {
        if (!debugWin) {
            QTimer::singleShot(100, this, SLOT(clearBacklog()));
            return;
        }
        auto bl = backlog; backlog.clear();
        for (auto it = bl.begin(); it != bl.end(); ++it)
            logLine(it->first, it->second);
    }
}

void App::setSBString(const QString &text, int timeout_msecs) {
    if (!debugWin) return;
    debugWin->statusBar()->showMessage(text, timeout_msecs);
}

void App::sysTrayMsg(const QString & msg, int timeout_msecs, bool iserror)
{
    if (sysTray /*&& settings.other.useNotificationCenter*/) {
        sysTray->showMessage(applicationDisplayName(), msg, iserror ? QSystemTrayIcon::Critical : QSystemTrayIcon::Information, timeout_msecs);
    } else {
        if (iserror) Error() << msg;
        else Log() << msg;
    }
}

void App::showPrefs()
{
    QDialog d;
    auto ui = new Ui::Preferences;
    ui->setupUi(&d);
#ifdef Q_OS_MAC
    d.setWindowTitle("Preferences");
#else
    d.setWindowTitle("Settings");
#endif

    ui->darkModeChk->setChecked(settings.appearance.useDarkStyle);
    connect(ui->darkModeChk, &QCheckBox::clicked, this, [this,&d](bool b) {
        settings.appearance.useDarkStyle = b;
        if (!!b != !!darkMode) {
            QMessageBox::information(&d, "Restart Required", "This change will take effect the next time the app is restarted.");
        }
    });
    d.exec();
    settings.save();
}

void App::about()
{
    QMessageBox::about(/*topLevelWidgets().length() ? topLevelWidgets()[0] :*/nullptr,
                       QString("About ") + applicationName(),
                       QString("%1\n\n%2\n%3")
                       .arg(APPNAME_FULL)
                       .arg("© 2018 Calin Culianu")
                       .arg("License: GPL v3")
                       );
}
