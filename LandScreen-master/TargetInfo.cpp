#include "TargetInfo.h"

#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

#include <array>

namespace {
struct AnimalDefinition {
    const char *key;
    const char *chineseName;
    const char *englishName;
    const char *color;
};

const std::array<AnimalDefinition, 5> kAnimals = {{
    {"elephant", "大象", "elephant", "#6C7A89"},
    {"tiger", "老虎", "tiger", "#E67E22"},
    {"monkey", "猴子", "monkey", "#9B6B43"},
    {"kongque", "孔雀", "kongque", "#168D82"},
    {"wolf", "狼", "wolf", "#52677D"},
}};

QString chineseNameFor(const QString &key)
{
    for (const AnimalDefinition &animal : kAnimals) {
        if (key == QLatin1String(animal.key))
            return QString::fromUtf8(animal.chineseName);
    }
    return key;
}

QString gridText(const Target &target)
{
    if (target.a >= 1 && target.a <= 9 && target.b >= 1 && target.b <= 7)
        return QString("A%1B%2").arg(target.a).arg(target.b);
    return QStringLiteral("位置待定位");
}
}

TargetInfo::TargetInfo(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    connect(refreshTimer, &QTimer::timeout, this, &TargetInfo::loadTargets);
}

void TargetInfo::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    loadTargets();
    refreshTimer->start();
}

void TargetInfo::hideEvent(QHideEvent *event)
{
    refreshTimer->stop();
    QDialog::hideEvent(event);
}

