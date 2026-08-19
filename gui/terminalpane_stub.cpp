// Fallback TerminalPane for builds without KF6 (e.g. macOS): no konsolepart to
// embed, so the pane always reports invalid and MainWindow falls back to
// opening the chat in an external terminal window.

#include <QVBoxLayout>

#include "terminalpane.h"

TerminalPane::TerminalPane(const QString &chatID, const QString &profile, QWidget *parent)
    : QWidget(parent), id(chatID) {
    Q_UNUSED(profile);
    error = tr("embedded Konsole terminals are not available on this platform");
}

void TerminalPane::emitTerminated() {
    if (terminatedEmitted)
        return;

    terminatedEmitted = true;
    emit terminated(id);
}

bool TerminalPane::shellAlive() const {
    return false;
}

void TerminalPane::runCommand(const QString &cwd, const QString &command) {
    Q_UNUSED(cwd);
    Q_UNUSED(command);
}

void TerminalPane::showEvent(QShowEvent *ev) {
    QWidget::showEvent(ev);
}

void TerminalPane::poll() {
}
