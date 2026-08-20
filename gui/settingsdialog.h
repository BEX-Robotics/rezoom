#pragma once
#include <QDialog>

class QCheckBox;
class QTableWidget;
class QVBoxLayout;
class Templates;

// Resumable-command template editor + app preferences.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Templates *templates, QWidget *parent = 0);

private:
    void accept() override;
    void addPrefChecks(QVBoxLayout *layout);

    Templates *templates = 0;
    QTableWidget *table = 0;
    QCheckBox *confirmClose = 0;
    QCheckBox *autoAdopt = 0;
    QCheckBox *resumeOnStart = 0;
    QCheckBox *autoStart = 0;
};
