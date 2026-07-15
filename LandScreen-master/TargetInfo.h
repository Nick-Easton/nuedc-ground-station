#pragma once

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <map>
#include "plane_targets.h"

class TargetInfo : public QDialog
{
    Q_OBJECT

public:
    explicit TargetInfo(QWidget *parent = nullptr);
    ~TargetInfo();

protected:
    void showEvent(QShowEvent *event) override; // 新增：重载showEvent

private:
    void onRescueButtonClicked(const Target& target);
    void setupUI();
    void loadTargets();
    void createTargetItem(const Target& target, int index);
    void createStatisticsSection(); // 新增：创建统计区域
    void updateStatistics(const std::vector<Target>& targets); // 新增：更新统计信息

    QVBoxLayout* mainLayout;
    QScrollArea* scrollArea;
    QWidget* scrollContent;
    QVBoxLayout* scrollLayout;
    
    // 新增：统计相关UI组件
    QWidget* statisticsWidget;
    QVBoxLayout* statisticsLayout;
    std::map<QString, QLabel*> statisticsLabels; // 存储各类型的统计标签

    QString posLabelStyle = "QLabel { font-size: 66px; font-weight: bold; }";
    QString buttonStyle = "QPushButton {background-color: rgb(255, 255, 255);font-size:40px;}";
};