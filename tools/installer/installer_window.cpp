/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QApplication>
#include <QStyle>
#include <QPainter>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace {

bool checkIsAdmin() {
#ifdef _WIN32
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
#else
    return false;
#endif
}

QPixmap loadInstallerLogo() {
    QPixmap pix(":/icons/viora_eda_logo.png");
    if (!pix.isNull()) return pix;

    QPixmap fallback(48, 48);
    fallback.fill(Qt::transparent);
    QPainter p(&fallback);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0, 210, 255));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(4, 4, 40, 40, 8, 8);
    p.setPen(QColor(11, 15, 23));
    QFont font("Segoe UI", 20, QFont::Bold);
    p.setFont(font);
    p.drawText(fallback.rect(), Qt::AlignCenter, "V");
    return fallback;
}

} // namespace

InstallerWindow::InstallerWindow(bool isUninstall, QWidget *parent)
    : QWidget(parent), m_isUninstall(isUninstall) {
    m_isAdmin = checkIsAdmin();
    m_config.isUninstall = isUninstall;

    // Default target path
    if (m_isAdmin) {
        m_config.installDir = "C:/Program Files/VioraEDA";
    } else {
        QString localApp = qEnvironmentVariable("LOCALAPPDATA");
        if (localApp.isEmpty()) {
            localApp = QDir::homePath() + "/AppData/Local";
        }
        m_config.installDir = QDir::cleanPath(localApp + "/Programs/VioraEDA");
    }

    setupUi();
}

InstallerWindow::~InstallerWindow() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->cancel();
        m_worker->wait();
    }
}

void InstallerWindow::setupUi() {
    setWindowTitle(m_isUninstall ? "VioraEDA 2026 Uninstaller" : "VioraEDA 2026 Setup");
    setFixedSize(760, 520);
    setWindowIcon(QIcon(loadInstallerLogo()));

    // Master Dark Palette and Styling
    setStyleSheet(
        "QWidget { background-color: #0b0f17; color: #e2e8f0; font-family: 'Segoe UI', sans-serif; font-size: 13px; }"
        "QLabel { color: #cbd5e1; }"
        "QLineEdit { background-color: #111827; border: 1px solid #374151; border-radius: 6px; padding: 7px 10px; color: #f8fafc; selection-background-color: #00d2ff; selection-color: #0b0f17; }"
        "QLineEdit:focus { border: 1px solid #00d2ff; }"
        "QTextEdit { background-color: #0f172a; border: 1px solid #1e293b; border-radius: 6px; color: #cbd5e1; font-family: 'Consolas', monospace; font-size: 11px; padding: 8px; }"
        "QPushButton { background-color: #1e293b; border: 1px solid #334155; border-radius: 6px; padding: 7px 18px; color: #f1f5f9; font-weight: 600; min-width: 80px; }"
        "QPushButton:hover { background-color: #334155; border-color: #475569; }"
        "QPushButton:pressed { background-color: #0f172a; }"
        "QPushButton#PrimaryBtn { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0099ff, stop:1 #00d2ff); color: #070d18; border: none; font-weight: 700; }"
        "QPushButton#PrimaryBtn:hover { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2bb0ff, stop:1 #38dcff); }"
        "QPushButton#PrimaryBtn:disabled { background: #1e293b; color: #64748b; border: 1px solid #334155; }"
        "QProgressBar { background-color: #111827; border: 1px solid #1e293b; border-radius: 6px; text-align: center; color: #f8fafc; font-weight: bold; height: 20px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #0077ff, stop:1 #00d2ff); border-radius: 5px; }"
        "QCheckBox { spacing: 8px; color: #e2e8f0; font-size: 13px; }"
        "QCheckBox::indicator { width: 18px; height: 18px; border-radius: 4px; border: 1px solid #475569; background-color: #111827; }"
        "QCheckBox::indicator:checked { background-color: #00d2ff; border-color: #00d2ff; image: none; }"
        "QComboBox { background-color: #111827; border: 1px solid #374151; border-radius: 6px; padding: 6px 12px; color: #f8fafc; }"
        "QComboBox QAbstractItemView { background-color: #111827; color: #f8fafc; selection-background-color: #00d2ff; selection-color: #0b0f17; border: 1px solid #374151; }"
    );

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left Sidebar
    mainLayout->addWidget(createSidebar());

    // Right Content Container
    auto *rightContainer = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(28, 24, 28, 20);
    rightLayout->setSpacing(16);

    m_stackedWidget = new QStackedWidget(this);
    if (m_isUninstall) {
        m_stackedWidget->addWidget(createWelcomePage());
        m_stackedWidget->addWidget(createProgressPage());
        m_stackedWidget->addWidget(createFinishPage());
    } else {
        m_stackedWidget->addWidget(createWelcomePage());
        m_stackedWidget->addWidget(createLicensePage());
        m_stackedWidget->addWidget(createComponentsPage());
        m_stackedWidget->addWidget(createDirectoryPage());
        m_stackedWidget->addWidget(createProgressPage());
        m_stackedWidget->addWidget(createFinishPage());
    }
    rightLayout->addWidget(m_stackedWidget, 1);

    // Bottom Navigation Bar
    auto *navLayout = new QHBoxLayout();
    navLayout->setContentsMargins(0, 8, 0, 0);
    navLayout->setSpacing(10);

    m_cancelBtn = new QPushButton("Cancel", this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &InstallerWindow::cancelInstallation);

    m_backBtn = new QPushButton("Back", this);
    m_backBtn->setEnabled(false);
    connect(m_backBtn, &QPushButton::clicked, this, &InstallerWindow::prevPage);

    m_nextBtn = new QPushButton(m_isUninstall ? "Uninstall" : "Next", this);
    m_nextBtn->setObjectName("PrimaryBtn");
    connect(m_nextBtn, &QPushButton::clicked, this, &InstallerWindow::nextPage);

    navLayout->addWidget(m_cancelBtn);
    navLayout->addStretch(1);
    navLayout->addWidget(m_backBtn);
    navLayout->addWidget(m_nextBtn);

    rightLayout->addLayout(navLayout);
    mainLayout->addWidget(rightContainer, 1);

    updateSidebarStep(0);
    updateDiskSpaceInfo();
}

