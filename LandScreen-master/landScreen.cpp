#include "landScreen.h"
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QByteArray>
#include <cmath>
#include<QRegularExpression>
#include<QMessageBox>

namespace {
QString landscreenServerIp()
{
    QByteArray value = qgetenv("LANDSCREEN_SERVER_IP");
    return value.isEmpty() ? QStringLiteral(SERVER_IP) : QString::fromUtf8(value);
}

quint16 landscreenServerPort()
{
    bool ok = false;
    quint16 port = qgetenv("LANDSCREEN_SERVER_PORT").toUShort(&ok);
    return ok ? port : static_cast<quint16>(SERVER_PORT);
}
}

LandScreen::LandScreen(QWidget *parent) : QWidget(parent)
{
    CreateUI();
    initSocket();
    dataSend["f1x"] = -1;
    dataSend["f1y"] = -1;
    dataSend["f2x" ] = -1;
    dataSend["f2y" ] = -1;
    dataSend["f3x" ] = -1;
    dataSend["f3y" ] = -1;
    dataSend["launch"] = false;

    // 信号与槽连接
    connect(this, &LandScreen::wayPointsReady, this, &LandScreen::drawOnMap);

}

LandScreen::~LandScreen()
{
    // 析构函数实现  if(wayPoints.empty()){
}

void LandScreen::initSocket()
{
    socket = new QTcpSocket(this);
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(500); // 每0.5秒重试一次

    connect(socket, &QTcpSocket::connected, this, [this]{
        qDebug() << "Connected to server";
        connectStatusLabel->setText("已连接");
        reconnectTimer->stop(); // 连接上了就停止重连
    });

    connect(socket, &QTcpSocket::disconnected, this, [this]{
        // 断开后再次尝试重连
        if (!reconnectTimer->isActive())
        {
            reconnectTimer->start();
            connectStatusLabel->setText("已断开");
        }

    });

    connect(socket, &QTcpSocket::readyRead, this, &LandScreen::ReadData);

    connect(reconnectTimer, &QTimer::timeout, this, [this]{
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            socket->abort(); // 清理旧连接
            socket->connectToHost(landscreenServerIp(), landscreenServerPort());
            qDebug() << "LandScreen socket reconnecting...";
        }
    });

    socket->connectToHost(landscreenServerIp(), landscreenServerPort());
    reconnectTimer->start();
}

