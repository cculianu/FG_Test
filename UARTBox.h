#ifndef UARTBOX_H
#define UARTBOX_H

#include <QWidget>

namespace Ui {
    class UARTBox;
}

class UARTBox : public QWidget
{
    Q_OBJECT

public:
    explicit UARTBox(QWidget *parent = nullptr);
    ~UARTBox() override;

private:
    Ui::UARTBox *ui;
};

#endif // UARTBOX_H
