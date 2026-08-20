#include <stdio.h>
#include <string.h>

#include <QApplication>
#include <QIcon>

#include "mainwindow.h"
#include "singleinstance.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--version")) {
            printf("rezoom %s\n", REZOOM_VERSION);
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("rezoom"));
    QApplication::setDesktopFileName(QStringLiteral("rezoom"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/rezoom.svg")));

    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--resume"));
    const QString resumeQuery = (i >= 0 && i + 1 < args.size()) ? args[i + 1] : QString();

    SingleInstance guard;

    if (!guard.primary()) {
        guard.forward(resumeQuery);
        return 0;
    }

    MainWindow window;
    QObject::connect(&guard, &SingleInstance::raiseRequested, &window, [&window] {
        window.show();
        window.raise();
        window.activateWindow();
    });
    QObject::connect(&guard, &SingleInstance::resumeRequested, &window,
                     [&window](const QString &q) { window.resumeByQuery(q); });
    window.show();

    if (!resumeQuery.isEmpty())
        window.resumeByQuery(resumeQuery);

    return app.exec();
}
