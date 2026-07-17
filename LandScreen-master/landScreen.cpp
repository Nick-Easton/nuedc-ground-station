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
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QHostAddress>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#endif

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

QString mapImagePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/../map.png");
}

void setUtf8Encoding(QTextStream &stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
}
}

LandScreen::LandScreen(QWidget *parent) : QWidget(parent)
{
    loadConnectionSettings();
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

    planningTimer = new QTimer(this);
    planningTimer->setSingleShot(true);
    connect(planningTimer, &QTimer::timeout, this, [this]() {
        if (!planningRequestActive)
            return;
        resetPlanningState(QStringLiteral("规划超时，请重试"), true);
        qWarning() << "Route planning timed out without a path response.";
    });

}

LandScreen::~LandScreen()
{
    // 析构函数实现  if(wayPoints.empty()){
}

void LandScreen::loadConnectionSettings()
{
    QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("LandScreen"));
    serverIp = settings.value(QStringLiteral("connection/server_ip"),
                              landscreenServerIp()).toString().trimmed();
    const int configuredPort = settings.value(
        QStringLiteral("connection/server_port"), landscreenServerPort()).toInt();
    serverPort = configuredPort > 0 && configuredPort <= 65535
        ? static_cast<quint16>(configuredPort)
        : static_cast<quint16>(SERVER_PORT);
}

void LandScreen::updateConnectionStatus(const QString &status)
{
    if (connectStatusLabel) {
        connectStatusLabel->setText(
            QStringLiteral("%1  %2:%3").arg(status, serverIp).arg(serverPort));
    }
}

void LandScreen::reconnectToServer()
{
    if (!socket)
        return;

    socket->abort();
    updateConnectionStatus(QStringLiteral("连接中"));
    socket->connectToHost(serverIp, serverPort);
    if (reconnectTimer && !reconnectTimer->isActive())
        reconnectTimer->start();
}

void LandScreen::showConnectionSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("机载电脑连接设置"));
    dialog.setMinimumWidth(460);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *hint = new QLabel(
        QStringLiteral("请输入机载电脑 NX 的 IP 地址。保存后将立即按新地址重新连接。"),
        &dialog);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-size: 16px; color: #333; padding: 6px;"));
    layout->addWidget(hint);

    QFormLayout *form = new QFormLayout();
    QLineEdit *ipEdit = new QLineEdit(serverIp, &dialog);
    ipEdit->setPlaceholderText(QStringLiteral("例如：192.168.1.20"));
    ipEdit->setMinimumHeight(42);
    ipEdit->setStyleSheet(QStringLiteral("font-size: 18px; padding: 4px;"));

    QSpinBox *portEdit = new QSpinBox(&dialog);
    portEdit->setRange(1, 65535);
    portEdit->setValue(serverPort);
    portEdit->setMinimumHeight(42);
    portEdit->setStyleSheet(QStringLiteral("font-size: 18px; padding: 4px;"));

    form->addRow(QStringLiteral("机载电脑 IP："), ipEdit);
    form->addRow(QStringLiteral("通信端口："), portEdit);
    layout->addLayout(form);

    QLabel *current = new QLabel(
        QStringLiteral("当前连接：%1:%2").arg(serverIp).arg(serverPort), &dialog);
    current->setStyleSheet(QStringLiteral("font-size: 15px; color: #666; padding: 6px;"));
    layout->addWidget(current);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存并重新连接"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->setStyleSheet(QStringLiteral(
        "QPushButton { min-height: 40px; min-width: 120px; font-size: 16px; }"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString newIp = ipEdit->text().trimmed();
        QHostAddress address;
        if (!address.setAddress(newIp) ||
            address.protocol() != QAbstractSocket::IPv4Protocol) {
            QMessageBox::warning(
                &dialog, QStringLiteral("IP 地址无效"),
                QStringLiteral("请输入正确的 IPv4 地址，例如 192.168.1.20。"));
            return;
        }

        serverIp = newIp;
        serverPort = static_cast<quint16>(portEdit->value());
        QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("LandScreen"));
        settings.setValue(QStringLiteral("connection/server_ip"), serverIp);
        settings.setValue(QStringLiteral("connection/server_port"), serverPort);
        settings.sync();
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted)
        reconnectToServer();
}

