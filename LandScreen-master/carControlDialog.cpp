#include "carControlDialog.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSlider>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {
constexpr quint8 kFrameVersion = 1;
constexpr quint8 kFrameImuOdom = 0x01;
constexpr quint8 kFrameMotorStatus = 0x02;
constexpr quint8 kFramePing = 0x80;
constexpr quint8 kFrameMotorArm = 0x82;
constexpr quint8 kFrameMotorDrive = 0x83;
constexpr quint8 kFrameMotorStop = 0x84;
constexpr quint8 kFrameCommandAck = 0x90;
constexpr quint16 kMotorArmGuard = 0xA55A;
constexpr quint16 kTelemetryReady = 1U << 1;
constexpr quint16 kMotorSystemReady = 1U << 0;
constexpr quint16 kMotorArmed = 1U << 1;
constexpr quint16 kMotorWatchdogTripped = 1U << 2;
constexpr quint16 kMotorCommandLimited = 1U << 3;
constexpr int kFreshTelemetryMs = 500;
constexpr int kFreshMotorStatusMs = 1200;

QFrame *makeCard(QWidget *parent)
{
    QFrame *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("controlCard"));
    card->setFrameShape(QFrame::NoFrame);
    return card;
}
}

CarSerialLink::CarSerialLink(QObject *parent)
    : QObject(parent)
{
    const QByteArray environmentPort = qgetenv("CAR_SERIAL_PORT");
    QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("LandScreen"));
    m_portPath = environmentPort.isEmpty()
        ? settings.value(QStringLiteral("car/serial_port"),
                         QStringLiteral("/dev/ttyACM0")).toString()
        : QString::fromUtf8(environmentPort);
    m_sequence = static_cast<quint16>(QRandomGenerator::global()->generate());
    m_pingNonce = static_cast<quint16>(QRandomGenerator::global()->generate());

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(1000);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &CarSerialLink::openSerial);
    m_reconnectTimer->start();

    m_pingTimer = new QTimer(this);
    m_pingTimer->setInterval(1000);
    connect(m_pingTimer, &QTimer::timeout, this, &CarSerialLink::sendPing);
    m_pingTimer->start();

    QTimer::singleShot(0, this, &CarSerialLink::openSerial);
}

CarSerialLink::~CarSerialLink()
{
    closeSerial(QStringLiteral("界面退出"));
}

QString CarSerialLink::portPath() const
{
    return m_portPath;
}

bool CarSerialLink::isOpen() const
{
    return m_fd >= 0;
}

void CarSerialLink::setPortPath(const QString &path)
{
    const QString cleaned = path.trimmed();
    if (cleaned.isEmpty() || cleaned == m_portPath)
        return;

    closeSerial(QStringLiteral("切换串口"));
    m_portPath = cleaned;
    QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("LandScreen"));
    settings.setValue(QStringLiteral("car/serial_port"), m_portPath);
    settings.sync();
    openSerial();
}

quint16 CarSerialLink::crc16(const QByteArray &data, int offset, int length)
{
    quint16 crc = 0xFFFF;
    for (int i = 0; i < length; ++i) {
        crc ^= static_cast<quint16>(
            static_cast<quint8>(data.at(offset + i))) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000U) ?
                static_cast<quint16>((crc << 1) ^ 0x1021U) :
                static_cast<quint16>(crc << 1);
    }
    return crc;
}

quint16 CarSerialLink::readU16(const QByteArray &data, int offset)
{
    return static_cast<quint16>(static_cast<quint8>(data.at(offset))) |
        (static_cast<quint16>(static_cast<quint8>(data.at(offset + 1))) << 8);
}

qint16 CarSerialLink::readI16(const QByteArray &data, int offset)
{
    return static_cast<qint16>(readU16(data, offset));
}

quint32 CarSerialLink::readU32(const QByteArray &data, int offset)
{
    return static_cast<quint32>(static_cast<quint8>(data.at(offset))) |
        (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8) |
        (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16) |
        (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

qint32 CarSerialLink::readI32(const QByteArray &data, int offset)
{
    return static_cast<qint32>(readU32(data, offset));
}

void CarSerialLink::appendU16(QByteArray &data, quint16 value)
{
    data.append(static_cast<char>(value & 0xFFU));
    data.append(static_cast<char>((value >> 8) & 0xFFU));
}

void CarSerialLink::appendI16(QByteArray &data, qint16 value)
{
    appendU16(data, static_cast<quint16>(value));
}

quint16 CarSerialLink::nextSequence()
{
    ++m_sequence;
    return m_sequence;
}

QByteArray CarSerialLink::makeFrame(quint8 type, const QByteArray &payload) const
{
    QByteArray frame;
    frame.reserve(payload.size() + 7);
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(kFrameVersion));
    frame.append(static_cast<char>(type));
    frame.append(static_cast<char>(payload.size()));
    frame.append(payload);
    const quint16 checksum = crc16(frame, 2, payload.size() + 3);
    appendU16(frame, checksum);
    return frame;
}

bool CarSerialLink::queueFrame(
    quint8 type, const QByteArray &payload, bool urgent)
{
    if (!isOpen())
        return false;
    if (urgent)
        m_txQueue.clear();
    m_txQueue.append(makeFrame(type, payload));
    if (m_txQueue.size() > 512) {
        m_txQueue.clear();
        emit protocolWarning(QStringLiteral("串口发送队列溢出，已丢弃待发命令"));
        return false;
    }
    flushTx();
    return true;
}

void CarSerialLink::flushTx()
{
#ifdef Q_OS_UNIX
    while (m_fd >= 0 && !m_txQueue.isEmpty()) {
        const ssize_t written = ::write(
            m_fd, m_txQueue.constData(), static_cast<size_t>(m_txQueue.size()));
        if (written > 0) {
            m_txQueue.remove(0, static_cast<int>(written));
        } else if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            closeSerial(QStringLiteral("串口写入失败"));
            return;
        }
    }
#endif
}

