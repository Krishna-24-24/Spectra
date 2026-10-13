#include "MainWindow.hpp"

#include <QApplication>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Multi-Threaded Video Analysis & Event Detection");
    QApplication::setOrganizationName("SPECTRA");

    MainWindow window;
    window.show();
    return app.exec();
}
