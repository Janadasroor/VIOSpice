/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QCommandLineParser>
#include <QJsonObject>
#include <map>
#include <memory>

class CLICommand {
public:
    virtual ~CLICommand() = default;
    
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    
    // Configures QCommandLineParser options
    virtual void setupParser(QCommandLineParser& parser) = 0;
    virtual QJsonObject inputSchema() const = 0;
    virtual QJsonObject outputSchema() const = 0;
    
    // Executed when subcommand is triggered
    virtual int execute(const QStringList& args, const QCommandLineParser& parser) = 0;
};

class CommandRegistry {
public:
    static CommandRegistry& instance();
    
    void registerCommand(std::unique_ptr<CLICommand> cmd);
    CLICommand* getCommand(const QString& name) const;
    QList<CLICommand*> allCommands() const;

private:
    CommandRegistry() = default;
    std::map<QString, std::unique_ptr<CLICommand>> m_commands;
};

void registerAllCommands();