void LandScreen::initSocket()
{
    socket = new QTcpSocket(this);
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(1000);

    connect(socket, &QTcpSocket::connected, this, [this]{
        qDebug() << "Connected to server";
        updateConnectionStatus(QStringLiteral("已连接"));
        reconnectTimer->stop(); // 连接上了就停止重连
    });

    connect(socket, &QTcpSocket::disconnected, this, [this]{
        // 断开后再次尝试重连
        if (!reconnectTimer->isActive())
        {
            reconnectTimer->start();
            updateConnectionStatus(QStringLiteral("未连接"));
        }

        if (planningRequestActive)
            resetPlanningState(QStringLiteral("连接已断开"), true);

    });

    connect(socket, &QTcpSocket::readyRead, this, &LandScreen::ReadData);

    connect(reconnectTimer, &QTimer::timeout, this, [this]{
        if (socket->state() == QAbstractSocket::UnconnectedState) {
            socket->abort(); // 清理旧连接
            socket->connectToHost(serverIp, serverPort);
            qDebug() << "LandScreen socket reconnecting...";
        }
    });

    updateConnectionStatus(QStringLiteral("连接中"));
    socket->connectToHost(serverIp, serverPort);
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
    mapLabel->setAlignment(Qt::AlignCenter);
    // 图片区域保持固定尺寸
    mapLabel->setFixedSize(525, 495);

    // 加载地图图片
    originalMapPixmap = QPixmap(mapImagePath());

    if (originalMapPixmap.isNull()) {
        qWarning() << "无法加载地图文件: ../map.png";
        mapLabel->setText("地图加载失败");
        mapLabel->setStyleSheet("background: black; color: white;");
        mapLabel->setFixedSize(525, 495);
    } else {
        // 按比例缩放地图
        QPixmap scaledPixmap = originalMapPixmap.scaledToWidth(620, Qt::SmoothTransformation);
        mapLabel->setPixmap(scaledPixmap);
        mapLabel->setFixedSize(scaledPixmap.size());
        mapLabel->setScaledContents(false);
    }

    // 创建右上角标签区域
    QWidget *rightTopWidget = new QWidget(topWidget);
    rightTopWidget->setMinimumWidth(480);
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
    launchButton = new QPushButton("启动识别", buttonWidget);
    launchButton->setEnabled(false);
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
        if (!routeReady) {
            QMessageBox::information(this, "请先规划", "请先发送禁飞区并等待地图显示规划航线。");
            return;
        }
        dataSend["launch"] = true;
        sendData();
        launchButton->setText("识别已启动");
        launchButton->setEnabled(false);
    });

    buttonLayout->addWidget(sendButton, 1);
    buttonLayout->addWidget(cancelButton, 1);
    buttonLayout->addWidget(launchButton, 1);

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

    connectionSettingsButton = new QPushButton(QStringLiteral("连接设置"), bottomWidget);
    connectionSettingsButton->setMinimumSize(110, 40);
    connectionSettingsButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #5f6b7a;"
        "    color: white;"
        "    font-size: 16px;"
        "    font-weight: bold;"
        "    border-radius: 5px;"
        "    padding: 6px 12px;"
        "}"
        "QPushButton:hover { background-color: #46515f; }"
    );
    connect(connectionSettingsButton, &QPushButton::clicked,
            this, &LandScreen::showConnectionSettings);

    summaryLayout->addWidget(labelTargetSummary);
    summaryLayout->addWidget(connectStatusLabel);
    summaryLayout->addWidget(connectionSettingsButton);
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

    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        resetPlanningState(QStringLiteral("未连接机载电脑"), false);
        QMessageBox::warning(
            this, QStringLiteral("无法规划"),
            QStringLiteral("地面站尚未连接机载电脑，请先检查连接设置和 TCP 8001。")
        );
        return;
    }

    // 从三个标签中提取AB数字
    QLabel* labels[] = {labelF1, labelF2, labelF3};
    QString keys[] = {"f1", "f2", "f3"};

    for (int i = 0; i < 3; ++i) {
        dataSend[keys[i] + "x"] = -1;
        dataSend[keys[i] + "y"] = -1;
    }

    for (int i = 0; i < 3; i++) {
        QString text = labels[i]->text();
        if (text != "NULL") {
            // 解析格式：禁飞区1（A2,B3）
            QRegularExpression regex(QStringLiteral("禁飞区\\d+（A(\\d+),B(\\d+)）"));
            QRegularExpressionMatch match = regex.match(text);
            if (match.hasMatch()) {
                int aValue = match.captured(1).toInt();
                int bValue = match.captured(2).toInt();
                if (aValue == 9 && bValue == 1) {
                    resetPlanningState(QStringLiteral("起降点不可禁飞"), true);
                    QMessageBox::warning(
                        this, QStringLiteral("禁飞区无效"),
                        QStringLiteral("A9B1 是无人机起降点，不能设置为禁飞区。")
                    );
                    return;
                }
                dataSend[keys[i] + "x"] = aValue;
                dataSend[keys[i] + "y"] = bValue;
                qDebug() << "提取" << keys[i] << ": A=" << aValue << ", B=" << bValue;
            }
        }
    }

    dataSend["launch"] = false;
    routeReady = false;
    planningRequestActive = true;
    wayPoints.clear();
    emit wayPointsReady();
    launchButton->setEnabled(false);
    launchButton->setText(QStringLiteral("航线规划中..."));
    planningTimer->start(10000);
    if (!sendData())
        resetPlanningState(QStringLiteral("发送失败，请重试"), true);
}

