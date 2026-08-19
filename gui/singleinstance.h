#pragma once
#include <QObject>

// One Rezoom per session: the first instance claims a DBus name; later
// launches forward their request (raise, optionally a --resume query) to it
// and exit. Where QtDBus is unavailable (macOS, no session bus), every
// instance just runs as primary.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = 0);

    bool primary() const { return isPrimary; }

    void forward(const QString &resumeQuery); // ask the primary to act

public slots:
    Q_SCRIPTABLE void Raise();
    Q_SCRIPTABLE void Resume(const QString &query);

signals:
    void raiseRequested();
    void resumeRequested(const QString &query);

private:
    bool isPrimary = true;
};
