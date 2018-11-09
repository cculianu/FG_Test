#include "UARTBox.h"
#include "ui_UARTBox.h"
#include "Util.h"
#include "Settings.h"
#include "SerialPortWorker.h"
#include <QSerialPortInfo>
#include <QTimer>
#include <QVector>
#include <map>
#include <chrono>
#include <QLineEdit>

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

    connect(ui->le, &QLineEdit::returnPressed, [this]{
        QString line = ui->le->text().trimmed() + "\r\n"; //TODO: figure out if we need the \r\n?
        ui->tb->insertPlainText(line);
        ui->tb->ensureCursorVisible();
        ui->le->clear();
        emit sendCharacters(line);
    });

    wrk = new SerialPortWorker;
    connect(wrk, SIGNAL(gotCharacters(QString)), this, SLOT(gotCharacters(QString)));
    connect(this, SIGNAL(portSettingsChanged(QString)), wrk, SLOT(applyNewPortSettings(QString)));
    connect(this, SIGNAL(sendCharacters(QString)), wrk, SLOT(sendCharacters(QString)));
    connect(wrk, SIGNAL(portError(QString)), this, SLOT(portError(QString)));
    using namespace std::chrono;
    QTimer::singleShot(300ms, this, SLOT(comboBoxesChanged())); ///< open port based on defaults we had from settings, etc
}

UARTBox::~UARTBox()
{
    delete wrk; wrk = nullptr;
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
        for (const auto & [p_val, p_str] : parityStrings) {
            for (const auto & [s_val, s_str] : stopBitStrings) {
                const int encoded = ((dataBits&0xff)<<16) | ((p_val&0xff)<<8) | (s_val&0xff);
                const QString item (QString::number(dataBits) + "/" + p_str + "/" + s_str);
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
    emit portSettingsChanged(SerialPortSettings(port(), baud(), flowControl(), bits(), parity(), stopBits()).toString());
}

void UARTBox::gotCharacters(QString chars)
{
    ui->tb->insertPlainText(chars);
    ui->tb->ensureCursorVisible();
    //Debug() << "UART Recv: '" << chars << "'";
}

void UARTBox::portError(QString err)
{
    ui->tb->insertHtml(QString("<font color=#ff6464><b>") + "PORT ERROR " + "</b></font>" + err.trimmed());
    ui->tb->insertPlainText("\n");
    ui->tb->ensureCursorVisible();
}

QString UARTBox::port() const { return Util::settings().uart.portName; }
QSerialPort::BaudRate UARTBox::baud() const { return QSerialPort::BaudRate(Util::settings().uart.baud); }
QSerialPort::FlowControl UARTBox::flowControl() const { return QSerialPort::FlowControl(Util::settings().uart.flowControl); }
QSerialPort::DataBits UARTBox::bits() const { return QSerialPort::DataBits((Util::settings().uart.bpsEncoded>>16)&0xff); }
QSerialPort::Parity UARTBox::parity() const { return QSerialPort::Parity((Util::settings().uart.bpsEncoded>>8)&0xff); }
QSerialPort::StopBits UARTBox::stopBits() const { return QSerialPort::StopBits(Util::settings().uart.bpsEncoded&0xff); }
