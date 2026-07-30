#ifndef GROUND_AIR_MONITOR_H
#define GROUND_AIR_MONITOR_H

#include <QJsonObject>
#include <QMatrix4x4>
#include <QMainWindow>
#include <QOpenGLFunctions_1_1>
#include <QOpenGLWidget>
#include <QPointF>
#include <QVector>
#include <QVector3D>

class QLabel;
class QPainter;
class QMouseEvent;
class QPushButton;
class QTcpSocket;
class QTextEdit;
class QTimer;
class QWheelEvent;

struct MonitorPose
{
    bool valid = false;
    double xM = 0.0;
    double yM = 0.0;
    double zM = 0.0;
    double yawDeg = 0.0;
    double speedMps = 0.0;
    double batteryPercent = -1.0;
};

struct MonitorTelemetry
{
    qint64 timestampMs = 0;
    QString source = QStringLiteral("lidar");
    MonitorPose car;
    MonitorPose drone;
    QString missionId = QStringLiteral("--");
    QString missionMode = QStringLiteral("DROP");
    QString missionState = QStringLiteral("IDLE");
    QString flightMode = QStringLiteral("--");
    bool armed = false;
    bool localizationOk = false;
    bool carLinkOk = false;
    bool droneLinkOk = false;
    bool targetVisible = false;
    double targetConfidence = 0.0;
    bool dropDone = false;
    bool touchdownConfirmed = false;
    double elapsedS = 0.0;
    int latencyMs = -1;
};

class FieldView : public QOpenGLWidget, protected QOpenGLFunctions_1_1
{
    Q_OBJECT
public:
    explicit FieldView(QWidget *parent = nullptr);
    void setTelemetry(const MonitorTelemetry &telemetry);
    void clearTrails();
    void resetView();
    void setAxesVisible(bool visible);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void appendTrail(QVector<QVector3D> &trail, const MonitorPose &pose);
    void setupCamera();
    void drawFloor();
    void drawTrack();
    void drawTrails();
    void drawCar(const MonitorPose &pose);
    void drawDrone(const MonitorPose &pose);
    void drawAxes();
    void drawCircle(double x, double y, double z, double radius, int segments,
                    bool filled);
    void drawBox(double halfX, double halfY, double height);
    void drawOverlay(QPainter &painter);
    QPointF projectToCanvas(const QVector3D &point) const;
    void resetCamera();

    MonitorTelemetry telemetry_;
    QVector<QVector3D> carTrail_;
    QVector<QVector3D> droneTrail_;
    QMatrix4x4 projection_;
    QMatrix4x4 view_;
    QPoint lastMousePosition_;
    double cameraYawDeg_ = -50.0;
    double cameraPitchDeg_ = 38.0;
    double cameraDistance_ = 8.5;
    bool axesVisible_ = true;
};

class GroundAirMonitor : public QMainWindow
{
    Q_OBJECT
public:
    explicit GroundAirMonitor(QWidget *parent = nullptr);

private slots:
    void connectToServer();
    void readSocketData();
    void updateFreshness();
    void toggleDemo();
    void updateDemo();

private:
    void createUi();
    QWidget *createRealtimeCard();
    QWidget *createLogCard();
    QLabel *makeValueLabel(const QString &placeholder = QStringLiteral("--"));
    void applyTelemetry(const MonitorTelemetry &telemetry, bool fromDemo = false);
    bool decodeTelemetry(const QByteArray &payload, MonitorTelemetry *result, QString *error) const;
    void updateStatusText();
    void setLinkState(const QString &text, const QString &color, bool connected);
    void appendLog(const QString &message, const QString &level = QStringLiteral("INFO"));
    QString chineseState(const QString &state) const;

    QTcpSocket *socket_ = nullptr;
    QTimer *freshnessTimer_ = nullptr;
    QTimer *demoTimer_ = nullptr;
    QByteArray socketBuffer_;
    MonitorTelemetry telemetry_;
    qint64 lastReceiveMs_ = 0;
    QString lastState_;
    bool demoEnabled_ = false;
    double demoTimeS_ = 0.0;

    FieldView *fieldView_ = nullptr;
    QLabel *linkBadge_ = nullptr;
    QLabel *mainStateLabel_ = nullptr;
    QTextEdit *logView_ = nullptr;
    QString fixedServerHost_ = QStringLiteral("127.0.0.1");
    quint16 fixedServerPort_ = 8001;

    QLabel *carX_ = nullptr;
    QLabel *carY_ = nullptr;
    QLabel *carYaw_ = nullptr;
    QLabel *droneX_ = nullptr;
    QLabel *droneY_ = nullptr;
    QLabel *droneZ_ = nullptr;
    QLabel *droneYaw_ = nullptr;
};

#endif