QWidget* InstallerWindow::createSidebar() {
    auto *sidebar = new QWidget(this);
    sidebar->setFixedWidth(210);
    sidebar->setStyleSheet("background-color: #070a0f; border-right: 1px solid #1e293b;");

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(20, 24, 20, 20);
    layout->setSpacing(14);

    // Suite Logo & Header
    auto *logoLabel = new QLabel(this);
    logoLabel->setPixmap(loadInstallerLogo().scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(logoLabel);

    auto *appTitle = new QLabel("VioraEDA 2026", this);
    appTitle->setStyleSheet("font-size: 16px; font-weight: 700; color: #f8fafc; margin-top: 4px;");
    layout->addWidget(appTitle);

    auto *appSubtitle = new QLabel(m_isUninstall ? "Uninstallation Wizard" : "Official Setup Wizard", this);
    appSubtitle->setStyleSheet("font-size: 11px; color: #00d2ff; font-weight: 600; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 12px;");
    layout->addWidget(appSubtitle);

    layout->addSpacing(10);

    // Wizard Step Badges
    QStringList steps;
    if (m_isUninstall) {
        steps = { "1. Overview", "2. Removing", "3. Complete" };
    } else {
        steps = { "1. Welcome", "2. License", "3. Components", "4. Configuration", "5. Installing", "6. Finish" };
    }

    for (int i = 0; i < steps.size(); ++i) {
        auto *lbl = new QLabel(steps[i], this);
        lbl->setStyleSheet("color: #64748b; font-size: 12px; font-weight: 600; padding: 4px 0px;");
        m_stepLabels.append(lbl);
        layout->addWidget(lbl);
    }

    layout->addStretch(1);

    auto *versionLabel = new QLabel("Version 2026.1\n64-bit Edition", this);
    versionLabel->setStyleSheet("font-size: 11px; color: #475569;");
    layout->addWidget(versionLabel);

    return sidebar;
}

void InstallerWindow::updateSidebarStep(int pageIndex) {
    for (int i = 0; i < m_stepLabels.size(); ++i) {
        if (i == pageIndex) {
            m_stepLabels[i]->setStyleSheet("color: #00d2ff; font-size: 12px; font-weight: 700; padding: 4px 0px;");
        } else if (i < pageIndex) {
            m_stepLabels[i]->setStyleSheet("color: #94a3b8; font-size: 12px; font-weight: 600; padding: 4px 0px;");
        } else {
            m_stepLabels[i]->setStyleSheet("color: #475569; font-size: 12px; font-weight: 600; padding: 4px 0px;");
        }
    }
}

QWidget* InstallerWindow::createWelcomePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *title = new QLabel(m_isUninstall ? "Uninstall VioraEDA 2026" : "Welcome to VioraEDA 2026", this);
    title->setStyleSheet("font-size: 22px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(title);

    auto *desc = new QLabel(
        m_isUninstall ?
        "This wizard will guide you through the clean uninstallation of VioraEDA Suite from your system.\n\n"
        "All application binaries, simulation engines, registered shell shortcuts, and registry file associations will be safely removed." :
        "Welcome to the next-generation Electronic Design Automation & Mixed-Signal Simulation suite.\n\n"
        "VioraEDA delivers GPU-accelerated schematic capture, multi-engine SPICE simulation, integrated MCU co-simulation, and high-density PCB layout in a unified workspace.",
        this
    );
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #94a3b8; font-size: 13px; line-height: 1.5;");
    layout->addWidget(desc);

    layout->addSpacing(10);

    if (!m_isUninstall) {
        auto *featuresBox = new QWidget(this);
        featuresBox->setStyleSheet("background-color: #0f172a; border: 1px solid #1e293b; border-radius: 8px; padding: 12px;");
        auto *fLayout = new QVBoxLayout(featuresBox);
        fLayout->setSpacing(8);

        auto addFeature = [&](const QString& title, const QString& sub) {
            auto *item = new QLabel(QString("<span style='color:#00d2ff; font-weight:700;'>[+]</span> <b>%1</b> &mdash; <span style='color:#94a3b8;'>%2</span>").arg(title, sub), featuresBox);
            item->setTextFormat(Qt::RichText);
            fLayout->addWidget(item);
        };

        addFeature("High-Performance Mixed-Signal SPICE", "ngspice 44, VioMATRIXC & AVR co-sim");
        addFeature("Schematic Capture & PCB Layout", "Multi-sheet hierarchical design with real-time DRC");
        addFeature("VioSpiceLib Component Library", "49,550+ production-grade symbols and footprints");
        addFeature("FluxScript JIT Compiler", "Sub-microsecond dynamic analog behavioral modeling");

        layout->addWidget(featuresBox);
    }

    layout->addStretch(1);
    return page;
}

QWidget* InstallerWindow::createLicensePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel("License Agreement", this);
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(title);

    auto *sub = new QLabel("Please review the license terms before continuing.", this);
    sub->setStyleSheet("color: #94a3b8; font-size: 12px;");
    layout->addWidget(sub);

    auto *licenseText = new QTextEdit(this);
    licenseText->setReadOnly(true);
    licenseText->setPlainText(
        "Apache License\n"
        "Version 2.0, January 2004\n"
        "http://www.apache.org/licenses/\n\n"
        "Copyright 2026 Janada Sroor\n\n"
        "Licensed under the Apache License, Version 2.0 (the \"License\");\n"
        "you may not use this file except in compliance with the License.\n"
        "You may obtain a copy of the License at\n\n"
        "    http://www.apache.org/licenses/LICENSE-2.0\n\n"
        "Unless required by applicable law or agreed to in writing, software\n"
        "distributed under the License is distributed on an \"AS IS\" BASIS,\n"
        "WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\n"
        "See the License for the specific language governing permissions and\n"
        "limitations under the License."
    );
    layout->addWidget(licenseText, 1);

    m_licenseCheckBox = new QCheckBox("I accept the terms in the License Agreement", this);
    connect(m_licenseCheckBox, &QCheckBox::toggled, this, &InstallerWindow::onLicenseCheckChanged);
    layout->addWidget(m_licenseCheckBox);

    return page;
}

