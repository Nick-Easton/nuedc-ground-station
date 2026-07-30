#include <QApplication>
#include <QTimer>
#include "groundAirMonitor.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    GroundAirMonitor monitor;
    if (qEnvironmentVariableIsSet("GROUND_AIR_MONITOR_WINDOWED"))
        monitor.show();
    else
        monitor.showFullScreen();
    if (qEnvironmentVariableIsSet("GROUND_AIR_MONITOR_DEMO")) {
        QTimer::singleShot(0, &monitor, [&monitor]() {
            QMetaObject::invokeMethod(&monitor, "toggleDemo", Qt::QueuedConnection);
        });
    }
    return a.exec();
}
