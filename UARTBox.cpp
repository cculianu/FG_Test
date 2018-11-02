#include "UARTBox.h"
#include "ui_UARTBox.h"

UARTBox::UARTBox(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::UARTBox)
{
    ui->setupUi(this);
}

UARTBox::~UARTBox()
{
    delete ui; ui = nullptr;
}
