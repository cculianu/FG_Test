#include "SerialPortWorker.h"
#include "Util.h"
#include <QTextStream>
#include <QStringList>
#include <QSerialPortInfo>
#include <QByteArray>

/* --- Port Settings --- */
QString SerialPortSettings::toString() const
{
    QString ret;
    {
        QTextStream ts(&ret);
        ts << port << ";;;" << baud << ";;;" << flowControl << ";;;" << bits << ";;;" << parity << ";;;" << stopBits;
    }
    return ret;
}

bool SerialPortSettings::fromString(const QString &s)
{
    bool ret = false;
    QStringList sl = s.split(";;;");
    if (sl.length() == 6) {
        bool ok;
        port = sl[0];
        baud = QSerialPort::BaudRate(sl[1].toInt(&ok));
        if (ok) flowControl = QSerialPort::FlowControl(sl[2].toInt(&ok));
        if (ok) bits = QSerialPort::DataBits(sl[3].toInt(&ok));
        if (ok) parity = QSerialPort::Parity(sl[4].toInt(&ok));
        if (ok) stopBits = QSerialPort::StopBits(sl[5].toInt(&ok));
        if (!ok) port = QString();
        ret = ok;
    }
    return ret;
}
/* --- /Port Settings --- */

SerialPortWorker::SerialPortWorker() : WorkerThread()
{
    thr.setObjectName("Serial Port Worker");
}

SerialPortWorker::~SerialPortWorker()
{
    stop(); /// need to explicitly call stop() in order to be able to delete 'sp' below.
    delete sp; sp = nullptr;
}

void SerialPortWorker::applyNewPortSettings(QString psstr)
{
    if (sp) { delete sp; sp = nullptr; }
    bool parseOk;
    const auto [port, baud, flowControl, bits, parity, stopBits] = SerialPortSettings(psstr, &parseOk);
    if (!parseOk) {
        emit portError("Invalid port settings string specified");
        return;
    }
    QSerialPortInfo info(port);
    Debug() << "Applying new port settings:" << info.portName() << " (Manuf: " << info.manufacturer() << ")" << " (Desc: " << info.description() << ") "
            << "/ baud,flow,bits,partity,stopBits = "
            << baud << "," << flowControl << "," << bits << "," << parity << "," << stopBits
            << "...";
    sp = new QSerialPort(info, this);
    sp->setBaudRate(baud);
    sp->setFlowControl(flowControl);
    sp->setDataBits(bits);
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

void SerialPortWorker::onReadyRead()
{
    if (!sp) return;
    if (QByteArray b = sp->readAll(); !b.isEmpty()) {
        QString s(b);
        Debug() << "Received: '" << s << "'";
        emit gotCharacters(s);
    } else {
        Error() << "Empty read!";
    }
    if (sp->error() != QSerialPort::NoError) {
        Error() << "Read error: " << sp->error();
        emit portError(sp->errorString());
    }
}

void SerialPortWorker::sendCharacters(QString s)
{
    if (!sp) return;
    if (!sp->isOpen()) {
        Error() << "Port not open";
        emit portError("Port not open");
        return;
    }
    QByteArray b(s.toUtf8());
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
