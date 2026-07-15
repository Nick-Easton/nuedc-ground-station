#pragma once

#include <vector>
#include <mutex>
#include<QString>
#include <fstream>      // 用于 std::ifstream
#include <sstream>      // 用于 std::istringstream
#include <string>       // 用于 std::string 和 std::getline
#include <algorithm>
#include <cmath>
#include<QDir>
struct Target {
    double x;
    double y;
    QString name;
    int n = 0;
    int a = -1;
    int b = -1;
    bool operator==(const Target& other) const
    {
        return name == other.name;
    }
};

class SharedData {
public:
    static SharedData& getInstance() {
        static SharedData instance;
        return instance;
    }
    
    std::vector<Target>& getTargets() {
        return targets_;
    }
    Target& getChosenTarget()
    {
        return target_chosen_;
    }
    std::mutex& getMutex() {
        return mutex_;
    }
void addTargetIfNew(Target& newTarget) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (targets_.empty()) {
        newTarget.n = 1;
        targets_.push_back(newTarget);
        std::ofstream logFile("log.txt", std::ios::app);
        logFile << newTarget.x << "," << newTarget.y << "," << newTarget.name.toStdString() << std::endl;
        return;
    }

    bool abSameFound = false;
    for (auto& t : targets_) {
    if(t.a == 9&&t.b == 1) return;//start point
        if (t.name == newTarget.name) {
            if (t.a == newTarget.a && t.b == newTarget.b) {
                double dis  = std::sqrt(std::pow(newTarget.x - t.x, 2) + std::pow(newTarget.y - t.y, 2));
                if (dis > 0.08) {
                    t.n += 1;
                    t.x = newTarget.x;
                    t.y = newTarget.y;
                    newTarget.n = t.n;
                    std::ofstream logFile("log.txt", std::ios::app);
                    logFile << newTarget.x << "," << newTarget.y << "," << newTarget.name.toStdString() << std::endl;
                }
                // 不管距离如何，只要ab相同就不再添加新目标
                return;
            }
            abSameFound = true;
        }
    }
    // 遍历完所有目标后，如果没有同名同ab的目标，则添加
    newTarget.n = 1;
    targets_.push_back(newTarget);
    std::ofstream logFile("log.txt", std::ios::app);
    logFile << newTarget.x << "," << newTarget.y << "," << newTarget.name.toStdString() << std::endl;
}


private:
    SharedData()
    {
        target_chosen_.x = -1;
        target_chosen_.y = -1;
        target_chosen_.name = "NULL";
        // Target t1,t2,t3,t4,t5;
        // t1.name ="a";
        // t1.n = 2;
        // targets_.push_back(t1);
        // t2.name = "b";
        // t2.n = 3;
        // targets_.push_back(t2);
        // t3.name ="a";
        // t3.n = 2;
        // targets_.push_back(t3);
        // t4.name ="c";
        // t4.n = 2;
        // targets_.push_back(t4);
        // t5.name ="d";
        // t5.n = 2;
        // targets_.push_back(t5);



    };


    std::vector<Target> targets_;
    Target target_chosen_;
    Target target_display;
    std::mutex mutex_;
};