quint16 CarSerialLink::sendArm()
{
    const quint16 sequence = nextSequence();
    QByteArray payload;
    appendU16(payload, sequence);
    appendU16(payload, kMotorArmGuard);
    queueFrame(kFrameMotorArm, payload);
    return sequence;
}

quint16 CarSerialLink::sendDrive(qint16 leftPermille, qint16 rightPermille)
{
    const quint16 sequence = nextSequence();
    QByteArray payload;
    appendU16(payload, sequence);
    appendI16(payload, leftPermille);
    appendI16(payload, rightPermille);
    queueFrame(kFrameMotorDrive, payload);
    return sequence;
}

quint16 CarSerialLink::sendStop(quint8 reason)
{
    const quint16 sequence = nextSequence();
    QByteArray payload;
    appendU16(payload, sequence);
    payload.append(static_cast<char>(reason));
    /* Two identical STOP frames cover the rare case where an urgent clear
       interrupts a partially written DRIVE frame. STOP is intentionally
       idempotent in the controller firmware. */
    queueFrame(kFrameMotorStop, payload, true);
    queueFrame(kFrameMotorStop, payload);
    return sequence;
}

void CarSerialLink::openSerial()
{
    if (isOpen())
        return;
#ifdef Q_OS_UNIX
    const QByteArray nativePath = QFile::encodeName(m_portPath);
    const int fd = ::open(nativePath.constData(),
                          O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        emit serialStateChanged(false,
            QStringLiteral("等待串口 %1").arg(m_portPath));
        return;
    }

    termios options {};
    if (tcgetattr(fd, &options) != 0) {
        ::close(fd);
        emit serialStateChanged(false, QStringLiteral("无法读取串口参数"));
        return;
    }
    cfmakeraw(&options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CRTSCTS;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        ::close(fd);
        emit serialStateChanged(false, QStringLiteral("无法设置 115200 8N1"));
        return;
    }

    m_fd = fd;
    m_rxBuffer.clear();
    m_txQueue.clear();
    m_readNotifier = new QSocketNotifier(
        static_cast<qintptr>(m_fd), QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated,
            this, &CarSerialLink::onReadable);
    emit serialStateChanged(true,
        QStringLiteral("已打开 %1").arg(m_portPath));
    sendPing();
#else
    emit serialStateChanged(false, QStringLiteral("小车串口仅支持 Linux/Nano"));
#endif
}

void CarSerialLink::closeSerial(const QString &reason)
{
    const bool wasOpen = isOpen();
    if (m_readNotifier) {
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
#ifdef Q_OS_UNIX
    if (m_fd >= 0)
        ::close(m_fd);
#endif
    m_fd = -1;
    m_rxBuffer.clear();
    m_txQueue.clear();
    if (wasOpen)
        emit serialStateChanged(false, reason);
}

void CarSerialLink::onReadable()
{
#ifdef Q_OS_UNIX
    char buffer[512];
    while (m_fd >= 0) {
        const ssize_t count = ::read(m_fd, buffer, sizeof(buffer));
        if (count > 0) {
            m_rxBuffer.append(buffer, static_cast<int>(count));
        } else if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            closeSerial(QStringLiteral("串口读取失败或设备断开"));
            return;
        }
    }
    parseRx();
    flushTx();
#endif
}

void CarSerialLink::parseRx()
{
    static const QByteArray header("\xAA\x55", 2);
    while (m_rxBuffer.size() >= 7) {
        const int start = m_rxBuffer.indexOf(header);
        if (start < 0) {
            const bool keepAa = !m_rxBuffer.isEmpty() &&
                static_cast<quint8>(m_rxBuffer.back()) == 0xAA;
            m_rxBuffer = keepAa ? QByteArray(1, static_cast<char>(0xAA)) :
                                  QByteArray();
            return;
        }
        if (start > 0)
            m_rxBuffer.remove(0, start);
        if (m_rxBuffer.size() < 7)
            return;

        const quint8 version = static_cast<quint8>(m_rxBuffer.at(2));
        const quint8 type = static_cast<quint8>(m_rxBuffer.at(3));
        const int payloadSize = static_cast<quint8>(m_rxBuffer.at(4));
        if (version != kFrameVersion || payloadSize > 64) {
            m_rxBuffer.remove(0, 1);
            continue;
        }
        const int frameSize = payloadSize + 7;
        if (m_rxBuffer.size() < frameSize)
            return;

        const quint16 expected = readU16(m_rxBuffer, frameSize - 2);
        const quint16 actual = crc16(m_rxBuffer, 2, payloadSize + 3);
        if (actual != expected) {
            m_rxBuffer.remove(0, 1);
            emit protocolWarning(QStringLiteral("收到 CRC 错误帧，已重同步"));
            continue;
        }

        handleFrame(type, m_rxBuffer.mid(5, payloadSize));
        m_rxBuffer.remove(0, frameSize);
    }
}

