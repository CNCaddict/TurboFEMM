// FEMM Qt 6 GUI — Application entry point
#include <QApplication>
#include <QSettings>
#include <QFileInfo>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("FEMM");
    app.setApplicationVersion("4.2");
    app.setOrganizationName("FEMM");

    MainWindow w;
    w.show();

    // If a file was passed on the command line, open it
    if (argc > 1) {
        w.openFile(QString::fromLocal8Bit(argv[1]));
    } else {
        // Reopen the last used file if it still exists
        QSettings settings;
        QString lastFile = settings.value("file/lastOpenedFile").toString();
        if (!lastFile.isEmpty() && QFileInfo::exists(lastFile))
            w.openFile(lastFile);
    }

    return app.exec();
}
