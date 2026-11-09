// Reunion Lite — Linux Qt front end.
//
// Mirrors the macOS 0.4 launcher: top bar, pack sidebar, mission table and an
// inspector with the game options panel.  Platform differences: the classic
// engine runs through Wine-FEX (Proton) instead of CrossOver, and CrossOver's
// bottle/trainer settings do not apply.
#include "campaign.h"
#include "client_defaults.h"
#include "launch.h"
#include "platform/linux/runprep.h"
#include "platform/linux/vera.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QSize>
#include <QUrl>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <map>
#include <cstdlib>
#include <unistd.h>
#include <functional>
#include <string>
#include <vector>

static rl::Campaign g_campaign;
static rl::LaunchOptions g_options;
static std::vector<rl::ModInfo> g_mods;

namespace {

QString Uid(const rl::Mission& m) {
    return QString::fromStdString(m.packId) + "#" + QString::number(m.index);
}

QString LogFilePath() {
    const QString dir = QDir::homePath() + "/.local/share/reunion-lite";
    QDir().mkpath(dir);
    return dir + "/last-launch.log";
}

QString FindVera20k() {
    const QString fromEnv = qEnvironmentVariable("VERA20K_BIN");
    if (!fromEnv.isEmpty() && QFileInfo::exists(fromEnv)) return fromEnv;
    QSettings settings("reunion-lite", "reunion-lite");
    const QString saved = settings.value("vera20kBin").toString();
    if (!saved.isEmpty() && QFileInfo::exists(saved)) return saved;
    const QStringList candidates = {
        QDir::homePath() + "/vera20k/target/release/vera20k",
        QDir::homePath() + "/.local/bin/vera20k",
        "/opt/vera20k/bin/vera20k",
        "/usr/local/bin/vera20k",
    };
    for (const QString& c : candidates)
        if (QFileInfo::exists(c)) return c;
    return {};
}

QIcon GameIcon(const std::string& gameRoot) {
    // Prefer the icon installed by scripts/linux/install-desktop.sh (PNG at
    // several sizes), then fall back to the game's .ico.
    const QString installed = QDir::homePath() +
        "/.local/share/icons/hicolor/256x256/apps/reunion-lite.png";
    if (QFileInfo::exists(installed)) {
        QIcon icon(installed);
        if (!icon.isNull()) return icon;
    }
    const char* names[] = {"Resources/clienticon.ico", "clienticon.ico",
                           "YURI.ICO", "yuri.ico", "game.ico"};
    for (const char* n : names) {
        const QString path = QString::fromStdString(gameRoot) + "/" + n;
        if (!QFileInfo::exists(path)) continue;
        QIcon icon(path);
        if (!icon.isNull()) return icon;
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    // KDE/Wayland: run natively so the compositor scale applies.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "wayland");
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setApplicationName("Reunion Lite");
    QApplication::setOrganizationName("reunion-lite");
    QApplication::setDesktopFileName("reunion-lite");

    QFont base = app.font();
    base.setPointSizeF(std::max(10.5, base.pointSizeF()));
    app.setFont(base);

    std::string root = ".";
    int launchRow = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--launch" && i + 1 < argc) launchRow = std::atoi(argv[++i]);
        else if (!a.empty() && a[0] != '-') root = a;
    }

    QSettings settings("reunion-lite", "reunion-lite");
    QStringList favorites = settings.value("favorites").toStringList();
    QIcon icon = GameIcon(root);
    if (!icon.isNull()) QApplication::setWindowIcon(icon);

    QWidget win;
    if (!icon.isNull()) win.setWindowIcon(icon);
    win.setWindowTitle("Reunion Lite — 红色警戒2 重聚");
    win.resize(1500, 900);

    // ---- top bar -------------------------------------------------------
    auto* chooseRoot = new QPushButton("游戏目录", &win);
    auto* rootLabel = new QLabel(QString::fromStdString(root), &win);
    rootLabel->setStyleSheet("color: palette(mid);");
    rootLabel->setMaximumWidth(360);
    auto* search = new QLineEdit(&win);
    search->setPlaceholderText("搜索任务 / 任务包 / 场景 / 阵营");
    search->setClearButtonEnabled(true);
    search->setMaximumWidth(420);
    auto* rescan = new QPushButton("重新扫描", &win);
    auto* trainer = new QPushButton("修改器", &win);
    trainer->setEnabled(false);
    trainer->setToolTip("Linux 上暂不支持 CrossOver 修改器");
    auto* settingsBtn = new QPushButton("设置", &win);

    auto* top = new QHBoxLayout;
    top->addWidget(chooseRoot);
    top->addWidget(rootLabel);
    top->addStretch(1);
    top->addWidget(search);
    top->addWidget(rescan);
    top->addWidget(trainer);
    top->addWidget(settingsBtn);

    // ---- sidebar -------------------------------------------------------
    auto* packs = new QListWidget(&win);
    packs->setMinimumWidth(190);
    packs->setMaximumWidth(320);

    // ---- mission table -------------------------------------------------
    auto* table = new QTableWidget(&win);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"★", "任务", "任务包", "阵营", "难度", "场景"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < 6; ++c)
        table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);

    // ---- inspector -----------------------------------------------------
    auto* inspector = new QWidget;
    auto* insName = new QLabel("选择一个任务", inspector);
    QFont nameFont = insName->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.35);
    nameFont.setBold(true);
    insName->setFont(nameFont);
    insName->setWordWrap(true);
    auto* starBtn = new QPushButton("☆", inspector);
    starBtn->setFixedWidth(34);
    starBtn->setToolTip("收藏 / 取消收藏");
    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(insName, 1);
    headerRow->addWidget(starBtn);

    auto* tags = new QLabel(inspector);
    tags->setTextFormat(Qt::PlainText);
    auto* scenario = new QLabel(inspector);
    scenario->setStyleSheet("color: palette(mid);");
    auto* insDesc = new QTextBrowser(inspector);
    insDesc->setMinimumHeight(90);
    insDesc->setMaximumHeight(220);

    auto* optTitle = new QLabel("游戏选项", inspector);
    QFont secFont = optTitle->font();
    secFont.setBold(true);
    optTitle->setFont(secFont);

    auto* modCombo = new QComboBox(inspector);
    auto* difficulty = new QComboBox(inspector);
    difficulty->addItems({"简单", "中等", "困难"});
    auto* credits = new QComboBox(inspector);
    credits->addItem("保持地图", -1);
    credits->addItem("10,000", 10000);
    credits->addItem("50,000", 50000);
    credits->addItem("100,000", 100000);
    auto* speed = new QComboBox(inspector);
    for (int i = 0; i <= 6; ++i) speed->addItem(QString::number(i), i);
    auto* autosave = new QComboBox(inspector);
    autosave->addItems({"关闭", "5 分钟", "10 分钟", "20 分钟"});
    auto* music = new QComboBox(inspector);
    music->addItem("保持", -1);
    music->addItem("关闭", 0);
    music->addItem("30%", 30);
    music->addItem("50%", 50);
    music->addItem("70%", 70);
    music->addItem("100%", 100);

    auto* movies = new QCheckBox("播放简报 / 过场", inspector);
    auto* satellite = new QCheckBox("透明迷雾", inspector);
    auto* terrain = new QCheckBox("地形扩展", inspector);
    auto* autoRepair = new QCheckBox("自动维修", inspector);
    auto* corr = new QCheckBox("数值修复", inspector);
    auto* reverse = new QCheckBox("军备反转", inspector);
    auto* ares = new QCheckBox("Ares（Syringe）", inspector);
    auto* phobos = new QCheckBox("Phobos", inspector);

    auto* optForm = new QFormLayout;
    optForm->addRow("模组", modCombo);
    optForm->addRow("难度", difficulty);
    optForm->addRow("初始资金", credits);
    optForm->addRow("游戏速度", speed);
    optForm->addRow("自动存档", autosave);
    optForm->addRow("游戏音乐", music);
    optForm->addRow(movies);
    optForm->addRow(satellite);
    optForm->addRow(terrain);
    optForm->addRow(autoRepair);
    optForm->addRow(corr);
    optForm->addRow(reverse);
    optForm->addRow(new QLabel("扩展平台", inspector));
    optForm->addRow(ares);
    optForm->addRow(phobos);

    auto* warningsLabel = new QLabel(inspector);
    warningsLabel->setWordWrap(true);
    warningsLabel->setStyleSheet("color: #e0a030;");
    warningsLabel->hide();

    auto* insLayout = new QVBoxLayout(inspector);
    insLayout->addLayout(headerRow);
    insLayout->addWidget(tags);
    insLayout->addWidget(scenario);
    insLayout->addWidget(insDesc, 1);
    insLayout->addWidget(optTitle);
    insLayout->addLayout(optForm);
    insLayout->addWidget(warningsLabel);
    insLayout->setContentsMargins(16, 12, 16, 12);

    auto* inspectorScroll = new QScrollArea(&win);
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setWidget(inspector);

    auto* launchBtn = new QPushButton("启动游戏", &win);
    launchBtn->setMinimumHeight(38);
    auto* stopBtn = new QPushButton("结束游戏", &win);
    stopBtn->hide();
    auto* statusLabel = new QLabel(&win);
    statusLabel->setStyleSheet("color: palette(mid);");
    auto* logBtn = new QPushButton("日志", &win);
    logBtn->hide();

    auto* launchBar = new QHBoxLayout;
    launchBar->addWidget(launchBtn, 1);
    launchBar->addWidget(stopBtn);
    launchBar->addWidget(statusLabel, 2);
    launchBar->addWidget(logBtn);

    auto* inspectorCol = new QWidget(&win);
    auto* inspectorColLayout = new QVBoxLayout(inspectorCol);
    inspectorColLayout->setContentsMargins(0, 0, 0, 0);
    inspectorColLayout->addWidget(inspectorScroll, 1);
    inspectorColLayout->addLayout(launchBar);

    auto* splitter = new QSplitter(Qt::Horizontal, &win);
    splitter->addWidget(packs);
    splitter->addWidget(table);
    splitter->addWidget(inspectorCol);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 2);
    splitter->setSizes({230, 720, 520});

    auto* engineBadge = new QLabel(&win);
    auto* resolutionLabel = new QLabel(&win);
    auto* countLabel = new QLabel(&win);
    auto* statusBar = new QStatusBar(&win);
    statusBar->addWidget(engineBadge);
    statusBar->addPermanentWidget(resolutionLabel);
    statusBar->addPermanentWidget(countLabel);
    statusBar->addPermanentWidget(statusLabel);

    auto* layout = new QVBoxLayout(&win);
    layout->addLayout(top);
    layout->addWidget(splitter, 1);
    layout->addWidget(statusBar);

    win.restoreGeometry(settings.value("geometry").toByteArray());
    if (settings.contains("gameRoot") && root == ".") {
        root = settings.value("gameRoot").toString().toStdString();
        rootLabel->setText(QString::fromStdString(root));
    }

    int currentPack = -1;  // -2 favorites, -1 all, >=0 pack index
    QString packFilter;
    QProcess* running = nullptr;
    QString logPath;

    auto engineName = [&]() {
        return settings.value("engine", "wine").toString() == "vera20k"
                   ? QString("VERA20K") : QString("Wine-FEX");
    };
    auto setStatus = [&](const QString& text) { statusLabel->setText(text); };

    std::function<void()> fill = [&]() {
        table->setRowCount(0);
        const QString f = search->text().trimmed();
        for (const auto& m : g_campaign.missions) {
            if (!m.Launchable()) continue;
            const bool fav = favorites.contains(Uid(m));
            if (currentPack == -2 && !fav) continue;
            if (!packFilter.isEmpty() && QString::fromStdString(m.packId) != packFilter) continue;
            const QString name = QString::fromStdString(m.name);
            if (!f.isEmpty() &&
                !name.contains(f, Qt::CaseInsensitive) &&
                !QString::fromStdString(m.packName).contains(f, Qt::CaseInsensitive) &&
                !QString::fromStdString(m.side).contains(f, Qt::CaseInsensitive) &&
                !QString::fromStdString(m.scenario).contains(f, Qt::CaseInsensitive))
                continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto* star = new QTableWidgetItem(fav ? "★" : "☆");
            // Store the position in the mission vector, not the per-pack
            // [Battles] index: the two diverge across packs.
            star->setData(Qt::UserRole, static_cast<int>(&m - g_campaign.missions.data()));
            star->setTextAlignment(Qt::AlignCenter);
            table->setItem(row, 0, star);
            table->setItem(row, 1, new QTableWidgetItem(name));
            table->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(m.packName)));
            table->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(m.side)));
            table->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(m.difficulty)));
            table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(m.scenario)));
        }
        countLabel->setText(QString("%1 / %2 个任务")
                                .arg(table->rowCount())
                                .arg(g_campaign.missions.size()));
    };

    auto selected = [&]() -> const rl::Mission* {
        int row = table->currentRow();
        if (row < 0 && table->rowCount() > 0) row = 0;
        if (row < 0) return nullptr;
        auto* item = table->item(row, 0);
        if (!item) return nullptr;
        const int idx = item->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= static_cast<int>(g_campaign.missions.size())) return nullptr;
        return &g_campaign.missions[idx];
    };

    auto optionWidgetsToState = [&]() {
        g_options.modId = modCombo->currentData().toString().toStdString();
        g_options.difficulty = difficulty->currentIndex();
        g_options.credits = credits->currentData().toInt();
        g_options.gameSpeed = speed->currentData().toInt();
        g_options.autosave = autosave->currentIndex();
        g_options.musicVolume = music->currentData().toInt();
        g_options.playMovies = movies->isChecked();
        g_options.satellite = satellite->isChecked();
        g_options.terrain = terrain->isChecked();
        g_options.autoRepair = autoRepair->isChecked();
        g_options.corr = corr->isChecked();
        g_options.reverse = reverse->isChecked();
        g_options.ares = ares->isChecked();
        g_options.phobos = phobos->isChecked();
    };
    auto stateToOptionWidgets = [&]() {
        difficulty->setCurrentIndex(std::clamp(g_options.difficulty, 0, 2));
        credits->setCurrentIndex(std::max(0, credits->findData(g_options.credits)));
        speed->setCurrentIndex(std::clamp(g_options.gameSpeed, 0, 6));
        autosave->setCurrentIndex(std::clamp(g_options.autosave, 0, 3));
        music->setCurrentIndex(std::max(0, music->findData(g_options.musicVolume)));
        movies->setChecked(g_options.playMovies);
        satellite->setChecked(g_options.satellite);
        terrain->setChecked(g_options.terrain);
        autoRepair->setChecked(g_options.autoRepair);
        corr->setChecked(g_options.corr);
        reverse->setChecked(g_options.reverse);
        ares->setChecked(g_options.ares);
        phobos->setChecked(g_options.phobos);
    };

    auto refreshMission = [&]() {
        const rl::Mission* m = selected();
        modCombo->clear();
        if (!m) {
            insName->setText("选择一个任务");
            tags->clear();
            scenario->clear();
            insDesc->clear();
            warningsLabel->hide();
            starBtn->setText("☆");
            return;
        }
        insName->setText(QString::fromStdString(m->name));
        QStringList tagList;
        tagList << QString::fromStdString(m->packName);
        if (!m->side.empty()) tagList << QString::fromStdString(m->side);
        tagList << QString::fromStdString(m->difficulty);
        if (m->ares) tagList << "Ares";
        if (m->phobos) tagList << "Phobos";
        if (m->tx) tagList << "TX";
        tags->setText(tagList.join("  ·  "));
        scenario->setText(QString::fromStdString(m->scenario));
        QString desc = QString::fromStdString(m->description);
        desc.replace('@', '\n');
        insDesc->setPlainText(desc);
        starBtn->setText(favorites.contains(Uid(*m)) ? "★" : "☆");
        settings.setValue("lastMission", Uid(*m));

        modCombo->addItem(QString("默认（%1）").arg(
            m->defaultMod.empty() ? "YR" : QString::fromStdString(m->defaultMod)), "");
        for (const auto& mod : g_mods) {
            if (m->modList.empty()) continue;
            if (QString::fromStdString(m->modList).contains(QString::fromStdString(mod.id)))
                modCombo->addItem(QString::fromStdString(mod.name), QString::fromStdString(mod.id));
        }
        if (!g_options.modId.empty()) {
            const int idx = modCombo->findData(QString::fromStdString(g_options.modId));
            if (idx >= 0) modCombo->setCurrentIndex(idx);
        }

        rl::LaunchPlan plan;
        rl::LaunchOptions probe = g_options;
        if (rl::BuildLaunchPlan(root, *m, probe, plan, &g_campaign.missions) &&
            !plan.warnings.empty()) {
            QStringList w;
            for (const auto& warning : plan.warnings) w << QString::fromStdString(warning);
            warningsLabel->setText("提示：" + w.join("；"));
            warningsLabel->show();
        } else {
            warningsLabel->hide();
        }
    };

    auto rebuildPacks = [&]() {
        packs->clear();
        int favCount = 0;
        for (const auto& m : g_campaign.missions)
            if (m.Launchable() && favorites.contains(Uid(m))) ++favCount;
        packs->addItem(QString("收藏 (%1)").arg(favCount));
        packs->addItem(QString("全部任务包 (%1)").arg(
            std::count_if(g_campaign.missions.begin(), g_campaign.missions.end(),
                          [](const rl::Mission& m) { return m.Launchable(); })));
        // One row per pack (by id), so every pack stays visible and the count
        // matches exactly what the filter shows.
        for (const auto& p : g_campaign.packs) {
            int count = 0;
            for (const auto& m : g_campaign.missions)
                if (m.Launchable() && m.packId == p.id) ++count;
            if (count == 0) continue;
            auto* item = new QListWidgetItem(
                QString("%1 (%2)").arg(QString::fromStdString(p.name)).arg(count));
            item->setData(Qt::UserRole, QString::fromStdString(p.id));
            packs->addItem(item);
        }
        packs->setCurrentRow(1);
    };

    auto toggleFavorite = [&]() {
        const rl::Mission* m = selected();
        if (!m) return;
        const QString uid = Uid(*m);
        if (favorites.contains(uid)) favorites.removeAll(uid);
        else favorites.append(uid);
        settings.setValue("favorites", favorites);
        rebuildPacks();
        fill();
        refreshMission();
    };

    auto launchSelected = [&](bool stopOnly) {};

    QObject::connect(starBtn, &QPushButton::clicked, toggleFavorite);
    QObject::connect(table, &QTableWidget::itemSelectionChanged, refreshMission);
    QObject::connect(table, &QTableWidget::cellClicked, [&](int row, int column) {
        if (column == 0) {
            table->setCurrentCell(row, 0);
            toggleFavorite();
        }
    });
    QObject::connect(search, &QLineEdit::textChanged, [&](const QString&) {
        fill();
        refreshMission();
    });
    QObject::connect(packs, &QListWidget::currentRowChanged, [&](int row) {
        if (row == 0) { currentPack = -2; packFilter.clear(); }
        else if (row == 1) { currentPack = -1; packFilter.clear(); }
        else {
            currentPack = row - 2;
            auto* item = packs->item(row);
            packFilter = item ? item->data(Qt::UserRole).toString() : QString();
        }
        fill();
        refreshMission();
    });

    auto doScan = [&]() {
        g_campaign = rl::ScanCampaigns(root);
        if (qEnvironmentVariableIsSet("REUNION_DUMP")) {
            std::map<std::string, int> reasons;
            for (const auto& m : g_campaign.missions)
                if (!m.Launchable()) reasons[m.badReason.empty() ? "(empty scenario)" : m.badReason]++;
            for (const auto& [reason, count] : reasons)
                qWarning("SKIP %4d  %s", count, reason.c_str());
            qWarning("total=%zu launchable=%zu", g_campaign.missions.size(),
                     static_cast<size_t>(std::count_if(g_campaign.missions.begin(),
                                                       g_campaign.missions.end(),
                                                       [](const rl::Mission& m) { return m.Launchable(); })));
        }
        g_mods = rl::LoadModRegistry(root);
        rl::LoadClientDefaults(root, g_options);
        stateToOptionWidgets();
        rebuildPacks();
        fill();
        refreshMission();
        engineBadge->setText(engineName());
        const QString lastUid = settings.value("lastMission").toString();
        if (!lastUid.isEmpty()) {
            for (int row = 0; row < table->rowCount(); ++row) {
                const int idx = table->item(row, 0)->data(Qt::UserRole).toInt();
                if (idx < 0 || idx >= static_cast<int>(g_campaign.missions.size())) continue;
                if (Uid(g_campaign.missions[idx]) != lastUid) continue;
                table->setCurrentCell(row, 0);
                table->scrollToItem(table->item(row, 1));
                break;
            }
        }
        resolutionLabel->setText(QString("%1 × %2")
                                     .arg(settings.value("videoWidth", 1280).toInt())
                                     .arg(settings.value("videoHeight", 720).toInt()));
        setStatus(QString("任务包 %1 / 任务 %2 / 问题 %3")
                      .arg(g_campaign.packs.size())
                      .arg(g_campaign.missions.size())
                      .arg(g_campaign.issues.size()));
    };
    QObject::connect(rescan, &QPushButton::clicked, doScan);
    QObject::connect(chooseRoot, &QPushButton::clicked, [&]() {
        const QString dir = QFileDialog::getExistingDirectory(&win, "选择游戏目录", rootLabel->text());
        if (dir.isEmpty()) return;
        root = dir.toStdString();
        rootLabel->setText(dir);
        settings.setValue("gameRoot", dir);
        const QIcon ic = GameIcon(root);
        if (!ic.isNull()) { QApplication::setWindowIcon(ic); win.setWindowIcon(ic); }
        doScan();
    });

    QObject::connect(settingsBtn, &QPushButton::clicked, [&]() {
        QDialog dlg(&win);
        dlg.setWindowTitle("设置");
        dlg.resize(560, 460);
        auto* form = new QFormLayout(&dlg);
        auto heading = [&](const char* text) {
            auto* label = new QLabel(QString::fromUtf8(text), &dlg);
            QFont f = label->font();
            f.setBold(true);
            label->setFont(f);
            form->addRow(label);
        };

        heading("路径");
        auto* gameRootEdit = new QLineEdit(QString::fromStdString(root), &dlg);
        auto* pickRoot = new QPushButton("选择…", &dlg);
        auto* rootRow = new QHBoxLayout;
        rootRow->addWidget(gameRootEdit, 1);
        rootRow->addWidget(pickRoot);
        form->addRow("游戏目录", rootRow);

        heading("引擎");
        auto* engine = new QComboBox(&dlg);
        engine->addItem("Wine-FEX（原版 gamemd）", "wine");
        engine->addItem("VERA20K（实验）", "vera20k");
        engine->setCurrentIndex(engine->findData(settings.value("engine", "wine").toString()));
        form->addRow("游戏引擎", engine);
        auto* veraPath = new QLineEdit(settings.value("vera20kBin").toString(), &dlg);
        veraPath->setPlaceholderText(
            "留空自动查找 ~/vera20k 或 ~/.local/bin/vera20k（请用 yeungtuzi/vera20k 的补丁版）");
        auto* pickVera = new QPushButton("选择…", &dlg);
        auto* veraRow = new QHBoxLayout;
        veraRow->addWidget(veraPath, 1);
        veraRow->addWidget(pickVera);
        form->addRow("VERA20K 可执行文件", veraRow);
        auto* hint = new QLabel(
            "VERA20K 是实验引擎：使用 Run/ 作为 ra2_dir，自动注入 Mod&AI 的 AI 根，"
            "不需要 Wine/Proton。Reunion 的 AIMD 需要补丁版引擎，请用 "
            "scripts/linux/build-vera20k.sh 构建 github.com/yeungtuzi/vera20k。", &dlg);
        hint->setWordWrap(true);
        form->addRow(hint);

        heading("画面");
        auto* resolution = new QComboBox(&dlg);
        resolution->addItem("1024 × 768", QSize(1024, 768));
        resolution->addItem("1280 × 720", QSize(1280, 720));
        resolution->addItem("1920 × 1080", QSize(1920, 1080));
        resolution->addItem("2560 × 1440", QSize(2560, 1440));
        {
            QSize cur(settings.value("videoWidth", 1280).toInt(),
                      settings.value("videoHeight", 720).toInt());
            const int idx = resolution->findData(cur);
            resolution->setCurrentIndex(idx >= 0 ? idx : 1);
        }
        form->addRow("窗口分辨率", resolution);
        auto* renderer = new QComboBox(&dlg);
        renderer->addItem("GDI（默认）", "gdi");
        renderer->addItem("OpenGL", "opengl");
        renderer->addItem("Direct3D9", "direct3d9");
        renderer->setCurrentIndex(
            std::max(0, renderer->findData(settings.value("videoRenderer", "gdi").toString())));
        form->addRow("渲染器（cnc-ddraw）", renderer);
        auto* veraBackend = new QComboBox(&dlg);
        veraBackend->addItem("自动（有 MTGPU Vulkan 就用）", "auto");
        veraBackend->addItem("MTGPU Vulkan（X11 窗口）", "mtgpu");
        veraBackend->addItem("软件渲染（llvmpipe / Wayland）", "software");
        veraBackend->setCurrentIndex(std::max(
            0, veraBackend->findData(settings.value("veraBackend", "auto").toString())));
        form->addRow("VERA20K 图形后端", veraBackend);
        auto* veraBackendHint = new QLabel(
            "MTGPU 的 Vulkan 驱动（/etc/vulkan/icd.d/mtvk.json）只支持 X11 surface，"
            "走 Wayland 会自动退回 llvmpipe。", &dlg);
        veraBackendHint->setWordWrap(true);
        form->addRow(veraBackendHint);

        heading("高级");
        auto* hideOnLaunch = new QCheckBox("启动游戏后隐藏启动器（省电）", &dlg);
        hideOnLaunch->setChecked(settings.value("hideOnLaunch", false).toBool());
        form->addRow(hideOnLaunch);
        auto* speedControl = new QCheckBox("速度控制（gamemd -speedcontrol）", &dlg);
        speedControl->setChecked(settings.value("speedControl", true).toBool());
        form->addRow(speedControl);
        auto* extraArgs = new QLineEdit(settings.value("extraArgs").toString(), &dlg);
        extraArgs->setPlaceholderText("附加给 gamemd 的参数，例如 -LOG -CD");
        form->addRow("附加启动参数", extraArgs);
        auto* fpsCounter = new QCheckBox("性能指示（VERA20K 启动即显示 FPS 读数）", &dlg);
        fpsCounter->setChecked(settings.value("fpsCounter", false).toBool());
        form->addRow(fpsCounter);
        auto* audioBuffer = new QSpinBox(&dlg);
        audioBuffer->setRange(0, 4096);
        audioBuffer->setSingleStep(256);
        audioBuffer->setSpecialValueText("自动");
        audioBuffer->setValue(settings.value("audioBuffer", 0).toInt());
        audioBuffer->setToolTip("VERA20K 音频周期（帧）；0 = 引擎默认。加载任务时爆音可调到 2048。");
        form->addRow("VERA20K 音频缓冲", audioBuffer);
        auto* fpsHint = new QLabel("游戏内也可以按 Ctrl+Alt+F 切换。", &dlg);
        fpsHint->setWordWrap(true);
        form->addRow(fpsHint);

        heading("客户端默认值");
        auto* defaultsHint = new QLabel("从 Client/CampaignSetting.ini 读取难度、资金、速度等默认值。", &dlg);
        defaultsHint->setWordWrap(true);
        form->addRow(defaultsHint);
        auto* resetDefaults = new QPushButton("恢复默认选项", &dlg);
        form->addRow(resetDefaults);
        QObject::connect(resetDefaults, &QPushButton::clicked, [&]() {
            rl::LoadClientDefaults(root, g_options);
            stateToOptionWidgets();
            refreshMission();
        });

        heading("诊断");
        auto* rescanNow = new QPushButton("重新扫描", &dlg);
        form->addRow(rescanNow);
        QObject::connect(rescanNow, &QPushButton::clicked, [&]() { doScan(); });

        auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(box);
        QObject::connect(pickRoot, &QPushButton::clicked, [&]() {
            const QString dir = QFileDialog::getExistingDirectory(&dlg, "选择游戏目录", gameRootEdit->text());
            if (!dir.isEmpty()) gameRootEdit->setText(dir);
        });
        QObject::connect(pickVera, &QPushButton::clicked, [&]() {
            const QString f = QFileDialog::getOpenFileName(&dlg, "选择 VERA20K 可执行文件");
            if (!f.isEmpty()) veraPath->setText(f);
        });
        QObject::connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() == QDialog::Accepted) {
            settings.setValue("engine", engine->currentData().toString());
            settings.setValue("vera20kBin", veraPath->text().trimmed());
            settings.setValue("hideOnLaunch", hideOnLaunch->isChecked());
            settings.setValue("speedControl", speedControl->isChecked());
            settings.setValue("extraArgs", extraArgs->text().trimmed());
            settings.setValue("fpsCounter", fpsCounter->isChecked());
            settings.setValue("audioBuffer", audioBuffer->value());
            const QSize res = resolution->currentData().toSize();
            settings.setValue("videoWidth", res.width());
            settings.setValue("videoHeight", res.height());
            settings.setValue("videoRenderer", renderer->currentData().toString());
            settings.setValue("veraBackend", veraBackend->currentData().toString());
            resolutionLabel->setText(QString("%1 × %2")
                                         .arg(res.width())
                                         .arg(res.height()));
            engineBadge->setText(engineName());
        const QString lastUid = settings.value("lastMission").toString();
        if (!lastUid.isEmpty()) {
            for (int row = 0; row < table->rowCount(); ++row) {
                const int idx = table->item(row, 0)->data(Qt::UserRole).toInt();
                if (idx < 0 || idx >= static_cast<int>(g_campaign.missions.size())) continue;
                if (Uid(g_campaign.missions[idx]) != lastUid) continue;
                table->setCurrentCell(row, 0);
                table->scrollToItem(table->item(row, 1));
                break;
            }
        }
        resolutionLabel->setText(QString("%1 × %2")
                                     .arg(settings.value("videoWidth", 1280).toInt())
                                     .arg(settings.value("videoHeight", 720).toInt()));
            const QString newRoot = gameRootEdit->text().trimmed();
            if (!newRoot.isEmpty() && newRoot != QString::fromStdString(root)) {
                root = newRoot.toStdString();
                rootLabel->setText(newRoot);
                settings.setValue("gameRoot", newRoot);
                const QIcon ic = GameIcon(root);
                if (!ic.isNull()) { QApplication::setWindowIcon(ic); win.setWindowIcon(ic); }
                doScan();
            }
        }
    });

    auto startStop = [&](bool stop) {
        if (stop) {
            if (running) running->terminate();
            return;
        }
        const rl::Mission* mission = selected();
        qWarning("launch requested: %s", mission ? mission->name.c_str() : "(none)");
        if (!mission) { setStatus("先选择一个任务"); return; }
        optionWidgetsToState();
        qWarning("building plan for %s", mission->name.c_str());
        rl::LaunchPlan plan;
        if (!rl::BuildLaunchPlan(root, *mission, g_options, plan, &g_campaign.missions)) {
            qWarning("plan failed: %s", plan.error.c_str());
            setStatus(QString::fromStdString("构建启动计划失败：" + plan.error));
            return;
        }
        qWarning("plan ok, preparing run: %s", plan.runDir.c_str());
        std::string err;
        std::vector<std::string> warnings;
        rlwin::VideoConfig video;
        video.width = settings.value("videoWidth", 1280).toInt();
        video.height = settings.value("videoHeight", 720).toInt();
        video.renderer = settings.value("videoRenderer", "gdi").toString().toStdString();
        if (!rlwin::PrepareRun(plan, g_options, err, warnings, video)) {
            qWarning("prepare failed: %s", err.c_str());
            setStatus(QString::fromStdString("准备 Run 失败：" + err));
            return;
        }
        qWarning("run prepared: %s", plan.runDir.c_str());
        QString runDir = QString::fromStdString(plan.runDir);
        runDir.replace('\\', '/');

        qWarning("engine setting = %s", qPrintable(settings.value("engine", "wine").toString()));
        if (settings.value("engine", "wine").toString() == "vera20k" ||
            qEnvironmentVariable("REUNION_ENGINE") == "vera20k") {
            std::string veraError;
            if (!rlwin::PrepareVera20k(plan, g_options, veraError)) {
                qWarning("vera prepare failed: %s", veraError.c_str());
                setStatus(QString::fromStdString("VERA20K 准备失败：" + veraError));
                return;
            }
            const QString bin = FindVera20k();
            qWarning("vera binary: %s", qPrintable(bin.isEmpty() ? "(not found)" : bin));
            if (bin.isEmpty()) {
                QMessageBox::warning(&win, "找不到 VERA20K",
                                     "请设置 VERA20K_BIN，或在“设置”里指定可执行文件。");
                return;
            }
            auto* proc = new QProcess(&win);
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("RA2_DIR", runDir);
            env.insert("RA2_SPAWN_INI", runDir + "/spawn-vera.ini");
            env.insert("VERA_TOLERANT_AI", "1");
            env.insert("VERA_FPS_CAP", "60");
            // MTGPU: the vendor Vulkan ICD lives in /etc/vulkan/icd.d and only
            // advertises X11 surfaces, so run the engine through XWayland when
            // we use it; otherwise stay on Wayland with llvmpipe.
            const QString backend = settings.value("veraBackend", "auto").toString();
            const bool mtvk = QFileInfo::exists("/etc/vulkan/icd.d/mtvk.json");
            const bool useMt = backend == "mtgpu" || (backend == "auto" && mtvk);
            if (useMt) {
                env.insert("WGPU_BACKEND", "vulkan");
                env.insert("VK_ICD_FILENAMES", "/etc/vulkan/icd.d/mtvk.json");
                env.remove("WAYLAND_DISPLAY");
                if (env.value("DISPLAY").isEmpty()) env.insert("DISPLAY", ":0");
                if (env.value("XAUTHORITY").isEmpty()) {
                    QDir xauthDir("/run/user/" + QString::number(::getuid()));
                    const QStringList files = xauthDir.entryList({"xauth_*"}, QDir::Files);
                    if (!files.isEmpty())
                        env.insert("XAUTHORITY", xauthDir.filePath(files.first()));
                }
            } else if (backend == "software") {
                env.remove("VK_ICD_FILENAMES");
            }
            env.insert("VERA_FPS_CAP_IDLE", "15");
            env.insert("LANG", "zh_CN.GBK");
            if (settings.value("fpsCounter", false).toBool())
                env.insert("VERA_FPS_COUNTER", "1");
            if (settings.value("audioBuffer", 0).toInt() > 0)
                env.insert("VERA_AUDIO_BUFFER", QString::number(settings.value("audioBuffer").toInt()));
            proc->setProcessEnvironment(env);
            proc->setWorkingDirectory(runDir);
            proc->setProcessChannelMode(QProcess::MergedChannels);
            proc->setStandardOutputFile(LogFilePath());
            proc->setProgram(bin);
            proc->setArguments({"-SPAWN"});
            QObject::connect(proc, &QProcess::errorOccurred, [&, proc](QProcess::ProcessError) {
                setStatus("启动 VERA20K 失败：" + proc->errorString());
            });
            QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                             [&, proc](int code, QProcess::ExitStatus) {
                setStatus(QString("VERA20K 退出（%1）").arg(code));
                win.show();
                running = nullptr;
                launchBtn->setEnabled(true);
                stopBtn->hide();
                proc->deleteLater();
            });
            proc->start();
            if (settings.value("hideOnLaunch", false).toBool()) win.hide();
            if (!proc->waitForStarted(3000))
                qWarning("vera start failed: %s", qPrintable(proc->errorString()));
            else
                qWarning("vera started pid=%lld", static_cast<long long>(proc->processId()));
            running = proc;
            launchBtn->setEnabled(false);
            stopBtn->show();
            logBtn->show();
            engineBadge->setText("VERA20K");
            setStatus(QString("VERA20K 运行中：%1").arg(QString::fromStdString(mission->name)));
            return;
        }

        const QString launcher = qEnvironmentVariable(
            "REUNION_LAUNCHER", "/home/lf/.local/share/m1000/work/ra2-launch.sh");
        const QString exe = plan.useAres ? "Syringe.exe" : "gamemd-spawn.exe";
        QStringList args;
        if (plan.useAres) args << "gamemd.exe" << "-SPAWN";
        else args << "-SPAWN";
        if (!plan.extraArgs.empty()) args << QString::fromStdString(plan.extraArgs);
        if (settings.value("speedControl", true).toBool() &&
            !QString::fromStdString(plan.extraArgs).contains("-speedcontrol"))
            args << "-speedcontrol";
        const QString extra = settings.value("extraArgs").toString().trimmed();
        if (!extra.isEmpty()) args << extra.split(' ', Qt::SkipEmptyParts);

        auto* proc = new QProcess(&win);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("REUNION_EXE", exe);
        if (qEnvironmentVariableIsEmpty("REUNION_DISPLAY"))
            env.insert("REUNION_DISPLAY", ":3");
        proc->setProcessEnvironment(env);
        proc->setWorkingDirectory(runDir);
        proc->setProcessChannelMode(QProcess::MergedChannels);
        proc->setStandardOutputFile(LogFilePath());
        proc->setProgram("bash");
        proc->setArguments(QStringList{launcher} + args);
        QObject::connect(proc, &QProcess::errorOccurred, [&, proc](QProcess::ProcessError) {
            setStatus("启动失败：" + proc->errorString());
        });
        QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [&, proc](int code, QProcess::ExitStatus) {
            rlwin::SyncSettingsBack(root);
            setStatus(QString("游戏退出（%1）").arg(code));
            win.show();
            running = nullptr;
            launchBtn->setEnabled(true);
            stopBtn->hide();
            proc->deleteLater();
        });
        proc->start();
        if (settings.value("hideOnLaunch", false).toBool()) win.hide();
        running = proc;
        launchBtn->setEnabled(false);
        stopBtn->show();
        logBtn->show();
        engineBadge->setText("Wine-FEX");
        setStatus(QString::fromStdString("已启动：" + mission->name));
    };

    QObject::connect(logBtn, &QPushButton::clicked, [&]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(LogFilePath()));
    });
    QObject::connect(launchBtn, &QPushButton::clicked, [&]() { startStop(false); });
    QObject::connect(stopBtn, &QPushButton::clicked, [&]() { startStop(true); });

    doScan();
    win.show();
    if (launchRow >= 0) {
        table->setCurrentCell(0, 0);
        QTimer::singleShot(500, [&]() { startStop(false); });
    }
    const int rc = app.exec();
    QSettings("reunion-lite", "reunion-lite").setValue("geometry", win.saveGeometry());
    return rc;
}