QWidget* InstallerWindow::createComponentsPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel("Select Components", this);
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(title);

    auto *sub = new QLabel("Choose which features and component packages you want to install.", this);
    sub->setStyleSheet("color: #94a3b8; font-size: 12px;");
    layout->addWidget(sub);

    auto *comboLayout = new QHBoxLayout();
    auto *typeLabel = new QLabel("Installation Type:", this);
    typeLabel->setStyleSheet("font-weight: 600; color: #f8fafc;");
    m_installTypeCombo = new QComboBox(this);
    m_installTypeCombo->addItem("Full Suite (Recommended - All Features & Libraries)");
    m_installTypeCombo->addItem("Minimal (Core Schematic Capture & SPICE Only)");
    m_installTypeCombo->addItem("Custom (Select Individual Packages)");
    connect(m_installTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &InstallerWindow::onInstallTypeChanged);
    comboLayout->addWidget(typeLabel);
    comboLayout->addWidget(m_installTypeCombo, 1);
    layout->addLayout(comboLayout);

    auto *treeBox = new QWidget(this);
    treeBox->setStyleSheet("background-color: #0f172a; border: 1px solid #1e293b; border-radius: 8px; padding: 12px;");
    auto *tLayout = new QVBoxLayout(treeBox);
    tLayout->setSpacing(10);

    m_chkCoreSuite = new QCheckBox("VioraEDA Core Suite (Schematic, PCB Layout, Analog Scope)", treeBox);
    m_chkCoreSuite->setChecked(true);
    m_chkCoreSuite->setEnabled(false); // Core is required

    m_chkSimulators = new QCheckBox("Simulation Engines (ngspice 44, VioMATRIXC, MCU Co-Sim)", treeBox);
    m_chkSimulators->setChecked(true);

    m_chkLibrary = new QCheckBox("VioSpiceLib Component Library (49,550+ Symbols & Footprints)", treeBox);
    m_chkLibrary->setChecked(true);

    m_chkCliTools = new QCheckBox("Viora CLI & FluxScript Tools (viora.exe, flux_runner.exe)", treeBox);
    m_chkCliTools->setChecked(true);

    m_chkExamples = new QCheckBox("Sample Circuits, Reference Projects & Templates", treeBox);
    m_chkExamples->setChecked(true);

    connect(m_chkSimulators, &QCheckBox::toggled, this, &InstallerWindow::onComponentToggled);
    connect(m_chkLibrary, &QCheckBox::toggled, this, &InstallerWindow::onComponentToggled);
    connect(m_chkCliTools, &QCheckBox::toggled, this, &InstallerWindow::onComponentToggled);
    connect(m_chkExamples, &QCheckBox::toggled, this, &InstallerWindow::onComponentToggled);

    tLayout->addWidget(m_chkCoreSuite);
    tLayout->addWidget(m_chkSimulators);
    tLayout->addWidget(m_chkLibrary);
    tLayout->addWidget(m_chkCliTools);
    tLayout->addWidget(m_chkExamples);

    layout->addWidget(treeBox);

    m_componentDescLabel = new QLabel("Full installation provides complete design, simulation, and hardware verification capabilities.", this);
    m_componentDescLabel->setStyleSheet("color: #64748b; font-size: 11px; font-style: italic;");
    layout->addWidget(m_componentDescLabel);

    layout->addStretch(1);
    return page;
}

