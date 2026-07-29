#include <QApplication>
#include <QScreen>    // 用于获取屏幕信息
#include <QSize>      // 用于设置固定尺寸
#include "landScreen.h"
#include "TargetInfo.h"
#include "carControlDialog.h"

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    if (!qgetenv("LANDSCREEN_CAR_CONTROL_ONLY").isEmpty()) {
        CarControlDialog carWindow;
        carWindow.showFullScreen();
        return a.exec();
    }
    
    LandScreen homeWindow;
    
    // 1. 设置固定窗口尺寸（例如 800x600）
    // const QSize fixedSize(600, 400);
    // homeWindow.setFixedSize(fixedSize); // 移除固定尺寸
    
    // 2. 计算居中位置
    QScreen *screen = QApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    int x = (screenGeometry.width() - 800) / 2; // 默认宽度800
    int y = (screenGeometry.height() - 600) / 2; // 默认高度600
    
    // 3. 移动窗口到屏幕中心
    homeWindow.move(x, y);
    
    homeWindow.showFullScreen();
    
    return a.exec();
}
