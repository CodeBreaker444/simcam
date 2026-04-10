#include "mainwindow.h"
#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("IP Camera Simulator");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("VISNET AI");

    // Use the Fusion style for consistent cross-platform look
    app.setStyle("Fusion");

    // App icon (title bar, taskbar, Alt+Tab)
    app.setWindowIcon(QIcon(":/appicon.svg"));

    MainWindow w;
    w.show();

    return app.exec();
}
