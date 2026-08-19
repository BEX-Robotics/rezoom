#include "singleinstance.h"

#ifdef REZOOM_HAVE_DBUS

#include <QDBusConnection>
#include <QDBusInterface>

static const char *dbusService = "com.bexrobotics.rezoom";

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!bus.isConnected())
        return; // no session bus — run as primary

    if (bus.registerService(QLatin1String(dbusService))) {
        bus.registerObject(QStringLiteral("/"), this, QDBusConnection::ExportScriptableSlots);
        return;
    }

    isPrimary = false;
}

void SingleInstance::forward(const QString &resumeQuery) {
    QDBusInterface iface(QLatin1String(dbusService), QStringLiteral("/"), QString());

    if (resumeQuery.isEmpty())
        iface.call(QStringLiteral("Raise"));
    else
        iface.call(QStringLiteral("Resume"), resumeQuery);
}

#else

SingleInstance::SingleInstance(QObject *parent) : QObject(parent) {
}

void SingleInstance::forward(const QString &) {
}

#endif

void SingleInstance::Raise() {
    emit raiseRequested();
}

void SingleInstance::Resume(const QString &query) {
    emit raiseRequested();
    emit resumeRequested(query);
}
