#include "Prefs.h"
#include "ui_Prefs.h"
#include "Settings.h"
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>

Prefs::Prefs(Settings &settings_in, QWidget *parent)
    : QDialog(parent),
      settings(settings_in), currentlyUsingDarkMode(settings.appearance.useDarkStyle)
{
    ui = new Ui::Preferences;
    ui->setupUi(this);
#ifdef Q_OS_MAC
    setWindowTitle("Preferences");
#else
    setWindowTitle("Settings");
#endif

    ui->darkModeChk->setChecked(settings.appearance.useDarkStyle);
    connect(ui->darkModeChk, &QCheckBox::clicked, this, [this](bool b) {
        settings.appearance.useDarkStyle = b;
        if (!!b != !!currentlyUsingDarkMode) {
            QMessageBox::information(this, "Restart Required", "This change will take effect the next time the app is restarted.");
        }
    });

    ui->destLE->setText(settings.saveDir);
    ui->prefixLE->setText(settings.savePrefix);

    connect(ui->destBut, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Specify Save Directory", settings.saveDir);
        if (QFileInfo fi(dir); fi.exists() && fi.isDir()) {
            ui->destLE->setText(dir);
            settings.saveDir = dir;
        } else {
            QMessageBox::critical(this, "Invalid Directory Specified", "The specified directory does not exist.");
        }
    });

    ui->formatCB->clear();
    for (const auto fmt : Settings::EnabledFormats) {
        ui->formatCB->addItem(Settings::fmt2String(fmt), int(fmt));
        if (settings.format == fmt) ui->formatCB->setCurrentIndex(ui->formatCB->count()-1);
    }
    ui->zipChk->setChecked(settings.zipEmbed);
    auto enableDisableZipChk = [this]() -> Settings::Fmt {
        auto fmt = Settings::Fmt(ui->formatCB->currentData().toInt());
        ui->zipChk->setEnabled(Settings::ZipableFormats.count(fmt));
        return fmt;
    };

    enableDisableZipChk();

    connect(ui->formatCB, QOverload<int>::of(&QComboBox::activated), this, [=]{
        // set format from UI
        settings.format = enableDisableZipChk();
    });
    connect(ui->zipChk, &QCheckBox::clicked, this, [=](bool b){
        settings.zipEmbed = b;
    });
}

Prefs::~Prefs()
{
    delete ui; ui = nullptr;
}

