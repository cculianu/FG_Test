#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Util.h"
#include <QMessageBox>

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
}

MainWindow::~MainWindow()
{
    delete ui;
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
