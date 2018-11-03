#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Util.h"
#include "App.h"
#include <QMessageBox>
#include <QCloseEvent>
#include "FakeFrameGenerator.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/Img/app_icon.png"));
    using Util::Connect; using Util::app;
    Connect(ui->actionDebug_Console, SIGNAL(triggered()), app(), SLOT(showRaiseDebugWin()));
#ifdef Q_OS_WIN
    setWindowTitle("Main Window");
#else
    setWindowTitle(qApp->applicationDisplayName() + " - Main Window");
#endif

    // testing...
    fgen = new FakeFrameGenerator();
    Connect(fgen, SIGNAL(generatedFrame(QImage)), ui->videoWidget, SLOT(updateFrame(QImage)));
    //connect(fgen, &FakeFrameGenerator::fps, this, [this](double fps) {
    connect(ui->videoWidget, &GLVideoWidget::fps, this, [this](double fps) {
        statusBar()->showMessage(QString("FPS %1").arg(fps));
    });

#ifndef Q_OS_MAC
    ui->menuWindow->addSeparator();
#endif
    // mac-specific weirdness adds this stuff to the app-global menu.
    QAction *a = ui->menuWindow->addAction(
#ifdef Q_OS_MAC
                "Preferences..."
#else
                "Settings..."
#endif
                , app(), SLOT(showPrefs()));
    a->setMenuRole(QAction::PreferencesRole);
    a = ui->menuWindow->addAction(QString("About ")+app()->applicationName(), app(), SLOT(about()));
    a->setMenuRole(QAction::AboutRole);
    a = ui->menuWindow->addAction("About Qt", app(), SLOT(aboutQt()));
    a->setMenuRole(QAction::AboutQtRole);
}

MainWindow::~MainWindow()
{
    delete fgen; fgen = nullptr;
    delete ui; ui = nullptr;
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (QMessageBox::question(this,"Confirm Quit", QString("Are you sure you wish to quit %1?").arg(qApp->applicationName())) == QMessageBox::Yes) {
        QMainWindow::closeEvent(e);
        e->accept();
        qApp->quit();
        return;
    }
    e->ignore();
}
