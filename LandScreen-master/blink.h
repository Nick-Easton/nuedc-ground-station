#ifndef BLINK_H
#define BLINK_H

#include <QWidget>
#include <QTimer>
#include <QColor>
class Blink : public QWidget
{
    Q_OBJECT
public:
    // 获取实例的静态方法，取代直接构造
    static Blink* getInstance(int32_t time,QColor color = Qt::red, QWidget *parent = nullptr);
    
    // 析构函数
    ~Blink();
    
private:
    // 私有构造函数，防止外部直接创建实例
    Blink(int32_t time,QColor color, QWidget *parent = nullptr);
    
    // 单例实例指针
    static Blink* instance;
    
    bool isLighted;
    QColor color;
    QTimer *timer;
    
private slots:
    void toggleColor();
    
protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // BLINK_H