void LandScreen::CreateUI()
{
    // 创建主布局 - 垂直布局
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // 创建上部区域 - 地图显示
    QWidget *topWidget = new QWidget(this);
    QHBoxLayout *topLayout = new QHBoxLayout(topWidget);
    topLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    topLayout->setContentsMargins(0, 0, 0, 0);

    // 创建地图显示区域
    mapLabel = new QLabel(topWidget);
    mapLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // 图片区域保持固定尺寸
    mapLabel->setFixedSize(525, 495);

    // 加载地图图片
    originalMapPixmap = QPixmap("../map.png");

    if (originalMapPixmap.isNull()) {
        qWarning() << "无法加载地图文件: ../map.png";
        mapLabel->setText("地图加载失败");
        mapLabel->setStyleSheet("background: black; color: white;");
        mapLabel->setFixedSize(525, 495);
    } else {
        // 按比例缩放地图
        QPixmap scaledPixmap = originalMapPixmap.scaled(360, 280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        mapLabel->setPixmap(scaledPixmap);
        mapLabel->setFixedSize(360, 280);
        mapLabel->setScaledContents(false);
    }

    // 创建右上角标签区域
    QWidget *rightTopWidget = new QWidget(topWidget);
    QVBoxLayout *rightTopLayout = new QVBoxLayout(rightTopWidget);
    rightTopLayout->setAlignment(Qt::AlignTop | Qt::AlignRight);
    rightTopLayout->setSpacing(10);
    rightTopLayout->setContentsMargins(10, 10, 10, 10);

    // 创建F1, F2, F3标签
    labelF1 = new QLabel("NULL", rightTopWidget);
    labelF1->setStyleSheet(
        "QLabel {"
        "    background-color: #f8f8f8;"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    padding: 5px;"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #333;"
        "}"
    );
    labelF1->setMinimumHeight(60); // 只设置最小高度
    labelF1->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    labelF1->setAlignment(Qt::AlignCenter);

    labelF2 = new QLabel("NULL", rightTopWidget);
    labelF2->setStyleSheet(
        "QLabel {"
        "    background-color: #f8f8f8;"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    padding: 5px;"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #333;"
        "}"
    );
    labelF2->setMinimumHeight(60);
    labelF2->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    labelF2->setAlignment(Qt::AlignCenter);

    labelF3 = new QLabel("NULL", rightTopWidget);
    labelF3->setStyleSheet(
        "QLabel {"
        "    background-color: #f8f8f8;"
        "    border: 1px solid #ccc;"
        "    border-radius: 3px;"
        "    padding: 5px;"
        "    font-size: 24px;"
        "    font-weight: bold;"
        "    color: #333;"
        "}"
    );
    labelF3->setMinimumHeight(60);
    labelF3->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    labelF3->setAlignment(Qt::AlignCenter);

    rightTopLayout->addWidget(labelF1);
    rightTopLayout->addWidget(labelF2);
    rightTopLayout->addWidget(labelF3);

    // 添加发送和取消按钮
    QWidget *buttonWidget = new QWidget(rightTopWidget);
    QHBoxLayout *buttonLayout = new QHBoxLayout(buttonWidget);
    buttonLayout->setSpacing(20);
    buttonLayout->setContentsMargins(0, 10, 0, 0);
    launchButton = new QPushButton("启动", buttonWidget);
    launchButton->setMinimumSize(80, 40); // 只设置最小
    launchButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    launchButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #dbdb18ff;"  
        "    border: 1px solid #dbdb18ff;"
        "    border-radius: 5px;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    color: white;"
        "}"
        "QPushButton:hover {"
        "    background-color: #dbdb18ff;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #1e7e34;"
        "}"

    );
    sendButton = new QPushButton("发送", buttonWidget);
    sendButton->setMinimumSize(80, 40); // 只设置最小尺寸
    sendButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    sendButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #28a745;"
        "    border: 1px solid #1e7e34;"
        "    border-radius: 5px;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    color: white;"
        "}"
        "QPushButton:hover {"
        "    background-color: #218838;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #1e7e34;"
        "}"
    );

    cancelButton = new QPushButton("取消", buttonWidget);
    cancelButton->setMinimumSize(80, 40);
    cancelButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    cancelButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #dc3545;"
        "    border: 1px solid #c82333;"
        "    border-radius: 5px;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    color: white;"
        "}"
        "QPushButton:hover {"
        "    background-color: #c82333;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #bd2130;"
        "}"
    );

    connect(sendButton, &QPushButton::clicked, [this]() {
        onSendClicked();
    });

    connect(cancelButton, &QPushButton::clicked, [this]() {
        onCancelClicked();
    });
    connect(launchButton, &QPushButton::clicked, [this]() {
        dataSend["launch"] = true;
        if(dataSend["f1x"] == -1 && dataSend["f2x"] == -1 && dataSend["f3x"] == -1){
            QMessageBox::warning(this, "警告", "没有禁飞区信息，无法启动");
            return;
        }
        sendData();
    });

    buttonLayout->addWidget(sendButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(launchButton);

    rightTopLayout->addWidget(buttonWidget);
    rightTopLayout->addStretch(); // 向下推送

    topLayout->addWidget(mapLabel);
    topLayout->addWidget(rightTopWidget);

    // 创建下部区域 - 按键控制（左下角对齐）
    QWidget *bottomWidget = new QWidget(this);
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottomWidget);
    bottomLayout->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    bottomLayout->setSpacing(15);
    bottomLayout->setContentsMargins(0, 0, 0, 0);

    // 第一行：标签A和9个按钮
    QWidget *rowAWidget = new QWidget();
    QHBoxLayout *rowALayout = new QHBoxLayout(rowAWidget);
    rowALayout->setAlignment(Qt::AlignLeft);
    rowALayout->setSpacing(8);
    rowALayout->setContentsMargins(0, 0, 0, 0);

    labelA = new QLabel("A", rowAWidget);
    labelA->setStyleSheet("font-weight: bold; color: #333; font-size: 16px;");
    labelA->setMinimumSize(40, 40); // 只设置最小尺寸
    labelA->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    labelA->setAlignment(Qt::AlignCenter);
    rowALayout->addWidget(labelA);

    for (int i = 0; i < 9; i++) {
        buttonsA[i] = new QPushButton(QString::number(i + 1), rowAWidget);
        buttonsA[i]->setMinimumSize(40, 40);
        buttonsA[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        buttonsA[i]->setStyleSheet(
            "QPushButton {"
            "    background-color: #ffffff;"
            "    border: 1px solid #ccc;"
            "    border-radius: 5px;"
            "    font-size: 14px;"
            "    font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e0e0e0;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #d0d0d0;"
            "}"
        );

        connect(buttonsA[i], &QPushButton::clicked, [this, i]() {
            onButtonAClicked(i);
        });

        rowALayout->addWidget(buttonsA[i]);
    }

    // 第二行：标签B和7个按钮
    QWidget *rowBWidget = new QWidget();
    QHBoxLayout *rowBLayout = new QHBoxLayout(rowBWidget);
    rowBLayout->setAlignment(Qt::AlignLeft);
    rowBLayout->setSpacing(8);
    rowBLayout->setContentsMargins(0, 0, 0, 0);

    labelB = new QLabel("B", rowBWidget);
    labelB->setStyleSheet("font-weight: bold; color: #333; font-size: 16px;");
    labelB->setMinimumSize(40, 40);
    labelB->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    labelB->setAlignment(Qt::AlignCenter);
    rowBLayout->addWidget(labelB);

    for (int i = 0; i < 7; i++) {
        buttonsB[i] = new QPushButton(QString::number(i + 1), rowBWidget);
        buttonsB[i]->setMinimumSize(40, 40);
        buttonsB[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        buttonsB[i]->setStyleSheet(
            "QPushButton {"
            "    background-color: #ffffff;"
            "    border: 1px solid #ccc;"
            "    border-radius: 5px;"
            "    font-size: 14px;"
            "    font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "    background-color: #e0e0e0;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #d0d0d0;"
            "}"
        );

        connect(buttonsB[i], &QPushButton::clicked, [this, i]() {
            onButtonBClicked(i);
        });

        rowBLayout->addWidget(buttonsB[i]);
    }

    // 新增：目标汇总标签和显示目标信息按钮
    QHBoxLayout *summaryLayout = new QHBoxLayout();
    labelTargetSummary = new QLabel("暂无目标信息", bottomWidget);
    labelTargetSummary->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");
    labelTargetSummary->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connectStatusLabel = new QLabel("未连接", bottomWidget);
    connectStatusLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #333;");

    showTargetInfoButton = new QPushButton("显示目标信息", bottomWidget);
    showTargetInfoButton->setMinimumSize(120, 40);
    showTargetInfoButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #007acc;"
        "    color: white;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    border-radius: 5px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #005a99;"
        "}"
    );
    connect(showTargetInfoButton, &QPushButton::clicked, [this]() {
        if (!targetInfoDialog)
            targetInfoDialog = new TargetInfo(this);
        targetInfoDialog->showFullScreen();
        targetInfoDialog->raise();
        targetInfoDialog->activateWindow();
    });

    summaryLayout->addWidget(labelTargetSummary);
    summaryLayout->addWidget(connectStatusLabel);
    summaryLayout->addWidget(showTargetInfoButton);

    bottomLayout->addLayout(summaryLayout); // 添加到下方布局

    // 将两行按钮添加到底部布局
    bottomLayout->addWidget(rowAWidget);
    bottomLayout->addWidget(rowBWidget);

    // 添加到主布局
    mainLayout->addWidget(topWidget);
    mainLayout->addStretch(); // 添加弹性空间，将按钮推到底部
    mainLayout->addWidget(bottomWidget);

    // 设置主布局
    setLayout(mainLayout);
    resize(800, 600);
    setWindowTitle("地图界面");
}

