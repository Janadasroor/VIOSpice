/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "installer_window.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QApplication>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QStandardPaths>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
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

    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/resources/icons/viora_eda_logo.png",
        appDir + "/../resources/icons/viora_eda_logo.png",
        QDir::currentPath() + "/resources/icons/viora_eda_logo.png",
        "C:/VioraEDA/resources/icons/viora_eda_logo.png"
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path)) {
            pix.load(path);
            if (!pix.isNull()) return pix;
        }
    }
    return pix;
}

} // namespace

InstallerWindow::InstallerWindow(bool isUninstall, QWidget *parent)
    : QWidget(parent), m_isUninstall(isUninstall), m_isAdmin(checkIsAdmin()) {
    
    setWindowTitle(m_isUninstall ? "VioraEDA 2026.1 Uninstall" : "VioraEDA 2026.1 Setup");
    resize(660, 440);
    
    QPixmap logoPix = loadInstallerLogo();
    if (!logoPix.isNull()) {
        setWindowIcon(QIcon(logoPix));
    }

    // Modern Dark QSS Theme
    setStyleSheet(R"(
        QWidget {
            background-color: #0d1117;
            color: #f4f4f5;
            font-family: 'Segoe UI', 'Inter', -apple-system, sans-serif;
            font-size: 13px;
        }
        QStackedWidget {
            background-color: #0d1117;
        }
        QLabel {
            color: #c9d1d9;
            background: transparent;
        }
        QLabel#titleLabel {
            font-size: 18px;
            font-weight: 700;
            color: #ffffff;
        }
        QLabel#subTitleLabel {
            font-size: 12px;
            color: #8b949e;
        }
        QTextEdit {
            background-color: #161b22;
            color: #c9d1d9;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 8px;
            font-family: 'Consolas', 'Courier New', monospace;
            font-size: 11px;
        }
        QLineEdit {
            background-color: #161b22;
            color: #ffffff;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 6px 10px;
        }
        QLineEdit:focus {
            border: 1px solid #00d2ff;
        }
        QPushButton {
            background-color: #21262d;
            color: #f4f4f5;
            border: 1px solid #30363d;
            border-radius: 6px;
            padding: 6px 16px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #30363d;
            color: #ffffff;
        }
        QPushButton:disabled {
            background-color: #161b22;
            color: #484f58;
            border: 1px solid #21262d;
        }
        QPushButton#primaryBtn {
            background-color: #007acc;
            color: #ffffff;
            border: 1px solid #0099ff;
        }
        QPushButton#primaryBtn:hover {
            background-color: #0099ff;
        }
        QPushButton#primaryBtn:disabled {
            background-color: #161b22;
            color: #484f58;
            border: 1px solid #21262d;
        }

        QProgressBar {
            background-color: #161b22;
            border: 1px solid #30363d;
            border-radius: 6px;
            height: 16px;
            text-align: center;
            color: #ffffff;
            font-size: 10px;
            font-weight: 700;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #007acc, stop:1 #00d2ff);
            border-radius: 5px;
        }
        QCheckBox {
            color: #f4f4f5;
            spacing: 8px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border: 1px solid #30363d;
            border-radius: 4px;
            background: #161b22;
        }
        QCheckBox::indicator:checked {
            background: #007acc;
            border-color: #00d2ff;
        }
    )");

    setupUi();
}

InstallerWindow::~InstallerWindow() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait(2000);
    }
}

