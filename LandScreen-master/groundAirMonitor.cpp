#include "groundAirMonitor.h"

#include <QApplication>
#include <QAbstractSocket>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kFieldWidthM = 4.0;
constexpr double kFieldHeightM = 5.0;

QFrame *card(QWidget *parent = nullptr)
{
    QFrame *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("card"));
    return frame;
}

QLabel *sectionTitle(const QString &title, const QString &subtitle, QWidget *parent)
{
    QLabel *label = new QLabel(
        QStringLiteral("<span style='font-size:18px;font-weight:700;color:#13243a'>%1</span>"
                       "<br><span style='font-size:12px;color:#8290a3'>%2</span>")
            .arg(title, subtitle), parent);
    label->setTextFormat(Qt::RichText);
    return label;
}

double number(const QJsonObject &object, const QStringList &keys, double fallback = 0.0)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isDouble())
            return value.toDouble();
    }
    return fallback;
}

bool booleanValue(const QJsonObject &object, const QStringList &keys, bool fallback = false)
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isBool())
            return value.toBool();
        if (value.isDouble())
            return value.toInt() != 0;
    }
    return fallback;
}

QString textValue(const QJsonObject &object, const QStringList &keys,
                  const QString &fallback = QString())
{
    for (const QString &key : keys) {
        const QJsonValue value = object.value(key);
        if (value.isString() && !value.toString().trimmed().isEmpty())
            return value.toString().trimmed();
    }
    return fallback;
}

bool finitePose(const MonitorPose &pose)
{
    return std::isfinite(pose.xM) && std::isfinite(pose.yM) &&
           std::isfinite(pose.zM) && std::isfinite(pose.yawDeg);
}
}

FieldView::FieldView(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(520, 420);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
    setCursor(Qt::OpenHandCursor);
}

void FieldView::setTelemetry(const MonitorTelemetry &telemetry)
{
    telemetry_ = telemetry;
    appendTrail(carTrail_, telemetry.car);
    appendTrail(droneTrail_, telemetry.drone);
    update();
}

void FieldView::clearTrails()
{
    carTrail_.clear();
    droneTrail_.clear();
    update();
}

void FieldView::appendTrail(QVector<QVector3D> &trail, const MonitorPose &pose)
{
    if (!pose.valid || !finitePose(pose))
        return;
    const QVector3D point(pose.xM, pose.yM, pose.zM);
    if (trail.isEmpty() || (trail.last() - point).length() > 0.015f)
        trail.append(point);
    while (trail.size() > 180)
        trail.removeFirst();
}

void FieldView::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.933f, 0.957f, 0.980f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void FieldView::resizeGL(int viewportWidth, int viewportHeight)
{
    Q_UNUSED(viewportWidth)
    Q_UNUSED(viewportHeight)
    // QOpenGLWidget has already configured the device-pixel-aware viewport.
}

void FieldView::setupCamera()
{
    projection_.setToIdentity();
    projection_.perspective(42.0f, static_cast<float>(width()) / qMax(1, height()),
                            0.1f, 60.0f);

    const double yaw = qDegreesToRadians(cameraYawDeg_);
    const double pitch = qDegreesToRadians(cameraPitchDeg_);
    const QVector3D target(2.0f, 2.5f, 0.35f);
    const QVector3D eye(
        target.x() + cameraDistance_ * std::cos(pitch) * std::cos(yaw),
        target.y() + cameraDistance_ * std::cos(pitch) * std::sin(yaw),
        target.z() + cameraDistance_ * std::sin(pitch));
    view_.setToIdentity();
    view_.lookAt(eye, target, QVector3D(0.0f, 0.0f, 1.0f));

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(projection_.constData());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view_.constData());
}

QPointF FieldView::projectToCanvas(const QVector3D &point) const
{
    const QVector4D clip = projection_ * view_ * QVector4D(point, 1.0f);
    if (qFuzzyIsNull(clip.w()))
        return QPointF(-1000, -1000);
    const QVector3D ndc = clip.toVector3DAffine();
    return QPointF((ndc.x() + 1.0) * 0.5 * width(),
                   (1.0 - ndc.y()) * 0.5 * height());
}

void FieldView::resetCamera()
{
    cameraYawDeg_ = -50.0;
    cameraPitchDeg_ = 38.0;
    cameraDistance_ = 8.5;
    update();
}

void FieldView::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition_ = event->pos();
    if (event->button() == Qt::LeftButton)
        setCursor(Qt::ClosedHandCursor);
}

void FieldView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPoint delta = event->pos() - lastMousePosition_;
    lastMousePosition_ = event->pos();
    cameraYawDeg_ += delta.x() * 0.45;
    cameraPitchDeg_ = qBound(12.0, cameraPitchDeg_ + delta.y() * 0.35, 82.0);
    update();
}

void FieldView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        setCursor(Qt::OpenHandCursor);
}

void FieldView::mouseDoubleClickEvent(QMouseEvent *)
{
    resetCamera();
}

void FieldView::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    cameraDistance_ = qBound(5.5, cameraDistance_ * std::pow(0.88, steps), 19.0);
    update();
    event->accept();
}

void FieldView::drawCircle(double x, double y, double z, double radius,
                           int segments, bool filled)
{
    glBegin(filled ? GL_TRIANGLE_FAN : GL_LINE_LOOP);
    if (filled)
        glVertex3d(x, y, z);
    for (int i = 0; i <= segments; ++i) {
        const double angle = 2.0 * M_PI * i / segments;
        glVertex3d(x + radius * std::cos(angle), y + radius * std::sin(angle), z);
    }
    glEnd();
}

void FieldView::drawBox(double halfX, double halfY, double boxHeight)
{
    const double z0 = 0.0;
    const double z1 = boxHeight;
    glBegin(GL_QUADS);
    glVertex3d(-halfX, -halfY, z1); glVertex3d(halfX, -halfY, z1);
    glVertex3d(halfX, halfY, z1); glVertex3d(-halfX, halfY, z1);
    glVertex3d(-halfX, -halfY, z0); glVertex3d(-halfX, halfY, z0);
    glVertex3d(halfX, halfY, z0); glVertex3d(halfX, -halfY, z0);
    glVertex3d(-halfX, -halfY, z0); glVertex3d(halfX, -halfY, z0);
    glVertex3d(halfX, -halfY, z1); glVertex3d(-halfX, -halfY, z1);
    glVertex3d(halfX, -halfY, z0); glVertex3d(halfX, halfY, z0);
    glVertex3d(halfX, halfY, z1); glVertex3d(halfX, -halfY, z1);
    glVertex3d(halfX, halfY, z0); glVertex3d(-halfX, halfY, z0);
    glVertex3d(-halfX, halfY, z1); glVertex3d(halfX, halfY, z1);
    glVertex3d(-halfX, halfY, z0); glVertex3d(-halfX, -halfY, z0);
    glVertex3d(-halfX, -halfY, z1); glVertex3d(-halfX, halfY, z1);
    glEnd();
}