QWidget* InstallerWindow::createDirectoryPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel("Installation Location & System Options", this);
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(title);

    auto *sub = new QLabel("Choose the target folder and configure Windows shell integrations.", this);
    sub->setStyleSheet("color: #94a3b8; font-size: 12px;");
    layout->addWidget(sub);

    // Destination Directory
    auto *dirBox = new QHBoxLayout();
    m_dirLineEdit = new QLineEdit(m_config.installDir, this);
    connect(m_dirLineEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_config.installDir = text;
        updateDiskSpaceInfo();
    });

    auto *browseBtn = new QPushButton("Browse...", this);
    connect(browseBtn, &QPushButton::clicked, this, &InstallerWindow::browseDirectory);

    dirBox->addWidget(m_dirLineEdit, 1);
    dirBox->addWidget(browseBtn);
    layout->addLayout(dirBox);

    // Privilege & Disk Space Info
    auto *infoBox = new QWidget(this);
    infoBox->setStyleSheet("background-color: #0f172a; border: 1px solid #1e293b; border-radius: 6px; padding: 10px;");
    auto *iLayout = new QGridLayout(infoBox);
    iLayout->setContentsMargins(8, 8, 8, 8);
    iLayout->setHorizontalSpacing(16);
    iLayout->setVerticalSpacing(6);

    m_privilegeLabel = new QLabel(this);
    m_privilegeLabel->setStyleSheet("font-weight: 600; color: #f8fafc;");
    if (m_isAdmin) {
        m_privilegeLabel->setText("Administrator Mode (Installing system-wide for all users)");
    } else {
        m_privilegeLabel->setText("Standard User Mode (Installing locally into user profile without UAC elevation)");
    }

    m_spaceRequiredLabel = new QLabel("Space required: Calculating...", this);
    m_spaceRequiredLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    m_spaceAvailableLabel = new QLabel("Space available: Calculating...", this);
    m_spaceAvailableLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    iLayout->addWidget(m_privilegeLabel, 0, 0, 1, 2);
    iLayout->addWidget(m_spaceRequiredLabel, 1, 0);
    iLayout->addWidget(m_spaceAvailableLabel, 1, 1);
    layout->addWidget(infoBox);

    layout->addSpacing(4);

    // System Integration Checkboxes
    auto *optLabel = new QLabel("Windows Integrations:", this);
    optLabel->setStyleSheet("font-weight: 600; color: #f8fafc;");
    layout->addWidget(optLabel);

    m_desktopShortcutCheckBox = new QCheckBox("Create a Desktop Shortcut", this);
    m_desktopShortcutCheckBox->setChecked(true);

    m_startMenuShortcutCheckBox = new QCheckBox("Create Start Menu Shortcuts (VioraEDA, Viora CLI, VioAVR, Uninstaller)", this);
    m_startMenuShortcutCheckBox->setChecked(true);

    m_addToPathCheckBox = new QCheckBox("Add VioraEDA and CLI binaries (viora, vioavr, flux_runner) to PATH", this);
    m_addToPathCheckBox->setChecked(true);

    m_setupGlobalEnvVarsCheckBox = new QCheckBox("Set global environment variables (VIOSPICE_HOME, VIOAVR_HOME, FLUX_HOME)", this);
    m_setupGlobalEnvVarsCheckBox->setChecked(true);

    m_associateFilesCheckBox = new QCheckBox("Associate VioraEDA with .flxsch, .flux, .flxpcb, .cir, .asc files", this);
    m_associateFilesCheckBox->setChecked(true);

    layout->addWidget(m_desktopShortcutCheckBox);
    layout->addWidget(m_startMenuShortcutCheckBox);
    layout->addWidget(m_addToPathCheckBox);
    layout->addWidget(m_setupGlobalEnvVarsCheckBox);
    layout->addWidget(m_associateFilesCheckBox);

    layout->addStretch(1);
    return page;
}

