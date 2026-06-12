/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget* parent = nullptr);
    
    void setStatus(const QString& status);
    void setProgress(int value, int total);

private:
    QLabel* m_logoLabel;
    QLabel* m_titleLabel;
    QLabel* m_statusLabel;
    QProgressBar* m_progressBar;
};

#endif // SPLASH_SCREEN_H
