/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MANIFEST_EDITOR_PANEL_H
#define MANIFEST_EDITOR_PANEL_H

#include <QWidget>
#include <QJsonObject>

class QLineEdit;
class QTextEdit;
class QListWidget;
class QLabel;
class QPushButton;
class QSplitter;

namespace IDE {

class ManifestEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ManifestEditorPanel(QWidget* parent = nullptr);
    void reapplyTheme();

    void loadManifest(const QString& manifestPath);
    bool saveManifest();
    bool isModified() const { return m_modified; }
    QString manifestPath() const { return m_manifestPath; }

signals:
    void modifiedChanged(bool modified);
    void saved(const QString& path);

private slots:
    void onFieldChanged();
    void onAddMenuEntry();
    void onRemoveMenuEntry();
    void onAddContext();
    void onRemoveContext();
    void onSaveClicked();

private:
    void setupUI();
    void loadFromJson(const QJsonObject& obj);
    QJsonObject toJson() const;
    void updateJsonPreview();
    void setModified(bool mod);

    QString m_manifestPath;
    bool m_modified = false;

    // Form fields
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_versionEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_mainFileEdit = nullptr;
    QTextEdit* m_descEdit = nullptr;

    // Menu entries
    QListWidget* m_menuList = nullptr;
    QLineEdit* m_menuPathEdit = nullptr;
    QLineEdit* m_menuActionEdit = nullptr;

    // Contexts
    QListWidget* m_contextList = nullptr;
    QLineEdit* m_contextTypeEdit = nullptr;
    QLineEdit* m_contextActionEdit = nullptr;

    // JSON preview
    QTextEdit* m_jsonPreview = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_saveBtn = nullptr;
};

} // namespace IDE

#endif // MANIFEST_EDITOR_PANEL_H
