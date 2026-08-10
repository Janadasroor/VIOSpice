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
#include <QTimer>

class InstallerWindow : public QWidget {
    Q_OBJECT

public:
    explicit InstallerWindow(QWidget *parent = nullptr);
    ~InstallerWindow() override = default;


private Q_SLOTS:
    void nextPage();
    void prevPage();
    void startInstallation();
    void updateProgress();
    void finishInstallation();
    void browseDirectory();
    void updateDiskSpaceInfo();

private:
    void setupUi();
    QWidget* createSidebar();
    QWidget* createWelcomePage();
    QWidget* createLicensePage();
    QWidget* createDirectoryPage();
    QWidget* createProgressPage();
    QWidget* createFinishPage();

    QStackedWidget *m_stackedWidget{nullptr};
    QPushButton *m_backBtn{nullptr};
    QPushButton *m_nextBtn{nullptr};
    QPushButton *m_cancelBtn{nullptr};

    QLineEdit *m_dirLineEdit{nullptr};
    QLabel *m_spaceRequiredLabel{nullptr};
    QLabel *m_spaceAvailableLabel{nullptr};
    QProgressBar *m_progressBar{nullptr};
    QLabel *m_statusLabel{nullptr};
    QCheckBox *m_launchCheckBox{nullptr};
    QTimer *m_progressTimer{nullptr};
    int m_progressValue{0};
};


#endif // DARK_INSTALLER_WINDOW_H