void CarSerialLink::handleFrame(quint8 type, const QByteArray &payload)
{
    if (type == kFrameImuOdom && payload.size() == 31) {
        CarTelemetry telemetry;
        telemetry.sequence = readU16(payload, 0);
        telemetry.timestampUs = readU32(payload, 2);
        telemetry.accelX = readI16(payload, 6);
        telemetry.accelY = readI16(payload, 8);
        telemetry.accelZ = readI16(payload, 10);
        telemetry.gyroX = readI16(payload, 12);
        telemetry.gyroY = readI16(payload, 14);
        telemetry.gyroZ = readI16(payload, 16);
        telemetry.temperature = readI16(payload, 18);
        telemetry.encoderLeft = readI32(payload, 20);
        telemetry.encoderRight = readI32(payload, 24);
        telemetry.status = readU16(payload, 28);
        telemetry.calibrationPercent =
            static_cast<quint8>(payload.at(30));
        emit telemetryReceived(telemetry);
    } else if (type == kFrameMotorStatus &&
               (payload.size() == 16 || payload.size() == 18)) {
        CarMotorStatus status;
        status.statusBits = readU16(payload, 0);
        status.lastSequence = readU16(payload, 2);
        status.targetLeft = readI16(payload, 4);
        status.targetRight = readI16(payload, 6);
        status.appliedLeft = readI16(payload, 8);
        status.appliedRight = readI16(payload, 10);
        status.watchdogRemainingMs = readU16(payload, 12);
        status.stopReason = static_cast<quint8>(payload.at(14));
        status.maxCommandPercent = static_cast<quint8>(payload.at(15));
        if (payload.size() == 18)
            status.batteryMillivolts = readU16(payload, 16);
        emit motorStatusReceived(status);
    } else if (type == kFrameCommandAck && payload.size() == 4) {
        emit commandAcknowledged(static_cast<quint8>(payload.at(0)),
                                 readU16(payload, 1),
                                 static_cast<quint8>(payload.at(3)));
    }
}

void CarSerialLink::sendPing()
{
    if (!isOpen())
        return;
    QByteArray payload;
    appendU16(payload, ++m_pingNonce);
    queueFrame(kFramePing, payload);
}

VirtualJoystick::VirtualJoystick(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::CrossCursor);
}

QPointF VirtualJoystick::value() const
{
    return m_value;
}

void VirtualJoystick::reset()
{
    m_pressed = false;
    m_value = QPointF();
    update();
    emit valueChanged(0.0, 0.0);
    emit released();
}

QPointF VirtualJoystick::eventPosition(QMouseEvent *event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}

void VirtualJoystick::updateFromPosition(const QPointF &position)
{
    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = std::max(1.0, std::min(width(), height()) * 0.38);
    QPointF delta = position - center;
    const double length = std::hypot(delta.x(), delta.y());
    if (length > radius)
        delta *= radius / length;

    double steering = delta.x() / radius;
    double throttle = -delta.y() / radius;
    if (std::hypot(steering, throttle) < 0.08) {
        steering = 0.0;
        throttle = 0.0;
    }
    m_value = QPointF(steering, throttle);
    update();
    emit valueChanged(steering, throttle);
}

void VirtualJoystick::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(width() / 2.0, height() / 2.0);
    const double radius = std::min(width(), height()) * 0.38;

    painter.setPen(QPen(QColor("#B8C3CC"), 2));
    painter.setBrush(QColor("#F8FAFC"));
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(QColor("#D9E1E7"), 1, Qt::DashLine));
    painter.drawLine(QPointF(center.x() - radius, center.y()),
                     QPointF(center.x() + radius, center.y()));
    painter.drawLine(QPointF(center.x(), center.y() - radius),
                     QPointF(center.x(), center.y() + radius));

    painter.setPen(QColor("#6B7B88"));
    painter.drawText(QRectF(center.x() - 35, center.y() - radius - 28, 70, 24),
                     Qt::AlignCenter, QStringLiteral("前进"));
    painter.drawText(QRectF(center.x() - 35, center.y() + radius + 4, 70, 24),
                     Qt::AlignCenter, QStringLiteral("后退"));
    painter.drawText(QRectF(center.x() - radius - 50, center.y() - 12, 45, 24),
                     Qt::AlignCenter, QStringLiteral("左"));
    painter.drawText(QRectF(center.x() + radius + 5, center.y() - 12, 45, 24),
                     Qt::AlignCenter, QStringLiteral("右"));

    const QPointF knobCenter = center +
        QPointF(m_value.x() * radius, -m_value.y() * radius);
    painter.setPen(QPen(isEnabled() ? QColor("#0E6EB8") : QColor("#9AA8B2"), 3));
    painter.setBrush(isEnabled() ? QColor("#1677C8") : QColor("#C5CED5"));
    painter.drawEllipse(knobCenter, 34, 34);
    painter.setBrush(QColor(255, 255, 255, 110));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(knobCenter + QPointF(-9, -9), 9, 9);
}

void VirtualJoystick::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !isEnabled())
        return;
    m_pressed = true;
    updateFromPosition(eventPosition(event));
    event->accept();
}

void VirtualJoystick::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed || !isEnabled())
        return;
    updateFromPosition(eventPosition(event));
    event->accept();
}

void VirtualJoystick::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_pressed)
        return;
    reset();
    event->accept();
}

void VirtualJoystick::hideEvent(QHideEvent *event)
{
    reset();
    QWidget::hideEvent(event);
}