void InstallerWindow::closeEvent(QCloseEvent *event) {
    if (m_worker && m_worker->isRunning()) {
        auto res = QMessageBox::question(this, "Cancel Installation",
            "Installation is currently in progress. Are you sure you want to cancel?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res == QMessageBox::Yes) {
            cancelInstallation();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        event->accept();
    }
}

void InstallerWindow::setupUi() {
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left Sidebar Banner
    mainLayout->addWidget(createSidebar());

    // Right Content Area
    auto *rightContainer = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(20, 20, 20, 16);
    rightLayout->setSpacing(12);

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->addWidget(createWelcomePage());
    m_stackedWidget->addWidget(createLicensePage());
    m_stackedWidget->addWidget(createDirectoryPage());
    m_stackedWidget->addWidget(createProgressPage());
    m_stackedWidget->addWidget(createFinishPage());

    rightLayout->addWidget(m_stackedWidget, 1);

    // Bottom Navigation Line Separator
    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("background-color: #30363d; min-height: 1px; max-height: 1px; border: none;");
    rightLayout->addWidget(line);

    // Bottom Control Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 4, 0, 0);
    btnLayout->setSpacing(8);

    btnLayout->addStretch();

    m_backBtn = new QPushButton("< Back", this);
    m_nextBtn = new QPushButton(m_isUninstall ? "Uninstall" : "Next >", this);
    m_nextBtn->setObjectName("primaryBtn");
    m_cancelBtn = new QPushButton("Cancel", this);

    btnLayout->addWidget(m_backBtn);
    btnLayout->addWidget(m_nextBtn);
    btnLayout->addWidget(m_cancelBtn);

    rightLayout->addLayout(btnLayout);
    mainLayout->addWidget(rightContainer, 1);

    connect(m_nextBtn, &QPushButton::clicked, this, &InstallerWindow::nextPage);
    connect(m_backBtn, &QPushButton::clicked, this, &InstallerWindow::prevPage);
    connect(m_cancelBtn, &QPushButton::clicked, this, &InstallerWindow::cancelInstallation);

    m_backBtn->setEnabled(false);

    if (m_isUninstall) {
        // In uninstall mode, skip license & directory pages and jump straight to confirm
        m_stackedWidget->setCurrentIndex(0);
        m_nextBtn->setText("Uninstall");
    }
}

QWidget* InstallerWindow::createSidebar() {
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName("installerSidebar");
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet(
        "QWidget#installerSidebar {"
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #090d12, stop:1 #161b22);"
        "   border-right: 1px solid #30363d;"
        "}"
        "QLabel {"
        "   border: none;"
        "   background: transparent;"
        "}"
    );

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 32, 16, 32);
    layout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    auto *iconLabel = new QLabel(sidebar);
    QPixmap pix = loadInstallerLogo();
    if (!pix.isNull()) {
        iconLabel->setPixmap(pix.scaled(110, 110, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    layout->addWidget(iconLabel, 0, Qt::AlignCenter);

    layout->addSpacing(16);

    auto *appName = new QLabel("VioraEDA", sidebar);
    appName->setStyleSheet("font-size: 20px; font-weight: 800; color: #ffffff; letter-spacing: 1px;");
    layout->addWidget(appName, 0, Qt::AlignCenter);

    auto *appVer = new QLabel(m_isUninstall ? "UNINSTALLER" : "2026.1 SETUP", sidebar);
    appVer->setStyleSheet("font-size: 11px; font-weight: 700; color: #00d2ff; letter-spacing: 1.5px;");
    layout->addWidget(appVer, 0, Qt::AlignCenter);

    layout->addStretch();

    auto *footerTag = new QLabel("EDA & SPICE SUITE", sidebar);
    footerTag->setStyleSheet("font-size: 9px; font-weight: 600; color: #484f58;");
    layout->addWidget(footerTag, 0, Qt::AlignCenter);

    return sidebar;
}

QWidget* InstallerWindow::createWelcomePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    if (m_isUninstall) {
        auto *title = new QLabel("Uninstall VioraEDA 2026.1", page);
        title->setObjectName("titleLabel");

        auto *desc = new QLabel(
            "This wizard will completely remove VioraEDA 2026.1 and its associated shortcuts, "
            "file associations, and environment settings from your computer.\n\n"
            "Click Uninstall to proceed with the removal.", page);
        desc->setWordWrap(true);

        layout->addWidget(title);
        layout->addSpacing(12);
        layout->addWidget(desc);
        layout->addStretch();
    } else {
        auto *title = new QLabel("Welcome to VioraEDA 2026.1 Setup", page);
        title->setObjectName("titleLabel");

        auto *desc = new QLabel(
            "Setup will install VioraEDA 2026.1 on your computer.\n\n"
            "VioraEDA is a modern, high-performance Electronic Design Automation suite "
            "with interactive schematic capture, mixed-signal SPICE simulation, "
            "and multi-layer PCB design.\n\n"
            "Click Next to continue.", page);
        desc->setWordWrap(true);

        layout->addWidget(title);
        layout->addSpacing(12);
        layout->addWidget(desc);
        layout->addStretch();
    }

    return page;
}

QWidget* InstallerWindow::createLicensePage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("License Agreement", page);
    title->setObjectName("titleLabel");

    auto *subTitle = new QLabel("Please review the license terms before installing VioraEDA 2026.1.", page);
    subTitle->setObjectName("subTitleLabel");

    auto *licenseText = new QTextEdit(page);
    licenseText->setReadOnly(true);
    licenseText->setText(
        "Apache License\n"
        "Version 2.0, January 2004\n"
        "http://www.apache.org/licenses/\n\n"
        "TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION\n\n"
        "1. Definitions.\n"
        "   'License' shall mean the terms and conditions for use, reproduction, and distribution as defined by Sections 1 through 9 of this document.\n\n"
        "   'Licensor' shall mean the copyright owner or entity authorized by the copyright owner that is granting the License.\n\n"
        "2. Grant of Copyright License.\n"
        "   Subject to the terms and conditions of this License, each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable copyright license to reproduce, prepare Derivative Works of, publicly display, publicly perform, sublicense, and distribute the Work and such Derivative Works in Source or Object form."
    );

    m_licenseCheckBox = new QCheckBox("I accept the terms in the License Agreement", page);
    connect(m_licenseCheckBox, &QCheckBox::toggled, this, &InstallerWindow::onLicenseCheckChanged);

    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(8);
    layout->addWidget(licenseText, 1);
    layout->addSpacing(10);
    layout->addWidget(m_licenseCheckBox);

    return page;
}

void InstallerWindow::onLicenseCheckChanged(bool checked) {
    if (m_stackedWidget && m_stackedWidget->currentIndex() == 1 && m_nextBtn) {
        m_nextBtn->setEnabled(checked);
    }
}

QWidget* InstallerWindow::createDirectoryPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Install Location & System Options", page);
    title->setObjectName("titleLabel");

    auto *subTitle = new QLabel("Select destination directory and system integration options.", page);
    subTitle->setObjectName("subTitleLabel");

    // Smart Privilege Detection: default to LocalAppData for Standard User, Program Files for Admin
    QString defaultPath;
    if (m_isAdmin) {
        defaultPath = "C:\\Program Files\\VioraEDA";
    } else {
        QString localApp = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (localApp.isEmpty()) localApp = "C:\\Users\\" + qgetenv("USERNAME") + "\\AppData\\Local";
        defaultPath = QDir::toNativeSeparators(localApp + "/Programs/VioraEDA");
    }

    auto *dirBox = new QWidget(page);
    auto *dirLayout = new QHBoxLayout(dirBox);
    dirLayout->setContentsMargins(0, 0, 0, 0);

    m_dirLineEdit = new QLineEdit(defaultPath, page);
    auto *browseBtn = new QPushButton("Browse...", page);

    dirLayout->addWidget(m_dirLineEdit, 1);
    dirLayout->addWidget(browseBtn);

    connect(browseBtn, &QPushButton::clicked, this, &InstallerWindow::browseDirectory);
    connect(m_dirLineEdit, &QLineEdit::textChanged, this, &InstallerWindow::updateDiskSpaceInfo);

    // Dark Card Info Box for Space Required & Space Available
    auto *spaceCard = new QWidget(page);
    spaceCard->setStyleSheet("background-color: #161b22; border: 1px solid #30363d; border-radius: 6px;");
    auto *spaceLayout = new QVBoxLayout(spaceCard);
    spaceLayout->setContentsMargins(12, 10, 12, 10);
    spaceLayout->setSpacing(4);

    m_privilegeLabel = new QLabel(m_isAdmin 
        ? "🛡️ System-Wide Installation (Administrator Mode)" 
        : "👤 Current User Installation (No Admin Rights Required)", spaceCard);
    m_privilegeLabel->setStyleSheet("border: none; font-size: 11px; font-weight: 600; color: #a1a1aa;");

    m_spaceRequiredLabel = new QLabel("Space required: Calculating...", spaceCard);
    m_spaceRequiredLabel->setStyleSheet("border: none; font-weight: 600; color: #ffffff;");

    m_spaceAvailableLabel = new QLabel("Space available: Calculating...", spaceCard);
    m_spaceAvailableLabel->setStyleSheet("border: none; font-weight: 600; color: #00d2ff;");

    spaceLayout->addWidget(m_privilegeLabel);
    spaceLayout->addWidget(m_spaceRequiredLabel);
    spaceLayout->addWidget(m_spaceAvailableLabel);

    // Options Checkboxes
    auto *optsContainer = new QWidget(page);
    auto *optsLayout = new QVBoxLayout(optsContainer);
    optsLayout->setContentsMargins(0, 8, 0, 0);
    optsLayout->setSpacing(8);

    m_desktopShortcutCheckBox = new QCheckBox("Create Desktop Shortcut", page);
    m_desktopShortcutCheckBox->setChecked(true);

    m_startMenuShortcutCheckBox = new QCheckBox("Create Start Menu Shortcuts (VioraEDA & CLI)", page);
    m_startMenuShortcutCheckBox->setChecked(true);

    m_addToPathCheckBox = new QCheckBox("Add VioraEDA to user PATH environment variable", page);
    m_addToPathCheckBox->setChecked(true);

    m_associateFilesCheckBox = new QCheckBox("Associate schematic & script files (.flxsch, .flux, .flxpcb, .cir)", page);
    m_associateFilesCheckBox->setChecked(true);

    optsLayout->addWidget(m_desktopShortcutCheckBox);
    optsLayout->addWidget(m_startMenuShortcutCheckBox);
    optsLayout->addWidget(m_addToPathCheckBox);
    optsLayout->addWidget(m_associateFilesCheckBox);

    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(10);
    layout->addWidget(dirBox);
    layout->addSpacing(8);
    layout->addWidget(spaceCard);
    layout->addWidget(optsContainer);
    layout->addStretch();

    updateDiskSpaceInfo();
    return page;
}