void FieldView::drawCar(const MonitorPose &pose)
{
    if (!pose.valid || !finitePose(pose))
        return;
    glPushMatrix();
    glTranslated(pose.xM, pose.yM, 0.035);
    glRotated(pose.yawDeg, 0, 0, 1);
    glColor4f(0.14f, 0.52f, 0.90f, 1.0f);
    drawBox(0.20, 0.14, 0.13);
    glColor4f(0.92f, 0.97f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex3d(0.23, 0.0, 0.145);
    glVertex3d(0.10, -0.07, 0.145);
    glVertex3d(0.10, 0.07, 0.145);
    glEnd();
    glPopMatrix();
}

void FieldView::drawDrone(const MonitorPose &pose)
{
    if (!pose.valid || !finitePose(pose))
        return;
    const double altitude = qMax(0.05, pose.zM);
    glColor4f(0.12f, 0.20f, 0.30f, 0.16f);
    drawCircle(pose.xM, pose.yM, 0.018, 0.23, 28, true);
    glColor4f(0.93f, 0.61f, 0.19f, 0.62f);
    glLineWidth(1.5f);
    glBegin(GL_LINES);
    glVertex3d(pose.xM, pose.yM, 0.03);
    glVertex3d(pose.xM, pose.yM, altitude);
    glEnd();

    glPushMatrix();
    glTranslated(pose.xM, pose.yM, altitude);
    glRotated(pose.yawDeg, 0, 0, 1);
    glLineWidth(5.0f);
    glColor4f(0.93f, 0.61f, 0.19f, 1.0f);
    glBegin(GL_LINES);
    glVertex3d(-0.22, -0.22, 0.02); glVertex3d(0.22, 0.22, 0.02);
    glVertex3d(0.22, -0.22, 0.02); glVertex3d(-0.22, 0.22, 0.02);
    glEnd();
    for (int sx : {-1, 1}) {
        for (int sy : {-1, 1}) {
            glColor4f(0.98f, 0.70f, 0.31f, 0.48f);
            drawCircle(0.22 * sx, 0.22 * sy, 0.035, 0.115, 24, true);
            glColor4f(0.75f, 0.43f, 0.10f, 1.0f);
            drawCircle(0.22 * sx, 0.22 * sy, 0.04, 0.115, 24, false);
        }
    }
    glColor4f(0.78f, 0.40f, 0.08f, 1.0f);
    drawBox(0.115, 0.09, 0.12);
    glPopMatrix();
}

void FieldView::drawFloor()
{
    glColor4f(0.985f, 0.992f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3d(0, 0, 0); glVertex3d(4, 0, 0);
    glVertex3d(4, 5, 0); glVertex3d(0, 5, 0);
    glEnd();

    glLineWidth(1.0f);
    glColor4f(0.82f, 0.87f, 0.92f, 1.0f);
    glBegin(GL_LINES);
    for (int x = 0; x <= 8; ++x) {
        glVertex3d(x * 0.5, 0, 0.006);
        glVertex3d(x * 0.5, 5, 0.006);
    }
    for (int y = 0; y <= 10; ++y) {
        glVertex3d(0, y * 0.5, 0.006);
        glVertex3d(4, y * 0.5, 0.006);
    }
    glEnd();

    glLineWidth(2.0f);
    glColor4f(0.52f, 0.62f, 0.73f, 1.0f);
    glBegin(GL_LINE_LOOP);
    glVertex3d(0, 0, 0.012); glVertex3d(4, 0, 0.012);
    glVertex3d(4, 5, 0.012); glVertex3d(0, 5, 0.012);
    glEnd();
}

void FieldView::drawTrack()
{
    glLineWidth(5.0f);
    glColor4f(0.18f, 0.28f, 0.38f, 1.0f);
    glBegin(GL_LINE_STRIP);
    glVertex3d(1.5, 2.0, 0.028);
    glVertex3d(1.5, 3.5, 0.028);
    for (int i = 1; i <= 40; ++i) {
        const double theta = M_PI - M_PI * i / 40.0;
        glVertex3d(2.25 + 0.75 * std::cos(theta),
                   3.5 + 0.75 * std::sin(theta), 0.028);
    }
    glVertex3d(3.0, 2.0, 0.028);
    for (int i = 1; i <= 40; ++i) {
        const double theta = -M_PI * i / 40.0;
        glVertex3d(2.25 + 0.75 * std::cos(theta),
                   2.0 + 0.75 * std::sin(theta), 0.028);
    }
    glEnd();

    const QVector<QVector3D> points = {
        QVector3D(1.5f, 2.0f, 0.035f), QVector3D(1.5f, 3.5f, 0.035f),
        QVector3D(3.0f, 3.5f, 0.035f), QVector3D(3.0f, 2.0f, 0.035f)
    };
    glColor4f(0.16f, 0.26f, 0.36f, 1.0f);
    for (const QVector3D &point : points)
        drawCircle(point.x(), point.y(), point.z(), 0.075, 24, true);

    glLineWidth(3.0f);
    glColor4f(0.08f, 0.61f, 0.43f, 1.0f);
    drawCircle(0.75, 0.75, 0.03, 0.28, 40, false);
    drawCircle(0.75, 0.75, 0.03, 0.18, 40, false);
}

void FieldView::drawTrails()
{
    auto drawTrail = [this](const QVector<QVector3D> &trail,
                            float red, float green, float blue, bool airborne) {
        if (trail.size() < 2)
            return;
        glColor4f(red, green, blue, 0.72f);
        glLineWidth(2.5f);
        glBegin(GL_LINE_STRIP);
        for (const QVector3D &point : trail)
            glVertex3f(point.x(), point.y(), airborne ? qMax(0.04f, point.z()) : 0.045f);
        glEnd();
    };
    drawTrail(carTrail_, 0.14f, 0.52f, 0.90f, false);
    drawTrail(droneTrail_, 0.93f, 0.61f, 0.19f, true);
}

void FieldView::drawAxes()
{
    glLineWidth(4.0f);
    glBegin(GL_LINES);
    glColor4f(0.88f, 0.26f, 0.26f, 1.0f);
    glVertex3d(0, 0, 0.04); glVertex3d(0.9, 0, 0.04);
    glColor4f(0.10f, 0.66f, 0.42f, 1.0f);
    glVertex3d(0, 0, 0.04); glVertex3d(0, 1.0, 0.04);
    glColor4f(0.20f, 0.48f, 0.84f, 1.0f);
    glVertex3d(0, 0, 0.04); glVertex3d(0, 0, 1.0);
    glEnd();
}

void FieldView::drawOverlay(QPainter &painter)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    painter.setPen(QColor(QStringLiteral("#718197")));
    painter.drawText(QRectF(12, 8, width() - 24, 24), Qt::AlignRight,
                     QStringLiteral("左键拖动旋转 · 滚轮缩放 · 双击复位视角"));

    const struct { const char *name; QVector3D position; } pointLabels[] = {
        {"A", QVector3D(1.5f, 2.0f, 0.11f)}, {"B", QVector3D(1.5f, 3.5f, 0.11f)},
        {"C", QVector3D(3.0f, 3.5f, 0.11f)}, {"D", QVector3D(3.0f, 2.0f, 0.11f)},
        {"H", QVector3D(0.75f, 0.75f, 0.08f)}, {"X", QVector3D(0.95f, 0, 0.06f)},
        {"Y", QVector3D(0, 1.06f, 0.06f)}, {"Z", QVector3D(0, 0, 1.06f)}
    };
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 10, QFont::DemiBold));
    for (const auto &item : pointLabels) {
        const QPointF p = projectToCanvas(item.position);
        painter.setPen(item.name[0] == 'H' ? QColor(QStringLiteral("#13835f"))
                                           : QColor(QStringLiteral("#344b63")));
        painter.drawText(QRectF(p.x() - 14, p.y() - 24, 28, 20), Qt::AlignCenter,
                         QString::fromLatin1(item.name));
    }

    auto drawChip = [&](const QPointF &anchor, const QString &text,
                        const QColor &accent, bool placeRight) {
        const double chipWidth = 122.0;
        double x = placeRight ? anchor.x() + 18.0 : anchor.x() - chipWidth - 18.0;
        double y = anchor.y() - 34.0;
        x = qBound(5.0, x, width() - chipWidth - 5.0);
        y = qBound(34.0, y, height() - 34.0);
        const QRectF chip(x, y, chipWidth, 27.0);
        painter.setBrush(QColor(255, 255, 255, 238));
        painter.setPen(QPen(accent, 1.2));
        painter.drawRoundedRect(chip, 7, 7);
        painter.setPen(accent.darker(125));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9, QFont::DemiBold));
        painter.drawText(chip, Qt::AlignCenter, text);
    };

    if (telemetry_.car.valid) {
        drawChip(projectToCanvas(QVector3D(telemetry_.car.xM, telemetry_.car.yM, 0.12f)),
                 QStringLiteral("小车 %1 m/s").arg(telemetry_.car.speedMps, 0, 'f', 2),
                 QColor(QStringLiteral("#2385e5")), false);
    }
    if (telemetry_.drone.valid) {
        drawChip(projectToCanvas(QVector3D(telemetry_.drone.xM, telemetry_.drone.yM,
                                           qMax(0.05, telemetry_.drone.zM) + 0.12)),
                 QStringLiteral("无人机 %1 m").arg(telemetry_.drone.zM, 0, 'f', 2),
                 QColor(QStringLiteral("#ee9b31")), true);
    }
}