CarControlDialog::CarControlDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("小车控制"));
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    m_link = new CarSerialLink(this);
    buildUi();

    connect(m_link, &CarSerialLink::serialStateChanged,
            this, &CarControlDialog::onSerialStateChanged);
    connect(m_link, &CarSerialLink::telemetryReceived,
            this, &CarControlDialog::onTelemetry);
    connect(m_link, &CarSerialLink::motorStatusReceived,
            this, &CarControlDialog::onMotorStatus);
    connect(m_link, &CarSerialLink::commandAcknowledged,
            this, &CarControlDialog::onCommandAck);
    connect(m_link, &CarSerialLink::protocolWarning, this,
            [this](const QString &message) {
                m_noticeLabel->setText(message);
                m_noticeLabel->setStyleSheet(
                    QStringLiteral("color:#A95C00; font-size:15px;"));
            });

    connect(m_joystick, &VirtualJoystick::valueChanged,
            this, &CarControlDialog::onJoystickChanged);
    connect(m_joystick, &VirtualJoystick::released, this, [this]() {
        if (m_armed && m_manualMode->isChecked() && m_link->isOpen())
            m_link->sendDrive(0, 0);
    });

    m_driveTimer = new QTimer(this);
    m_driveTimer->setInterval(50);
    connect(m_driveTimer, &QTimer::timeout,
            this, &CarControlDialog::sendDriveHeartbeat);
    m_driveTimer->start();

    m_healthTimer = new QTimer(this);
    m_healthTimer->setInterval(100);
    connect(m_healthTimer, &QTimer::timeout,
            this, &CarControlDialog::refreshSafetyState);
    m_healthTimer->start();

    m_armLongPressTimer = new QTimer(this);
    m_armLongPressTimer->setSingleShot(true);
    m_armLongPressTimer->setInterval(2000);
    connect(m_armLongPressTimer, &QTimer::timeout,
            this, &CarControlDialog::sendArmAfterLongPress);
    connect(m_armButton, &QPushButton::pressed,
            this, &CarControlDialog::beginArmPress);
    connect(m_armButton, &QPushButton::released,
            this, &CarControlDialog::finishArmPress);

    m_rateWindow.start();
    refreshSafetyState();
}

CarControlDialog::~CarControlDialog()
{
    if (m_link && m_link->isOpen())
        m_link->sendStop(2);
}

QLabel *CarControlDialog::makeValueLabel(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setStyleSheet(QStringLiteral(
        "font-size:17px; font-weight:600; color:#25313C;"));
    return label;
}

