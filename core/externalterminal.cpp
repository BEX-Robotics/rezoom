#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include "externalterminal.h"

namespace ExternalTerminal {

#ifdef Q_OS_MACOS

static QString shellQuote(QString s) {
    s.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

static QString appleScriptQuote(QString s) {
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return s;
}

void launch(const QString &cwd, const QString &command) {
    QStringList pieces;

    if (!cwd.isEmpty())
        pieces << QStringLiteral("cd %1").arg(shellQuote(cwd));

    if (!command.trimmed().isEmpty())
        pieces << command.trimmed();

    const QString line = pieces.join(QStringLiteral(" && "));
    const QString script =
        QStringLiteral("tell application \"Terminal\" to do script \"%1\"")
            .arg(appleScriptQuote(line));

    QProcess::startDetached(QStringLiteral("osascript"),
                            {QStringLiteral("-e"), script, QStringLiteral("-e"),
                             QStringLiteral("tell application \"Terminal\" to activate")});
}

#else

// PATH order lets a user's konsole wrapper (e.g. ~/.local/bin) win.
static QString konsoleBinary() {
    const QString found = QStandardPaths::findExecutable(QStringLiteral("konsole"));
    return found.isEmpty() ? QStringLiteral("konsole") : found;
}

void launch(const QString &cwd, const QString &command) {
    QStringList args;

    if (!cwd.isEmpty())
        args << "--workdir" << cwd;

    if (!command.trimmed().isEmpty())
        args << "-e" << userShell() << "-ic" << command;

    QProcess::startDetached(konsoleBinary(), args);
}

#endif

QString userShell() {
    const QString shell = qEnvironmentVariable("SHELL");
    return (!shell.isEmpty() && QFile::exists(shell)) ? shell : QStringLiteral("/bin/sh");
}

} // namespace ExternalTerminal
