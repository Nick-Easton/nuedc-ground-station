#ifndef LANDSCREEN_H
#define LANDSCREEN_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPushButton>
#include <QTcpSocket>
#include <QTimer>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include<vector>
#include"plane_targets.h"
#include"QJsonArray"
#include"TargetInfo.h"
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8001
#define OFFSET_X 0.25
#define OFFSET_Y 0.25
class CarControlDialog;
struct Point
{
    qint8 a;
    qint8 b;
};
class LandScreen : public QWidget
{
    Q_OBJECT
public:
    explicit LandScreen(QWidget *parent = nullptr);
    ~LandScreen() override;

private slots:
    void ReadData();
    void drawOnMap(); // 新增槽声明

    signals:
        void wayPointsReady(); // 新增信号声明

private:
    QLabel *mapLabel = nullptr;
    QPixmap originalMapPixmap;

    // 下方按钮
    QLabel *labelA = nullptr;
    QLabel *labelB = nullptr;
    QPushButton *buttonsA[9];
    QPushButton *buttonsB[7];

    // 右上角标签
    QLabel *labelF1 = nullptr;
    QLabel *labelF2 = nullptr;
    QLabel *labelF3 = nullptr;
    QLabel* connectStatusLabel = nullptr; // 新增：连接状态标签
    QPushButton *connectionSettingsButton = nullptr;
    QPushButton *carControlButton = nullptr;
    CarControlDialog *carControlDialog = nullptr;

    // 发送和取消按钮
    QPushButton *sendButton = nullptr;
    QPushButton *cancelButton = nullptr;

    // 目标信息相关
    QLabel *labelTargetSummary = nullptr; // 新增：目标汇总标签
    QPushButton *showTargetInfoButton = nullptr; // 新增：显示目标信息按钮
    TargetInfo *targetInfoDialog = nullptr; // 新增：目标信息对话框

    QPushButton* launchButton = nullptr; // 新增：启动按钮

    // 按钮状态管理
    int selectedButtonA = -1; // 当前选中的A行按钮索引，-1表示无选中
    int selectedButtonB = -1; // 当前选中的B行按钮索引，-1表示无选中
    Point receivedPoint = {-1, -1}; // 当前选中的点，-1表示无选中
    // Socket相关
    QTcpSocket *socket = nullptr;
    QTimer *reconnectTimer = nullptr;
    QTimer *planningTimer = nullptr;
    QString serverIp;
    quint16 serverPort = SERVER_PORT;
    QJsonObject dataSend;

    Target receivedTarget = {-1,-1,"NULL"};
    std::vector<Target> targets;
    std::vector<Point> wayPoints;
    bool routeReady = false;
    bool planningRequestActive = false;
    QString resultsFilePath;
    QSet<QString> savedGridResultSignatures;
    bool hasGridResults = false;
    void CreateUI();
    void onButtonAClicked(int index);
    void onButtonBClicked(int index);
    void updateButtonAStyles();
    void updateButtonBStyles();
    void addForbidden();
    void onSendClicked();
    void onCancelClicked();
    void parseJson(const QByteArray &jsonData);
    bool sendData();
    void resetPlanningState(const QString &buttonText, bool clearRoute);
    void initSocket();
    void loadConnectionSettings();
    void showConnectionSettings();
    void reconnectToServer();
    void updateConnectionStatus(const QString &status);
    void updateTargetSummaryLabel(); // 新增：更新目标汇总标签
};

#endif // LANDSCREEN_H
