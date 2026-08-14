/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INSTALLER_WINDOW_H
#define INSTALLER_WINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include "installer_worker.h"

class InstallerWindow : public QWidget {
    Q_OBJECT

public:
    explicit InstallerWindow(bool isUninstall = false, QWidget *parent = nullptr);
    ~InstallerWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void nextPage();
    void prevPage();
    void startInstallation();
    void onProgressChanged(int percentage, const QString& currentFile, double speedMBps, quint64 bytesCopied, quint64 totalBytes);
    void onStatusChanged(const QString& statusText);
    void onInstallationFinished(bool success, const QString& message);
    void finishInstallation();
    void cancelInstallation();
    void browseDirectory();
    void updateDiskSpaceInfo();
    void onLicenseCheckChanged(bool checked);

private:
    void setupUi();
    QWidget* createSidebar();
    QWidget* createWelcomePage();
    QWidget* createLicensePage();
    QWidget* createDirectoryPage();
    QWidget* createProgressPage();
    QWidget* createFinishPage();

    bool m_isUninstall{false};
    bool m_isAdmin{false};

    QStackedWidget *m_stackedWidget{nullptr};
    QPushButton *m_backBtn{nullptr};
    QPushButton *m_nextBtn{nullptr};
    QPushButton *m_cancelBtn{nullptr};

    // License Page
    QCheckBox *m_licenseCheckBox{nullptr};

    // Directory & Options Page
    QLineEdit *m_dirLineEdit{nullptr};
    QLabel *m_spaceRequiredLabel{nullptr};
    QLabel *m_spaceAvailableLabel{nullptr};
    QLabel *m_privilegeLabel{nullptr};
    QCheckBox *m_desktopShortcutCheckBox{nullptr};
    QCheckBox *m_startMenuShortcutCheckBox{nullptr};
    QCheckBox *m_addToPathCheckBox{nullptr};
    QCheckBox *m_associateFilesCheckBox{nullptr};

    // Progress Page
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_speedLabel{nullptr};
    QLabel *m_currentFileLabel{nullptr};

    // Finish Page
    QLabel *m_finishTitleLabel{nullptr};
    QLabel *m_finishDescLabel{nullptr};
    QCheckBox *m_launchCheckBox{nullptr};

    InstallerWorker *m_worker{nullptr};
    bool m_installationSuccess{false};
};

#endif // INSTALLER_WINDOW_H