void LandScreen::onButtonAClicked(int index)
{
    // 更新选中状态
    selectedButtonA = index;

    // 更新按钮样式
    updateButtonAStyles();

    qDebug() << "A行按钮被点击:" << (index + 1);

    // 检查是否A和B都有选中的按钮
    if (selectedButtonA != -1 && selectedButtonB != -1) {
        addForbidden();
    }
}

void LandScreen::onButtonBClicked(int index)
{
    // 更新选中状态
    selectedButtonB = index;

    // 更新按钮样式
    updateButtonBStyles();

    qDebug() << "B行按钮被点击:" << (index + 1);

    // 检查是否A和B都有选中的按钮
    if (selectedButtonA != -1 && selectedButtonB != -1) {
        addForbidden();
    }
}

void LandScreen::updateButtonAStyles()
{
    for (int i = 0; i < 9; i++) {
        if (i == selectedButtonA) {
            // 选中状态：蓝色背景，不可点击
            buttonsA[i]->setStyleSheet(
                "QPushButton {"
                "    background-color: #007acc;"
                "    border: 1px solid #005a99;"
                "    border-radius: 5px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "    color: white;"
                "}"
            );
            buttonsA[i]->setEnabled(false);
        } else {
            // 未选中状态：白色背景，可点击
            buttonsA[i]->setStyleSheet(
                "QPushButton {"
                "    background-color: #ffffff;"
                "    border: 1px solid #ccc;"
                "    border-radius: 5px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "    background-color: #e0e0e0;"
                "}"
                "QPushButton:pressed {"
                "    background-color: #d0d0d0;"
                "}"
            );
            buttonsA[i]->setEnabled(true);
        }
    }
}

