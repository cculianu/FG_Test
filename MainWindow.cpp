#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Util.h"
#include "App.h"
#include <QMessageBox>
#include <QCloseEvent>
#include "FakeFrameGenerator.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow), fgen(nullptr)
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
    connect(fgen, &FakeFrameGenerator::fps, this, [this](double fps) {
        statusBar()->showMessage(QString("FPS %1").arg(fps));
    });
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