void LandScreen::onCancelClicked()
{
    qDebug() << "取消按钮被点击，清除所有标签";

    // 将三个标签全部设置为NULL
    labelF1->setText("NULL");
    labelF2->setText("NULL");
    labelF3->setText("NULL");

    for (int index = 1; index <= 3; ++index) {
        dataSend[QString("f%1x").arg(index)] = -1;
        dataSend[QString("f%1y").arg(index)] = -1;
    }
    dataSend["launch"] = false;

    selectedButtonA = -1;
    selectedButtonB = -1;
    updateButtonAStyles();
    updateButtonBStyles();
    resetPlanningState(QStringLiteral("启动识别"), true);

    qDebug() << "所有标签已重置为NULL";
}

void LandScreen::resetPlanningState(const QString &buttonText, bool clearRoute)
{
    planningRequestActive = false;
    if (planningTimer)
        planningTimer->stop();

    routeReady = false;
    if (launchButton) {
        launchButton->setEnabled(false);
        launchButton->setText(buttonText);
    }

    if (clearRoute) {
        wayPoints.clear();
        emit wayPointsReady();
    }
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
    if (obj.value("reset_targets").toBool(false)) {
        SharedData &sharedData = SharedData::getInstance();
        {
            std::lock_guard<std::mutex> lock(sharedData.getMutex());
            sharedData.getTargets().clear();
            Target &chosen = sharedData.getChosenTarget();
            chosen = {-1, -1, "NULL"};
        }
        savedGridResultSignatures.clear();
        resultsFilePath = QCoreApplication::applicationDirPath()
            + QString("/../animal_results_%1.csv")
                  .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        QFile resultFile(resultsFilePath);
        if (resultFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&resultFile);
            setUtf8Encoding(stream);
            stream << "timestamp,grid,animal,count\n";
        }
        updateTargetSummaryLabel();
    }

    if (obj.contains("grid_result") && obj["grid_result"].isObject()) {
        const QJsonObject result = obj["grid_result"].toObject();
        const int a = result.value("a").toInt(-1);
        const int b = result.value("b").toInt(-1);
        const QJsonObject counts = result.value("counts").toObject();
        const QStringList classNames = {
            "elephant", "tiger", "monkey", "kongque", "wolf"
        };

        if (a >= 1 && a <= 9 && b >= 1 && b <= 7) {
            SharedData &sharedData = SharedData::getInstance();
            {
                std::lock_guard<std::mutex> lock(sharedData.getMutex());
                std::vector<Target> &targets = sharedData.getTargets();
                Target &chosen = sharedData.getChosenTarget();
                for (const QString &className : classNames) {
                    const int count = counts.value(className).toInt(0);
                    if (count <= 0)
                        continue;

                    auto existing = std::find_if(
                        targets.begin(), targets.end(),
                        [a, b, &className](const Target &target) {
                            return target.name == className && target.a == a && target.b == b;
                        }
                    );
                    if (existing == targets.end()) {
                        Target target;
                        target.name = className;
                        target.a = a;
                        target.b = b;
                        target.x = (b - 1) * 0.5;
                        target.y = (9 - a) * 0.5;
                        target.n = count;
                        targets.push_back(target);
                        chosen = target;
                    } else {
                        existing->n = count;
                        chosen = *existing;
                    }
                }
            }

            const QString grid = QString("A%1B%2").arg(a).arg(b);
            const QString signature = grid + "|"
                + QString::fromUtf8(QJsonDocument(counts).toJson(QJsonDocument::Compact));
            if (!savedGridResultSignatures.contains(signature)) {
                if (resultsFilePath.isEmpty()) {
                    resultsFilePath = QCoreApplication::applicationDirPath()
                        + QString("/../animal_results_%1.csv")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
                    QFile newFile(resultsFilePath);
                    if (newFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream newStream(&newFile);
                        setUtf8Encoding(newStream);
                        newStream << "timestamp,grid,animal,count\n";
                    }
                }

                QFile resultFile(resultsFilePath);
                if (resultFile.open(QIODevice::Append | QIODevice::Text)) {
                    QTextStream stream(&resultFile);
                    setUtf8Encoding(stream);
                    const QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
                    bool animalFound = false;
                    for (const QString &className : classNames) {
                        const int count = counts.value(className).toInt(0);
                        if (count > 0) {
                            stream << timestamp << ',' << grid << ','
                                   << className << ',' << count << '\n';
                            animalFound = true;
                        }
                    }
                    if (!animalFound)
                        stream << timestamp << ',' << grid << ",none,0\n";
                    savedGridResultSignatures.insert(signature);
                    qDebug() << "Saved grid recognition result:" << resultsFilePath << grid;
                }
            }
            updateTargetSummaryLabel();
        }
        return;
    }

    if (obj.contains("planner") && obj["planner"].isArray() &&
        !obj["planner"].toArray().isEmpty() &&
        !planningRequestActive && !routeReady) {
        qDebug() << "Ignoring a route received after planning was cancelled or timed out.";
        return;
    }

    if (obj.contains("forbidden") && obj["forbidden"].isArray()) {
        QLabel *forbiddenLabels[] = {labelF1, labelF2, labelF3};
        for (int index = 0; index < 3; ++index) {
            dataSend[QString("f%1x").arg(index + 1)] = -1;
            dataSend[QString("f%1y").arg(index + 1)] = -1;
            forbiddenLabels[index]->setText("NULL");
        }

        QJsonArray forbiddenArray = obj["forbidden"].toArray();
        int index = 0;
        for (const QJsonValue &value : forbiddenArray) {
            if (index >= 3 || !value.isObject()) continue;
            QJsonObject zone = value.toObject();
            int a = zone["a"].toInt(-1);
            int b = zone["b"].toInt(-1);
            if (a < 1 || a > 9 || b < 1 || b > 7) continue;

            dataSend[QString("f%1x").arg(index + 1)] = a;
            dataSend[QString("f%1y").arg(index + 1)] = b;
            forbiddenLabels[index]->setText(
                QString("禁飞区%1（A%2,B%3）").arg(index + 1).arg(a).arg(b)
            );
            ++index;
        }
    }

    if (!obj.contains("planner") || !obj["planner"].isArray()) {
        qWarning() << "JSON does not contain 'planner' array.";
        return;
    }

    QJsonArray plannerArray = obj["planner"].toArray();
    if (plannerArray.isEmpty()) {
        if (planningRequestActive)
            resetPlanningState(QStringLiteral("规划失败，请检查禁飞区"), true);
    } else {
        std::vector<Point> parsedWayPoints;
        for (const QJsonValue &val : plannerArray) {
            if (!val.isObject()) continue;
            QJsonObject pointObj = val.toObject();
            if (pointObj.contains("x") && pointObj.contains("y")) {
                double x = pointObj["x"].toDouble();
                double y = pointObj["y"].toDouble();
                Point pt;
                pt.a = static_cast<qint8>(9 - std::round(y / 0.5));
                pt.b = static_cast<qint8>(std::round(x / 0.5) + 1);
                if (pt.a < 1 || pt.a > 9 || pt.b < 1 || pt.b > 7)
                    continue;
                qDebug() << "a:" << pt.a << "b:" << pt.b;
                parsedWayPoints.push_back(pt);
            }
        }

        if (parsedWayPoints.empty()) {
            resetPlanningState(QStringLiteral("无可用航线"), true);
        } else {
            wayPoints.swap(parsedWayPoints);
            planningRequestActive = false;
            if (planningTimer)
                planningTimer->stop();
            routeReady = true;
            launchButton->setEnabled(true);
            launchButton->setText(QStringLiteral("启动识别"));
            emit wayPointsReady();
        }
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

bool LandScreen::sendData()
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        QJsonDocument doc(dataSend);
        QByteArray jsonData = doc.toJson(QJsonDocument::Compact);
        jsonData.append("\n");
        if (socket->write(jsonData) < 0) {
            qWarning() << "Socket write failed:" << socket->errorString();
            return false;
        }
        qDebug() << "LandScreen sent data:" << jsonData;
        return true;
    } else {
        qDebug() << "Socket not connected, cannot send data";
        return false;
    }
}