void FieldView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    setupCamera();
    drawFloor();
    drawTrack();
    drawTrails();
    drawCar(telemetry_.car);
    drawDrone(telemetry_.drone);
    drawAxes();
    glFlush();

    QPainter painter(this);
    drawOverlay(painter);
}

GroundAirMonitor::GroundAirMonitor(QWidget *parent) : QMainWindow(parent)
{
    createUi();

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        demoEnabled_ = false;
        demoTimer_->stop();
        demoButton_->setText(QStringLiteral("演示数据"));
        setLinkState(QStringLiteral("实时数据已连接"), QStringLiteral("#168f6a"), true);
        connectButton_->setText(QStringLiteral("断开"));
        appendLog(QStringLiteral("已连接数据服务器 %1:%2")
                      .arg(serverEdit_->text()).arg(portEdit_->value()));
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        connectButton_->setText(QStringLiteral("连接"));
        setLinkState(QStringLiteral("等待数据连接"), QStringLiteral("#8a98aa"), false);
        appendLog(QStringLiteral("数据连接已断开"), QStringLiteral("WARN"));
    });
    connect(socket_, &QTcpSocket::readyRead, this, &GroundAirMonitor::readSocketData);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(socket_, &QAbstractSocket::errorOccurred,
            this, [this](QAbstractSocket::SocketError) {
        setLinkState(QStringLiteral("连接失败"), QStringLiteral("#d25353"), false);
    });
#else
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this](QAbstractSocket::SocketError) {
        setLinkState(QStringLiteral("连接失败"), QStringLiteral("#d25353"), false);
    });
#endif

    freshnessTimer_ = new QTimer(this);
    freshnessTimer_->setInterval(250);
    connect(freshnessTimer_, &QTimer::timeout, this, &GroundAirMonitor::updateFreshness);
    freshnessTimer_->start();

    demoTimer_ = new QTimer(this);
    demoTimer_->setInterval(100);
    connect(demoTimer_, &QTimer::timeout, this, &GroundAirMonitor::updateDemo);

    QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("GroundAirMonitor"));
    serverEdit_->setText(settings.value(QStringLiteral("server_ip"),
        QStringLiteral("127.0.0.1")).toString());
    portEdit_->setValue(settings.value(QStringLiteral("server_port"), 8001).toInt());
    appendLog(QStringLiteral("监控台已启动，当前为只读模式"));
    appendLog(QStringLiteral("点击“演示数据”可在雷达接入前检查界面"));
}

