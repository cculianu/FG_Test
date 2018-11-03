#include "UARTBox.h"
#include "ui_UARTBox.h"
#include "Util.h"
#include "Settings.h"
#include <QSerialPortInfo>
#include <QtSerialPort>
#include <QVector>
#include <map>
#include <QLineEdit>
#include <QMutexLocker>

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
        emit sendLine(line);
    });
    wrk = new Worker(this);
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
    mut.lock();
    settings.uart.baud = ui->baudCb->currentData().toInt();
    settings.uart.bpsEncoded = ui->bpsCb->currentData().toInt();
    settings.uart.flowControl = ui->flowCb->currentData().toInt();
    settings.uart.portName = ui->portCb->currentText();
    settings.save(Settings::UART);
    mut.unlock();
    emit portSettingsChanged();
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

QString UARTBox::port() const { QMutexLocker ml(&mut); return Util::settings().uart.portName; }
QSerialPort::BaudRate UARTBox::baud() const { QMutexLocker ml(&mut); return QSerialPort::BaudRate(Util::settings().uart.baud); }
QSerialPort::FlowControl UARTBox::flowControl() const { QMutexLocker ml(&mut); return QSerialPort::FlowControl(Util::settings().uart.flowControl); }
QSerialPort::DataBits UARTBox::bits() const { QMutexLocker ml(&mut); return QSerialPort::DataBits((Util::settings().uart.bpsEncoded>>16)&0xff); }
QSerialPort::Parity UARTBox::parity() const { QMutexLocker ml(&mut); return QSerialPort::Parity((Util::settings().uart.bpsEncoded>>8)&0xff); }
QSerialPort::StopBits UARTBox::stopBits() const { QMutexLocker ml(&mut); return QSerialPort::StopBits(Util::settings().uart.bpsEncoded&0xff); }


/* --- UARTBox::Worker --- */
UARTBox::Worker::Worker(UARTBox *ub)
    : ub(ub)
{
    thr.setObjectName("UART Worker");

    connect(this, SIGNAL(gotCharacters(QString)), ub, SLOT(gotCharacters(QString)));
    connect(ub, &UARTBox::portSettingsChanged, this, &Worker::applyNewPortSettings);
    connect(ub, SIGNAL(sendLine(QString)), this, SLOT(sendLine(QString)));
    connect(this, SIGNAL(portError(QString)), ub, SLOT(portError(QString)));
    QTimer::singleShot(300, this, &Worker::applyNewPortSettings);
}

UARTBox::Worker::~Worker() {
    stop(); /// need to explicitly call stop() in order to be able to delete 'sp' below.
    delete sp; sp = nullptr;
}

void UARTBox::Worker::applyNewPortSettings()
{
    if (sp) { delete sp; sp = nullptr; }
    QSerialPortInfo info(ub->port());
    const auto baud = ub->baud();
    const auto flowControl = ub->flowControl();
    const auto bits = ub->bits();
    const auto parity = ub->parity();
    const auto stopBits = ub->stopBits();
    Debug() << "Applying new port settings:" << info.portName() << " (Manuf: " << info.manufacturer() << ")" << " (Desc: " << info.description() << ") "
            << "/ baud,flow,bits,partity,stopBits = "
            << baud << "," << flowControl << "," << bits << "," << parity << "," << stopBits
            << "...";
    sp = new QSerialPort(info, this);
    sp->setBaudRate(baud);
    sp->setDataBits(bits);
    sp->setFlowControl(flowControl);
    sp->setParity(parity);
    sp->setStopBits(stopBits);
    if (!sp->open(QIODevice::ReadWrite)) {
        const auto error = sp->error();
        Error() << "Could not open port, code: " << error;
        emit portError(QString("Could not open port: ") + sp->errorString());
    } else {
        // no error
        connect(sp, SIGNAL(readyRead()), this, SLOT(onReadyRead()));
    }
}

void UARTBox::Worker::onReadyRead()
{
    if (!sp) return;
    if (QByteArray b = sp->readAll(); !b.isEmpty()) {
        Debug() << "Received: '" << QString(b) << "'";
        emit gotCharacters(QString(b));
    } else {
        Error() << "Empty read!";
    }
    if (sp->error() != QSerialPort::NoError) {
        Error() << "Read error: " << sp->error();
    }
}

void UARTBox::Worker::sendLine(QString line)
{
    if (!sp) return;
    if (!sp->isOpen()) {
        Error() << "Port not open";
        emit portError("Port not open");
        return;
    }
    QByteArray b(line.toUtf8());
    if (b.isEmpty()) return;

    if (const auto n = sp->write(b); n != b.length()) {
        Error() << "Write returned " << n << ", expected " << b.length();
        emit portError("Short write");
    } else {
        Debug() << "Sending: '" << QString(b) << "'";
        if (!sp->waitForBytesWritten(1000)) {
            Error() << "waitForBytesWritten timeout";
            //emit portError("Write timeout");
        } else {
            Debug() << "waitForBytesWritten ok";
        }
    }
    if (sp->error() != QSerialPort::NoError) {
        Error() << "Write error: " << sp->error() << ", " << sp->errorString();
        emit portError(sp->errorString());
    }
}