QWidget* InstallerWindow::createProgressPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel(m_isUninstall ? "Removing VioraEDA Suite" : "Installing VioraEDA Suite", this);
    title->setStyleSheet("font-size: 20px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(title);

    m_statusLabel = new QLabel("Initializing installation pipeline...", this);
    m_statusLabel->setStyleSheet("color: #00d2ff; font-weight: 600; font-size: 13px;");
    layout->addWidget(m_statusLabel);

    layout->addSpacing(8);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);

    auto *metricsBox = new QWidget(this);
    metricsBox->setStyleSheet("background-color: #0f172a; border: 1px solid #1e293b; border-radius: 6px; padding: 10px;");
    auto *mLayout = new QVBoxLayout(metricsBox);
    mLayout->setSpacing(6);

    m_currentFileLabel = new QLabel("File: Preparing...", metricsBox);
    m_currentFileLabel->setStyleSheet("color: #cbd5e1; font-size: 12px;");

    auto *subMetricsLayout = new QHBoxLayout();
    m_speedLabel = new QLabel("Transfer Speed: 0.0 MB/s", metricsBox);
    m_speedLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    m_timeRemainingLabel = new QLabel("Time Remaining: Estimating...", metricsBox);
    m_timeRemainingLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    subMetricsLayout->addWidget(m_speedLabel);
    subMetricsLayout->addStretch(1);
    subMetricsLayout->addWidget(m_timeRemainingLabel);

    mLayout->addWidget(m_currentFileLabel);
    mLayout->addLayout(subMetricsLayout);

    layout->addWidget(metricsBox);
    layout->addStretch(1);
    return page;
}

