/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EXTENSION_SCAFFOLD_DIALOG_H
#define EXTENSION_SCAFFOLD_DIALOG_H

#include <QDialog>

class QLineEdit;
class QTextEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace IDE {

class ExtensionScaffoldDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExtensionScaffoldDialog(QWidget* parent = nullptr);

    QString extensionId() const { return m_id; }
    QString extensionPath() const { return m_path; }

private slots:
    void onNext();
    void onBack();
    void onCreate();
    void onNameChanged(const QString& name);

private:
    void setupUI();
    void updatePreview();
    QString generateId(const QString& name) const;
    QString scaffoldManifest() const;
    QString scaffoldMain() const;

    QStackedWidget* m_stack = nullptr;

    // Step 1: Info
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_authorEdit = nullptr;
    QLineEdit* m_versionEdit = nullptr;
    QTextEdit* m_descEdit = nullptr;

    // Step 2: Template
    QComboBox* m_templateCombo = nullptr;
    QTextEdit* m_previewEdit = nullptr;

    // Step 3: Confirm
    QLabel* m_confirmLabel = nullptr;

    QPushButton* m_backBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QPushButton* m_createBtn = nullptr;

    QLabel* m_statusLabel = nullptr;

    QString m_id;
    QString m_path;
};

} // namespace IDE

#endif // EXTENSION_SCAFFOLD_DIALOG_H
