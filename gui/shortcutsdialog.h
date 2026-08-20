#pragma once
#include <QDialog>

// Ctrl+Shift+/ overlay: every keyboard shortcut in one glance.
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget *parent = 0);
};
