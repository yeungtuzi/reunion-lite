#include "campaign.h"
#include "launch.h"
#include "platform/linux/runprep.h"

#include <QApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMessageBox>
#include <QFileInfo>
#include <QTimer>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdlib>
#include <string>
#include <vector>

static rl::Campaign g_campaign;

static void FillTable(QTableWidget* table, const QString& filter) {
    table->setRowCount(0);
    const QString f = filter.trimmed();
    for (const auto& m : g_campaign.missions) {
        if (!m.Launchable()) continue;
        const QString name = QString::fromStdString(m.name);
        const QString pack = QString::fromStdString(m.packName);
        if (!f.isEmpty() && !name.contains(f, Qt::CaseInsensitive) &&
            !pack.contains(f, Qt::CaseInsensitive))
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, static_cast<int>(&m - &g_campaign.missions[0]));
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, new QTableWidgetItem(pack));
        table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(m.side)));
        table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(m.difficulty)));
    }
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    std::string root = ".";
    int launchRow = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--launch" && i + 1 < argc) launchRow = std::atoi(argv[++i]);
        else if (!a.empty() && a[0] != '-') root = a;
    }

    QWidget win;
    win.setWindowTitle("Reunion Lite (Qt)");
    win.resize(1000, 650);

    auto* search = new QLineEdit(&win);
    search->setPlaceholderText("搜索任务名 / 任务包 / 场景");
    auto* rescan = new QPushButton("重新扫描", &win);
    auto* launch = new QPushButton("启动游戏", &win);
    auto* table = new QTableWidget(&win);
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"任务", "任务包", "阵营", "难度"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto* status = new QStatusBar(&win);

    auto* top = new QHBoxLayout;
    top->addWidget(search, 1);
    top->addWidget(rescan);
    top->addWidget(launch);

    auto* layout = new QVBoxLayout(&win);
    layout->addLayout(top);
    layout->addWidget(table, 1);
    layout->addWidget(status);

    auto doScan = [&]() {
        g_campaign = rl::ScanCampaigns(root);
        FillTable(table, search->text());
        status->showMessage(
            QString("任务包 %1 / 任务 %2 / 问题 %3")
                .arg(g_campaign.packs.size())
                .arg(g_campaign.missions.size())
                .arg(g_campaign.issues.size()));
    };

    QObject::connect(search, &QLineEdit::textChanged, [&](const QString& t) { FillTable(table, t); });
    QObject::connect(rescan, &QPushButton::clicked, doScan);
    auto startSelected = [&]() {
        int row = table->currentRow();
        if (row < 0 && table->rowCount() > 0) row = 0;
        if (row < 0) { qWarning("no row selected"); status->showMessage("先选择一个任务"); return; }
        auto* item = table->item(row, 0);
        if (!item) return;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= (int)g_campaign.missions.size()) return;
        const rl::Mission& mission = g_campaign.missions[idx];

        rl::LaunchOptions options;
        rl::LaunchPlan plan;
        std::string err;
        if (!rl::BuildLaunchPlan(root, mission, options, plan, &g_campaign.missions)) {
            qWarning("plan failed: %s", plan.error.c_str());
            status->showMessage(QString::fromStdString("构建启动计划失败：" + plan.error));
            return;
        }
        std::vector<std::string> warnings;
        if (!rlwin::PrepareRun(plan, options, err, warnings)) {
            qWarning("prepare failed: %s", err.c_str());
            status->showMessage(QString::fromStdString("准备 Run 失败：" + err));
            return;
        }

        const QString wine = qEnvironmentVariable("REUNION_WINE",
            "/opt/steamos/steam/steamapps/common/Proton 11.0 (ARM64)/files/bin-arm64/wine");
        const QString prefix = qEnvironmentVariable("REUNION_WINEPREFIX",
            "/home/lf/.local/share/m1000/work/ra2-pfx");
        QString runDir = QString::fromStdString(plan.runDir);
        runDir.replace('\\', '/');
        const QString exe = plan.useAres ? "Syringe.exe" : "gamemd-spawn.exe";
        QStringList args;
        if (plan.useAres) { args << "gamemd.exe" << "-SPAWN"; }
        else { args << "-SPAWN"; }
        if (!plan.extraArgs.empty()) args << QString::fromStdString(plan.extraArgs);

        auto* proc = new QProcess(&win);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("WINEPREFIX", prefix);
        env.insert("WINEDLLPATH", "/opt/steamos/steam/steamapps/common/Proton 11.0 (ARM64)/files/lib/wine");
        env.insert("LD_LIBRARY_PATH",
                   "/opt/steamos/steam/steamapps/common/Proton 11.0 (ARM64)/files/lib/aarch64-linux-gnu:"
                   "/opt/steamos/steam/steamapps/common/Proton 11.0 (ARM64)/files/lib");
        env.insert("WINESERVER",
                   "/opt/steamos/steam/steamapps/common/Proton 11.0 (ARM64)/files/bin-arm64/wineserver");
        env.insert("DISPLAY", ":0");
        env.insert("XDG_RUNTIME_DIR", "/run/user/1000");
        env.insert("XAUTHORITY", qEnvironmentVariable("XAUTHORITY"));
        proc->setProcessEnvironment(env);
        proc->setWorkingDirectory(runDir);
        proc->setProgram(wine);
        proc->setArguments(QStringList{exe} + args);
        QObject::connect(proc, &QProcess::errorOccurred, [&, proc](QProcess::ProcessError) {
            qWarning("launch error: %s", qPrintable(proc->errorString()));
            status->showMessage("启动失败：" + proc->errorString());
        });
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [&, proc](int code, QProcess::ExitStatus) {
            rlwin::SyncSettingsBack(root);
            status->showMessage(QString("游戏退出（%1）").arg(code));
            proc->deleteLater();
        });
        proc->start();
        qWarning("launching %s -> %s", mission.name.c_str(), qPrintable(proc->program()));
        status->showMessage(QString::fromStdString("已启动：" + mission.name));
    };
    QObject::connect(launch, &QPushButton::clicked, startSelected);
    QObject::connect(table, &QTableWidget::cellDoubleClicked, [&](int, int) { startSelected(); });

    if (launchRow >= 0) {
        table->setCurrentCell(launchRow, 0);
        QTimer::singleShot(500, [&]() { startSelected(); });
    }

    doScan();
    win.show();
    return app.exec();
}
