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
#include <QComboBox>
#include <QList>
#include "installer_worker.h"

class InstallerWindow : public QWidget {
    Q_OBJECT

public:
    explicit InstallerWindow(bool isUninstall = false, QWidget *parent = nullptr);
    ~InstallerWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void applyNativeWindowTheme();

private Q_SLOTS:
    void nextPage();
    void prevPage();
    void onInstallTypeChanged(int index);
    void onComponentToggled();
    void browseDirectory();
    void updateDiskSpaceInfo();
    void onLicenseCheckChanged(bool checked);
    void startInstallation();
    void cancelInstallation();
    void onProgressUpdated(const ProgressMetrics& metrics);
    void onStatusUpdated(const QString& statusText);
    void onFinished(bool success, const QString& errorMessage);
    void finishInstallation();

private:
    void setupUi();
    void updateSidebarStep(int pageIndex);
    QWidget* createSidebar();
    QWidget* createWelcomePage();
    QWidget* createLicensePage();
    QWidget* createComponentsPage();
    QWidget* createDirectoryPage();
    QWidget* createProgressPage();
    QWidget* createFinishPage();

    bool m_isUninstall{false};
    bool m_isAdmin{false};
    bool m_installationSuccess{false};

    QStackedWidget *m_stackedWidget{nullptr};
    QPushButton *m_backBtn{nullptr};
    QPushButton *m_nextBtn{nullptr};
    QPushButton *m_cancelBtn{nullptr};

    // Sidebar Step Indicators
    QList<QLabel*> m_stepLabels;

    // License Page
    QCheckBox *m_licenseCheckBox{nullptr};

    // Components Page
    QComboBox *m_installTypeCombo{nullptr};
    QCheckBox *m_chkCoreSuite{nullptr};
    QCheckBox *m_chkSimulators{nullptr};
    QCheckBox *m_chkLibrary{nullptr};
    QCheckBox *m_chkCliTools{nullptr};
    QCheckBox *m_chkExamples{nullptr};
    QLabel *m_componentDescLabel{nullptr};

    // Directory & Options Page
    QLineEdit *m_dirLineEdit{nullptr};
    QLabel *m_spaceRequiredLabel{nullptr};
    QLabel *m_spaceAvailableLabel{nullptr};
    QLabel *m_privilegeLabel{nullptr};
    QCheckBox *m_desktopShortcutCheckBox{nullptr};
    QCheckBox *m_startMenuShortcutCheckBox{nullptr};
    QCheckBox *m_addToPathCheckBox{nullptr};
    QCheckBox *m_setupGlobalEnvVarsCheckBox{nullptr};
    QCheckBox *m_associateFilesCheckBox{nullptr};

    // Progress Page
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_speedLabel{nullptr};
    QLabel *m_currentFileLabel{nullptr};
    QLabel *m_timeRemainingLabel{nullptr};

    // Finish Page
    QLabel *m_finishTitleLabel{nullptr};
    QLabel *m_finishDescLabel{nullptr};
    QCheckBox *m_launchCheckBox{nullptr};

    InstallerWorker *m_worker{nullptr};
    InstallConfig m_config;
};

#endif // INSTALLER_WINDOW_H