void GroundAirMonitor::createUi()
{
    setWindowTitle(QStringLiteral("陆空协同实时监控台"));
    resize(1600, 940);
    setMinimumSize(1280, 760);
    setStyleSheet(QStringLiteral(R"(
        QMainWindow { background: #edf2f8; }
        QWidget { font-family: "Microsoft YaHei", "Noto Sans CJK SC", sans-serif; color: #17283d; }
        QFrame#card { background: #ffffff; border: 1px solid #dfe7f0; border-radius: 13px; }
        QLineEdit, QSpinBox { min-height: 34px; background: #f7f9fc; border: 1px solid #d8e1ec;
                            border-radius: 7px; padding: 0 9px; font-size: 13px; }
        QPushButton { min-height: 35px; border: 0; border-radius: 7px; background: #e8eef6;
                      color: #25405f; padding: 0 14px; font-weight: 600; }
        QPushButton:hover { background: #dce7f3; }
        QPushButton#primary { background: #1976d2; color: white; }
        QPushButton#primary:hover { background: #1268ba; }
        QTextEdit { background: #101d2d; color: #cbd8e8; border: 0; border-radius: 10px;
                    padding: 8px; font-family: Consolas, "Microsoft YaHei"; font-size: 12px; }
    )"));

    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(22, 17, 22, 18);
    root->setSpacing(14);

    QHBoxLayout *header = new QHBoxLayout();
    QVBoxLayout *titleBox = new QVBoxLayout();
    QLabel *title = new QLabel(QStringLiteral("陆空协同实时监控台"));
    title->setStyleSheet(QStringLiteral("font-size:26px;font-weight:800;color:#102238;"));
    QLabel *subtitle = new QLabel(QStringLiteral("2026 全国大学生电子设计竞赛 D 题 · 只读监控端"));
    subtitle->setStyleSheet(QStringLiteral("font-size:13px;color:#7b899b;"));
    titleBox->addWidget(title);
    titleBox->addWidget(subtitle);
    header->addLayout(titleBox);
    header->addStretch();

    clockLabel_ = new QLabel(QStringLiteral("--:--:--"));
    clockLabel_->setStyleSheet(QStringLiteral("font-size:14px;color:#64758a;margin-right:10px;"));
    linkBadge_ = new QLabel(QStringLiteral("●  等待数据连接"));
    linkBadge_->setAlignment(Qt::AlignCenter);
    linkBadge_->setMinimumWidth(154);
    setLinkState(QStringLiteral("等待数据连接"), QStringLiteral("#8a98aa"), false);
    header->addWidget(clockLabel_);
    header->addWidget(linkBadge_);
    root->addLayout(header);

    QHBoxLayout *body = new QHBoxLayout();
    body->setSpacing(14);

    QScrollArea *leftScroll = new QScrollArea();
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setFixedWidth(310);
    leftScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    QWidget *left = new QWidget();
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);
    leftLayout->addWidget(createConnectionCard());
    leftLayout->addWidget(createVehicleCard(QStringLiteral("小车定位"), QStringLiteral("#2385e5"), false));
    leftLayout->addWidget(createVehicleCard(QStringLiteral("无人机定位"), QStringLiteral("#ee9b31"), true));
    leftLayout->addStretch();
    leftScroll->setWidget(left);
    body->addWidget(leftScroll);

    QFrame *fieldCard = card();
    QVBoxLayout *fieldLayout = new QVBoxLayout(fieldCard);
    fieldLayout->setContentsMargins(16, 15, 16, 13);
    QHBoxLayout *fieldHeader = new QHBoxLayout();
    fieldHeader->addWidget(sectionTitle(QStringLiteral("场地实时态势"),
        QStringLiteral("激光雷达三维坐标 · 位置与轨迹仅用于显示"), fieldCard));
    fieldHeader->addStretch();
    QLabel *legend = new QLabel(QStringLiteral("<span style='color:#2385e5'>● 小车</span>　"
                                                "<span style='color:#ee9b31'>● 无人机</span>"));
    legend->setStyleSheet(QStringLiteral("font-size:12px;background:#f4f7fb;padding:7px 10px;border-radius:7px;"));
    fieldHeader->addWidget(legend);
    fieldLayout->addLayout(fieldHeader);
    fieldView_ = new FieldView(fieldCard);
    fieldLayout->addWidget(fieldView_, 1);
    body->addWidget(fieldCard, 1);

    QScrollArea *rightScroll = new QScrollArea();
    rightScroll->setWidgetResizable(true);
    rightScroll->setFrameShape(QFrame::NoFrame);
    rightScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rightScroll->setFixedWidth(320);
    rightScroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
    QWidget *right = new QWidget();
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);
    rightLayout->addWidget(createMissionCard());
    rightLayout->addWidget(createHealthCard());
    rightLayout->addStretch();
    rightScroll->setWidget(right);
    body->addWidget(rightScroll);
    root->addLayout(body, 1);

    QHBoxLayout *logHeader = new QHBoxLayout();
    QLabel *logTitle = new QLabel(QStringLiteral("关键日志"));
    logTitle->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;"));
    QPushButton *clearButton = new QPushButton(QStringLiteral("清空"));
    clearButton->setFixedWidth(70);
    clearButton->setFixedHeight(28);
    connect(clearButton, &QPushButton::clicked, this, [this]() { logView_->clear(); });
    logHeader->addWidget(logTitle);
    logHeader->addStretch();
    logHeader->addWidget(clearButton);
    root->addLayout(logHeader);
    logView_ = new QTextEdit();
    logView_->setReadOnly(true);
    logView_->setFixedHeight(112);
    root->addWidget(logView_);
}

QWidget *GroundAirMonitor::createConnectionCard()
{
    QFrame *frame = card();
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 15, 16, 16);
    layout->setSpacing(10);
    layout->addWidget(sectionTitle(QStringLiteral("数据连接"),
        QStringLiteral("只接收，不发送运动控制命令"), frame));

    QHBoxLayout *endpoint = new QHBoxLayout();
    serverEdit_ = new QLineEdit();
    serverEdit_->setPlaceholderText(QStringLiteral("NX / 雷达桥 IP"));
    portEdit_ = new QSpinBox();
    portEdit_->setRange(1, 65535);
    portEdit_->setFixedWidth(86);
    endpoint->addWidget(serverEdit_, 1);
    endpoint->addWidget(portEdit_);
    layout->addLayout(endpoint);

    QHBoxLayout *buttons = new QHBoxLayout();
    connectButton_ = new QPushButton(QStringLiteral("连接"));
    connectButton_->setObjectName(QStringLiteral("primary"));
    demoButton_ = new QPushButton(QStringLiteral("演示数据"));
    connect(connectButton_, &QPushButton::clicked, this, [this]() {
        if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState)
            disconnectFromServer();
        else
            connectToServer();
    });
    connect(demoButton_, &QPushButton::clicked, this, &GroundAirMonitor::toggleDemo);
    buttons->addWidget(connectButton_);
    buttons->addWidget(demoButton_);
    layout->addLayout(buttons);

    sourceLabel_ = new QLabel(QStringLiteral("数据源　--"));
    packetAgeLabel_ = new QLabel(QStringLiteral("数据龄　--"));
    sourceLabel_->setStyleSheet(QStringLiteral("font-size:12px;color:#65768a;"));
    packetAgeLabel_->setStyleSheet(QStringLiteral("font-size:12px;color:#65768a;"));
    layout->addWidget(sourceLabel_);
    layout->addWidget(packetAgeLabel_);
    return frame;
}

QLabel *GroundAirMonitor::makeValueLabel(const QString &placeholder)
{
    QLabel *label = new QLabel(placeholder);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setStyleSheet(QStringLiteral("font-size:14px;font-weight:700;color:#243b57;"));
    return label;
}

QWidget *GroundAirMonitor::createVehicleCard(const QString &title, const QString &accent, bool drone)
{
    QFrame *frame = card();
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(16, 14, 16, 15);
    QLabel *heading = new QLabel(QStringLiteral("●  %1").arg(title));
    heading->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;color:%1;").arg(accent));
    layout->addWidget(heading);

    QGridLayout *values = new QGridLayout();
    values->setVerticalSpacing(7);
    const QStringList names = drone
        ? QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("高度"),
                      QStringLiteral("航向"), QStringLiteral("电量"), QStringLiteral("飞行模式")}
        : QStringList{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("航向"),
                      QStringLiteral("速度"), QStringLiteral("电量")};
    QVector<QLabel *> labels;
    for (int row = 0; row < names.size(); ++row) {
        QLabel *name = new QLabel(names.at(row));
        name->setStyleSheet(QStringLiteral("font-size:12px;color:#7f8da0;"));
        QLabel *value = makeValueLabel();
        values->addWidget(name, row, 0);
        values->addWidget(value, row, 1);
        labels.append(value);
    }
    layout->addLayout(values);
    if (drone) {
        droneX_ = labels[0]; droneY_ = labels[1]; droneZ_ = labels[2];
        droneYaw_ = labels[3]; droneBattery_ = labels[4]; flightMode_ = labels[5];
    } else {
        carX_ = labels[0]; carY_ = labels[1]; carYaw_ = labels[2];
        carSpeed_ = labels[3]; carBattery_ = labels[4];
    }
    return frame;
}

