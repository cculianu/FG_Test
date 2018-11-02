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

private slots:
    void comboBoxesChanged();

private:
    Ui::UARTBox *ui;
    void setupComboBoxes();
};

#endif // UARTBOX_H
