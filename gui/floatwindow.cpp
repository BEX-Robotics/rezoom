#include <QCloseEvent>
#include <QTabWidget>
#include <QVBoxLayout>

#include "chatview.h"
#include "floatwindow.h"

FloatWindow::FloatWindow(const QString &name, QWidget *parent)
    : QWidget(parent, Qt::Window), winName(name) {

    // \xe2\x80\x94 = UTF-8 for "—" (em dash)
    setWindowTitle(QStringLiteral("Rezoom \xe2\x80\x94 ") + name);
    resize(900, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    tabs = new QTabWidget(this);
    tabs->setTabsClosable(true);
    tabs->setDocumentMode(true);
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        auto *view = qobject_cast<ChatView *>(tabs->widget(i));

        if (view)
            emit returnRequested(this, view);
    });
    layout->addWidget(tabs);
}

int FloatWindow::count() const {
    return tabs->count();
}

QList<ChatView *> FloatWindow::views() const {
    QList<ChatView *> out;

    for (int i = 0; i < tabs->count(); ++i) {
        auto *view = qobject_cast<ChatView *>(tabs->widget(i));

        if (view)
            out.append(view);
    }

    return out;
}

void FloatWindow::addView(ChatView *view, const QString &title) {
    tabs->addTab(view, title);
    tabs->setCurrentWidget(view);
}

void FloatWindow::takeView(ChatView *view) {
    const int i = tabs->indexOf(view);

    if (i >= 0)
        tabs->removeTab(i);
}

void FloatWindow::setTabTitle(ChatView *view, const QString &title) {
    const int i = tabs->indexOf(view);

    if (i >= 0)
        tabs->setTabText(i, title);
}

void FloatWindow::focusView(ChatView *view) {
    tabs->setCurrentWidget(view);
    show();
    raise();
    activateWindow();
}

void FloatWindow::closeEvent(QCloseEvent *ev) {
    emit closing(this);
    ev->accept();
}
