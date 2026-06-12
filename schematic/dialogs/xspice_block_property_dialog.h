/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef XSPICE_BLOCK_PROPERTY_DIALOG_H
#define XSPICE_BLOCK_PROPERTY_DIALOG_H

#include <QDialog>
#include <QJsonObject>

class QComboBox;
class QFormLayout;
class QLineEdit;
class QTextEdit;
class XspiceBlockItem;

class XspiceBlockPropertyDialog : public QDialog {
    Q_OBJECT
public:
    explicit XspiceBlockPropertyDialog(XspiceBlockItem* item, QWidget* parent = nullptr);

    QString modelType() const;
    QJsonObject xspiceParams() const;

private Q_SLOTS:
    void onModelTypeChanged(int index);
    void accept() override;

private:
    void rebuildParamForm();

    XspiceBlockItem* m_item;
    QComboBox* m_typeCombo;
    QFormLayout* m_paramLayout;
    QMap<QString, QWidget*> m_paramWidgets;
    QTextEdit* m_preview;
};

#endif
