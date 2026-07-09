/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMMAND_PALETTE_H
#define COMMAND_PALETTE_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>

namespace IDE {

struct PaletteCommand {
    QString name;
    QString shortcut;
    std::function<void()> action;
};

class CommandPalette : public QWidget {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr);

    void addCommand(const QString& name, const QString& shortcut, std::function<void()> action);
    void addSeparator(const QString& label);
    void showPalette();
    void hidePalette();
    bool isPaletteVisible() const;

signals:
    void commandExecuted();

private slots:
    void onFilterChanged(const QString& text);
    void onItemActivated(QListWidgetItem* item);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void paintEvent(QPaintEvent* e) override;

private:
    void applyTheme();
    void rebuildFilteredList(const QString& filter);
    void positionOverParent();

    QLineEdit* m_searchEdit = nullptr;
    QListWidget* m_listWidget = nullptr;
    QList<PaletteCommand> m_commands;
};

} // namespace IDE

#endif // COMMAND_PALETTE_H
