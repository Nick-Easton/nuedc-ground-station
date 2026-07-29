#include <QApplication>
#include "carControlDialog.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    CarControlDialog carWindow;
    carWindow.showFullScreen();
    return a.exec();
}