QWidget *GroundAirMonitor::createMissionCard()
{
    QFrame *frame = card();
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(17, 16, 17, 17);
    layout->setSpacing(9);
    layout->addWidget(sectionTitle(QStringLiteral("无人机实时状态"),
        QStringLiteral("任务状态机文字显示"), frame));

    mainStateLabel_ = new QLabel(QStringLiteral("任务待机"));
    mainStateLabel_->setAlignment(Qt::AlignCenter);
    mainStateLabel_->setMinimumHeight(62);
    mainStateLabel_->setStyleSheet(QStringLiteral(
        "background:#eef5ff;color:#196fbd;border:1px solid #cfe2f7;border-radius:10px;"
        "font-size:24px;font-weight:800;"));
    missionMetaLabel_ = new QLabel(QStringLiteral("模式 --　｜　计时 00:00.0"));
    missionMetaLabel_->setAlignment(Qt::AlignCenter);
    missionMetaLabel_->setStyleSheet(QStringLiteral("font-size:12px;color:#728196;"));
    layout->addWidget(mainStateLabel_);
    layout->addWidget(missionMetaLabel_);

    const QStringList stages = {
        QStringLiteral("起飞"), QStringLiteral("稳定悬停"), QStringLiteral("捕获小车"),
        QStringLiteral("伴飞"), QStringLiteral("抛投 / 动态降落"),
        QStringLiteral("再起飞 / 返航"), QStringLiteral("降落完成")
    };
    for (const QString &stage : stages) {
        QLabel *label = new QLabel(QStringLiteral("○　%1").arg(stage));
        label->setMinimumHeight(28);
        label->setStyleSheet(QStringLiteral("font-size:13px;color:#8a97a8;padding-left:4px;"));
        stageLabels_.append(label);
        layout->addWidget(label);
    }
    return frame;
}

QWidget *GroundAirMonitor::createHealthCard()
{
    QFrame *frame = card();
    QVBoxLayout *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(17, 15, 17, 16);
    layout->setSpacing(9);
    layout->addWidget(sectionTitle(QStringLiteral("定位与链路"),
        QStringLiteral("异常时仅告警，不介入控制"), frame));
    localizationLabel_ = new QLabel();
    carLinkLabel_ = new QLabel();
    droneLinkLabel_ = new QLabel();
    targetLabel_ = new QLabel();
    const QList<QLabel *> labels = {localizationLabel_, carLinkLabel_, droneLinkLabel_, targetLabel_};
    for (QLabel *label : labels) {
        label->setMinimumHeight(30);
        label->setStyleSheet(QStringLiteral("background:#f5f8fb;border-radius:7px;padding:0 9px;font-size:12px;"));
        layout->addWidget(label);
    }
    localizationLabel_->setText(QStringLiteral("○　激光雷达定位　等待数据"));
    carLinkLabel_->setText(QStringLiteral("○　小车数据链路　等待数据"));
    droneLinkLabel_->setText(QStringLiteral("○　无人机数据链路　等待数据"));
    targetLabel_->setText(QStringLiteral("○　视觉目标　等待数据"));
    return frame;
}

void GroundAirMonitor::connectToServer()
{
    const QString host = serverEdit_->text().trimmed();
    if (host.isEmpty()) {
        appendLog(QStringLiteral("服务器地址不能为空"), QStringLiteral("ERROR"));
        return;
    }
    demoEnabled_ = false;
    if (demoTimer_)
        demoTimer_->stop();
    demoButton_->setText(QStringLiteral("演示数据"));
    QSettings settings(QStringLiteral("NUEDC"), QStringLiteral("GroundAirMonitor"));
    settings.setValue(QStringLiteral("server_ip"), host);
    settings.setValue(QStringLiteral("server_port"), portEdit_->value());
    setLinkState(QStringLiteral("正在连接"), QStringLiteral("#d08a19"), false);
    socket_->abort();
    socket_->connectToHost(host, static_cast<quint16>(portEdit_->value()));
}

void GroundAirMonitor::disconnectFromServer()
{
    socket_->abort();
    socketBuffer_.clear();
}

void GroundAirMonitor::readSocketData()
{
    socketBuffer_.append(socket_->readAll());
    while (true) {
        const int newline = socketBuffer_.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = socketBuffer_.left(newline).trimmed();
        socketBuffer_.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        MonitorTelemetry decoded;
        QString error;
        if (decodeTelemetry(line, &decoded, &error))
            applyTelemetry(decoded);
        else if (!error.isEmpty())
            appendLog(QStringLiteral("忽略无效遥测：%1").arg(error), QStringLiteral("WARN"));
    }
    if (socketBuffer_.size() > 1024 * 1024) {
        socketBuffer_.clear();
        appendLog(QStringLiteral("接收缓存超过限制，已清空"), QStringLiteral("ERROR"));
    }
}

