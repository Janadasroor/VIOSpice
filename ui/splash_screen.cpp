/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "splash_screen.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include "../core/visuals/theme_manager.h"

SplashScreen::SplashScreen(QWidget* parent) 
    : QWidget(parent, Qt::FramelessWindowHint | Qt::SplashScreen) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(540, 380);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    auto container = new QWidget(this);
    container->setObjectName("splashContainer");
    container->setStyleSheet(
        "#splashContainer {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #0d1117, stop:0.5 #161b22, stop:1 #0b0e14);"
        "  border: 1px solid #30363d;"
        "  border-radius: 16px;"
        "}"
        "QLabel {"
        "  background: transparent;"
        "}"
    );
    mainLayout->addWidget(container);

    auto layout = new QVBoxLayout(container);
    layout->setContentsMargins(36, 32, 36, 28);
    layout->setSpacing(12);

    // Top Brand Logo
    m_logoLabel = new QLabel(this);
    QPixmap logoPixmap(":/icons/viora_eda_logo.png");
    if (!logoPixmap.isNull()) {
        m_logoLabel->setPixmap(logoPixmap.scaled(110, 110, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logoLabel);

    layout->addSpacing(4);

    // Main App Title
    m_titleLabel = new QLabel("VioraEDA", this);
    m_titleLabel->setStyleSheet(
        "font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "font-size: 30px;"
        "font-weight: 800;"
        "letter-spacing: 5px;"
        "color: #ffffff;"
    );
    m_titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_titleLabel);

    // Subtitle Tagline
    auto subtitle = new QLabel("ELECTRONIC DESIGN AUTOMATION SUITE", this);
    subtitle->setStyleSheet(
        "font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "font-size: 10px;"
        "font-weight: 700;"
        "letter-spacing: 3px;"
        "color: #58a6ff;"
    );
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    layout->addStretch();

    // Status Row Layout (Status text left, version tag right)
    auto statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(0, 0, 0, 0);

    m_statusLabel = new QLabel("Initializing SPICE Engine...", this);
    m_statusLabel->setStyleSheet(
        "font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "font-size: 12px;"
        "font-weight: 500;"
        "color: #8b949e;"
    );
    m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    statusRow->addWidget(m_statusLabel);

    statusRow->addStretch();

    auto verBadge = new QLabel("v2026.1", this);
    verBadge->setStyleSheet(
        "font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;"
        "font-size: 11px;"
        "font-weight: 600;"
        "color: #7d8590;"
        "background-color: #21262d;"
        "border: 1px solid #30363d;"
        "border-radius: 6px;"
        "padding: 2px 8px;"
    );
    statusRow->addWidget(verBadge);

    layout->addLayout(statusRow);

    // Modern Gradient Progress Bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "  background-color: #21262d;"
        "  border: none;"
        "  border-radius: 3px;"
        "}"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00d2ff, stop:0.5 #0078ff, stop:1 #5856d6);"
        "  border-radius: 3px;"
        "}"
    );
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar);



    // Center splash screen on primary monitor
    if (auto* screen = QGuiApplication::primaryScreen()) {
        move(screen->availableGeometry().center() - rect().center());
    }
}

void SplashScreen::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw subtle outer drop shadow around card container
    QRect cardRect = rect().adjusted(15, 15, -15, -15);
    QPainterPath path;
    path.addRoundedRect(cardRect, 16, 16);

    // Soft outer shadow glow
    for (int i = 0; i < 12; ++i) {
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(cardRect.adjusted(-i, -i, i, i), 16 + i, 16 + i);
        painter.fillPath(shadowPath, QColor(0, 0, 0, 15 - i));
    }
}

void SplashScreen::setStatus(const QString& status) {
    m_statusLabel->setText(status);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

void SplashScreen::setProgress(int value, int total) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(value);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

