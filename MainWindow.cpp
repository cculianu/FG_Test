#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "Util.h"
#include "App.h"
#include "FakeFrameGenerator.h"
#include "Recorder.h"
#include <QMessageBox>
#include <QCloseEvent>
#include <QToolBar>
#include <QLabel>
#include <QCheckBox>
#include <QGridLayout>

namespace {
    static QString b2s(bool b)  { return  (b ? "ON" : "OFF"); }
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    updateStatusMessageThrottled([this]{updateStatusMessage();}),
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

    // testing...
    fgen = new FakeFrameGenerator();
    Connect(fgen, SIGNAL(generatedFrame(const Frame &)), ui->videoWidget, SLOT(updateFrame(const Frame &)));
    connect(ui->videoWidget, &GLVideoWidget::fps, this, [this](double fps) {
        statusStrings[FPS1] = QString("%1 FPS (display)").arg(fps, 7, 'g', 3);
        updateStatusMessageThrottled();
    });
    connect(fgen, &FakeFrameGenerator::fps, this, [this](double fps) {
        statusStrings[FPS2] = QString("%1 FPS (generate)").arg(fps, 7, 'g', 3);
        updateStatusMessageThrottled();
    });
    connect(ui->videoWidget, &GLVideoWidget::displayedFrame, this, [this](quint64 frameNum) {
        statusStrings[FrameNum] = QString("Frame %1").arg(frameNum);
        updateStatusMessageThrottled();
    });
    rec = new Recorder(this);
    connect(rec, &Recorder::wroteFrame, this, [this](quint64 frameNum){
        if (!rec->isRecording()) return; // sometimes events continue to arrive after finishing recording. ignore them.
        statusStrings[FrameNumRec] = QString("Fr.%1 (saved)").arg(frameNum);
        updateStatusMessageThrottled();
    });
    connect(rec, &Recorder::stopped, this, [this](){
        kill_dlg();
        statusStrings[Recording] = "";
        statusStrings[FrameNumRec] = "";
        statusStrings[Dropped] = "";
        tbActs["record"]->setChecked(false);
        updateToolBar();
        updateStatusMessageThrottled();
    });
    connect(rec, &Recorder::started, this, [this](QString fname) {
        kill_dlg();
        statusStrings[Recording] = QString("Saving to '%1'...").arg(fname);
        tbActs["record"]->setChecked(true);
        updateToolBar();
        updateStatusMessageThrottled();
    });
    connect(rec, &Recorder::error, this, [this](QString error){
        QMessageBox::critical(this, "Error", error);
    });
    connect(rec, &Recorder::frameDropped, this, [this](quint64 frame){
        if (!rec->isRecording()) return; // sometimes events continue to arrive after finishing recording. ignore them.
        Q_UNUSED(frame);
        long n = 0;
        if (QStringList sl = statusStrings[Dropped].split(" "); sl.length() > 1) {
            n = sl.first().toLong();
        }
        statusStrings[Dropped] = QString("%1 Dropped").arg(++n);
    });
    connect(fgen, &FakeFrameGenerator::generatedFrame, rec, &Recorder::saveFrame);

    ui->statusBar->setFont(QFont("Fixed"));
}

void MainWindow::updateStatusMessage()
{
    static const QString sep("  -  ");
    const QString s = QStringList(statusStrings.toList()).join(sep).split(sep, QString::SkipEmptyParts).join(sep);
    statusBar()->showMessage(s);
}

MainWindow::~MainWindow()
{
    delete rec; rec = nullptr;
    delete fgen; fgen = nullptr;
    delete ui; ui = nullptr;
}

void MainWindow::closeEvent(QCloseEvent *e) {
    if (!rec || !rec->isRecording() || QMessageBox::question(this,"Confirm Quit", QString("A recording is running.\nAre you sure you wish to quit %1?").arg(qApp->applicationName())) == QMessageBox::Yes) {
        QMainWindow::closeEvent(e);
        e->accept();
        qApp->quit();
        return;
    }
    e->ignore();
}

void MainWindow::kill_dlg()
{
    if (dlg_tmp) { dlg_tmp->deleteLater(); dlg_tmp = nullptr; }
}

void MainWindow::show_dlg(const QString &msg)
{
    static const QString title("Please wait");
    kill_dlg();
    dlg_tmp = new QDialog(this, Qt::Dialog|Qt::MSWindowsFixedSizeDialogHint);
    dlg_tmp->setWindowTitle(title);
    auto gl = new QGridLayout(dlg_tmp);
    dlg_tmp->setLayout(gl);
    auto l = new QLabel(msg, dlg_tmp);
    l->setAlignment(Qt::AlignCenter);
    gl->addWidget(l);
    dlg_tmp->open();
}

void MainWindow::setupToolBar()
{
    auto tb = ui->toolBar;
    QAction *a;

    tbActs["record"] = a = tb->addAction("Recording OFF", this, [this](bool b) {
        if (b && !rec->isRecording()) {
            show_dlg("Recording Starting...");
            QString err = rec->start(Util::settings());
            if (!err.isEmpty()) {
                kill_dlg();
                emit rec->error(err);
            }
        } else if (!b && rec->isRecording()) {
            show_dlg("Please Wait...");
            rec->stop();
        }
        updateToolBar();
    });
    a->setCheckable(true);
    tb->addSeparator();

    tbActs["clock"] = a = tb->addAction("Clock OFF", this, &MainWindow::updateToolBar);
    a->setCheckable(true);
    tbActs["timing"] = a = tb->addAction("Timing OFF", this, &MainWindow::updateToolBar);
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

void MainWindow::updateToolBar()
{
    bool b;
    b = tbActs["clock"]->isChecked();
    tbActs["clock"]->setText(QString("Clock %1").arg(b2s(b)));
    b = tbActs["timing"]->isChecked();
    tbActs["timing"]->setText(QString("Timing %1").arg(b2s(b)));
    b = tbActs["record"]->isChecked() && rec->isRecording();
    tbActs["record"]->setText(QString("Recording %1").arg(b2s(b)));
}
