#include <QGridLayout>
#include <QLabel>

#include "shortcutsdialog.h"

struct ShortcutRow {
    const char *chord; // empty = section header in `what`
    const char *what;
};

// Single source for the overlay; MainWindow assigns the real QActions.
static const ShortcutRow rows[] = {
    {"", "Create"},
    {"Ctrl+Shift+N", "New Claude session"},
    {"Ctrl+Shift+T", "New terminal"},
    {"Ctrl+Shift+S", "New SSH session"},
    {"Ctrl+Shift+A", "Adopt sessions"},
    {"", "Navigate"},
    {"Ctrl+Shift+F", "Search chats"},
    {"Ctrl+PgUp / Ctrl+PgDn", "Previous / next chat"},
    {"", "Current chat"},
    {"Ctrl+Shift+Return", "Resume / beam in / connect"},
    {"Ctrl+Shift+R", "Rename"},
    {"Ctrl+Shift+E", "Archive / unarchive"},
    {"Ctrl+Shift+D", "Float out / pull back"},
    {"Ctrl+Shift+O", "Pop out to Konsole"},
    {"Ctrl+Shift+W", "Close embedded pane (stays resumable)"},
    {"", "App"},
    {"Ctrl+Shift+P", "Settings"},
    {"Ctrl+Shift+/", "This overlay"},
};

ShortcutsDialog::ShortcutsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("Keyboard shortcuts"));

    auto *grid = new QGridLayout(this);
    grid->setHorizontalSpacing(24);
    int r = 0;

    for (const ShortcutRow &row : rows) {
        if (!*row.chord) {
            auto *header = new QLabel(QStringLiteral("<b>%1</b>").arg(tr(row.what)), this);

            if (r)
                grid->setRowMinimumHeight(r++, 12);

            grid->addWidget(header, r++, 0, 1, 2);
            continue;
        }

        auto *chord = new QLabel(QLatin1String(row.chord), this);
        chord->setStyleSheet("font-family: monospace; color: palette(link);");
        grid->addWidget(chord, r, 0);
        grid->addWidget(new QLabel(tr(row.what), this), r++, 1);
    }
}