bool GroundAirMonitor::decodeTelemetry(const QByteArray &payload,
                                       MonitorTelemetry *result, QString *error) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("JSON 解析失败");
        return false;
    }
    QJsonObject root = document.object();
    if (root.value(QStringLiteral("telemetry")).isObject())
        root = root.value(QStringLiteral("telemetry")).toObject();
    const QString type = textValue(root, {QStringLiteral("type")});
    if (!type.isEmpty() && type != QStringLiteral("ground_air_telemetry")) {
        error->clear();
        return false;
    }
    if (!root.value(QStringLiteral("car")).isObject() ||
        !root.value(QStringLiteral("drone")).isObject()) {
        *error = QStringLiteral("缺少 car 或 drone 对象");
        return false;
    }

    MonitorTelemetry value;
    value.timestampMs = static_cast<qint64>(number(root, {QStringLiteral("timestamp_ms")},
        QDateTime::currentMSecsSinceEpoch()));
    value.source = textValue(root, {QStringLiteral("source")}, QStringLiteral("lidar"));
    const QJsonObject car = root.value(QStringLiteral("car")).toObject();
    const QJsonObject drone = root.value(QStringLiteral("drone")).toObject();
    const QJsonObject mission = root.value(QStringLiteral("mission")).toObject();
    const QJsonObject links = root.value(QStringLiteral("links")).toObject();

    value.car.xM = number(car, {QStringLiteral("x_m"), QStringLiteral("field_x_m")});
    value.car.yM = number(car, {QStringLiteral("y_m"), QStringLiteral("field_y_m")});
    value.car.yawDeg = number(car, {QStringLiteral("yaw_deg")},
        qRadiansToDegrees(number(car, {QStringLiteral("yaw_rad")})));
    value.car.speedMps = number(car, {QStringLiteral("speed_mps")});
    value.car.batteryPercent = number(car, {QStringLiteral("battery_percent")}, -1.0);
    value.car.valid = booleanValue(car, {QStringLiteral("valid")}, true) && finitePose(value.car);

    value.drone.xM = number(drone, {QStringLiteral("x_m"), QStringLiteral("field_x_m")});
    value.drone.yM = number(drone, {QStringLiteral("y_m"), QStringLiteral("field_y_m")});
    value.drone.zM = number(drone, {QStringLiteral("z_m"), QStringLiteral("altitude_m")});
    value.drone.yawDeg = number(drone, {QStringLiteral("yaw_deg")},
        qRadiansToDegrees(number(drone, {QStringLiteral("yaw_rad")})));
    value.drone.speedMps = number(drone, {QStringLiteral("speed_mps")});
    value.drone.batteryPercent = number(drone, {QStringLiteral("battery_percent")}, -1.0);
    value.drone.valid = booleanValue(drone, {QStringLiteral("valid")}, true) && finitePose(value.drone);

    value.missionId = textValue(mission, {QStringLiteral("mission_id")}, QStringLiteral("--"));
    value.missionMode = textValue(mission, {QStringLiteral("mode")}, QStringLiteral("DROP")).toUpper();
    value.missionState = textValue(mission, {QStringLiteral("state"), QStringLiteral("mission_state")},
                                   QStringLiteral("IDLE")).toUpper();
    value.elapsedS = number(mission, {QStringLiteral("elapsed_s")});
    value.dropDone = booleanValue(mission, {QStringLiteral("drop_done")});
    value.touchdownConfirmed = booleanValue(mission, {QStringLiteral("touchdown_confirmed")});
    value.flightMode = textValue(drone, {QStringLiteral("flight_mode")}, QStringLiteral("--"));
    value.armed = booleanValue(drone, {QStringLiteral("armed")});
    value.targetVisible = booleanValue(drone, {QStringLiteral("target_visible")});
    value.targetConfidence = number(drone, {QStringLiteral("target_confidence")});
    value.localizationOk = booleanValue(links, {QStringLiteral("localization_ok")},
                                   value.car.valid && value.drone.valid);
    value.carLinkOk = booleanValue(links, {QStringLiteral("car_link_ok")}, value.car.valid);
    value.droneLinkOk = booleanValue(links, {QStringLiteral("drone_link_ok"), QStringLiteral("ground_link_ok")},
                                  value.drone.valid);
    value.latencyMs = static_cast<int>(number(root, {QStringLiteral("latency_ms")}, -1));
    *result = value;
    return true;
}

void GroundAirMonitor::applyTelemetry(const MonitorTelemetry &telemetry, bool fromDemo)
{
    telemetry_ = telemetry;
    lastReceiveMs_ = QDateTime::currentMSecsSinceEpoch();
    fieldView_->setTelemetry(telemetry_);

    auto positionText = [](bool valid, double value) {
        return valid ? QStringLiteral("%1 m").arg(value, 0, 'f', 2) : QStringLiteral("--");
    };
    carX_->setText(positionText(telemetry_.car.valid, telemetry_.car.xM));
    carY_->setText(positionText(telemetry_.car.valid, telemetry_.car.yM));
    carYaw_->setText(telemetry_.car.valid ? QStringLiteral("%1°").arg(telemetry_.car.yawDeg, 0, 'f', 1) : QStringLiteral("--"));
    carSpeed_->setText(telemetry_.car.valid ? QStringLiteral("%1 m/s").arg(telemetry_.car.speedMps, 0, 'f', 2) : QStringLiteral("--"));
    carBattery_->setText(formattedBattery(telemetry_.car.batteryPercent));
    droneX_->setText(positionText(telemetry_.drone.valid, telemetry_.drone.xM));
    droneY_->setText(positionText(telemetry_.drone.valid, telemetry_.drone.yM));
    droneZ_->setText(positionText(telemetry_.drone.valid, telemetry_.drone.zM));
    droneYaw_->setText(telemetry_.drone.valid ? QStringLiteral("%1°").arg(telemetry_.drone.yawDeg, 0, 'f', 1) : QStringLiteral("--"));
    droneBattery_->setText(formattedBattery(telemetry_.drone.batteryPercent));
    flightMode_->setText(telemetry_.flightMode);
    sourceLabel_->setText(QStringLiteral("数据源　%1%2")
        .arg(telemetry_.source, fromDemo ? QStringLiteral("（演示）") : QString()));
    updateStatusText();
    updateStageList();

    auto healthText = [](bool ok, const QString &name) {
        return QStringLiteral("%1　%2　%3").arg(ok ? QStringLiteral("●") : QStringLiteral("●"),
            name, ok ? QStringLiteral("正常") : QStringLiteral("异常 / 未就绪"));
    };
    auto healthStyle = [](bool ok) {
        return QStringLiteral("background:%1;color:%2;border-radius:7px;padding:0 9px;font-size:12px;")
            .arg(ok ? QStringLiteral("#edf9f4") : QStringLiteral("#fff1f1"),
                 ok ? QStringLiteral("#16825f") : QStringLiteral("#c14f4f"));
    };
    localizationLabel_->setText(healthText(telemetry_.localizationOk, QStringLiteral("激光雷达定位")));
    localizationLabel_->setStyleSheet(healthStyle(telemetry_.localizationOk));
    carLinkLabel_->setText(healthText(telemetry_.carLinkOk, QStringLiteral("小车数据链路")));
    carLinkLabel_->setStyleSheet(healthStyle(telemetry_.carLinkOk));
    droneLinkLabel_->setText(healthText(telemetry_.droneLinkOk, QStringLiteral("无人机数据链路")));
    droneLinkLabel_->setStyleSheet(healthStyle(telemetry_.droneLinkOk));
    targetLabel_->setText(QStringLiteral("%1　视觉目标　%2%3")
        .arg(telemetry_.targetVisible ? QStringLiteral("●") : QStringLiteral("●"),
             telemetry_.targetVisible ? QStringLiteral("已锁定　") : QStringLiteral("未锁定"),
             telemetry_.targetVisible ? QStringLiteral("%1%").arg(telemetry_.targetConfidence * 100.0, 0, 'f', 0) : QString()));
    targetLabel_->setStyleSheet(healthStyle(telemetry_.targetVisible));

    if (lastState_ != telemetry_.missionState) {
        appendLog(QStringLiteral("无人机状态：%1 → %2")
            .arg(lastState_.isEmpty() ? QStringLiteral("--") : chineseState(lastState_),
                 chineseState(telemetry_.missionState)));
        lastState_ = telemetry_.missionState;
    }
}

