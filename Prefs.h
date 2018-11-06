#ifndef PREFS_H
#define PREFS_H

#include <QDialog>

namespace Ui {
    class Preferences;
}
struct Settings;

class Prefs : public QDialog
{
    Q_OBJECT
public:
    explicit Prefs(Settings & settings, QWidget *parent = nullptr);
    ~Prefs() override;

signals:

public slots:

private:
    Ui::Preferences *ui = nullptr;
    Settings & settings;
    const bool currentlyUsingDarkMode;

};

#endif // PREFS_H
