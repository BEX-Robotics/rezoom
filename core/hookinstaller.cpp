#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include "hookinstaller.h"

static const char *hookName = "rezoom-notify-hook";

static QString settingsPath() {
    const QString override = qEnvironmentVariable("REZOOM_CLAUDE_DIR");
    return (override.isEmpty() ? QDir::homePath() + "/.claude" : override) + "/settings.json";
}

static QJsonObject readSettings(bool *ok) {
    QFile f(settingsPath());
    *ok = true;

    if (!f.exists())
        return {}; // no settings yet — we create the file

    if (!f.open(QIODevice::ReadOnly)) {
        *ok = false;
        return {};
    }

    QJsonParseError err = {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    *ok = err.error == QJsonParseError::NoError && doc.isObject();

    return doc.object();
}

static bool writeSettings(const QJsonObject &root, QString *error) {
    const QString path = settingsPath();
    QDir().mkpath(QFileInfo(path).path());

    if (QFile::exists(path)) {
        QFile::remove(path + ".bak-rezoom");
        QFile::copy(path, path + ".bak-rezoom");
    }

    QSaveFile out(path);

    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }

    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return out.commit();
}

static bool entryIsOurs(const QJsonValue &entry) {
    for (const QJsonValue &h : entry.toObject()["hooks"].toArray())
        if (h.toObject()["command"].toString().contains(QLatin1String(hookName)))
            return true;

    return false;
}

QString HookInstaller::hookPath() {
    return QStandardPaths::findExecutable(QLatin1String(hookName));
}

bool HookInstaller::installed() {
    bool ok = false;
    const QJsonArray entries = readSettings(&ok)["hooks"].toObject()["Notification"].toArray();

    for (const QJsonValue &e : entries)
        if (entryIsOurs(e))
            return true;

    return false;
}

bool HookInstaller::install(QString *error) {
    bool ok = false;
    QJsonObject root = readSettings(&ok);

    if (!ok) {
        *error = QStringLiteral("~/.claude/settings.json is not valid JSON; left untouched");
        return false;
    }

    if (installed())
        return true;

    QJsonObject hook;
    hook["type"] = QStringLiteral("command");
    hook["command"] = hookPath();
    hook["async"] = true;
    QJsonObject entry;
    entry["hooks"] = QJsonArray{hook};

    QJsonObject hooks = root["hooks"].toObject();
    QJsonArray notification = hooks["Notification"].toArray();
    notification.append(entry);
    hooks["Notification"] = notification;
    root["hooks"] = hooks;

    return writeSettings(root, error);
}

bool HookInstaller::uninstall(QString *error) {
    bool ok = false;
    QJsonObject root = readSettings(&ok);

    if (!ok) {
        *error = QStringLiteral("~/.claude/settings.json is not valid JSON; left untouched");
        return false;
    }

    QJsonObject hooks = root["hooks"].toObject();
    QJsonArray kept;

    for (const QJsonValue &e : hooks["Notification"].toArray())
        if (!entryIsOurs(e))
            kept.append(e);

    if (kept.isEmpty())
        hooks.remove("Notification");
    else
        hooks["Notification"] = kept;

    root["hooks"] = hooks;
    return writeSettings(root, error);
}
