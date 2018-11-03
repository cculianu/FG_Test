#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Util.h"
#include "App.h"
#include "FakeFrameGenerator.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QToolBar>
#include <QLabel>
#include <QCheckBox>

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

    // mac-specific weirdness adds this stuff to the app-global menu.
    QAction *a = ui->menuWindow->addAction(
#ifdef Q_OS_MAC
                "Preferences..."
#else
                "&Settings"
#endif
                , app(), SLOT(showPrefs()));
#ifndef Q_OS_MAC
    a->setShortcut(QKeySequence(Qt::CTRL+Qt::Key_Comma));
    ui->menuWindow->addSeparator();
#endif
    a->setMenuRole(QAction::PreferencesRole);
    a = ui->menuWindow->addAction(QString("About ")+app()->applicationName(), app(), SLOT(about()));
    a->setMenuRole(QAction::AboutRole);
    a = ui->menuWindow->addAction("About Qt", app(), SLOT(aboutQt()));
    a->setMenuRole(QAction::AboutQtRole);

    setupToolBar();
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

void MainWindow::setupToolBar()
{
    static const auto b2s = [](bool b) -> QString {
        return  (b ? "ON" : "OFF");
    };
    auto tb = ui->toolBar;
    QAction *a;
    tbActs["clock"] = a = tb->addAction("Clock OFF", this, [this](bool b){
        Debug() << "Clock on/off: " <<  b2s(b);
        tbActs["clock"]->setText(QString("Clock %1").arg(b2s(b)));
    });
    a->setCheckable(true);
    tbActs["timing"] = a = tb->addAction("Timing OFF", this, [this](bool b){
        Debug() << "Timing on/off: " <<  b2s(b);
        tbActs["timing"]->setText(QString("Timing %1").arg(b2s(b)));
    });
    a->setCheckable(true);
    tb->addSeparator();
    tb->addWidget(new QLabel("Registers"));
    for (int i = 32; i > 0; --i) {
        QCheckBox *chk;
        tbActs[QString("reg%1").arg(i)] = tb->addWidget(chk = new QCheckBox(QString::number(i)));
        connect(chk, &QCheckBox::toggled, this, [=](bool b){
            Debug() << "Checkbox " << chk->text() << " (" << i << ") toggled " << b2s(b);
        });
    }
}
