#include "konsoletitles.h"

#ifdef REZOOM_HAVE_DBUS

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

QHash<int, QString> KonsoleTitles::byKonsolePid() {
    QHash<int, QString> out;
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.isConnected())
        return out;

    const QStringList names = bus.interface()->registeredServiceNames();

    for (const QString &name : names) {
        if (!name.startsWith(QLatin1String("org.kde.konsole-")))
            continue;

        bool ok = false;
        const int pid = name.mid(name.lastIndexOf('-') + 1).toInt(&ok);

        if (!ok)
            continue;

        // Their konsoles are one session per window (--separate); role 1 =
        // the display title. Short timeout — a hung konsole must not stall us.
        QDBusMessage call = QDBusMessage::createMethodCall(
            name, QStringLiteral("/Sessions/1"), QStringLiteral("org.kde.konsole.Session"),
            QStringLiteral("title"));
        call << 1;
        const QDBusMessage reply = bus.call(call, QDBus::Block, 120);

        if (reply.type() == QDBusMessage::ReplyMessage)
            out.insert(pid, reply.arguments().value(0).toString());
    }

    return out;
}

int KonsoleTitles::sessionCount(int konsolePid) {
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.konsole-%1").arg(konsolePid), QStringLiteral("/Sessions"),
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 300);

    if (reply.type() != QDBusMessage::ReplyMessage)
        return -1;

    return reply.arguments().value(0).toString().count(QLatin1String("<node name="));
}

#else

QHash<int, QString> KonsoleTitles::byKonsolePid() {
    return {};
}

int KonsoleTitles::sessionCount(int) {
    return -1;
}

#endif

QString KonsoleTitles::claudeStateFromTitle(const QString &title) {
    if (title.size() < 3 || title.at(1) != ' ')
        return {};

    const QChar g = title.at(0);

    if (g == QChar(0x2733)) // ✳
        return QStringLiteral("idle");

    if (g.unicode() >= 0x25D0 && g.unicode() <= 0x25D3) // ◐ ◑ ◒ ◓
        return QStringLiteral("busy");

    return {};
}

QString KonsoleTitles::stripStatusGlyph(QString title) {
    // A leading spinner glyph is one symbol char + space; letters (Hebrew
    // included) and digits are real content and stay.
    if (title.size() > 2 && title.at(1) == ' ' && !title.at(0).isLetterOrNumber())
        title = title.mid(2);

    return title.trimmed();
}