void TargetInfo::setupUI()
{
    setWindowTitle(QStringLiteral("动物目标信息"));
    setModal(true);
    setMinimumSize(960, 640);
    setStyleSheet("QDialog { background: #F4F6F8; color: #25313C; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(18);

    QLabel *title = new QLabel(QStringLiteral("动物识别结果"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 30px; font-weight: 700; color: #1F2D3D;");
    mainLayout->addWidget(title);

    QLabel *subtitle = new QLabel(
        QStringLiteral("固定显示 5 类动物；数量按识别记录汇总，位置按 A/B 方格显示"), this
    );
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("font-size: 15px; color: #6B7785;");
    mainLayout->addWidget(subtitle);

    QGridLayout *cardLayout = new QGridLayout;
    cardLayout->setHorizontalSpacing(14);
    cardLayout->setVerticalSpacing(14);

    for (int index = 0; index < static_cast<int>(kAnimals.size()); ++index) {
        const AnimalDefinition &animal = kAnimals[index];
        QFrame *card = new QFrame(this);
        card->setMinimumHeight(150);
        card->setStyleSheet(QString(
            "QFrame { background: white; border: 1px solid #D9E0E6; "
            "border-top: 6px solid %1; border-radius: 10px; }"
        ).arg(animal.color));

        QVBoxLayout *cardBody = new QVBoxLayout(card);
        cardBody->setContentsMargins(16, 12, 16, 14);
        cardBody->setSpacing(5);

        QLabel *name = new QLabel(
            QString("%1  /  %2")
                .arg(QString::fromUtf8(animal.chineseName))
                .arg(QLatin1String(animal.englishName)),
            card
        );
        name->setAlignment(Qt::AlignCenter);
        name->setStyleSheet("border: none; font-size: 18px; font-weight: 700;");

        QLabel *count = new QLabel(QStringLiteral("0"), card);
        count->setAlignment(Qt::AlignCenter);
        count->setStyleSheet(QString(
            "border: none; font-size: 40px; font-weight: 800; color: %1;"
        ).arg(animal.color));

        QLabel *unit = new QLabel(QStringLiteral("数量"), card);
        unit->setAlignment(Qt::AlignCenter);
        unit->setStyleSheet("border: none; font-size: 13px; color: #7B8794;");

        QLabel *grids = new QLabel(QStringLiteral("所在格子：暂无"), card);
        grids->setAlignment(Qt::AlignCenter);
        grids->setWordWrap(true);
        grids->setStyleSheet(
            "border: none; background: #F7F9FB; border-radius: 5px; "
            "padding: 6px; font-size: 13px; color: #465565;"
        );

        cardBody->addWidget(name);
        cardBody->addWidget(count);
        cardBody->addWidget(unit);
        cardBody->addWidget(grids);
        cardLayout->addWidget(card, 0, index);

        animalWidgets.insert(QLatin1String(animal.key), {count, grids});
    }
    mainLayout->addLayout(cardLayout);

    QFrame *detailFrame = new QFrame(this);
    detailFrame->setStyleSheet(
        "QFrame { background: white; border: 1px solid #D9E0E6; border-radius: 10px; }"
    );
    QVBoxLayout *detailLayout = new QVBoxLayout(detailFrame);
    detailLayout->setContentsMargins(16, 14, 16, 16);
    detailLayout->setSpacing(10);

    QLabel *detailTitle = new QLabel(QStringLiteral("按方格统计"), detailFrame);
    detailTitle->setStyleSheet("border: none; font-size: 19px; font-weight: 700;");
    detailLayout->addWidget(detailTitle);

    detailTable = new QTableWidget(detailFrame);
    detailTable->setColumnCount(3);
    detailTable->setHorizontalHeaderLabels(
        {QStringLiteral("动物种类"), QStringLiteral("所在方格"), QStringLiteral("数量")}
    );
    detailTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    detailTable->verticalHeader()->setVisible(false);
    detailTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    detailTable->setSelectionMode(QAbstractItemView::NoSelection);
    detailTable->setAlternatingRowColors(true);
    detailTable->setStyleSheet(
        "QTableWidget { border: 1px solid #E1E6EB; gridline-color: #E8EDF1; "
        "font-size: 16px; alternate-background-color: #F7F9FB; }"
        "QHeaderView::section { background: #EAF0F5; border: none; "
        "border-right: 1px solid #D7DFE6; padding: 10px; font-size: 16px; "
        "font-weight: 700; }"
    );
    detailLayout->addWidget(detailTable, 1);
    mainLayout->addWidget(detailFrame, 1);

    QHBoxLayout *footer = new QHBoxLayout;
    totalLabel = new QLabel(QStringLiteral("目标总数：0"), this);
    totalLabel->setStyleSheet(
        "background: #22364A; color: white; border-radius: 7px; "
        "padding: 11px 20px; font-size: 20px; font-weight: 700;"
    );

    QPushButton *closeButton = new QPushButton(QStringLiteral("返回主页面"), this);
    closeButton->setMinimumSize(180, 48);
    closeButton->setStyleSheet(
        "QPushButton { background: #1677C8; color: white; border: none; "
        "border-radius: 7px; font-size: 17px; font-weight: 700; }"
        "QPushButton:hover { background: #0E65AD; }"
    );
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

    footer->addWidget(totalLabel);
    footer->addStretch();
    footer->addWidget(closeButton);
    mainLayout->addLayout(footer);
}

void TargetInfo::loadTargets()
{
    std::vector<Target> targets;
    SharedData &sharedData = SharedData::getInstance();
    {
        std::lock_guard<std::mutex> lock(sharedData.getMutex());
        targets = sharedData.getTargets();
    }

    QMap<QString, int> totals;
    QMap<QString, QMap<QString, int>> byGrid;
    for (const AnimalDefinition &animal : kAnimals)
        totals.insert(QLatin1String(animal.key), 0);

    for (const Target &target : targets) {
        const QString key = target.name.trimmed().toLower();
        if (!totals.contains(key))
            continue;
        const int count = qMax(1, target.n);
        const QString grid = gridText(target);
        totals[key] += count;
        byGrid[key][grid] += count;
    }

    int grandTotal = 0;
    int rowCount = 0;
    for (const AnimalDefinition &animal : kAnimals) {
        const QString key = QLatin1String(animal.key);
        grandTotal += totals.value(key);
        rowCount += byGrid.value(key).size();

        const AnimalWidgets widgets = animalWidgets.value(key);
        widgets.count->setText(QString::number(totals.value(key)));

        QStringList locations;
        const QMap<QString, int> grids = byGrid.value(key);
        for (auto iterator = grids.cbegin(); iterator != grids.cend(); ++iterator)
            locations << QString("%1 ×%2").arg(iterator.key()).arg(iterator.value());
        widgets.grids->setText(
            locations.isEmpty()
                ? QStringLiteral("所在格子：暂无")
                : QStringLiteral("所在格子：") + locations.join(QStringLiteral("、"))
        );
    }

    detailTable->clearContents();
    detailTable->setRowCount(qMax(1, rowCount));
    int row = 0;
    for (const AnimalDefinition &animal : kAnimals) {
        const QString key = QLatin1String(animal.key);
        const QMap<QString, int> grids = byGrid.value(key);
        for (auto iterator = grids.cbegin(); iterator != grids.cend(); ++iterator) {
            const QString displayName = QString("%1 / %2")
                .arg(chineseNameFor(key))
                .arg(key);
            detailTable->setItem(row, 0, new QTableWidgetItem(displayName));
            detailTable->setItem(row, 1, new QTableWidgetItem(iterator.key()));
            detailTable->setItem(row, 2, new QTableWidgetItem(QString::number(iterator.value())));
            for (int column = 0; column < 3; ++column)
                detailTable->item(row, column)->setTextAlignment(Qt::AlignCenter);
            ++row;
        }
    }

    if (rowCount == 0) {
        QTableWidgetItem *empty = new QTableWidgetItem(QStringLiteral("暂无有效识别结果"));
        empty->setTextAlignment(Qt::AlignCenter);
        detailTable->setItem(0, 0, empty);
        detailTable->setSpan(0, 0, 1, 3);
    } else {
        detailTable->clearSpans();
    }

    totalLabel->setText(QString("目标总数：%1").arg(grandTotal));
}
