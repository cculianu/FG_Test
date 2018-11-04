#ifndef SERIALPORTWORKER_H
#define SERIALPORTWORKER_H

#include "WorkerThread.h"
#include <QSerialPort>

struct SerialPortSettings {
    QString                   port; /* default null */
    QSerialPort::BaudRate     baud        = QSerialPort::Baud115200;
    QSerialPort::FlowControl  flowControl = QSerialPort::NoFlowControl;
    QSerialPort::DataBits     bits        = QSerialPort::Data8;
    QSerialPort::Parity       parity      = QSerialPort::NoParity;
    QSerialPort::StopBits     stopBits    = QSerialPort::OneStop;

    bool isNull() const { return port.isEmpty(); }

    QString toString() const;
    bool fromString(const QString &);

    SerialPortSettings() {}
    SerialPortSettings(
            const QString & port,
            QSerialPort::BaudRate baud,
            QSerialPort::FlowControl flowControl,
            QSerialPort::DataBits bits,
            QSerialPort::Parity parity,
            QSerialPort::StopBits stopBits
            ) : port(port), baud(baud), flowControl(flowControl), bits(bits), parity(parity), stopBits(stopBits) {}
    SerialPortSettings(const QString &s, bool *ok = nullptr) { bool b = fromString(s);  if (ok) *ok = b; }
};

class SerialPortWorker : public WorkerThread
{
    Q_OBJECT
public:
    SerialPortWorker(); ///< always constructed with parent=nullptr because it calls moveToThread on construction.
    ~SerialPortWorker() override;

signals:
    void gotCharacters(QString chars); ///< emitted when characters have arrived from the port
    void portError(QString errorString); ///< emitted when there is some error from the underlying API

public slots:
    void sendCharacters(QString); ///< call this to send characters
    /// Call the below at least once after construction to create/open the port. May be called anytime after to change the port and/or port settings.
    void applyNewPortSettings(QString portSettingsString /* MUST be a valid port settings string obtained from PortSettings::toString()! */);

private slots:
    void onReadyRead();

private:
    QSerialPort *sp = nullptr;
};

#endif // SERIALPORTWORKER_H
