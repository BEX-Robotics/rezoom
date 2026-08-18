#include <QSettings>

#include "chat.h"
#include "templates.h"

// → ~/.config/rezoom/rezoom.conf (INI on Linux). Relies on C++17 guaranteed
// copy elision — QSettings itself is not copyable.
static QSettings confFile() {
    return QSettings(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
}

Templates::Templates() {
    seedDefaults();
}

void Templates::seedDefaults() {
    QSettings s = confFile();

    if (s.value("general/seeded").toBool())
        return;

    s.beginGroup("templates");
    s.setValue("claude-new", "claude");
    s.setValue("claude-resume", "claude --resume {session_id}");
    s.setValue("ssh-entry", "{entry_command}");
    s.setValue("ssh-claude", "ssh -t {host} 'cd {cwd} && claude --resume {session_id}'");
    s.setValue("ssh-tmux", "ssh -t {host} tmux attach -t {tmux_session}");
    s.setValue("tmux-attach", "tmux attach -t {tmux_session}");
    s.setValue("shell", "");
    s.endGroup();
    s.setValue("general/seeded", true);
}

QList<Templates::Entry> Templates::all() const {
    QSettings s = confFile();
    s.beginGroup("templates");
    QList<Entry> out;
    const QStringList keys = s.childKeys();

    for (const QString &k : keys)
        out.append({k, s.value(k).toString()});

    return out;
}

QString Templates::command(const QString &name) const {
    QSettings s = confFile();

    return s.value("templates/" + name).toString();
}

void Templates::replaceAll(const QList<Entry> &entries) {
    QSettings s = confFile();
    s.remove("templates");
    s.beginGroup("templates");

    for (const Entry &e : entries)
        if (!e.name.trimmed().isEmpty())
            s.setValue(e.name.trimmed(), e.command);

    s.endGroup();
}

QString Templates::defaultTemplateFor(const Chat &c) const {
    if (c.kind == "claude")
        return c.claudeSessionID.isEmpty() ? "claude-new" : "claude-resume";

    if (c.kind == "ssh") {
        if (!c.claudeSessionID.isEmpty() && !c.host.isEmpty())
            return "ssh-claude";

        return "ssh-entry";
    }

    if (c.kind == "tmux")
        return c.host.isEmpty() ? "tmux-attach" : "ssh-tmux";

    return "shell";
}

QString Templates::resolveFor(const Chat &c) const {
    if (!c.commandOverride.trimmed().isEmpty())
        return expand(c.commandOverride, c);

    QString name = c.templateName;

    if (name.isEmpty())
        name = defaultTemplateFor(c);

    return expand(command(name), c);
}

QString Templates::expand(QString tpl, const Chat &c) {
    tpl.replace("{session_id}", c.claudeSessionID);
    tpl.replace("{cwd}", c.cwd);
    tpl.replace("{host}", c.host);
    tpl.replace("{tmux_session}", c.tmuxSession);
    tpl.replace("{entry_command}", c.entryCommand);
    tpl.replace("{title}", c.title);

    return tpl.trimmed();
}

bool Templates::confirmClose() const {
    QSettings s = confFile();

    return s.value("general/confirm_close", true).toBool();
}

void Templates::setConfirmClose(bool v) {
    QSettings s = confFile();
    s.setValue("general/confirm_close", v);
}
