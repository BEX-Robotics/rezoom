#include <stdio.h>
#include <string.h>

#include <QApplication>
#include <QIcon>

#include "mainwindow.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--version")) {
            printf("rezoom %s\n", REZOOM_VERSION);
            return 0;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("rezoom"));
    QApplication::setApplicationDisplayName(QStringLiteral("Rezoom"));
    QApplication::setDesktopFileName(QStringLiteral("rezoom"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/rezoom.svg")));

    MainWindow window;
    window.show();

    const QStringList args = app.arguments();
    const int i = args.indexOf(QStringLiteral("--resume"));

    if (i >= 0 && i + 1 < args.size())
        window.resumeByQuery(args[i + 1]);

    return app.exec();
}
