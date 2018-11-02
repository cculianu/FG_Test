#include "UARTBox.h"
#include "ui_UARTBox.h"
#include "Util.h"
#include "Settings.h"
#include <QSerialPortInfo>
#include <QtSerialPort>
#include <QVector>
#include <map>

namespace  {
    const QVector<QString> flowControlStrings = {
        "None", "Hardware", "Software"
    };
    const std::map<QSerialPort::Parity, QString> parityStrings = {
        { QSerialPort::NoParity, "N" },
        { QSerialPort::EvenParity, "E" },
        { QSerialPort::OddParity, "O" },
        { QSerialPort::SpaceParity, "Sp" },
        { QSerialPort::MarkParity, "Mk" }
    };
    const std::map<QSerialPort::StopBits, QString> stopBitStrings = {
        { QSerialPort::OneStop, "1" },
        { QSerialPort::OneAndHalfStop, "1.5" },
        { QSerialPort::TwoStop, "2" }
    };
}

UARTBox::UARTBox(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::UARTBox)
{
    ui->setupUi(this);
    setupComboBoxes();
}

UARTBox::~UARTBox()
{
    delete ui; ui = nullptr;
}

void UARTBox::setupComboBoxes()
{
    const Settings &settings(Util::settings());

    // portCb
    auto ports = QSerialPortInfo::availablePorts();
    ui->portCb->clear();
    for(auto & pi : ports) {
        ui->portCb->addItem(pi.portName());
        if (pi.portName() == settings.uart.portName)
            ui->portCb->setCurrentIndex(ui->portCb->count()-1);
    }
    if (!ui->portCb->count()) {
        Error() << "No serial ports found!";
        ui->portCb->addItem("ERROR");
    }
    // baudCb
    ui->baudCb->clear();
    auto bauds = QSerialPortInfo::standardBaudRates();
    for(auto & baud : bauds) {
        ui->baudCb->addItem(QString::number(int(baud)), int(baud));
        if (int(baud) == settings.uart.baud)
            ui->baudCb->setCurrentIndex(ui->baudCb->count()-1);
    }
    if (!ui->baudCb->count()) {
        Error() << "No valid baud rates found!";
        ui->baudCb->addItem("ERROR");
    }
    // flowCb
    ui->flowCb->clear();
    for(int i = 0; i < flowControlStrings.count(); ++i) {
        ui->flowCb->addItem(flowControlStrings[i], i);
        if (i == settings.uart.flowControl)
            ui->flowCb->setCurrentIndex(ui->flowCb->count()-1);
    }
    // bpsCb
    ui->bpsCb->clear();
    for (int dataBits = 8; dataBits >= 5; --dataBits) {
        for (const auto & pars : parityStrings) {
            for (const auto & stp : stopBitStrings) {
                const int encoded = ((dataBits&0xff)<<16) | ((pars.first&0xff)<<8) | (stp.first&0xff);
                const QString item (QString::number(dataBits) + "/" + pars.second + "/" + stp.second);
                ui->bpsCb->addItem(item, encoded);
                if (encoded == settings.uart.bpsEncoded)
                    ui->bpsCb->setCurrentIndex(ui->bpsCb->count()-1);
            }
        }
    }
    using Util::Connect;
    const QVector<QComboBox *> cbs = { ui->portCb, ui->baudCb, ui->bpsCb, ui->flowCb };
    for (auto & cb : cbs)
        Connect(cb, SIGNAL(currentIndexChanged(int)), this, SLOT(comboBoxesChanged()));
}

void UARTBox::comboBoxesChanged()
{
    Settings & settings(Util::settings());
    settings.uart.baud = ui->baudCb->currentData().toInt();
    settings.uart.bpsEncoded = ui->bpsCb->currentData().toInt();
    settings.uart.flowControl = ui->flowCb->currentData().toInt();
    settings.uart.portName = ui->portCb->currentText();
    settings.save(Settings::UART);
}
