/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "command_registry.h"

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry reg;
    return reg;
}

void CommandRegistry::registerCommand(std::unique_ptr<CLICommand> cmd) {
    if (cmd) {
        m_commands[cmd->name()] = std::move(cmd);
    }
}

CLICommand* CommandRegistry::getCommand(const QString& name) const {
    auto it = m_commands.find(name);
    if (it != m_commands.end()) {
        return it->second.get();
    }
    return nullptr;
}

QList<CLICommand*> CommandRegistry::allCommands() const {
    QList<CLICommand*> list;
    for (const auto& pair : m_commands) {
        list.append(pair.second.get());
    }
    return list;
}

void registerSchematicCommands();
void registerNetlistCommands();
void registerSymbolCommands();
void registerFootprintCommands();
void registerPCBCommands();
void registerPluginCommands();
void registerExtCommands();
void registerExtTestCommand();
void registerExtPreviewCommand();
void registerExtSchemaCommand();
void registerExtWatchCommand();
void registerExtComposeCommand();
void registerMiscCommands();

void registerAllCommands() {
    registerSchematicCommands();
    registerNetlistCommands();
    registerSymbolCommands();
    registerFootprintCommands();
    registerPCBCommands();
    registerPluginCommands();
    registerExtCommands();
    registerExtTestCommand();
    registerExtPreviewCommand();
    registerExtSchemaCommand();
    registerExtWatchCommand();
    registerExtComposeCommand();
    registerMiscCommands();
}

