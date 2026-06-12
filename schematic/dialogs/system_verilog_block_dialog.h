/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SYSTEMVERILOGBLOCKDIALOG_H
#define SYSTEMVERILOGBLOCKDIALOG_H

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QPushButton;
class QListWidget;
class SystemVerilogBlockItem;

class SystemVerilogBlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit SystemVerilogBlockDialog(SystemVerilogBlockItem* item, QWidget* parent = nullptr);

    QString svFilePath() const;
    QString moduleName() const;

private Q_SLOTS:
    void onBrowseFile();
    void onAccept();

private:
    void extractPortsFromFile();

    SystemVerilogBlockItem* m_item;
    QLineEdit* m_filePathEdit;
    QLineEdit* m_moduleNameEdit;
    QListWidget* m_portList;
    QPushButton* m_browseBtn;

    QStringList m_inputPorts;
    QStringList m_outputPorts;
};

#endif // SYSTEMVERILOGBLOCKDIALOG_H