void LandScreen::updateButtonBStyles()
{
    for (int i = 0; i < 7; i++) {
        if (i == selectedButtonB) {
            // 选中状态：蓝色背景，不可点击
            buttonsB[i]->setStyleSheet(
                "QPushButton {"
                "    background-color: #007acc;"
                "    border: 1px solid #005a99;"
                "    border-radius: 5px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "    color: white;"
                "}"
            );
            buttonsB[i]->setEnabled(false);
        } else {
            // 未选中状态：白色背景，可点击
            buttonsB[i]->setStyleSheet(
                "QPushButton {"
                "    background-color: #ffffff;"
                "    border: 1px solid #ccc;"
                "    border-radius: 5px;"
                "    font-size: 14px;"
                "    font-weight: bold;"
                "}"
                "QPushButton:hover {"
                "    background-color: #e0e0e0;"
                "}"
                "QPushButton:pressed {"
                "    background-color: #d0d0d0;"
                "}"
            );
            buttonsB[i]->setEnabled(true);
        }
    }
}

void LandScreen::addForbidden()
{
    qDebug() << "addForbidden() 被调用 - A行按钮:" << (selectedButtonA + 1) << ", B行按钮:" << (selectedButtonB + 1);

    // 遍历三个标签，找到第一个内容为"NULL"的标签并更新
    QLabel* labels[] = {labelF1, labelF2, labelF3};

    for (int i = 0; i < 3; i++) {
        if (labels[i]->text() == "NULL") {
            QString forbiddenText = QString("禁飞区%1（A%2,B%3）")
                                    .arg(i + 1)
                                    .arg(selectedButtonA + 1)
                                    .arg(selectedButtonB + 1);
            labels[i]->setText(forbiddenText);
            qDebug() << "设置标签F" << (i + 1) << "内容为:" << forbiddenText;
            break;
        }
    }

    // 恢复A行按钮状态
    selectedButtonA = -1;
    updateButtonAStyles();

    // 恢复B行按钮状态
    selectedButtonB = -1;
    updateButtonBStyles();

    qDebug() << "按钮状态已恢复";
}

void LandScreen::onSendClicked()
{
    qDebug() << "发送按钮被点击";

    // 从三个标签中提取AB数字
    QLabel* labels[] = {labelF1, labelF2, labelF3};
    QString keys[] = {"f1", "f2", "f3"};

    for (int i = 0; i < 3; i++) {
        QString text = labels[i]->text();
        if (text != "NULL") {
            // 解析格式：禁飞区1（A2,B3）
            QRegularExpression regex(QStringLiteral("禁飞区\\d+（A(\\d+),B(\\d+)）"));
            QRegularExpressionMatch match = regex.match(text);
            if (match.hasMatch()) {
                int aValue = match.captured(1).toInt();
                int bValue = match.captured(2).toInt();
                dataSend[keys[i] + "x"] = aValue;
                dataSend[keys[i] + "y"] = bValue;
                qDebug() << "提取" << keys[i] << ": A=" << aValue << ", B=" << bValue;
            }
        }
    }

    sendData();
}

void LandScreen::onCancelClicked()
{
    qDebug() << "取消按钮被点击，清除所有标签";

    // 将三个标签全部设置为NULL
    labelF1->setText("NULL");
    labelF2->setText("NULL");
    labelF3->setText("NULL");

    qDebug() << "所有标签已重置为NULL";
}