QWidget* InstallerWindow::createFinishPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    m_finishTitleLabel = new QLabel(m_isUninstall ? "Uninstallation Complete" : "Installation Finished", this);
    m_finishTitleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #f8fafc;");
    layout->addWidget(m_finishTitleLabel);

    m_finishDescLabel = new QLabel(
        m_isUninstall ?
        "VioraEDA has been successfully removed from your system." :
        "VioraEDA 2026 has been successfully installed.\n\nYou can launch VioraEDA directly from the Desktop, Start Menu, or by clicking Finish below.",
        this
    );
    m_finishDescLabel->setWordWrap(true);
    m_finishDescLabel->setStyleSheet("color: #94a3b8; font-size: 13px; line-height: 1.5;");
    layout->addWidget(m_finishDescLabel);

    layout->addSpacing(10);

    if (!m_isUninstall) {
        m_launchCheckBox = new QCheckBox("Launch VioraEDA 2026 now", this);
        m_launchCheckBox->setChecked(true);
        layout->addWidget(m_launchCheckBox);
    }

    layout->addStretch(1);
    return page;
}

void InstallerWindow::onLicenseCheckChanged(bool checked) {
    if (m_stackedWidget->currentIndex() == 1) {
        m_nextBtn->setEnabled(checked);
    }
}

void InstallerWindow::onInstallTypeChanged(int index) {
    if (index == 0) {
        // Full
        m_chkSimulators->setChecked(true);
        m_chkLibrary->setChecked(true);
        m_chkCliTools->setChecked(true);
        m_chkExamples->setChecked(true);
        m_componentDescLabel->setText("Full installation provides complete design, simulation, and hardware verification capabilities.");
    } else if (index == 1) {
        // Minimal
        m_chkSimulators->setChecked(true);
        m_chkLibrary->setChecked(false);
        m_chkCliTools->setChecked(false);
        m_chkExamples->setChecked(false);
        m_componentDescLabel->setText("Minimal installation installs the core schematic editor and SPICE simulator with minimal disk footprint.");
    } else {
        // Custom
        m_componentDescLabel->setText("Custom installation allows selecting individual packages.");
    }
    updateDiskSpaceInfo();
}

void InstallerWindow::onComponentToggled() {
    m_config.components.coreSuite = true;
    m_config.components.simulators = m_chkSimulators->isChecked();
    m_config.components.componentLibrary = m_chkLibrary->isChecked();
    m_config.components.cliTools = m_chkCliTools->isChecked();
    m_config.components.examplesAndTemplates = m_chkExamples->isChecked();
    updateDiskSpaceInfo();
}

void InstallerWindow::browseDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Installation Directory", m_dirLineEdit->text());
    if (!dir.isEmpty()) {
        m_dirLineEdit->setText(QDir::toNativeSeparators(dir));
        m_config.installDir = dir;
        updateDiskSpaceInfo();
    }
}