void InstallerWindow::updateDiskSpaceInfo() {
    if (!m_spaceRequiredLabel || !m_spaceAvailableLabel || !m_dirLineEdit) return;

    QString appDir = QCoreApplication::applicationDirPath();
    quint64 totalBytes = 0;
    QDirIterator it(appDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalBytes += it.fileInfo().size();
    }

    if (totalBytes < 100 * 1024 * 1024) {
        totalBytes = static_cast<quint64>(420) * 1024 * 1024;
    }

    double reqMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    if (reqMB >= 1024.0) {
        m_spaceRequiredLabel->setText(QString("Space required: %1 GB").arg(reqMB / 1024.0, 0, 'f', 2));
    } else {
        m_spaceRequiredLabel->setText(QString("Space required: %1 MB").arg(reqMB, 0, 'f', 1));
    }

    QString dirPath = m_dirLineEdit->text().trimmed();
    if (dirPath.isEmpty()) {
        m_spaceAvailableLabel->setText("Space available: N/A");
        return;
    }

#ifdef _WIN32
    ULARGE_INTEGER freeBytesAvailableToCaller;
    ULARGE_INTEGER totalNumberOfBytes;
    ULARGE_INTEGER totalNumberOfFreeBytes;

    std::wstring wpath = dirPath.toStdWString();
    if (GetDiskFreeSpaceExW(wpath.c_str(), &freeBytesAvailableToCaller, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        double freeMB = static_cast<double>(freeBytesAvailableToCaller.QuadPart) / (1024.0 * 1024.0);
        if (freeMB >= 1024.0) {
            m_spaceAvailableLabel->setText(QString("Space available: %1 GB").arg(freeMB / 1024.0, 0, 'f', 1));
        } else {
            m_spaceAvailableLabel->setText(QString("Space available: %1 MB").arg(freeMB, 0, 'f', 1));
        }
        return;
    }

    QString root = dirPath.left(3);
    if (root.endsWith(":\\") || root.endsWith(":/")) {
        std::wstring wroot = root.toStdWString();
        if (GetDiskFreeSpaceExW(wroot.c_str(), &freeBytesAvailableToCaller, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
            double freeMB = static_cast<double>(freeBytesAvailableToCaller.QuadPart) / (1024.0 * 1024.0);
            if (freeMB >= 1024.0) {
                m_spaceAvailableLabel->setText(QString("Space available: %1 GB").arg(freeMB / 1024.0, 0, 'f', 1));
            } else {
                m_spaceAvailableLabel->setText(QString("Space available: %1 MB").arg(freeMB, 0, 'f', 1));
            }
            return;
        }
    }
#endif

    m_spaceAvailableLabel->setText("Space available: Unknown");
}

QWidget* InstallerWindow::createProgressPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel(m_isUninstall ? "Uninstalling VioraEDA 2026.1" : "Installing VioraEDA 2026.1", page);
    title->setObjectName("titleLabel");

    m_statusLabel = new QLabel("Initializing installation engine...", page);
    m_statusLabel->setStyleSheet("font-weight: 600; color: #ffffff;");

    m_currentFileLabel = new QLabel("", page);
    m_currentFileLabel->setStyleSheet("font-size: 11px; color: #8b949e;");

    m_progressBar = new QProgressBar(page);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    m_speedLabel = new QLabel("", page);
    m_speedLabel->setStyleSheet("font-size: 11px; font-weight: 600; color: #00d2ff;");

    layout->addWidget(title);
    layout->addSpacing(16);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_currentFileLabel);
    layout->addSpacing(6);
    layout->addWidget(m_progressBar);
    layout->addSpacing(4);
    layout->addWidget(m_speedLabel);
    layout->addStretch();

    return page;
}