void GroundAirMonitor::updateStatusText()
{
    const QString chinese = chineseState(telemetry_.missionState);
    mainStateLabel_->setText(chinese);
    const bool error = telemetry_.missionState.contains(QStringLiteral("ERROR")) ||
                       telemetry_.missionState.contains(QStringLiteral("FAIL")) ||
                       telemetry_.missionState.contains(QStringLiteral("ABORT"));
    const bool complete = telemetry_.missionState == QStringLiteral("COMPLETE") ||
                          telemetry_.missionState == QStringLiteral("LANDED");
    const QString bg = error ? QStringLiteral("#fff0f0") : complete ? QStringLiteral("#ebf9f3") : QStringLiteral("#eef5ff");
    const QString fg = error ? QStringLiteral("#c94848") : complete ? QStringLiteral("#148260") : QStringLiteral("#196fbd");
    const QString border = error ? QStringLiteral("#f2caca") : complete ? QStringLiteral("#c4eadc") : QStringLiteral("#cfe2f7");
    mainStateLabel_->setStyleSheet(QStringLiteral(
        "background:%1;color:%2;border:1px solid %3;border-radius:10px;font-size:24px;font-weight:800;")
        .arg(bg, fg, border));
    const int minutes = static_cast<int>(telemetry_.elapsedS) / 60;
    const double seconds = telemetry_.elapsedS - minutes * 60;
    const QString mode = telemetry_.missionMode == QStringLiteral("MOVING_LAND")
        ? QStringLiteral("动态起降") : telemetry_.missionMode == QStringLiteral("DROP")
        ? QStringLiteral("抛投任务") : telemetry_.missionMode;
    missionMetaLabel_->setText(QStringLiteral("%1　｜　任务 %2　｜　计时 %3:%4")
        .arg(mode, telemetry_.missionId)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 4, 'f', 1, QLatin1Char('0')));
}

void GroundAirMonitor::updateStageList()
{
    const QString state = telemetry_.missionState;
    int active = -1;
    if (state == QStringLiteral("TAKEOFF") || state == QStringLiteral("TAKEOFF_1P5M")) active = 0;
    else if (state.contains(QStringLiteral("HOVER"))) active = 1;
    else if (state.contains(QStringLiteral("ACQUIRE"))) active = 2;
    else if (state.contains(QStringLiteral("FOLLOW"))) active = 3;
    else if (state.contains(QStringLiteral("DROP")) || state.contains(QStringLiteral("DESCENT")) ||
             state.contains(QStringLiteral("TOUCHDOWN")) || state.contains(QStringLiteral("HOLD"))) active = 4;
    else if (state.contains(QStringLiteral("RE_TAKEOFF")) || state.contains(QStringLiteral("RETURN")) ||
             state == QStringLiteral("RTL")) active = 5;
    else if (state.contains(QStringLiteral("LAND")) || state.contains(QStringLiteral("COMPLETE"))) active = 6;

    for (int i = 0; i < stageLabels_.size(); ++i) {
        QLabel *label = stageLabels_.at(i);
        QString base = label->text();
        if (base.size() > 2)
            base = base.mid(2);
        if (i < active) {
            label->setText(QStringLiteral("✓　%1").arg(base));
            label->setStyleSheet(QStringLiteral("font-size:13px;color:#16825f;padding-left:4px;font-weight:600;"));
        } else if (i == active) {
            label->setText(QStringLiteral("●　%1").arg(base));
            label->setStyleSheet(QStringLiteral("font-size:13px;color:#1976d2;background:#edf5fd;border-radius:7px;padding-left:8px;font-weight:700;"));
        } else {
            label->setText(QStringLiteral("○　%1").arg(base));
            label->setStyleSheet(QStringLiteral("font-size:13px;color:#8a97a8;padding-left:4px;"));
        }
    }
}

void GroundAirMonitor::setLinkState(const QString &text, const QString &color, bool connected)
{
    if (!linkBadge_)
        return;
    linkBadge_->setText(QStringLiteral("%1  %2").arg(connected ? QStringLiteral("●") : QStringLiteral("●"), text));
    linkBadge_->setStyleSheet(QStringLiteral(
        "font-size:12px;font-weight:700;color:%1;background:%2;border:1px solid %3;"
        "border-radius:15px;padding:7px 12px;")
        .arg(color, connected ? QStringLiteral("#e9f8f2") : QStringLiteral("#f5f7fa"),
             connected ? QStringLiteral("#bde5d7") : QStringLiteral("#dce3eb")));
}

void GroundAirMonitor::updateFreshness()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    clockLabel_->setText(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd  HH:mm:ss")));
    if (lastReceiveMs_ <= 0) {
        packetAgeLabel_->setText(QStringLiteral("数据龄　--"));
        return;
    }
    const qint64 age = now - lastReceiveMs_;
    packetAgeLabel_->setText(QStringLiteral("数据龄　%1 ms").arg(age));
    packetAgeLabel_->setStyleSheet(QStringLiteral("font-size:12px;color:%1;")
        .arg(age > 1200 ? QStringLiteral("#c94d4d") : QStringLiteral("#16825f")));
    if (!demoEnabled_ && socket_->state() == QAbstractSocket::ConnectedState && age > 1200)
        setLinkState(QStringLiteral("数据超时"), QStringLiteral("#c94d4d"), false);
    else if (!demoEnabled_ && socket_->state() == QAbstractSocket::ConnectedState)
        setLinkState(QStringLiteral("实时数据已连接"), QStringLiteral("#168f6a"), true);
}

void GroundAirMonitor::toggleDemo()
{
    demoEnabled_ = !demoEnabled_;
    if (demoEnabled_) {
        if (socket_)
            socket_->abort();
        demoTimeS_ = 0.0;
        fieldView_->clearTrails();
        demoTimer_->start();
        demoButton_->setText(QStringLiteral("停止演示"));
        setLinkState(QStringLiteral("演示数据运行中"), QStringLiteral("#8b66cf"), true);
        appendLog(QStringLiteral("已启用本地演示数据"));
    } else {
        demoTimer_->stop();
        demoButton_->setText(QStringLiteral("演示数据"));
        setLinkState(QStringLiteral("等待数据连接"), QStringLiteral("#8a98aa"), false);
        appendLog(QStringLiteral("演示数据已停止"));
    }
}

