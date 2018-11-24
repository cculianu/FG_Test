#ifndef UARTBOX_H
#define UARTBOX_H

#include <QWidget>
#include <QSerialPort>

class SerialPortWorker;

namespace Ui {
    class UARTBox;
}

class UARTBox : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(QString port READ port FINAL)
    Q_PROPERTY(QSerialPort::BaudRate baud READ baud FINAL)
    Q_PROPERTY(QSerialPort::FlowControl flowControl READ flowControl FINAL)
    Q_PROPERTY(QSerialPort::DataBits bits READ bits FINAL)
    Q_PROPERTY(QSerialPort::Parity parity READ parity FINAL)
    Q_PROPERTY(QSerialPort::StopBits stopBits READ stopBits FINAL)

public:
    explicit UARTBox(QWidget *parent = nullptr);
    ~UARTBox() override;

    QString port() const;                         ///< the current port setting
    QSerialPort::BaudRate baud() const;           ///< the current baud setting
    QSerialPort::FlowControl flowControl() const; ///< the current flow control setting
    QSerialPort::DataBits bits() const;           ///< the current bits setting
    QSerialPort::Parity parity() const;           ///< the current parity setting
    QSerialPort::StopBits stopBits() const;       ///< the current stop bits setting

public slots:
    void sendCharacters(QString);
    void protocol_Write(int CMD_Code, int Value_1, qint32 Value_2); ///< formats a command and sends it using sendCharacters(). Format: "~%02d%05d%06d\r\n"

signals:
    void portSettingsChanged(QString);
    void charactersOut(QString); ///< emitted when characters were sent out through the port
    void charactersIn(QString); ///< emitted when characters arrived in from the port

private slots:
    void comboBoxesChanged();
    void gotCharacters(QString chars);
    void portError(QString err);

private:
    Ui::UARTBox *ui;
    void setupComboBoxes();

    SerialPortWorker *wrk = nullptr;
};

#endif // UARTBOX_H
