/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IDE_DEBUGGER_H
#define IDE_DEBUGGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVariant>

namespace IDE {

struct Breakpoint {
    QString filePath;
    int line;
    bool enabled = true;
    QString condition; // Optional conditional expression
};

struct StackFrame {
    QString functionName;
    QString filePath;
    int line;
};

struct Variable {
    QString name;
    QString value;
    QString type;
};

class IdeDebugger : public QObject {
    Q_OBJECT
public:
    explicit IdeDebugger(QObject* parent = nullptr);

    // Session lifecycle
    bool start(const QString& filePath);
    void stop();
    bool isRunning() const;
    bool isPaused() const;

    // Breakpoint management
    void addBreakpoint(const QString& filePath, int line);
    void addConditionalBreakpoint(const QString& filePath, int line, const QString& condition);
    void removeBreakpoint(const QString& filePath, int line);
    void toggleBreakpoint(const QString& filePath, int line);
    void clearBreakpoints();
    QList<Breakpoint> breakpoints() const;
    bool checkBreakpointCondition(const Breakpoint& bp);

    // Execution control
    void continueExecution();
    void stepOver();
    void stepInto();
    void stepOut();
    void runToLine(int line);

    // Variable inspection
    QList<Variable> variables() const;
    QString variableValue(const QString& name);
    void setVariable(const QString& name, const QString& value);
    QString evaluateExpression(const QString& expr);

    // Stack access
    QList<StackFrame> stackTrace() const;
    QString currentFile() const;
    int currentLine() const;
    QString currentFunction() const;

    // Configuration
    void setBreakOnExceptions(bool enabled);
    void setStepSkipSystemCode(bool enabled);

    // State
    enum State { Inactive, Starting, Running, Paused, Stepping, Terminated };
    State state() const { return m_state; }
    QString stateString() const;

signals:
    void stateChanged(State newState);
    void breakpointHit(const QString& filePath, int line);
    void exceptionThrown(const QString& message);
    void outputReceived(const QString& text);
    void variablesUpdated();
    void stopped();
    void currentLocationChanged();
    void stackFrameChanged();

private:
    State m_state = Inactive;
    QString m_filePath;
    int m_currentLine = 0;
    QString m_currentFunction;
    QMap<QString, QList<Breakpoint>> m_breakpoints;
    bool m_breakOnExceptions = true;
    bool m_stepSkipSystemCode = true;
};

} // namespace IDE

#endif // IDE_DEBUGGER_H
