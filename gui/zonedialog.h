#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

// "Run under another Claude account": pick an existing account zone or name
// a new one. Creating a zone happens only here — multi-account stays out of
// sight until someone asks for it.
class ZoneDialog : public QDialog {
    Q_OBJECT
public:
    // hasHistory: the chat already has a conversation, which can't move.
    ZoneDialog(const QString &currentZone, bool hasHistory, QWidget *parent = 0);

    // The chosen zone name ("" = default account), created if new.
    QString chosenZone() const { return chosen; }

private:
    void accept() override;
    void updateState();

    QComboBox *pick = 0;
    QLineEdit *newName = 0;
    QCheckBox *share = 0;
    QLabel *explain = 0;
    QString current;
    QString chosen;
    bool history = false;
};
