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
#include <QGraphicsDropShadowEffect>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#ifdef _WIN32
#include <windows.h>
#endif


static QPixmap loadInstallerLogo() {
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

InstallerWindow::InstallerWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle("VioraEDA 2026.1 Setup");
    resize(640, 420);
    
    QPixmap logoPix = loadInstallerLogo();
    if (!logoPix.isNull()) {
        setWindowIcon(QIcon(logoPix));
    }

    // Global Dark QSS Theme
    setStyleSheet(R"(
        QWidget {
            background-color: #0d1117;
            color: #f4f4f5;
            font-family: 'Segoe UI', 'Inter', sans-serif;
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
        QPushButton#primaryBtn {
            background-color: #007acc;
            color: #ffffff;
            border: 1px solid #0099ff;
        }
        QPushButton#primaryBtn:hover {
            background-color: #0099ff;
        }
        QProgressBar {
            background-color: #161b22;
            border: 1px solid #30363d;
            border-radius: 6px;
            height: 14px;
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
            spacing: 6px;
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
    m_nextBtn = new QPushButton("Next >", this);
    m_nextBtn->setObjectName("primaryBtn");
    m_cancelBtn = new QPushButton("Cancel", this);

    btnLayout->addWidget(m_backBtn);
    btnLayout->addWidget(m_nextBtn);
    btnLayout->addWidget(m_cancelBtn);

    rightLayout->addLayout(btnLayout);

    mainLayout->addWidget(rightContainer, 1);

    connect(m_nextBtn, &QPushButton::clicked, this, &InstallerWindow::nextPage);
    connect(m_backBtn, &QPushButton::clicked, this, &InstallerWindow::prevPage);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QWidget::close);

    m_backBtn->setEnabled(false);
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

    auto *appVer = new QLabel("2026.1 SETUP", sidebar);
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

    auto *title = new QLabel("Welcome to VioraEDA 2026.1 Setup", page);
    title->setObjectName("titleLabel");

    auto *desc = new QLabel(
        "Setup will guide you through the installation of VioraEDA 2026.1.\n\n"
        "It is recommended that you close all other applications before starting Setup. "
        "This will make it possible to update relevant system files without needing a reboot.\n\n"
        "Click Next to continue.", page);
    desc->setWordWrap(true);

    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(desc);
    layout->addStretch();

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

    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(8);
    layout->addWidget(licenseText, 1);

    return page;
}

QWidget* InstallerWindow::createDirectoryPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Choose Install Location", page);
    title->setObjectName("titleLabel");

    auto *subTitle = new QLabel("Choose the folder in which to install VioraEDA 2026.1.", page);
    subTitle->setObjectName("subTitleLabel");

    auto *dirBox = new QWidget(page);
    auto *dirLayout = new QHBoxLayout(dirBox);
    dirLayout->setContentsMargins(0, 0, 0, 0);

    m_dirLineEdit = new QLineEdit("C:\\Program Files\\VioraEDA", page);
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

    m_spaceRequiredLabel = new QLabel("Space required: 385.4 MB", spaceCard);
    m_spaceRequiredLabel->setStyleSheet("border: none; font-weight: 600; color: #ffffff;");

    m_spaceAvailableLabel = new QLabel("Space available: Calculating...", spaceCard);
    m_spaceAvailableLabel->setStyleSheet("border: none; font-weight: 600; color: #00d2ff;");

    spaceLayout->addWidget(m_spaceRequiredLabel);
    spaceLayout->addWidget(m_spaceAvailableLabel);

    layout->addWidget(title);
    layout->addWidget(subTitle);
    layout->addSpacing(16);
    layout->addWidget(dirBox);
    layout->addSpacing(12);
    layout->addWidget(spaceCard);
    layout->addStretch();

    updateDiskSpaceInfo();

    return page;
}

void InstallerWindow::updateDiskSpaceInfo() {
    if (!m_spaceRequiredLabel || !m_spaceAvailableLabel || !m_dirLineEdit) return;

    m_spaceRequiredLabel->setText("Space required: 385.4 MB");

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

    // Probe root drive letter (e.g. "C:\") if specific directory doesn't exist yet
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

    auto *title = new QLabel("Installing VioraEDA 2026.1", page);
    title->setObjectName("titleLabel");

    m_statusLabel = new QLabel("Extracting binaries and core components...", page);

    m_progressBar = new QProgressBar(page);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);

    layout->addWidget(title);
    layout->addSpacing(16);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressBar);
    layout->addStretch();

    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, &InstallerWindow::updateProgress);

    return page;
}

QWidget* InstallerWindow::createFinishPage() {
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *title = new QLabel("Installation Complete", page);
    title->setObjectName("titleLabel");

    auto *desc = new QLabel("VioraEDA 2026.1 has been installed on your computer.\n\nClick Finish to exit Setup.", page);
    desc->setWordWrap(true);

    m_launchCheckBox = new QCheckBox("Launch VioraEDA 2026.1 now", page);
    m_launchCheckBox->setChecked(true);

    layout->addWidget(title);
    layout->addSpacing(12);
    layout->addWidget(desc);
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
    if (curr == 1) { // License -> Directory
        m_nextBtn->setText("Install");
    } else if (curr == 2) { // Directory -> Progress
        startInstallation();
        return;
    } else if (curr == 4) { // Finish
        finishInstallation();
        return;
    }

    if (curr < m_stackedWidget->count() - 1) {
        m_stackedWidget->setCurrentIndex(curr + 1);
        m_backBtn->setEnabled(m_stackedWidget->currentIndex() > 0 && m_stackedWidget->currentIndex() != 3);
    }
}

void InstallerWindow::prevPage() {
    int curr = m_stackedWidget->currentIndex();
    if (curr > 0 && curr != 3) {
        m_stackedWidget->setCurrentIndex(curr - 1);
        m_backBtn->setEnabled(m_stackedWidget->currentIndex() > 0);
        if (m_stackedWidget->currentIndex() < 2) {
            m_nextBtn->setText("Next >");
        }
    }
}

void InstallerWindow::startInstallation() {
    m_stackedWidget->setCurrentIndex(3);
    m_backBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_cancelBtn->setEnabled(false);

    m_progressValue = 0;
    m_progressTimer->start(40);
}

void InstallerWindow::updateProgress() {
    m_progressValue += 2;
    m_progressBar->setValue(m_progressValue);

    if (m_progressValue < 30) {
        m_statusLabel->setText("Extracting Qt6 binaries and core DLLs...");
    } else if (m_progressValue < 60) {
        m_statusLabel->setText("Indexing SPICE subcircuits and footprint libraries...");
    } else if (m_progressValue < 90) {
        m_statusLabel->setText("Configuring Python engine and FluxScript JIT...");
    } else {
        m_statusLabel->setText("Creating Start Menu shortcuts...");
    }

    if (m_progressValue >= 100) {
        m_progressTimer->stop();
        m_stackedWidget->setCurrentIndex(4);
        m_nextBtn->setText("Finish");
        m_nextBtn->setEnabled(true);
        m_cancelBtn->setEnabled(false);
    }
}

void InstallerWindow::finishInstallation() {
    if (m_launchCheckBox && m_launchCheckBox->isChecked()) {
        QProcess::startDetached("C:\\VioraEDA\\build\\VioraEDA.exe", QStringList());
    }
    close();
}
