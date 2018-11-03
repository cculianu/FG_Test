#ifndef UARTBOX_H
#define UARTBOX_H

#include <QWidget>
#include <QSerialPort>
#include <QMutex>
#include "WorkerThread.h"

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

signals:
    void portSettingsChanged();
    void sendCharacters(QString);

private slots:
    void comboBoxesChanged();
    void gotCharacters(QString chars);
    void portError(QString err);

private:
    Ui::UARTBox *ui;
    void setupComboBoxes();

    class Worker;
    Worker *wrk = nullptr;
    mutable QMutex mut;
};

/* Helper class. Not meant to be constructed outside of UARTBox */
class UARTBox::Worker : public WorkerThread {
    Q_OBJECT
    friend class UARTBox; ///< only UARTBox should be using us
protected:
    explicit Worker(UARTBox *uartBox /* must be non-null */);
    ~Worker() override;

signals:
    void gotCharacters(QString chars);
    void portError(QString errorString);

protected slots:
    void sendCharacters(QString);

private slots:
    void onReadyRead();

private:
    void applyNewPortSettings();

    UARTBox *ub = nullptr;
    QSerialPort *sp = nullptr;
};


#endif // UARTBOX_H