void CarControlDialog::buildUi()
{
    setStyleSheet(QStringLiteral(
        "QDialog { background:#EEF2F5; color:#25313C; }"
        "QFrame#controlCard { background:#FFFFFF; border:1px solid #D7E0E6;"
        " border-radius:10px; }"
        "QLabel#cardTitle { font-size:20px; font-weight:700; color:#25313C; }"
        "QPushButton { min-height:44px; border-radius:7px; padding:5px 14px;"
        " font-size:16px; font-weight:600; }"
        "QRadioButton { font-size:16px; spacing:7px; }"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(12);

    QFrame *topBar = new QFrame(this);
    topBar->setStyleSheet(QStringLiteral(
        "QFrame { background:#FFFFFF; border-radius:9px; }"));
    QHBoxLayout *top = new QHBoxLayout(topBar);
    top->setContentsMargins(12, 8, 12, 8);
    top->setSpacing(14);
    QPushButton *closeButton = new QPushButton(QStringLiteral("关闭界面"), topBar);
    closeButton->setMinimumWidth(120);
    closeButton->setStyleSheet(QStringLiteral(
        "background:#5F6B7A; color:white; border:1px solid #45515E;"));
    connect(closeButton, &QPushButton::clicked,
            this, &CarControlDialog::closeInterface);
    QLabel *title = new QLabel(QStringLiteral("小车控制"), topBar);
    title->setStyleSheet(QStringLiteral(
        "font-size:26px; font-weight:800; color:#1F2D38;"));
    QLabel *subtitle = new QLabel(
        QStringLiteral("C07A / S27F · 仅调试模式允许手动控制"), topBar);
    subtitle->setStyleSheet(QStringLiteral("font-size:15px; color:#687985;"));
    m_manualMode = new QRadioButton(QStringLiteral("手动调试"), topBar);
    m_autoMode = new QRadioButton(QStringLiteral("自动比赛"), topBar);
    m_manualMode->setChecked(true);
    connect(m_autoMode, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            requestStop(3, false);
            m_noticeLabel->setText(QStringLiteral(
                "自动比赛模式已锁定人工摇杆；本页仅监视小车状态。"));
        }
        refreshSafetyState();
    });
    m_emergencyButton = new QPushButton(QStringLiteral("急停"), topBar);
    m_emergencyButton->setMinimumWidth(120);
    m_emergencyButton->setStyleSheet(QStringLiteral(
        "background:#D9363E; color:white; border:2px solid #B71924;"
        "font-size:20px; font-weight:800;"));
    connect(m_emergencyButton, &QPushButton::clicked,
            this, &CarControlDialog::emergencyStop);
    top->addWidget(closeButton);
    top->addWidget(title);
    top->addWidget(subtitle);
    top->addStretch();
    top->addWidget(m_manualMode);
    top->addWidget(m_autoMode);
    top->addWidget(m_emergencyButton);
    root->addWidget(topBar);

    QHBoxLayout *content = new QHBoxLayout();
    content->setSpacing(12);

    QFrame *statusCard = makeCard(this);
    statusCard->setMinimumWidth(285);
    statusCard->setMaximumWidth(330);
    QVBoxLayout *statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(18, 16, 18, 16);
    statusLayout->setSpacing(11);
    QLabel *statusTitle = new QLabel(QStringLiteral("小车状态"), statusCard);
    statusTitle->setObjectName(QStringLiteral("cardTitle"));
    statusLayout->addWidget(statusTitle);
    QGridLayout *statusGrid = new QGridLayout();
    statusGrid->setHorizontalSpacing(10);
    statusGrid->setVerticalSpacing(11);
    auto addStatus = [&](int row, const QString &name, QLabel *&value) {
        QLabel *nameLabel = new QLabel(name, statusCard);
        nameLabel->setStyleSheet(QStringLiteral("font-size:16px; color:#6A7A86;"));
        value = makeValueLabel(QStringLiteral("--"), statusCard);
        statusGrid->addWidget(nameLabel, row, 0);
        statusGrid->addWidget(value, row, 1);
    };
    addStatus(0, QStringLiteral("串口"), m_serialValue);
    addStatus(1, QStringLiteral("控制固件"), m_firmwareValue);
    addStatus(2, QStringLiteral("IMU 校准"), m_calibrationValue);
    addStatus(3, QStringLiteral("电机电池"), m_batteryValue);
    addStatus(4, QStringLiteral("遥测速率"), m_telemetryRateValue);
    addStatus(5, QStringLiteral("左编码器累计"), m_encoderLeftValue);
    addStatus(6, QStringLiteral("右编码器累计"), m_encoderRightValue);
    addStatus(7, QStringLiteral("目标 PWM"), m_targetValue);
    addStatus(8, QStringLiteral("实际 PWM"), m_appliedValue);
    addStatus(9, QStringLiteral("看门狗余量"), m_watchdogValue);
    statusLayout->addLayout(statusGrid);
    statusLayout->addStretch();
    QPushButton *serialSettings = new QPushButton(
        QStringLiteral("串口设置"), statusCard);
    serialSettings->setStyleSheet(QStringLiteral(
        "background:#5F6B7A; color:white;"));
    connect(serialSettings, &QPushButton::clicked,
            this, &CarControlDialog::showSerialSettings);
    statusLayout->addWidget(serialSettings);

    QFrame *safetyCard = makeCard(this);
    safetyCard->setMinimumWidth(315);
    safetyCard->setMaximumWidth(360);
    QVBoxLayout *safetyLayout = new QVBoxLayout(safetyCard);
    safetyLayout->setContentsMargins(18, 16, 18, 16);
    safetyLayout->setSpacing(12);
    QLabel *safetyTitle = new QLabel(QStringLiteral("电机安全"), safetyCard);
    safetyTitle->setObjectName(QStringLiteral("cardTitle"));
    safetyLayout->addWidget(safetyTitle);
    m_stateBanner = new QLabel(QStringLiteral("电机未使能"), safetyCard);
    m_stateBanner->setAlignment(Qt::AlignCenter);
    m_stateBanner->setMinimumHeight(50);
    safetyLayout->addWidget(m_stateBanner);
    QLabel *hint = new QLabel(
        QStringLiteral("全部条件满足后，持续按住启动按钮 2 秒。松手、断开串口或停止刷新命令都会触发停车。"),
        safetyCard);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-size:15px; color:#667985;"));
    safetyLayout->addWidget(hint);
    m_checkSerial = new QLabel(safetyCard);
    m_checkCalibration = new QLabel(safetyCard);
    m_checkCentered = new QLabel(safetyCard);
    safetyLayout->addWidget(m_checkSerial);
    safetyLayout->addWidget(m_checkCalibration);
    safetyLayout->addWidget(m_checkCentered);
    m_armButton = new QPushButton(QStringLiteral("按住 2 秒启动电机"), safetyCard);
    m_armButton->setMinimumHeight(68);
    safetyLayout->addWidget(m_armButton);
    QLabel *limitTitle = new QLabel(QStringLiteral("调试速度上限"), safetyCard);
    limitTitle->setStyleSheet(QStringLiteral("font-size:16px; color:#596B77;"));
    m_speedValue = makeValueLabel(QStringLiteral("15%"), safetyCard);
    QHBoxLayout *limitHeader = new QHBoxLayout();
    limitHeader->addWidget(limitTitle);
    limitHeader->addStretch();
    limitHeader->addWidget(m_speedValue);
    safetyLayout->addLayout(limitHeader);
    m_speedSlider = new QSlider(Qt::Horizontal, safetyCard);
    m_speedSlider->setRange(10, 40);
    m_speedSlider->setValue(15);
    m_speedSlider->setTickInterval(5);
    m_speedSlider->setTickPosition(QSlider::TicksBelow);
    connect(m_speedSlider, &QSlider::valueChanged, this, [this](int value) {
        m_speedValue->setText(QStringLiteral("%1%").arg(value));
    });
    safetyLayout->addWidget(m_speedSlider);
    QLabel *limitHint = new QLabel(
        QStringLiteral("首次架空方向测试保持 15%；固件硬上限 40%。"), safetyCard);
    limitHint->setWordWrap(true);
    limitHint->setStyleSheet(QStringLiteral("font-size:14px; color:#A45A00;"));
    safetyLayout->addWidget(limitHint);
    safetyLayout->addStretch();

    QFrame *joystickCard = makeCard(this);
    QVBoxLayout *joystickLayout = new QVBoxLayout(joystickCard);
    joystickLayout->setContentsMargins(18, 16, 18, 16);
    joystickLayout->setSpacing(8);
    QLabel *joystickTitle = new QLabel(QStringLiteral("运动摇杆"), joystickCard);
    joystickTitle->setObjectName(QStringLiteral("cardTitle"));
    m_joystickNumbers = new QLabel(
        QStringLiteral("转向 +0.00   油门 +0.00   左/右 0 / 0"), joystickCard);
    m_joystickNumbers->setAlignment(Qt::AlignCenter);
    m_joystickNumbers->setStyleSheet(QStringLiteral(
        "font-size:16px; color:#4C6170; font-family:monospace;"));
    m_joystick = new VirtualJoystick(joystickCard);
    joystickLayout->addWidget(joystickTitle);
    joystickLayout->addWidget(m_joystickNumbers);
    joystickLayout->addWidget(m_joystick, 1);
    QLabel *releaseHint = new QLabel(
        QStringLiteral("触控按下后拖动；手指离开立即发送左右轮 0。"), joystickCard);
    releaseHint->setAlignment(Qt::AlignCenter);
    releaseHint->setStyleSheet(QStringLiteral("font-size:14px; color:#687985;"));
    joystickLayout->addWidget(releaseHint);

    content->addWidget(statusCard);
    content->addWidget(safetyCard);
    content->addWidget(joystickCard, 1);
    root->addLayout(content, 1);

    m_noticeLabel = new QLabel(
        QStringLiteral("等待 C07A 控制固件和电机状态帧。禁止在车轮落地时进行首次测试。"), this);
    m_noticeLabel->setAlignment(Qt::AlignCenter);
    m_noticeLabel->setWordWrap(true);
    m_noticeLabel->setMinimumHeight(34);
    m_noticeLabel->setStyleSheet(QStringLiteral(
        "background:#FFF4DD; color:#925400; border:1px solid #E8C982;"
        "border-radius:6px; font-size:15px; padding:5px;"));
    root->addWidget(m_noticeLabel);
}