QWidget* InstallerWindow::createFinishPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    m_finishTitleLabel = new QLabel("Installation Complete", page);
    m_finishTitleLabel->setObjectName("titleLabel");

    m_finishDescLabel = new QLabel(
        "VioraEDA 2026.1 has been successfully installed on your computer.\n\nClick Finish to exit Setup.", page);
    m_finishDescLabel->setWordWrap(true);

    m_launchCheckBox = new QCheckBox("Launch VioraEDA 2026.1 now", page);
    m_launchCheckBox->setChecked(true);
    if (m_isUninstall) {
        m_launchCheckBox->setVisible(false);
    }

    layout->addWidget(m_finishTitleLabel);
    layout->addSpacing(12);
    layout->addWidget(m_finishDescLabel);
    layout->addSpacing(16);
    layout->addWidget(m_launchCheckBox);
    layout->addStretch();

    return page;
}

void InstallerWindow::browseDirectory() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Installation Directory", m_dirLineEdit->text());
    if (!dir.isEmpty()) {
        m_dirLineEdit->setText(dir);
    }
}

void InstallerWindow::nextPage() {
    int curr = m_stackedWidget->currentIndex();

    if (m_isUninstall) {
        if (curr == 0) { // Welcome -> Progress (Start Uninstall)
            startInstallation();
            return;
        } else if (curr == 4) { // Finish
            finishInstallation();
            return;
        }
    }

    if (curr == 1) { // License -> Directory
        m_nextBtn->setText("Install");
        m_nextBtn->setEnabled(true);
    } else if (curr == 2) { // Directory -> Progress (Start Install)
        startInstallation();
        return;
    } else if (curr == 4) { // Finish
        finishInstallation();
        return;
    }

    if (curr < m_stackedWidget->count() - 1) {
        int newIdx = curr + 1;
        m_stackedWidget->setCurrentIndex(newIdx);
        m_backBtn->setEnabled(newIdx > 0 && newIdx != 3);

        if (newIdx == 1 && m_licenseCheckBox) {
            m_nextBtn->setEnabled(m_licenseCheckBox->isChecked());
        }
    }
}

