#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>

#include "zones.h"

static QSettings conf() {
    return QSettings(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
}

QString Zones::defaultDir() {
    const QString override = qEnvironmentVariable("REZOOM_CLAUDE_DIR");
    return override.isEmpty() ? QDir::homePath() + "/.claude" : override;
}

QList<Zones::Zone> Zones::all() {
    QList<Zone> out = {{QString(), defaultDir()}};
    QSettings s = conf();
    s.beginGroup("zones");
    const QStringList names = s.childKeys();

    for (const QString &n : names)
        out.append({n, s.value(n).toString()});

    return out;
}

bool Zones::any() {
    return all().size() > 1;
}

QString Zones::dirFor(const QString &name) {
    if (name.isEmpty())
        return defaultDir();

    for (const Zone &z : all())
        if (z.name == name)
            return z.dir;

    return defaultDir();
}

static void shareSetupInto(const QString &dir) {
    const QString from = Zones::defaultDir();

    for (const char *item : {"settings.json", "CLAUDE.md", "rules", "commands", "agents", "skills"}) {
        const QString src = from + '/' + QLatin1String(item);
        const QString dst = dir + '/' + QLatin1String(item);

        if (QFile::exists(src) && !QFile::exists(dst))
            QFile::link(src, dst);
    }
}

bool Zones::add(const QString &name, bool shareSetup, QString *error) {
    static const QRegularExpression valid("^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$");

    if (!valid.match(name).hasMatch()) {
        *error = QStringLiteral("Use letters, digits, - and _ (up to 32).");
        return false;
    }

    for (const Zone &z : all())
        if (z.name == name)
            return true; // already there — nothing to do

    const QString dir = QDir::homePath() + "/.claude-" + name;

    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("Couldn't create %1.").arg(dir);
        return false;
    }

    if (shareSetup)
        shareSetupInto(dir);

    QSettings s = conf();
    s.setValue("zones/" + name, dir);
    return true;
}

QString Zones::envPrefix(const QString &name) {
    if (name.isEmpty())
        return {};

    QString dir = dirFor(name);
    dir.replace('\'', QLatin1String("'\\''"));
    return QStringLiteral("CLAUDE_CONFIG_DIR='%1' ").arg(dir);
}
