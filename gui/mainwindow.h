#pragma once
#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>

#include "core/liveregistry.h"
#include "core/notifications.h"
#include "core/sessionstore.h"
#include "core/templates.h"

class QLineEdit;
class QListView;
class QMenu;
class QPushButton;
class QSplitter;
class QStackedWidget;
class ChatListModel;
class ChatView;
class FloatWindow;
class TerminalPane;
struct RemoteFinding;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

    // Select + launch the first chat matching query (id/sid prefix or title
    // substring) — backs the `rezoom --resume <query>` startup flag.
    void resumeByQuery(const QString &query);

protected:
    void closeEvent(QCloseEvent *ev) override;

private slots:
    void onChatSelected();
    void onRegistryUpdated();
    void onPaneTerminated(const QString &chatID);
    void onChildClaude(const QString &chatID, int claudePID);
    void onChildCodex(const QString &chatID, int codexPID);
    void onChildSsh(const QString &chatID, const QStringList &cmdline);
    void onChildTmux(const QString &chatID, const QStringList &cmdline);
    void returnToMain(FloatWindow *w, ChatView *view);
    void reclaimWindow(FloatWindow *w);

private:
    QWidget *buildLeftPanel();
    QWidget *buildRightPanel();
    void buildNewMenu(QPushButton *button);
    ChatView *viewFor(const QString &chatID);
    FloatWindow *floatOf(ChatView *view) const;
    FloatWindow *makeFloatWindow(const QString &name);
    void dropFloat(FloatWindow *w);
    void floatChat(const QString &chatID, FloatWindow *target); // target 0 → new window
    void buildFloatMenu(QMenu *menu, const QString &chatID);
    void refreshView(const QString &chatID);
    void launchChat(const QString &chatID, const QString &commandOverride = QString());
    void pullInLive(const QString &chatID);
    void verifyPull(const QString &chatID, int pid);
    void offerPullRecovery(const QString &chatID, int pid, const QString &why,
                           const QString &fixCommand);
    void closeAttemptPane(const QString &chatID);
    void resumeWhenGone(const QString &chatID, int pid, int triesLeft);
    void selectChat(const QString &chatID);
    void showContextMenu(const QPoint &pos);
    void renameChat(const QString &chatID);
    void editCommand(const QString &chatID);
    void archiveChat(const QString &chatID, bool on);
    void deleteChat(const QString &chatID);
    void popOut(const QString &chatID);
    void raiseExternal(int pid);
    void scanRemote(const QString &chatID);
    void adoptRemoteFindings(const Chat &base, const QList<RemoteFinding> &found);
    void newClaude();
    void newTerminal();
    void newSsh();
    void openAdopt();
    void openSettings();
    void buildShortcuts();
    void addChord(const char *chord, const char *what, void (MainWindow::*slot)());
    void actOnCurrent();  // resume / beam / connect — state-aware
    void renameCurrent();
    void archiveToggleCurrent();
    void floatToggleCurrent();
    void popOutCurrent();
    void closePaneCurrent();
    void stepChat(int delta);
    void nextChat();
    void prevChat();
    void focusSearch();
    void openShortcuts();
    void updateArchivedButton();
    void autoAdoptNew();
    Chat chatFromLive(const LiveEntry &e);
    void fixDerivedTitlesOnce();
    void rememberRunning();  // persist which chats have live panes (crash-safe)
    void resumePrevious();   // relaunch them on startup, staggered
    void preTrustCwds(const QStringList &cwds);
    void saveUiState();
    void restoreUiState();
    int livePaneCount() const;

    SessionStore store;
    Templates templates;
    LiveRegistry registry;
    NotificationWatcher notifications;
    ChatListModel *model = 0;
    QListView *list = 0;
    QLineEdit *search = 0;
    QSplitter *splitter = 0;
    QStackedWidget *stack = 0;
    QWidget *emptyPage = 0;
    QPushButton *archivedBtn = 0;
    QHash<QString, ChatView *> views;
    QHash<QString, TerminalPane *> panes;
    QList<FloatWindow *> floats;
    QSet<QString> unread;
    QHash<QString, QString> lastStatus; // chatID → last seen live status
    QString currentID;
    bool shuttingDown = false;
    int floatCounter = 0;
};
