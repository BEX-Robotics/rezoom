#include <QDateTime>
#include <QRegularExpression>
#include <QUuid>

#include "chat.h"

static const char *tints[] = {"tint-neutral", "tint-red", "tint-green",
                              "tint-blue", "tint-purple", "tint-orange"};

Chat Chat::create(const QString &kind) {
    Chat c = {};
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.kind = kind;
    c.createdAt = QDateTime::currentMSecsSinceEpoch();
    c.lastActiveAt = c.createdAt;
    c.tint = randomTint(c.id);

    return c;
}

QString Chat::randomTint(const QString &seed) {
    return QLatin1String(tints[qHash(seed) % 6]);
}

QString Chat::tintColorHex(const QString &tint) {
    if (tint == "tint-red")
        return "#e05a4e";

    if (tint == "tint-green")
        return "#3fa34d";

    if (tint == "tint-blue")
        return "#3a7bd5";

    if (tint == "tint-purple")
        return "#8e5bb8";

    if (tint == "tint-orange")
        return "#dd7f2b";

    return "#6b7680"; // tint-neutral / unknown
}

QJsonObject Chat::toJson() const {
    QJsonObject o;
    o["id"] = id;
    o["title"] = title;
    o["kind"] = kind;
    o["claudeSessionId"] = claudeSessionID;
    o["cwd"] = cwd;
    o["host"] = host;
    o["entryCommand"] = entryCommand;
    o["templateName"] = templateName;
    o["commandOverride"] = commandOverride;
    o["tmuxSession"] = tmuxSession;
    o["preview"] = preview;
    o["tint"] = tint;
    o["archived"] = archived;
    o["createdAt"] = createdAt;
    o["lastActiveAt"] = lastActiveAt;

    return o;
}

Chat Chat::fromJson(const QJsonObject &o) {
    Chat c = {};
    c.id = o["id"].toString();
    c.title = o["title"].toString();
    c.kind = o["kind"].toString();
    c.claudeSessionID = o["claudeSessionId"].toString();
    c.cwd = o["cwd"].toString();
    c.host = o["host"].toString();
    c.entryCommand = o["entryCommand"].toString();
    c.templateName = o["templateName"].toString();
    c.commandOverride = o["commandOverride"].toString();
    c.tmuxSession = o["tmuxSession"].toString();
    c.preview = o["preview"].toString();
    c.tint = o["tint"].toString();
    c.archived = o["archived"].toBool();
    c.createdAt = static_cast<qint64>(o["createdAt"].toDouble());
    c.lastActiveAt = static_cast<qint64>(o["lastActiveAt"].toDouble());

    return c;
}

QString Chat::monogram() const {
    QString base = title.trimmed();

    if (base.isEmpty())
        base = kind;

    // First letters of the first two words, or a lone first char.
    const QStringList words = base.split(QRegularExpression("[\\s/_-]+"), Qt::SkipEmptyParts);
    QString m;

    for (const QString &w : words) {
        m += w.at(0).toUpper();

        if (m.size() == 2)
            break;
    }

    if (m.isEmpty())
        m = "?";

    return m;
}