void CarControlDialog::onSerialStateChanged(bool open, const QString &detail)
{
    m_serialOpen = open;
    m_serialValue->setText(open ? QStringLiteral("已打开") : QStringLiteral("未连接"));
    m_serialValue->setToolTip(detail);
    if (!open) {
        m_calibrated = false;
        m_systemReady = false;
        m_armed = false;
        m_armPending = false;
        m_telemetryAge.invalidate();
        m_motorStatusAge.invalidate();
        m_joystick->reset();
        m_batteryValue->setText(QStringLiteral("--"));
    }
    refreshSafetyState();
}

void CarControlDialog::onTelemetry(const CarTelemetry &telemetry)
{
    if (!m_telemetryAge.isValid())
        m_telemetryAge.start();
    else
        m_telemetryAge.restart();
    m_calibrated = (telemetry.status & kTelemetryReady) != 0 &&
        telemetry.calibrationPercent == 100;
    m_calibrationValue->setText(m_calibrated
        ? QStringLiteral("完成")
        : QStringLiteral("%1%").arg(telemetry.calibrationPercent));
    m_encoderLeftValue->setText(QString::number(telemetry.encoderLeft));
    m_encoderRightValue->setText(QString::number(telemetry.encoderRight));

    ++m_telemetryFrames;
    if (m_rateWindow.elapsed() >= 1000) {
        const double rate = m_telemetryFrames * 1000.0 / m_rateWindow.elapsed();
        m_telemetryRateValue->setText(QStringLiteral("%1 Hz").arg(rate, 0, 'f', 1));
        m_telemetryFrames = 0;
        m_rateWindow.restart();
    }
    refreshSafetyState();
}

void CarControlDialog::onMotorStatus(const CarMotorStatus &status)
{
    if (!m_motorStatusAge.isValid())
        m_motorStatusAge.start();
    else
        m_motorStatusAge.restart();
    m_systemReady = (status.statusBits & kMotorSystemReady) != 0;
    m_armed = (status.statusBits & kMotorArmed) != 0;
    if (m_armed)
        m_armPending = false;
    m_firmwareValue->setText(status.batteryMillivolts > 0
        ? QStringLiteral("控制版 v2") : QStringLiteral("控制版 v1"));
    m_targetValue->setText(QStringLiteral("%1 / %2‰")
        .arg(status.targetLeft).arg(status.targetRight));
    m_appliedValue->setText(QStringLiteral("%1 / %2‰")
        .arg(status.appliedLeft).arg(status.appliedRight));
    m_watchdogValue->setText(m_armed
        ? QStringLiteral("%1 ms").arg(status.watchdogRemainingMs)
        : QStringLiteral("已停止"));
    if (status.batteryMillivolts > 0) {
        m_batteryValue->setText(QStringLiteral("%1 V")
            .arg(status.batteryMillivolts / 1000.0, 0, 'f', 2));
        m_batteryValue->setToolTip(QStringLiteral(
            "C07A PA15 ADC 实测电机供电电压；首次使用请与万用表校准。"));
    } else {
        m_batteryValue->setText(QStringLiteral("固件未上报"));
        m_batteryValue->setToolTip(QStringLiteral(
            "旧版 16 字节电机状态帧不包含电池电压。"));
    }

    if ((status.statusBits & kMotorWatchdogTripped) != 0) {
        m_noticeLabel->setText(QStringLiteral(
            "控制板看门狗已触发并撤销电机使能；需要重新长按启动。"));
        m_noticeLabel->setStyleSheet(QStringLiteral(
            "background:#FFE9EA; color:#A71925; border:1px solid #E5A0A6;"
            "border-radius:6px; font-size:15px; padding:5px;"));
    } else if ((status.statusBits & kMotorCommandLimited) != 0) {
        m_noticeLabel->setText(QStringLiteral(
            "控制板已将命令限制到固件硬上限。"));
    } else if (!m_armed && status.stopReason != 0) {
        m_noticeLabel->setText(QStringLiteral("最近停车原因：%1")
            .arg(stopReasonText(status.stopReason)));
    }
    refreshSafetyState();
}

