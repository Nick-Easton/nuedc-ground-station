#pragma once

#include <QDialog>
#include <QLabel>
#include <QMap>
#include <QTableWidget>
#include <QTimer>

#include "plane_targets.h"

class QHideEvent;
class QShowEvent;

class TargetInfo : public QDialog
{
    Q_OBJECT

public:
    explicit TargetInfo(QWidget *parent = nullptr);
    ~TargetInfo() override = default;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct AnimalWidgets {
        QLabel *count = nullptr;
        QLabel *grids = nullptr;
    };

    void setupUI();
    void loadTargets();

    QMap<QString, AnimalWidgets> animalWidgets;
    QTableWidget *detailTable = nullptr;
    QLabel *totalLabel = nullptr;
    QTimer *refreshTimer = nullptr;
};