void InstallerWindow::updateDiskSpaceInfo() {
    if (!m_spaceRequiredLabel || !m_spaceAvailableLabel) return;

    // Estimate size based on components
    double reqMB = 120.0; // Core suite
    if (m_chkSimulators && m_chkSimulators->isChecked()) reqMB += 45.0;
    if (m_chkLibrary && m_chkLibrary->isChecked()) reqMB += 180.0;
    if (m_chkCliTools && m_chkCliTools->isChecked()) reqMB += 25.0;
    if (m_chkExamples && m_chkExamples->isChecked()) reqMB += 15.0;

    m_spaceRequiredLabel->setText(QString("Space required: <b>%1 MB</b>").arg(QString::number(reqMB, 'f', 1)));

    QString path = m_dirLineEdit ? m_dirLineEdit->text() : m_config.installDir;
    QStorageInfo storage(path);
    if (!storage.isValid()) {
        storage.setPath(QDir::rootPath());
    }

    if (storage.isValid()) {
        double availMB = (double)storage.bytesAvailable() / (1024.0 * 1024.0);
        if (availMB > 1024.0) {
            m_spaceAvailableLabel->setText(QString("Space available: <b>%1 GB</b>").arg(QString::number(availMB / 1024.0, 'f', 2)));
        } else {
            m_spaceAvailableLabel->setText(QString("Space available: <b>%1 MB</b>").arg(QString::number(availMB, 'f', 1)));
        }
    } else {
        m_spaceAvailableLabel->setText("Space available: Unknown");
    }
}

void InstallerWindow::nextPage() {
    int current = m_stackedWidget->currentIndex();
    int count = m_stackedWidget->count();

    if (m_isUninstall) {
        if (current == 0) {
            startInstallation();
            return;
        } else if (current == 2) {
            finishInstallation();
            return;
        }
    }

    if (current == count - 1) {
        finishInstallation();
        return;
    }

    if (current == 3) {
        // Location & Options page -> Start installation
        startInstallation();
        return;
    }

    int next = current + 1;
    m_stackedWidget->setCurrentIndex(next);
    updateSidebarStep(next);

    bool canGoBack = (next > 0 && next < (m_isUninstall ? 1 : 4));
    m_backBtn->setEnabled(canGoBack);
    m_backBtn->setVisible(canGoBack);

    if (next == 1 && !m_isUninstall) {
        m_nextBtn->setEnabled(m_licenseCheckBox && m_licenseCheckBox->isChecked());
    } else {
        m_nextBtn->setEnabled(true);
    }

    if (next == 3 && !m_isUninstall) {
        m_nextBtn->setText("Install");
    } else {
        m_nextBtn->setText(m_isUninstall ? "Uninstall" : "Next");
    }
}

void InstallerWindow::prevPage() {
    if (m_worker && m_worker->isRunning()) return;
    int current = m_stackedWidget->currentIndex();
    if (current >= (m_isUninstall ? 1 : 4)) return;

    if (current > 0) {
        int prev = current - 1;
        m_stackedWidget->setCurrentIndex(prev);
        updateSidebarStep(prev);
        bool canGoBack = (prev > 0);
        m_backBtn->setEnabled(canGoBack);
        m_backBtn->setVisible(canGoBack);
        m_nextBtn->setEnabled(true);
        m_nextBtn->setText(m_isUninstall ? "Uninstall" : "Next");
    }
}

void InstallerWindow::startInstallation() {
    // Gather options
    if (!m_isUninstall) {
        m_config.installDir = m_dirLineEdit->text().trimmed();
        m_config.systemOptions.createDesktopShortcut = m_desktopShortcutCheckBox->isChecked();
        m_config.systemOptions.createStartMenuShortcuts = m_startMenuShortcutCheckBox->isChecked();
        m_config.systemOptions.addToPathEnvironment = m_addToPathCheckBox->isChecked();
        m_config.systemOptions.setupGlobalEnvironmentVariables = m_setupGlobalEnvVarsCheckBox->isChecked();
        m_config.systemOptions.registerFileAssociations = m_associateFilesCheckBox->isChecked();
    }

    int progressPageIndex = m_isUninstall ? 1 : 4;
    m_stackedWidget->setCurrentIndex(progressPageIndex);
    updateSidebarStep(progressPageIndex);

    m_backBtn->setEnabled(false);
    m_backBtn->setVisible(false);
    m_nextBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    m_worker = new InstallerWorker(m_config, this);
    connect(m_worker, &InstallerWorker::progressUpdated, this, &InstallerWindow::onProgressUpdated);
    connect(m_worker, &InstallerWorker::statusUpdated, this, &InstallerWindow::onStatusUpdated);
    connect(m_worker, &InstallerWorker::finished, this, &InstallerWindow::onFinished);
    m_worker->start();
}