// 自定义槽函数实现
void LandScreen::drawOnMap()
{
    if (originalMapPixmap.isNull()) {
        return;
    }

    QPixmap pixmap = originalMapPixmap.scaled(
        mapLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation
    );
    const int mapWidth = pixmap.width();
    const int mapHeight = pixmap.height();
    // The supplied arena image is A1..A9 from left to right and B1..B7
    // from bottom to top. This matches the planner's x/y conversion.
    const int cols = 9;
    const int rows = 7;
    double cellWidth = static_cast<double>(mapWidth) / cols;
    double cellHeight = static_cast<double>(mapHeight) / rows;

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.save();
    painter.setPen(QPen(QColor(255, 255, 255, 180), 1));
    painter.setBrush(Qt::NoBrush);
    for (int column = 0; column <= cols; ++column)
        painter.drawLine(QPointF(column * cellWidth, 0), QPointF(column * cellWidth, mapHeight));
    for (int row = 0; row <= rows; ++row)
        painter.drawLine(QPointF(0, row * cellHeight), QPointF(mapWidth, row * cellHeight));
    painter.setPen(QColor(20, 40, 70));
    painter.setFont(QFont("Sans Serif", 10, QFont::Bold));
    for (int column = 0; column < cols; ++column)
        painter.drawText(QRectF(column * cellWidth, 1, cellWidth, 18), Qt::AlignCenter, QString("A%1").arg(column + 1));
    for (int row = 0; row < rows; ++row)
        painter.drawText(QRectF(1, row * cellHeight, 32, cellHeight), Qt::AlignVCenter, QString("B%1").arg(rows - row));
    painter.restore();

    painter.save();
    for (int index = 1; index <= 3; ++index) {
        int a = dataSend.value(QString("f%1x").arg(index)).toInt(-1);
        int b = dataSend.value(QString("f%1y").arg(index)).toInt(-1);
        if (a < 1 || a > cols || b < 1 || b > rows) {
            continue;
        }

        QRectF forbiddenRect(
            (a - 1) * cellWidth,
            mapHeight - b * cellHeight,
            cellWidth,
            cellHeight
        );
        painter.fillRect(forbiddenRect, QColor(180, 0, 0, 105));
        painter.setPen(QPen(QColor(130, 0, 0), 3));
        painter.drawRect(forbiddenRect);
        painter.drawLine(forbiddenRect.topLeft(), forbiddenRect.bottomRight());
        painter.drawLine(forbiddenRect.topRight(), forbiddenRect.bottomLeft());
    }
    painter.restore();

    QPen pen(Qt::red, 4);
    painter.setPen(pen);

    QVector<QPointF> centers;
    for (const Point& pt : wayPoints) {
        int a = pt.a;
        int b = pt.b;
        if (a < 1 || a > cols || b < 1 || b > rows) {
            qDebug() << "无效的坐标点: a=" << a << "b=" << b;
            continue;
        }
        double cx = ((a - 0.5) * cellWidth);
        double cy = (mapHeight - (b - 0.5) * cellHeight);
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
