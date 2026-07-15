#include "blink.h"
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QDebug>

// 初始化静态成员
Blink* Blink::instance = nullptr;

// 获取实例的静态方法
Blink* Blink::getInstance(int32_t time,QColor color, QWidget *parent)
{
    if (!instance) {
        instance = new Blink(time,color, parent);
    }
    return instance;
}

Blink::Blink(int32_t time,QColor color, QWidget *parent) : QWidget(parent), isLighted(true), color(color)
{
    qDebug() << "blink created";
    
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Blink::toggleColor);
    timer->start(500); // 0.5秒触发一次

    QTimer::singleShot(time, this, [this]() {
        this->close();
        this->deleteLater();  // 安排在事件循环中删除对象
    });
    // this->setWindowState(Qt::WindowFullScreen);

}

Blink::~Blink()
{
    // 确保实例指针在销毁时被重置
    if (instance == this) {
        instance = nullptr;
    }
}

void Blink::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    // 根据 isLighted 选择颜色
    painter.fillRect(this->rect(), isLighted ? color : Qt::white);
}

void Blink::toggleColor()
{
    isLighted = !isLighted; // 切换颜色
    update();               // 触发重绘
}
