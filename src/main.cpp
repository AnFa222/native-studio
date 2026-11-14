#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("NativeTools"));
    QCoreApplication::setApplicationName(QStringLiteral("Native Studio"));

    MainWindow window;
    window.show();
    return application.exec();
}
