#pragma once
#include <QList>
#include <QWidget>

class QTabWidget;
class ChatView;

// A floating top-level window hosting a group of chats as tabs — e.g. one
// group per screen. Closing a tab (or the window) returns chats to the main
// window; the layout is persisted across Rezoom restarts.
class FloatWindow : public QWidget {
    Q_OBJECT
public:
    explicit FloatWindow(const QString &name, QWidget *parent = 0);

    QString name() const { return winName; }
    int count() const;
    QList<ChatView *> views() const;

    void addView(ChatView *view, const QString &title);
    void takeView(ChatView *view);
    void setTabTitle(ChatView *view, const QString &title);
    void focusView(ChatView *view);

signals:
    void returnRequested(FloatWindow *self, ChatView *view);
    void closing(FloatWindow *self);

protected:
    void closeEvent(QCloseEvent *ev) override;

private:
    QTabWidget *tabs = 0;
    QString winName;
};