void InstallerWindow::prevPage() {
    int curr = m_stackedWidget->currentIndex();
    if (curr > 0 && curr != 3) {
        int newIdx = curr - 1;
        m_stackedWidget->setCurrentIndex(newIdx);
        m_backBtn->setEnabled(newIdx > 0);

        if (newIdx < 2) {
            m_nextBtn->setText("Next >");
        }

        if (newIdx == 1 && m_licenseCheckBox) {
            m_nextBtn->setEnabled(m_licenseCheckBox->isChecked());
        } else {
            m_nextBtn->setEnabled(true);
        }
    }
}

void InstallerWindow::startInstallation() {
    m_stackedWidget->setCurrentIndex(3);
    m_backBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);

    InstallOptions opts;
    if (m_isUninstall) {
        opts.isUninstall = true;
        // Detect installed path from registry or current location
        QSettings uninstReg("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VioraEDA", QSettings::NativeFormat);
        QString installedPath = uninstReg.value("InstallLocation").toString();
        if (installedPath.isEmpty()) {
            installedPath = QCoreApplication::applicationDirPath() + "/..";
        }
        opts.installDir = QDir::cleanPath(installedPath);
    } else {
        opts.isUninstall = false;
        opts.installDir = m_dirLineEdit ? m_dirLineEdit->text().trimmed() : "C:\\Program Files\\VioraEDA";
        opts.createDesktopShortcut = m_desktopShortcutCheckBox ? m_desktopShortcutCheckBox->isChecked() : true;
        opts.createStartMenuShortcut = m_startMenuShortcutCheckBox ? m_startMenuShortcutCheckBox->isChecked() : true;
        opts.addToPath = m_addToPathCheckBox ? m_addToPathCheckBox->isChecked() : true;
        opts.associateFiles = m_associateFilesCheckBox ? m_associateFilesCheckBox->isChecked() : true;
    }

    m_worker = new InstallerWorker(opts, this);
    connect(m_worker, &InstallerWorker::progressChanged, this, &InstallerWindow::onProgressChanged);
    connect(m_worker, &InstallerWorker::statusChanged, this, &InstallerWindow::onStatusChanged);
    connect(m_worker, &InstallerWorker::installationFinished, this, &InstallerWindow::onInstallationFinished);

    m_worker->start();
}

