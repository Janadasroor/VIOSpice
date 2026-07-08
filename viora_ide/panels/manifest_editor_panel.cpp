/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "manifest_editor_panel.h"
#include "../core/ide_theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

namespace IDE {

ManifestEditorPanel::ManifestEditorPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    ThemeManager::instance().registerThemeCallback(this, [this]() { reapplyTheme(); });
}

void ManifestEditorPanel::setupUI() {
    auto tc = currentTheme();
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Vertical);

    // Form section
    auto* formWidget = new QWidget();
    auto* formLayout = new QVBoxLayout(formWidget);
    formLayout->setContentsMargins(12, 8, 12, 8);
    formLayout->setSpacing(6);

    // Basic info
    auto* basicGroup = new QWidget();
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setContentsMargins(0, 0, 0, 0);

    auto labelStyle = QString("color: %1; font-size: 10pt;").arg(tc.textSecondary);
    auto editStyle = QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                     "padding: 4px 8px; border-radius: 3px; }").arg(tc.bgPanel, tc.textPrimary, tc.border);

    m_idEdit = new QLineEdit();
    m_idEdit->setStyleSheet(editStyle);
    basicLayout->addRow(new QLabel("ID:"), m_idEdit);

    m_nameEdit = new QLineEdit();
    m_nameEdit->setStyleSheet(editStyle);
    basicLayout->addRow(new QLabel("Name:"), m_nameEdit);

    m_versionEdit = new QLineEdit();
    m_versionEdit->setStyleSheet(editStyle);
    basicLayout->addRow(new QLabel("Version:"), m_versionEdit);

    m_authorEdit = new QLineEdit();
    m_authorEdit->setStyleSheet(editStyle);
    basicLayout->addRow(new QLabel("Author:"), m_authorEdit);

    m_mainFileEdit = new QLineEdit();
    m_mainFileEdit->setStyleSheet(editStyle);
    basicLayout->addRow(new QLabel("Main File:"), m_mainFileEdit);

    m_descEdit = new QTextEdit();
    m_descEdit->setMaximumHeight(60);
    m_descEdit->setStyleSheet(QString("QTextEdit { background: %1; color: %2; border: 1px solid %3; padding: 4px; }").arg(tc.bgPanel, tc.textPrimary, tc.border));
    basicLayout->addRow(new QLabel("Description:"), m_descEdit);

    formLayout->addWidget(basicGroup);

    // Menu entries
    auto* menuGroup = new QWidget();
    auto* menuLayout = new QVBoxLayout(menuGroup);
    menuLayout->setContentsMargins(0, 4, 0, 0);

    auto* menuHeader = new QHBoxLayout();
    menuHeader->addWidget(new QLabel("Menu Entries"));
    auto* addMenuBtn = new QPushButton("+");
    addMenuBtn->setFixedSize(24, 24);
    addMenuBtn->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: none; border-radius: 4px; }").arg(tc.border, tc.textPrimary));
    connect(addMenuBtn, &QPushButton::clicked, this, &ManifestEditorPanel::onAddMenuEntry);
    menuHeader->addWidget(addMenuBtn);
    auto* removeMenuBtn = new QPushButton("-");
    removeMenuBtn->setFixedSize(24, 24);
    removeMenuBtn->setStyleSheet(addMenuBtn->styleSheet());
    connect(removeMenuBtn, &QPushButton::clicked, this, &ManifestEditorPanel::onRemoveMenuEntry);
    menuHeader->addWidget(removeMenuBtn);
    menuHeader->addStretch();
    menuLayout->addLayout(menuHeader);

    m_menuList = new QListWidget();
    m_menuList->setMaximumHeight(80);
    m_menuList->setStyleSheet(QString("QListWidget { background: %1; color: %2; border: 1px solid %3; }").arg(tc.bgPanel, tc.textPrimary, tc.border));
    menuLayout->addWidget(m_menuList);

    auto* menuInputLayout = new QHBoxLayout();
    m_menuPathEdit = new QLineEdit();
    m_menuPathEdit->setPlaceholderText("Menu path (e.g. Extensions/My Ext)");
    m_menuPathEdit->setStyleSheet(editStyle);
    menuInputLayout->addWidget(m_menuPathEdit);
    m_menuActionEdit = new QLineEdit();
    m_menuActionEdit->setPlaceholderText("Action function name");
    m_menuActionEdit->setStyleSheet(editStyle);
    menuInputLayout->addWidget(m_menuActionEdit);
    menuLayout->addLayout(menuInputLayout);

    formLayout->addWidget(menuGroup);

    // Context handlers
    auto* contextGroup = new QWidget();
    auto* contextLayout = new QVBoxLayout(contextGroup);
    contextLayout->setContentsMargins(0, 4, 0, 0);

    auto* contextHeader = new QHBoxLayout();
    contextHeader->addWidget(new QLabel("Context Handlers"));
    auto* addCtxBtn = new QPushButton("+");
    addCtxBtn->setFixedSize(24, 24);
    addCtxBtn->setStyleSheet(addMenuBtn->styleSheet());
    connect(addCtxBtn, &QPushButton::clicked, this, &ManifestEditorPanel::onAddContext);
    contextHeader->addWidget(addCtxBtn);
    auto* removeCtxBtn = new QPushButton("-");
    removeCtxBtn->setFixedSize(24, 24);
    removeCtxBtn->setStyleSheet(addMenuBtn->styleSheet());
    connect(removeCtxBtn, &QPushButton::clicked, this, &ManifestEditorPanel::onRemoveContext);
    contextHeader->addWidget(removeCtxBtn);
    contextHeader->addStretch();
    contextLayout->addLayout(contextHeader);

    m_contextList = new QListWidget();
    m_contextList->setMaximumHeight(80);
    m_contextList->setStyleSheet(m_menuList->styleSheet());
    contextLayout->addWidget(m_contextList);

    auto* ctxInputLayout = new QHBoxLayout();
    m_contextTypeEdit = new QLineEdit();
    m_contextTypeEdit->setPlaceholderText("Component type (e.g. R, C, V)");
    m_contextTypeEdit->setStyleSheet(editStyle);
    ctxInputLayout->addWidget(m_contextTypeEdit);
    m_contextActionEdit = new QLineEdit();
    m_contextActionEdit->setPlaceholderText("Action function name");
    m_contextActionEdit->setStyleSheet(editStyle);
    ctxInputLayout->addWidget(m_contextActionEdit);
    contextLayout->addLayout(ctxInputLayout);

    formLayout->addWidget(contextGroup);

    splitter->addWidget(formWidget);

    // JSON preview
    m_jsonPreview = new QTextEdit();
    m_jsonPreview->setReadOnly(true);
    m_jsonPreview->setStyleSheet(
        QString("QTextEdit { background: %1; color: %2; border: none; "
        "  font-family: 'Consolas', monospace; font-size: 10pt; padding: 8px; }").arg(tc.bgEditor, tc.textPrimary)
    );
    m_jsonPreview->setPlaceholderText("JSON preview will appear here...");
    splitter->addWidget(m_jsonPreview);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter);

    // Bottom bar
    auto* bottomBar = new QWidget();
    bottomBar->setStyleSheet(QString("background: %1; border-top: 1px solid %2;").arg(tc.bgPanel, tc.border));
    auto* bottomLayout = new QHBoxLayout(bottomBar);
    bottomLayout->setContentsMargins(12, 6, 12, 6);

    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet(QString("color: %1;").arg(tc.textSecondary));
    bottomLayout->addWidget(m_statusLabel);

    bottomLayout->addStretch();

    m_saveBtn = new QPushButton("Save");
    m_saveBtn->setStyleSheet(
        QString(
            "QPushButton { background: %1; color: white; border: none; padding: 6px 16px; "
            "border-radius: 4px; font-weight: bold; }"
            "QPushButton:hover { background: %2; }"
        ).arg(tc.accentBlue, tc.green)
    );
    connect(m_saveBtn, &QPushButton::clicked, this, &ManifestEditorPanel::onSaveClicked);
    bottomLayout->addWidget(m_saveBtn);

    mainLayout->addWidget(bottomBar);

    // Connect change signals
    auto connectChange = [this]() {
        connect(m_idEdit, &QLineEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
        connect(m_nameEdit, &QLineEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
        connect(m_versionEdit, &QLineEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
        connect(m_authorEdit, &QLineEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
        connect(m_mainFileEdit, &QLineEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
        connect(m_descEdit, &QTextEdit::textChanged, this, &ManifestEditorPanel::onFieldChanged);
    };
    connectChange();
}

void ManifestEditorPanel::loadManifest(const QString& manifestPath) {
    m_manifestPath = manifestPath;

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_statusLabel->setText("Could not open manifest.json");
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        m_statusLabel->setText("JSON parse error: " + error.errorString());
        return;
    }

    loadFromJson(doc.object());
    setModified(false);
    m_statusLabel->setText("Loaded: " + manifestPath);
}

void ManifestEditorPanel::loadFromJson(const QJsonObject& obj) {
    m_idEdit->setText(obj["id"].toString());
    m_nameEdit->setText(obj["name"].toString());
    m_versionEdit->setText(obj["version"].toString());
    m_authorEdit->setText(obj["author"].toString());
    m_mainFileEdit->setText(obj["main"].toString());
    m_descEdit->setPlainText(obj["description"].toString());

    // Menu entries
    m_menuList->clear();
    QJsonArray menuArr = obj["menu"].toArray();
    for (const auto& entry : menuArr) {
        QJsonObject menuObj = entry.toObject();
        QString path = menuObj["path"].toString();
        QString action = menuObj["action"].toString();
        m_menuList->addItem(QString("%1 -> %2").arg(path, action));
    }

    // Context handlers
    m_contextList->clear();
    QJsonArray ctxArr = obj["contexts"].toArray();
    for (const auto& entry : ctxArr) {
        QJsonObject ctxObj = entry.toObject();
        QString type = ctxObj["componentType"].toString();
        QString action = ctxObj["action"].toString();
        m_contextList->addItem(QString("%1 -> %2").arg(type, action));
    }

    updateJsonPreview();
}

QJsonObject ManifestEditorPanel::toJson() const {
    QJsonObject obj;
    obj["id"] = m_idEdit->text();
    obj["name"] = m_nameEdit->text();
    obj["version"] = m_versionEdit->text();
    obj["author"] = m_authorEdit->text();
    obj["main"] = m_mainFileEdit->text();
    obj["description"] = m_descEdit->toPlainText();

    QJsonArray menuArr;
    for (int i = 0; i < m_menuList->count(); ++i) {
        QString text = m_menuList->item(i)->text();
        int arrowIdx = text.indexOf(" -> ");
        if (arrowIdx >= 0) {
            QJsonObject entry;
            entry["path"] = text.left(arrowIdx);
            entry["action"] = text.mid(arrowIdx + 4);
            menuArr.append(entry);
        }
    }
    obj["menu"] = menuArr;

    QJsonArray ctxArr;
    for (int i = 0; i < m_contextList->count(); ++i) {
        QString text = m_contextList->item(i)->text();
        int arrowIdx = text.indexOf(" -> ");
        if (arrowIdx >= 0) {
            QJsonObject entry;
            entry["componentType"] = text.left(arrowIdx);
            entry["action"] = text.mid(arrowIdx + 4);
            ctxArr.append(entry);
        }
    }
    obj["contexts"] = ctxArr;

    return obj;
}

void ManifestEditorPanel::updateJsonPreview() {
    QJsonDocument doc(toJson());
    m_jsonPreview->setPlainText(doc.toJson(QJsonDocument::Indented));
}

void ManifestEditorPanel::setModified(bool mod) {
    if (m_modified != mod) {
        m_modified = mod;
        m_saveBtn->setEnabled(mod);
        m_statusLabel->setText(mod ? "Modified" : "Saved");
        emit modifiedChanged(mod);
    }
}

void ManifestEditorPanel::onFieldChanged() {
    setModified(true);
    updateJsonPreview();
}

void ManifestEditorPanel::onAddMenuEntry() {
    QString path = m_menuPathEdit->text().trimmed();
    QString action = m_menuActionEdit->text().trimmed();
    if (path.isEmpty() || action.isEmpty()) return;

    m_menuList->addItem(QString("%1 -> %2").arg(path, action));
    m_menuPathEdit->clear();
    m_menuActionEdit->clear();
    setModified(true);
    updateJsonPreview();
}

void ManifestEditorPanel::onRemoveMenuEntry() {
    delete m_menuList->takeItem(m_menuList->currentRow());
    setModified(true);
    updateJsonPreview();
}

void ManifestEditorPanel::onAddContext() {
    QString type = m_contextTypeEdit->text().trimmed();
    QString action = m_contextActionEdit->text().trimmed();
    if (type.isEmpty() || action.isEmpty()) return;

    m_contextList->addItem(QString("%1 -> %2").arg(type, action));
    m_contextTypeEdit->clear();
    m_contextActionEdit->clear();
    setModified(true);
    updateJsonPreview();
}

void ManifestEditorPanel::onRemoveContext() {
    delete m_contextList->takeItem(m_contextList->currentRow());
    setModified(true);
    updateJsonPreview();
}

bool ManifestEditorPanel::saveManifest() {
    if (m_manifestPath.isEmpty()) return false;

    QFile file(m_manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, "Save Error", "Could not write to manifest.json");
        return false;
    }

    QJsonDocument doc(toJson());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    setModified(false);
    emit saved(m_manifestPath);
    return true;
}

void ManifestEditorPanel::onSaveClicked() {
    saveManifest();
}

void ManifestEditorPanel::reapplyTheme() {
    auto tc = currentTheme();
    QString editStyle = QString("QLineEdit { background: %1; color: %2; border: 1px solid %3; "
                     "padding: 4px 8px; border-radius: 3px; }").arg(tc.bgPanel, tc.textPrimary, tc.border);

    auto applyEdit = [editStyle](QLineEdit* e) { if (e) e->setStyleSheet(editStyle); };
    applyEdit(m_idEdit); applyEdit(m_nameEdit); applyEdit(m_versionEdit);
    applyEdit(m_authorEdit); applyEdit(m_mainFileEdit);
    applyEdit(m_menuPathEdit); applyEdit(m_menuActionEdit);
    applyEdit(m_contextTypeEdit); applyEdit(m_contextActionEdit);

    if (m_descEdit) m_descEdit->setStyleSheet(QString("QTextEdit { background: %1; color: %2; border: 1px solid %3; padding: 4px; }").arg(tc.bgPanel, tc.textPrimary, tc.border));
    if (m_jsonPreview) m_jsonPreview->setStyleSheet(QString("QTextEdit { background: %1; color: %2; border: none; font-family: 'Consolas', monospace; font-size: 10pt; padding: 8px; }").arg(tc.bgEditor, tc.textPrimary));
    if (m_menuList) m_menuList->setStyleSheet(QString("QListWidget { background: %1; color: %2; border: 1px solid %3; }").arg(tc.bgPanel, tc.textPrimary, tc.border));
    if (m_contextList) m_contextList->setStyleSheet(QString("QListWidget { background: %1; color: %2; border: 1px solid %3; }").arg(tc.bgPanel, tc.textPrimary, tc.border));
    if (m_saveBtn) m_saveBtn->setStyleSheet(QString("QPushButton { background: %1; color: white; border: none; padding: 6px 16px; border-radius: 4px; font-weight: bold; }QPushButton:hover { background: %2; }").arg(tc.accentBlue, tc.green));
}

} // namespace IDE