void GroundAirMonitor::updateDemo()
{
    demoTimeS_ += 0.1;
    const double cycle = std::fmod(demoTimeS_, 86.0);
    const double trackLength = 3.0 + 2.0 * M_PI * 0.75;
    const double distance = std::fmod(qMax(0.0, cycle - 4.0) * 0.12, trackLength);
    QPointF car(1.5, 2.0);
    double yaw = 90.0;
    if (distance <= 1.5) {
        car = QPointF(1.5, 2.0 + distance);
        yaw = 90.0;
    } else if (distance <= 1.5 + M_PI * 0.75) {
        const double theta = M_PI - (distance - 1.5) / 0.75;
        car = QPointF(2.25 + 0.75 * std::cos(theta), 3.5 + 0.75 * std::sin(theta));
        yaw = qRadiansToDegrees(theta - M_PI / 2.0);
    } else if (distance <= 3.0 + M_PI * 0.75) {
        const double segment = distance - (1.5 + M_PI * 0.75);
        car = QPointF(3.0, 3.5 - segment);
        yaw = -90.0;
    } else {
        const double theta = -(distance - (3.0 + M_PI * 0.75)) / 0.75;
        car = QPointF(2.25 + 0.75 * std::cos(theta), 2.0 + 0.75 * std::sin(theta));
        yaw = qRadiansToDegrees(theta - M_PI / 2.0);
    }

    MonitorTelemetry demo;
    demo.timestampMs = QDateTime::currentMSecsSinceEpoch();
    demo.source = QStringLiteral("lidar_simulator");
    demo.missionId = QStringLiteral("D-DEMO-01");
    demo.missionMode = QStringLiteral("DROP");
    demo.elapsedS = cycle;
    demo.car.valid = true;
    demo.car.xM = car.x(); demo.car.yM = car.y(); demo.car.yawDeg = yaw;
    demo.car.speedMps = cycle < 4.0 ? 0.0 : 0.12;
    demo.car.batteryPercent = 86.0 - cycle * 0.08;
    demo.drone.valid = true;
    demo.drone.xM = cycle < 4.0 ? 0.75 : qBound(0.0, car.x() - 0.10, 4.0);
    demo.drone.yM = cycle < 4.0 ? 0.75 : qBound(0.0, car.y() - 0.05, 5.0);
    demo.drone.yawDeg = yaw;
    demo.drone.zM = cycle < 4.0 ? cycle / 4.0 * 1.5 :
                    cycle > 72.0 ? qMax(0.0, 1.5 - (cycle - 72.0) * 0.12) : 1.5;
    demo.drone.batteryPercent = 93.0 - cycle * 0.16;
    demo.flightMode = QStringLiteral("OFFBOARD");
    demo.armed = cycle > 0.5 && cycle < 84.0;
    demo.localizationOk = true;
    demo.carLinkOk = true;
    demo.droneLinkOk = true;
    demo.targetVisible = cycle > 8.0 && cycle < 58.0;
    demo.targetConfidence = demo.targetVisible ? 0.94 + 0.03 * std::sin(cycle) : 0.0;
    if (cycle < 4.0) demo.missionState = QStringLiteral("TAKEOFF");
    else if (cycle < 7.0) demo.missionState = QStringLiteral("HOVER_3S");
    else if (cycle < 10.0) demo.missionState = QStringLiteral("ACQUIRE_CAR");
    else if (cycle < 46.0) demo.missionState = QStringLiteral("FOLLOW_CAR");
    else if (cycle < 51.0) demo.missionState = QStringLiteral("ALIGN_AND_DROP");
    else if (cycle < 58.0) { demo.missionState = QStringLiteral("DROP"); demo.dropDone = true; }
    else if (cycle < 72.0) { demo.missionState = QStringLiteral("RETURN_HOME"); demo.dropDone = true; }
    else if (cycle < 84.0) { demo.missionState = QStringLiteral("LANDING"); demo.dropDone = true; }
    else { demo.missionState = QStringLiteral("COMPLETE"); demo.dropDone = true; }
    applyTelemetry(demo, true);
}

QString GroundAirMonitor::chineseState(const QString &state) const
{
    static const QMap<QString, QString> names = {
        {QStringLiteral("IDLE"), QStringLiteral("任务待机")},
        {QStringLiteral("TAKEOFF"), QStringLiteral("正在起飞")},
        {QStringLiteral("TAKEOFF_1P5M"), QStringLiteral("起飞至 1.5 m")},
        {QStringLiteral("HOVER"), QStringLiteral("稳定悬停")},
        {QStringLiteral("HOVER_3S"), QStringLiteral("稳定悬停 3 秒")},
        {QStringLiteral("ACQUIRE_CAR"), QStringLiteral("搜索并捕获小车")},
        {QStringLiteral("FOLLOW_CAR"), QStringLiteral("伴飞中")},
        {QStringLiteral("ALIGN_AND_DROP"), QStringLiteral("对准投放点")},
        {QStringLiteral("DROP"), QStringLiteral("正在抛投")},
        {QStringLiteral("DYNAMIC_DESCENT"), QStringLiteral("动态下降")},
        {QStringLiteral("TOUCHDOWN"), QStringLiteral("接触确认")},
        {QStringLiteral("HOLD_ON_CAR"), QStringLiteral("随车停留")},
        {QStringLiteral("RE_TAKEOFF"), QStringLiteral("再次起飞")},
        {QStringLiteral("RETURN_HOME"), QStringLiteral("返航 H 点")},
        {QStringLiteral("RTL"), QStringLiteral("自动返航")},
        {QStringLiteral("LANDING"), QStringLiteral("正在降落")},
        {QStringLiteral("LANDED"), QStringLiteral("已降落")},
        {QStringLiteral("COMPLETE"), QStringLiteral("任务完成")},
        {QStringLiteral("ABORT"), QStringLiteral("任务中止")},
        {QStringLiteral("ERROR"), QStringLiteral("任务故障")},
        {QStringLiteral("FAILED"), QStringLiteral("任务失败")}
    };
    return names.value(state.toUpper(), state.isEmpty() ? QStringLiteral("状态未知") : state);
}

QString GroundAirMonitor::formattedBattery(double percent) const
{
    if (!std::isfinite(percent) || percent < 0.0)
        return QStringLiteral("--");
    return QStringLiteral("%1 %").arg(qBound(0.0, percent, 100.0), 0, 'f', 0);
}

void GroundAirMonitor::appendLog(const QString &message, const QString &level)
{
    if (!logView_)
        return;
    const QString color = level == QStringLiteral("ERROR") ? QStringLiteral("#ff7d7d")
        : level == QStringLiteral("WARN") ? QStringLiteral("#f4c86c") : QStringLiteral("#76d9bb");
    logView_->append(QStringLiteral("<span style='color:#75879c'>%1</span> "
                                    "<span style='color:%2'>[%3]</span> %4")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")),
             color, level, message.toHtmlEscaped()));
}
