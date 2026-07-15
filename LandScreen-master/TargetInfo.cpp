#include "TargetInfo.h"
#include <QMessageBox>
#include <QFont>
#include <QDebug>

TargetInfo::TargetInfo(QWidget *parent)
    : QDialog(parent)
    , mainLayout(nullptr)
    , scrollArea(nullptr)
    , scrollContent(nullptr)
    , scrollLayout(nullptr)
{
    setupUI();
    // 不在构造函数里加载数据
}

void TargetInfo::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // 清空旧内容
    QLayoutItem *child;
    while ((child = scrollLayout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    loadTargets(); // 每次显示时重新加载数据
}

TargetInfo::~TargetInfo()
{
}

void TargetInfo::setupUI()
{
    setWindowTitle("目标信息");
    setModal(true); // 设置为模态对话框
    resize(800, 600); // 增大窗口尺寸以容纳统计信息

    // 主布局
    mainLayout = new QVBoxLayout(this);
    
    // 创建水平布局来放置目标列表和统计信息
    QHBoxLayout* contentLayout = new QHBoxLayout;
    
    // 左侧：目标列表
    QWidget* targetListWidget = new QWidget;
    QVBoxLayout* targetListLayout = new QVBoxLayout(targetListWidget);
    
    QLabel* targetListTitle = new QLabel("目标详细信息");
    targetListTitle->setStyleSheet("font-size: 16px; font-weight: bold; margin: 5px;");
    targetListLayout->addWidget(targetListTitle);

    // 创建滚动区域
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 滚动内容容器
    scrollContent = new QWidget;
    scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setAlignment(Qt::AlignTop);

    scrollArea->setWidget(scrollContent);
    targetListLayout->addWidget(scrollArea);
    
    // 右侧：统计信息
    createStatisticsSection();
    
    // 添加到水平布局
    contentLayout->addWidget(targetListWidget, 2); // 目标列表占2/3
    contentLayout->addWidget(statisticsWidget, 1); // 统计信息占1/3
    
    mainLayout->addLayout(contentLayout);

    // 添加关闭按钮
    QPushButton* closeButton = new QPushButton("关闭");
    closeButton->setFont(QFont("Arial", 12, QFont::Bold));
    closeButton->setMinimumHeight(40);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeButton);
}

void TargetInfo::createStatisticsSection()
{
    statisticsWidget = new QWidget;
    statisticsWidget->setStyleSheet("QWidget { background-color: #e8e8e8; border: 1px solid #ccc; }");
    statisticsLayout = new QVBoxLayout(statisticsWidget);
    
    QLabel* statisticsTitle = new QLabel("目标统计");
    statisticsTitle->setStyleSheet("font-size: 16px; font-weight: bold; margin: 5px; text-align: center;");
    statisticsTitle->setAlignment(Qt::AlignCenter);
    statisticsLayout->addWidget(statisticsTitle);
    
    statisticsLayout->addStretch(); // 添加弹性空间，让统计信息居中显示
}

void TargetInfo::updateStatistics(const std::vector<Target>& targets)
{
    // 清除旧的统计标签
    for (auto& pair : statisticsLabels) {
        statisticsLayout->removeWidget(pair.second);
        delete pair.second;
    }
    statisticsLabels.clear();
    
    // 统计各类型目标的n值总和
    std::map<QString, int> typeSum;
    for (const auto& target : targets) {
        if (target.name != "NULL") {
            typeSum[target.name] += target.n;
        }
    }
    
    // 创建统计标签
    for (const auto& pair : typeSum) {
        QLabel* countLabel = new QLabel(QString("%1: %2").arg(pair.first).arg(pair.second));
        countLabel->setStyleSheet("font-size: 14px; margin: 3px; padding: 5px; background-color: white; border-radius: 3px;");
        countLabel->setAlignment(Qt::AlignCenter);
        
        statisticsLabels[pair.first] = countLabel;
        statisticsLayout->insertWidget(statisticsLayout->count() - 1, countLabel); // 在弹性空间前插入
    }
    
    // 添加总数统计
    int totalSum = 0;
    for (const auto& pair : typeSum) {
        totalSum += pair.second;
    }
    
    QLabel* totalLabel = new QLabel(QString("总计: %1").arg(totalSum));
    totalLabel->setStyleSheet("font-size: 15px; font-weight: bold; margin: 5px; padding: 8px; background-color: #d0d0d0; border-radius: 3px;");
    totalLabel->setAlignment(Qt::AlignCenter);
    statisticsLayout->insertWidget(statisticsLayout->count() - 1, totalLabel);
}

void TargetInfo::loadTargets()
{
    SharedData& sharedData = SharedData::getInstance();
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lock(sharedData.getMutex());
        targets = sharedData.getTargets();
    }

    // 更新统计信息
    updateStatistics(targets);

    // 创建所有目标项
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].name!="NULL")
        {
            createTargetItem(targets[i], static_cast<int>(i));
            qDebug()<<"name"<<targets[i].name<<"x"<<targets[i].x<<"y"<<targets[i].y;
        }
    }
}

void TargetInfo::createTargetItem(const Target& target, int index)
{
    // 创建水平布局的目标项
    QWidget* itemWidget = new QWidget;
    QHBoxLayout* itemLayout = new QHBoxLayout(itemWidget);

    // 去除边框，仅设置背景色和最小高度
    itemWidget->setStyleSheet("QWidget { margin: 1px; padding: 2px; background-color: #f0f0f0; min-height: 24px; }");

    // 设置较小字体
    QFont labelFont("Arial", 15, QFont::Normal);

    // 名称标签
    QLabel* nameLabel = new QLabel(target.name);
    nameLabel->setMinimumWidth(50);
    nameLabel->setFont(labelFont);
    nameLabel->setStyleSheet("font-size: 15px;");

    // X坐标标签（整数显示）
    QLabel* xLabel = new QLabel(QString("A: %1").arg(int(target.a)));
    xLabel->setMinimumWidth(40);
    xLabel->setFont(labelFont);
    xLabel->setStyleSheet("font-size: 15px;");

    // Y坐标标签（整数显示）
    QLabel* yLabel = new QLabel(QString("B: %1").arg(int(target.b)));
    yLabel->setMinimumWidth(40);
    yLabel->setFont(labelFont);
    yLabel->setStyleSheet("font-size: 15px;");

    QLabel* nLabel = new QLabel(QString("N: %1").arg(int(target.n)));
    yLabel->setMinimumWidth(40);
    yLabel->setFont(labelFont);
    yLabel->setStyleSheet("font-size: 15px;");

    // 添加到布局（无按钮）
    itemLayout->addWidget(nameLabel);
    itemLayout->addStretch(); // 添加弹性空间
    itemLayout->addWidget(xLabel);
    itemLayout->addStretch(); // 添加弹性空间
    itemLayout->addWidget(yLabel);
    itemLayout->addStretch(); // 添加弹性空间
    itemLayout->addWidget(nLabel);

    // 添加到滚动布局
    scrollLayout->addWidget(itemWidget);
}

void TargetInfo::onRescueButtonClicked(const Target& target)
{
    SharedData& sharedData = SharedData::getInstance();
        std::lock_guard<std::mutex> lock(sharedData.getMutex());
        Target& chosenTarget = sharedData.getChosenTarget();

        // 设置选中的目标
        chosenTarget.x = target.x;
        chosenTarget.y = target.y;
        chosenTarget.name = target.name;
}
