#include <QApplication>
#include <QPixmap>
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
    const QString screenshotPath = qEnvironmentVariable("GROUND_AIR_MONITOR_SCREENSHOT");
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(1500, &monitor, [&monitor, screenshotPath]() {
            monitor.grab().save(screenshotPath);
            qApp->quit();
        });
    }
    return a.exec();
}