void LandScreen::parseJson(const QByteArray &jsonData)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << parseError.errorString();
        return;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("planner") || !obj["planner"].isArray()) {
        qWarning() << "JSON does not contain 'planner' array.";
        return;
    }

    QJsonArray plannerArray = obj["planner"].toArray();
    if (!plannerArray.isEmpty()) {
        wayPoints.clear();
        for (const QJsonValue &val : plannerArray) {
            if (!val.isObject()) continue;
            QJsonObject pointObj = val.toObject();
            if (pointObj.contains("x") && pointObj.contains("y")) {
                double x = pointObj["x"].toDouble();
                double y = pointObj["y"].toDouble();
                Point pt;
                pt.a =8-static_cast<qint8>(y/0.5);
                pt.b = static_cast<qint8>(x/0.5);
                qDebug() << "a:" << pt.a << "b:" << pt.b;
                wayPoints.push_back(pt);
            }
        }
        emit wayPointsReady();
    }
    SharedData& data = SharedData::getInstance();
    Target& receivedTarget = data.getChosenTarget();
    if (obj.contains("tx")) receivedTarget.x = obj["tx"].toDouble();
    if (obj.contains("ty")) receivedTarget.y = obj["ty"].toDouble();
    if (obj.contains("tn")) receivedTarget.name = obj["tn"].toString();
    receivedTarget.a = 9-static_cast<qint8>(std::round(receivedTarget.y/0.5));
    receivedTarget.b = static_cast<qint8>(std::round(receivedTarget.x/0.5))+1;
    data.addTargetIfNew(receivedTarget);

    // for循环结束后发射信号
    updateTargetSummaryLabel();// 新增：更新目标汇总标签
}

// 新增：更新目标汇总标签的方法
void LandScreen::updateTargetSummaryLabel()
{
    SharedData& sharedData = SharedData::getInstance();
    std::lock_guard<std::mutex> lock(sharedData.getMutex());
    const Target& t = sharedData.getChosenTarget();
    if (t.name != "NULL") {
        labelTargetSummary->setText(
            QString("%1, A:%2, B:%3, 数量：%4")
            .arg(t.name)
            .arg(t.a)
            .arg(t.b)
            .arg(t.n)
        );
    } else {
        labelTargetSummary->setText("暂无目标信息");
    }
}

void LandScreen::ReadData()
{
    while (socket->canReadLine()) {
        QByteArray data = socket->readLine().trimmed();
        parseJson(data);
    }
}

void LandScreen::sendData()
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonDocument doc(dataSend);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        jsonData.append("\n");
        socket->write(jsonData);
        qDebug() << "LandScreen sent data:" << jsonData;
    } else {
        qDebug() << "Socket not connected, cannot send data";
    }
}

// 自定义槽函数实现
void LandScreen::drawOnMap()
{
    if (originalMapPixmap.isNull() || wayPoints.empty()) {
        return;
    }

    int mapWidth = mapLabel->width();
    int mapHeight = mapLabel->height();
    const int cols = 9;
    const int rows = 7;
    double cellWidth = static_cast<double>(mapWidth) / cols;
    double cellHeight = static_cast<double>(mapHeight) / rows;

    QPixmap pixmap = originalMapPixmap.scaled(mapWidth, mapHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(Qt::red, 4);
    painter.setPen(pen);

    QVector<QPointF> centers;
    for (const Point& pt : wayPoints) {
        int a = pt.a;
        int b = pt.b;
        if (a < 0 || a >= cols || b < 0 || b >= rows) {
            qDebug() << "无效的坐标点: a=" << a << "b=" << b;
            continue;
        }
        double cx = ((a + 0.5) * cellWidth);
        double cy = (mapHeight - (b + 0.5) * cellHeight);
        cx = qBound(0.0, cx, static_cast<double>(mapWidth));
        cy = qBound(0.0, cy, static_cast<double>(mapHeight));
        centers.append(QPointF(cx, cy));
    }

    // 依次连接中心点并在中点画箭头
    for (int i = 1; i < centers.size(); ++i) {
        QPointF p1 = centers[i - 1];
        QPointF p2 = centers[i];
        painter.drawLine(p1, p2);

        // 箭头参数
        QPointF mid = (p1 + p2) / 2;
        double angle = std::atan2(p2.y() - p1.y(), p2.x() - p1.x());
        double arrowLen = 10;
        double arrowAngle = 3.14159265358979323846 / 7; // 约25度

        QPointF arrowP1(
            mid.x() - arrowLen * std::cos(angle - arrowAngle),
            mid.y() - arrowLen * std::sin(angle - arrowAngle)
        );
        QPointF arrowP2(
            mid.x() - arrowLen * std::cos(angle + arrowAngle),
            mid.y() - arrowLen * std::sin(angle + arrowAngle)
        );

        painter.drawLine(mid, arrowP1);
        painter.drawLine(mid, arrowP2);
    }

    mapLabel->setPixmap(pixmap);

    qDebug() << "drawOnMap 完成，wayPoints数:" << wayPoints.size();
}
