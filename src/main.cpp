#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NativeTools"));
    QCoreApplication::setApplicationName(QStringLiteral("Native Studio"));

    MainWindow window;
    if (window.restoreWindowLayout()) window.show();
    else window.showMaximized();
    QTimer::singleShot(40, &window, &MainWindow::restoreSessionDeferred);
    QTimer::singleShot(140, &window, &MainWindow::startDeferredServices);
    return application.exec();
}
