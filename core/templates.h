#pragma once
#include <QList>
#include <QString>

struct Chat;

// Resumable-command templates + app prefs, stored as a plain conf file:
// ~/.config/rezoom/rezoom.conf   ([templates] group, hand-editable INI).
// Placeholders: {session_id} {cwd} {host} {tmux_session} {entry_command} {title}
class Templates {
public:
    struct Entry {
        QString name;
        QString command;
    };

    Templates(); // seeds defaults on first run

    QList<Entry> all() const;
    QString command(const QString &name) const;
    void replaceAll(const QList<Entry> &entries);

    // Pick override > explicit template > default-by-kind, then expand.
    QString resolveFor(const Chat &c) const;
    QString defaultTemplateFor(const Chat &c) const;
    static QString expand(QString tpl, const Chat &c);

    bool confirmClose() const;
    void setConfirmClose(bool v);

private:
    void seedDefaults();
};