void CarControlDialog::onCommandAck(
    quint8 commandType, quint16 sequence, quint8 result)
{
    if (commandType == kFrameMotorArm && sequence == m_armRequestSequence) {
        m_armPending = false;
        if (result == 0) {
            m_noticeLabel->setText(QStringLiteral(
                "控制板已接受使能请求，等待电机状态确认。"));
        } else {
            m_noticeLabel->setText(QStringLiteral("启动被控制板拒绝：%1")
                .arg(commandResultText(result)));
            m_noticeLabel->setStyleSheet(QStringLiteral(
                "background:#FFE9EA; color:#A71925; border:1px solid #E5A0A6;"
                "border-radius:6px; font-size:15px; padding:5px;"));
        }
    } else if (commandType == kFrameMotorDrive && result > 1) {
        m_noticeLabel->setText(QStringLiteral("运动命令未执行：%1")
            .arg(commandResultText(result)));
    }
    refreshSafetyState();
}

void CarControlDialog::onJoystickChanged(double steering, double throttle)
{
    m_steering = steering;
    m_throttle = throttle;
    const auto command = mixedDriveCommand();
    m_joystickNumbers->setText(QStringLiteral(
        "转向 %1%2   油门 %3%4   左/右 %5 / %6")
        .arg(steering >= 0 ? QStringLiteral("+") : QString())
        .arg(steering, 0, 'f', 2)
        .arg(throttle >= 0 ? QStringLiteral("+") : QString())
        .arg(throttle, 0, 'f', 2)
        .arg(command.first).arg(command.second));
    refreshSafetyState();
}

QPair<qint16, qint16> CarControlDialog::mixedDriveCommand() const
{
    double left = m_throttle + m_steering;
    double right = m_throttle - m_steering;
    const double maximum = std::max({1.0, std::abs(left), std::abs(right)});
    left /= maximum;
    right /= maximum;
    const double limitPermille = m_speedSlider->value() * 10.0;
    return qMakePair(static_cast<qint16>(std::lround(left * limitPermille)),
                     static_cast<qint16>(std::lround(right * limitPermille)));
}

void CarControlDialog::sendDriveHeartbeat()
{
    if (!m_armed || !m_manualMode->isChecked() || !telemetryFresh() ||
        !motorStatusFresh() || !m_link->isOpen())
        return;
    const auto command = mixedDriveCommand();
    m_link->sendDrive(command.first, command.second);
}

bool CarControlDialog::telemetryFresh() const
{
    return m_telemetryAge.isValid() &&
        m_telemetryAge.elapsed() <= kFreshTelemetryMs;
}

bool CarControlDialog::motorStatusFresh() const
{
    return m_motorStatusAge.isValid() &&
        m_motorStatusAge.elapsed() <= kFreshMotorStatusMs;
}

bool CarControlDialog::joystickCentered() const
{
    return std::hypot(m_steering, m_throttle) < 0.02;
}

bool CarControlDialog::canArm() const
{
    return m_serialOpen && telemetryFresh() && motorStatusFresh() &&
        m_calibrated && m_systemReady && joystickCentered() &&
        m_manualMode->isChecked() && !m_armPending;
}

void CarControlDialog::setChecklistLabel(
    QLabel *label, bool ok, const QString &text)
{
    label->setText(QStringLiteral("%1  %2").arg(ok ? QStringLiteral("✓") :
                                                QStringLiteral("○"), text));
    label->setStyleSheet(QStringLiteral(
        "font-size:16px; font-weight:600; color:%1; padding:3px;")
        .arg(ok ? QStringLiteral("#198754") : QStringLiteral("#8A99A4")));
}

void CarControlDialog::refreshSafetyState()
{
    const bool serialHealthy = m_serialOpen && telemetryFresh() &&
        motorStatusFresh();
    setChecklistLabel(m_checkSerial, serialHealthy,
                      QStringLiteral("C07A 双向通信与控制状态正常"));
    setChecklistLabel(m_checkCalibration, m_calibrated && m_systemReady,
                      QStringLiteral("IMU 校准完成，控制板 READY"));
    setChecklistLabel(m_checkCentered, joystickCentered(),
                      QStringLiteral("摇杆已回中"));

    if (!motorStatusFresh()) {
        m_systemReady = false;
        m_armed = false;
        m_firmwareValue->setText(QStringLiteral("未检测到"));
    }
    if (!telemetryFresh())
        m_calibrated = false;

    m_joystick->setEnabled(m_armed && m_manualMode->isChecked() &&
                           serialHealthy);
    if (!m_joystick->isEnabled() && !joystickCentered())
        m_joystick->reset();
    m_speedSlider->setEnabled(!m_armed && !m_armPending &&
                              m_manualMode->isChecked());
    updateArmUi();
}

void CarControlDialog::updateArmUi()
{
    if (m_armed) {
        m_stateBanner->setText(QStringLiteral("电机已使能 · 300 ms 看门狗运行中"));
        m_stateBanner->setStyleSheet(QStringLiteral(
            "background:#E8F7EE; color:#147A42; border:1px solid #9BD2B2;"
            "border-radius:7px; font-size:17px; font-weight:700;"));
        m_armButton->setText(QStringLiteral("轻触正常停车"));
        m_armButton->setEnabled(m_manualMode->isChecked());
        m_armButton->setStyleSheet(QStringLiteral(
            "background:#E9A12A; color:white; border:1px solid #C57C08;"
            "font-size:19px; font-weight:800;"));
    } else if (m_armPending) {
        m_stateBanner->setText(QStringLiteral("等待控制板确认"));
        m_stateBanner->setStyleSheet(QStringLiteral(
            "background:#FFF4DD; color:#925400; border:1px solid #E8C982;"
            "border-radius:7px; font-size:17px; font-weight:700;"));
        m_armButton->setText(QStringLiteral("使能请求已发送…"));
        m_armButton->setEnabled(false);
    } else {
        m_stateBanner->setText(QStringLiteral("电机未使能"));
        m_stateBanner->setStyleSheet(QStringLiteral(
            "background:#EEF2F5; color:#5D6F7B; border:1px solid #CFD8DE;"
            "border-radius:7px; font-size:17px; font-weight:700;"));
        m_armButton->setText(canArm() ? QStringLiteral("按住 2 秒启动电机") :
            QStringLiteral("等待安全条件"));
        m_armButton->setEnabled(canArm());
        m_armButton->setStyleSheet(QStringLiteral(
            "QPushButton { background:#1677C8; color:white; border:1px solid #0E609F;"
            " font-size:19px; font-weight:800; }"
            "QPushButton:disabled { background:#B9C5CD; color:#EDF1F3;"
            " border-color:#AAB6BE; }"));
    }
}