void InstallerWindow::onProgressUpdated(const ProgressMetrics& metrics) {
    m_progressBar->setValue(metrics.percentage);
    m_currentFileLabel->setText(QString("File: %1 (%2 of %3)")
        .arg(metrics.currentFileName)
        .arg(metrics.filesProcessed)
        .arg(metrics.totalFiles));
    
    m_speedLabel->setText(QString("Transfer Speed: %1 MB/s")
        .arg(QString::number(metrics.transferSpeedMBps, 'f', 1)));

    if (metrics.estimatedSecondsRemaining > 0) {
        m_timeRemainingLabel->setText(QString("Time Remaining: %1s")
            .arg(metrics.estimatedSecondsRemaining));
    } else {
        m_timeRemainingLabel->setText("Time Remaining: Completing...");
    }
}

void InstallerWindow::onStatusUpdated(const QString& statusText) {
    m_statusLabel->setText(statusText);
}

void InstallerWindow::onFinished(bool success, const QString& errorMessage) {
    m_installationSuccess = success;
    int finishPageIndex = m_isUninstall ? 2 : 5;
    m_stackedWidget->setCurrentIndex(finishPageIndex);
    updateSidebarStep(finishPageIndex);

    m_backBtn->setEnabled(false);
    m_backBtn->setVisible(false);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_nextBtn->setEnabled(true);
    m_nextBtn->setText("Finish");

    if (!success) {
        m_finishTitleLabel->setText("Operation Failed");
        m_finishTitleLabel->setStyleSheet("font-size: 22px; font-weight: 700; color: #ef4444;");
        m_finishDescLabel->setText(QString("The setup encountered an error and could not complete:\n\n%1").arg(errorMessage));
        if (m_launchCheckBox) m_launchCheckBox->setVisible(false);
    }
}

void InstallerWindow::finishInstallation() {
    if (m_installationSuccess && !m_isUninstall && m_launchCheckBox && m_launchCheckBox->isChecked()) {
        QString appExe = QDir::cleanPath(m_config.installDir + "/bin/VioraEDA.exe");
        if (QFile::exists(appExe)) {
            QProcess::startDetached(appExe, QStringList());
        }
    }
    close();
}

void InstallerWindow::cancelInstallation() {
    if (m_worker && m_worker->isRunning()) {
        if (QMessageBox::question(this, "Cancel Installation", "Are you sure you want to cancel the installation?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_worker->cancel();
            m_statusLabel->setText("Cancelling and rolling back...");
            m_cancelBtn->setEnabled(false);
        }
        return;
    }
    close();
}

void InstallerWindow::closeEvent(QCloseEvent *event) {
    if (m_worker && m_worker->isRunning()) {
        if (QMessageBox::question(this, "Cancel Installation", "Setup is currently running. Do you want to abort?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_worker->cancel();
            m_worker->wait();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}

void InstallerWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    applyNativeWindowTheme();
}

void InstallerWindow::applyNativeWindowTheme() {
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(this->winId());
    if (hwnd) {
        // 1. Enable immersive dark mode for title bar and control box (minimize, maximize, close)
        BOOL useDarkMode = TRUE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE (20 on Windows 11 & Windows 10 build 2004+)
        DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode));
        // Fallback for earlier Windows 10 builds (1809 / 1903)
        DwmSetWindowAttribute(hwnd, 19, &useDarkMode, sizeof(useDarkMode));

        // 2. Set Caption / Window Control Bar Background Color to match installer background (#0b0f17 -> RGB(11, 15, 23))
        // DWMWA_CAPTION_COLOR = 35 (Windows 11)
        COLORREF captionColor = RGB(11, 15, 23);
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));

        // 3. Set Title Text Color (#f8fafc -> RGB(248, 250, 252))
        // DWMWA_TEXT_COLOR = 36 (Windows 11)
        COLORREF textColor = RGB(248, 250, 252);
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));

        // 4. Set Window Border Color (#1e293b -> RGB(30, 41, 59))
        // DWMWA_BORDER_COLOR = 34 (Windows 11)
        COLORREF borderColor = RGB(30, 41, 59);
        DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));
    }
#endif
}
