#ifndef CARCONTROLDIALOG_H
#define CARCONTROLDIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QElapsedTimer>
#include <QObject>
#include <QPair>
#include <QPointF>
#include <QWidget>

class QCloseEvent;
class QHideEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;
class QRadioButton;
class QSlider;
class QSocketNotifier;
class QTimer;

struct CarTelemetry
{
    quint16 sequence = 0;
    quint32 timestampUs = 0;
    qint16 accelX = 0;
    qint16 accelY = 0;
    qint16 accelZ = 0;
    qint16 gyroX = 0;
    qint16 gyroY = 0;
    qint16 gyroZ = 0;
    qint16 temperature = 0;
    qint32 encoderLeft = 0;
    qint32 encoderRight = 0;
    quint16 status = 0;
    quint8 calibrationPercent = 0;
};

struct CarMotorStatus
{
    quint16 statusBits = 0;
    quint16 lastSequence = 0;
    qint16 targetLeft = 0;
    qint16 targetRight = 0;
    qint16 appliedLeft = 0;
    qint16 appliedRight = 0;
    quint16 watchdogRemainingMs = 0;
    quint8 stopReason = 0;
    quint8 maxCommandPercent = 0;
};

class CarSerialLink : public QObject
{
    Q_OBJECT
public:
    explicit CarSerialLink(QObject *parent = nullptr);
    ~CarSerialLink() override;

    QString portPath() const;
    bool isOpen() const;
    void setPortPath(const QString &path);

    quint16 sendArm();
    quint16 sendDrive(qint16 leftPermille, qint16 rightPermille);
    quint16 sendStop(quint8 reason);

signals:
    void serialStateChanged(bool open, const QString &detail);
    void telemetryReceived(const CarTelemetry &telemetry);
    void motorStatusReceived(const CarMotorStatus &status);
    void commandAcknowledged(quint8 commandType, quint16 sequence, quint8 result);
    void protocolWarning(const QString &message);

private slots:
    void openSerial();
    void onReadable();
    void sendPing();

private:
    static quint16 crc16(const QByteArray &data, int offset, int length);
    static quint16 readU16(const QByteArray &data, int offset);
    static qint16 readI16(const QByteArray &data, int offset);
    static quint32 readU32(const QByteArray &data, int offset);
    static qint32 readI32(const QByteArray &data, int offset);
    static void appendU16(QByteArray &data, quint16 value);
    static void appendI16(QByteArray &data, qint16 value);

    quint16 nextSequence();
    QByteArray makeFrame(quint8 type, const QByteArray &payload) const;
    bool queueFrame(quint8 type, const QByteArray &payload, bool urgent = false);
    void flushTx();
    void closeSerial(const QString &reason);
    void parseRx();
    void handleFrame(quint8 type, const QByteArray &payload);

    QString m_portPath;
    int m_fd = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_pingTimer = nullptr;
    QByteArray m_rxBuffer;
    QByteArray m_txQueue;
    quint16 m_sequence = 0;
    quint16 m_pingNonce = 0;
};

class VirtualJoystick : public QWidget
{
    Q_OBJECT
public:
    explicit VirtualJoystick(QWidget *parent = nullptr);
    QPointF value() const;
    void reset();

signals:
    void valueChanged(double steering, double throttle);
    void released();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void updateFromPosition(const QPointF &position);
    QPointF eventPosition(QMouseEvent *event) const;

    QPointF m_value;
    bool m_pressed = false;
};

class CarControlDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CarControlDialog(QWidget *parent = nullptr);
    ~CarControlDialog() override;

protected:
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSerialStateChanged(bool open, const QString &detail);
    void onTelemetry(const CarTelemetry &telemetry);
    void onMotorStatus(const CarMotorStatus &status);
    void onCommandAck(quint8 commandType, quint16 sequence, quint8 result);
    void onJoystickChanged(double steering, double throttle);
    void sendDriveHeartbeat();
    void refreshSafetyState();
    void beginArmPress();
    void finishArmPress();
    void sendArmAfterLongPress();
    void emergencyStop();
    void showSerialSettings();
    void returnToMap();

private:
    static QLabel *makeValueLabel(const QString &text, QWidget *parent);
    static QString stopReasonText(quint8 reason);
    static QString commandResultText(quint8 result);
    void buildUi();
    void setChecklistLabel(QLabel *label, bool ok, const QString &text);
    bool telemetryFresh() const;
    bool motorStatusFresh() const;
    bool joystickCentered() const;
    bool canArm() const;
    void updateArmUi();
    void requestStop(quint8 reason, bool repeat);
    QPair<qint16, qint16> mixedDriveCommand() const;

    CarSerialLink *m_link = nullptr;
    VirtualJoystick *m_joystick = nullptr;
    QTimer *m_driveTimer = nullptr;
    QTimer *m_healthTimer = nullptr;
    QTimer *m_armLongPressTimer = nullptr;
    QElapsedTimer m_telemetryAge;
    QElapsedTimer m_motorStatusAge;
    QElapsedTimer m_rateWindow;

    QLabel *m_serialValue = nullptr;
    QLabel *m_firmwareValue = nullptr;
    QLabel *m_calibrationValue = nullptr;
    QLabel *m_telemetryRateValue = nullptr;
    QLabel *m_encoderLeftValue = nullptr;
    QLabel *m_encoderRightValue = nullptr;
    QLabel *m_targetValue = nullptr;
    QLabel *m_appliedValue = nullptr;
    QLabel *m_watchdogValue = nullptr;
    QLabel *m_stateBanner = nullptr;
    QLabel *m_checkSerial = nullptr;
    QLabel *m_checkCalibration = nullptr;
    QLabel *m_checkCentered = nullptr;
    QLabel *m_joystickNumbers = nullptr;
    QLabel *m_speedValue = nullptr;
    QLabel *m_noticeLabel = nullptr;
    QPushButton *m_armButton = nullptr;
    QPushButton *m_emergencyButton = nullptr;
    QRadioButton *m_manualMode = nullptr;
    QRadioButton *m_autoMode = nullptr;
    QSlider *m_speedSlider = nullptr;

    bool m_serialOpen = false;
    bool m_calibrated = false;
    bool m_systemReady = false;
    bool m_armed = false;
    bool m_armPending = false;
    bool m_pressedWhileArmed = false;
    bool m_longPressTriggered = false;
    quint16 m_armRequestSequence = 0;
    int m_telemetryFrames = 0;
    double m_steering = 0.0;
    double m_throttle = 0.0;
};

#endif