void InstallerWindow::onProgressChanged(int percentage, const QString& currentFile, double speedMBps, quint64 bytesCopied, quint64 totalBytes) {
    if (m_progressBar) {
        m_progressBar->setValue(percentage);
    }
    if (m_currentFileLabel) {
        m_currentFileLabel->setText(currentFile.isEmpty() ? "" : QString("Writing: %1").arg(currentFile));
    }
    if (m_speedLabel) {
        double copiedMB = static_cast<double>(bytesCopied) / (1024.0 * 1024.0);
        double totalMB = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
        if (speedMBps > 0.05) {
            m_speedLabel->setText(QString("%1 MB / %2 MB (%3 MB/s)").arg(copiedMB, 0, 'f', 1).arg(totalMB, 0, 'f', 1).arg(speedMBps, 0, 'f', 1));
        } else {
            m_speedLabel->setText(QString("%1 MB / %2 MB").arg(copiedMB, 0, 'f', 1).arg(totalMB, 0, 'f', 1));
        }
    }
}

void InstallerWindow::onStatusChanged(const QString& statusText) {
    if (m_statusLabel) {
        m_statusLabel->setText(statusText);
    }
}

void InstallerWindow::onInstallationFinished(bool success, const QString& message) {
    m_installationSuccess = success;
    m_stackedWidget->setCurrentIndex(4);
    m_nextBtn->setText("Finish");
    m_nextBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);

    if (m_finishTitleLabel && m_finishDescLabel) {
        if (m_isUninstall) {
            m_finishTitleLabel->setText(success ? "Uninstallation Complete" : "Uninstallation Incomplete");
            m_finishDescLabel->setText(success 
                ? "VioraEDA 2026.1 has been cleanly removed from your computer.\n\nClick Finish to exit."
                : QString("Uninstallation could not complete: %1\n\nClick Finish to exit.").arg(message));
        } else {
            m_finishTitleLabel->setText(success ? "Installation Complete" : "Installation Incomplete");
            m_finishDescLabel->setText(success
                ? "VioraEDA 2026.1 has been successfully installed on your computer.\n\nClick Finish to exit Setup."
                : QString("Installation encountered an issue: %1\n\nClick Finish to exit.").arg(message));
        }
    }
}

void InstallerWindow::cancelInstallation() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
    } else {
        close();
    }
}

void InstallerWindow::finishInstallation() {
    if (m_installationSuccess && !m_isUninstall && m_launchCheckBox && m_launchCheckBox->isChecked()) {
        QString binDir = m_dirLineEdit ? (m_dirLineEdit->text().trimmed() + "/bin") : "C:/Program Files/VioraEDA/bin";
        QString appExe = binDir + "/VioraEDA.exe";
        if (QFile::exists(appExe)) {
            QProcess::startDetached(appExe, QStringList());
        }
    }
    close();
}
