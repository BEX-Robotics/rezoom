#include <QDir>
#include <QFile>
#include <QProcess>

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

// The user's tinted-profile wrapper if present, plain konsole otherwise.
static QString konsoleBinary() {
    const QString wrapper = QDir::homePath() + "/.local/bin/konsole";

    if (QFile::exists(wrapper))
        return wrapper;

    return QStringLiteral("konsole");
}

void launch(const QString &cwd, const QString &command) {
    QStringList args;

    if (!cwd.isEmpty())
        args << "--workdir" << cwd;

    if (!command.trimmed().isEmpty())
        args << "-e" << "zsh" << "-ic" << command;

    QProcess::startDetached(konsoleBinary(), args);
}

#endif

} // namespace ExternalTerminal