void CarControlDialog::beginArmPress()
{
    m_pressedWhileArmed = m_armed;
    m_longPressTriggered = false;
    if (m_pressedWhileArmed) {
        m_armButton->setText(QStringLiteral("松手后停车"));
    } else if (canArm()) {
        m_armButton->setText(QStringLiteral("继续按住… 2 秒"));
        m_armLongPressTimer->start();
    }
}

void CarControlDialog::finishArmPress()
{
    if (m_pressedWhileArmed) {
        requestStop(0, false);
    } else if (m_armLongPressTimer->isActive()) {
        m_armLongPressTimer->stop();
        m_noticeLabel->setText(QStringLiteral("长按不足 2 秒，未发送启动命令。"));
    }
    m_pressedWhileArmed = false;
    m_longPressTriggered = false;
    refreshSafetyState();
}

void CarControlDialog::sendArmAfterLongPress()
{
    if (!canArm()) {
        m_noticeLabel->setText(QStringLiteral("安全条件在长按期间发生变化，启动已取消。"));
        refreshSafetyState();
        return;
    }
    m_longPressTriggered = true;
    m_armPending = true;
    m_armRequestSequence = m_link->sendArm();
    m_noticeLabel->setText(QStringLiteral("使能请求已发送，等待 CRC 回执。"));
    updateArmUi();
}

void CarControlDialog::requestStop(quint8 reason, bool repeat)
{
    m_armLongPressTimer->stop();
    m_armPending = false;
    if (m_joystick)
        m_joystick->reset();
    if (m_link && m_link->isOpen()) {
        m_link->sendStop(reason);
        if (repeat) {
            QTimer::singleShot(50, this, [this, reason]() {
                if (m_link->isOpen()) m_link->sendStop(reason);
            });
            QTimer::singleShot(100, this, [this, reason]() {
                if (m_link->isOpen()) m_link->sendStop(reason);
            });
        }
    }
}

void CarControlDialog::emergencyStop()
{
    requestStop(1, true);
    m_noticeLabel->setText(QStringLiteral(
        "急停命令已连续发送；若串口已断，C07A 会在 300 ms 看门狗超时后停车。"));
    m_noticeLabel->setStyleSheet(QStringLiteral(
        "background:#FFE9EA; color:#A71925; border:1px solid #E5A0A6;"
        "border-radius:6px; font-size:15px; font-weight:700; padding:5px;"));
    refreshSafetyState();
}

void CarControlDialog::showSerialSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("小车串口设置"));
    dialog.setMinimumWidth(620);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLabel *hint = new QLabel(QStringLiteral(
        "推荐填写 /dev/serial/by-id/... 的稳定路径；也可用 CAR_SERIAL_PORT 环境变量提供首次默认值。"),
        &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    QFormLayout *form = new QFormLayout();
    QLineEdit *pathEdit = new QLineEdit(m_link->portPath(), &dialog);
    pathEdit->setMinimumHeight(42);
    form->addRow(QStringLiteral("C07A 串口："), pathEdit);
    layout->addLayout(form);
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存并重连"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (pathEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("串口无效"),
                                 QStringLiteral("串口路径不能为空。"));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        requestStop(2, false);
        m_link->setPortPath(pathEdit->text());
    }
}

void CarControlDialog::closeInterface()
{
    requestStop(2, true);
    m_noticeLabel->setText(QStringLiteral("正在安全停车并关闭界面…"));
    QTimer::singleShot(150, this, &QDialog::close);
}

QString CarControlDialog::stopReasonText(quint8 reason)
{
    switch (reason) {
    case 1: return QStringLiteral("急停");
    case 2: return QStringLiteral("界面关闭或切换串口");
    case 3: return QStringLiteral("切换到自动模式");
    case 4: return QStringLiteral("300 ms 命令看门狗超时");
    case 5: return QStringLiteral("控制板不再 READY");
    case 6: return QStringLiteral("控制板上电/复位");
    default: return QStringLiteral("正常停车");
    }
}

QString CarControlDialog::commandResultText(quint8 result)
{
    switch (result) {
    case 0: return QStringLiteral("成功");
    case 1: return QStringLiteral("已被固件限幅");
    case 2: return QStringLiteral("控制板尚未 READY");
    case 3: return QStringLiteral("电机未使能");
    case 4: return QStringLiteral("启动保护字错误");
    case 5: return QStringLiteral("命令序号陈旧或重复");
    case 6: return QStringLiteral("命令长度错误");
    default: return QStringLiteral("未知错误 %1").arg(result);
    }
}

void CarControlDialog::hideEvent(QHideEvent *event)
{
    if (m_link && (m_armed || m_armPending))
        requestStop(2, false);
    QDialog::hideEvent(event);
}

void CarControlDialog::closeEvent(QCloseEvent *event)
{
    if (m_link)
        requestStop(2, false);
    QDialog::closeEvent(event);
}
