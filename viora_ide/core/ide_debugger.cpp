/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ide_debugger.h"
#include <QTimer>
#include <QDebug>
#include <QRegularExpression>

namespace IDE {

IdeDebugger::IdeDebugger(QObject* parent)
    : QObject(parent) {
}

// ============================================================================
// Session lifecycle
// ============================================================================

bool IdeDebugger::start(const QString& filePath) {
    if (m_state == Running || m_state == Paused) return false;
    m_filePath = filePath;
    m_currentLine = 0;
    m_currentFunction.clear();
    m_state = Starting;
    emit stateChanged(m_state);
    emit outputReceived(QString("[Debug] Starting debug session for %1").arg(filePath));
    m_state = Running;
    emit stateChanged(m_state);
    return true;
}

void IdeDebugger::stop() {
    m_state = Terminated;
    m_currentLine = 0;
    m_currentFunction.clear();
    emit stateChanged(m_state);
    emit outputReceived("[Debug] Session terminated");
}

bool IdeDebugger::isRunning() const { return m_state == Running || m_state == Stepping; }
bool IdeDebugger::isPaused() const { return m_state == Paused; }

// ============================================================================
// Breakpoint management
// ============================================================================

void IdeDebugger::addBreakpoint(const QString& filePath, int line) {
    Breakpoint bp;
    bp.filePath = filePath;
    bp.line = line;
    bp.enabled = true;
    m_breakpoints[filePath].append(bp);
    emit outputReceived(QString("[Debug] Breakpoint at %1:%2").arg(filePath).arg(line));
}

void IdeDebugger::addConditionalBreakpoint(const QString& filePath, int line, const QString& condition) {
    Breakpoint bp;
    bp.filePath = filePath;
    bp.line = line;
    bp.enabled = true;
    bp.condition = condition;
    m_breakpoints[filePath].append(bp);
    emit outputReceived(QString("[Debug] Conditional breakpoint at %1:%2 — if %3").arg(filePath).arg(line).arg(condition));
}

bool IdeDebugger::checkBreakpointCondition(const Breakpoint& bp) {
    if (bp.condition.isEmpty()) return true;

    // Parse "name op value" pattern: x == 5, x > 10, x < 0, x != 0
    static QRegularExpression re(R"(^\s*(\w+)\s*(==|!=|>=|<=|>|<)\s*(.+)$)");
    QRegularExpressionMatch match = re.match(bp.condition.trimmed());

    if (match.hasMatch()) {
        QString varName = match.captured(1);
        QString op = match.captured(2);
        QString rhsStr = match.captured(3).trimmed();

        QString varValue = variableValue(varName);
        if (varValue.isEmpty()) return false;

        bool ok;
        double lhs = varValue.toDouble(&ok);
        if (!ok) return true;
        double rhs = rhsStr.toDouble(&ok);
        if (!ok) return varValue == rhsStr;

        if (op == "==") return qFuzzyCompare(lhs, rhs);
        if (op == "!=") return !qFuzzyCompare(lhs, rhs);
        if (op == ">") return lhs > rhs;
        if (op == "<") return lhs < rhs;
        if (op == ">=") return lhs >= rhs;
        if (op == "<=") return lhs <= rhs;
    }
    return true;
}

void IdeDebugger::removeBreakpoint(const QString& filePath, int line) {
    auto& bps = m_breakpoints[filePath];
    for (int i = bps.size() - 1; i >= 0; --i) {
        if (bps[i].line == line) { bps.removeAt(i); break; }
    }
}

void IdeDebugger::toggleBreakpoint(const QString& filePath, int line) {
    auto& bps = m_breakpoints[filePath];
    for (int i = 0; i < bps.size(); ++i) {
        if (bps[i].line == line) { bps.removeAt(i); return; }
    }
    addBreakpoint(filePath, line);
}

void IdeDebugger::clearBreakpoints() { m_breakpoints.clear(); }

QList<Breakpoint> IdeDebugger::breakpoints() const {
    QList<Breakpoint> result;
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it)
        result.append(it.value());
    return result;
}

// ============================================================================
// Execution control
// ============================================================================

void IdeDebugger::continueExecution() {
    if (m_state != Paused && m_state != Stepping) return;
    m_state = Running;
    emit stateChanged(m_state);
}

void IdeDebugger::stepOver() {
    if (m_state != Paused && m_state != Stepping) return;
    m_state = Stepping;
    m_currentLine++;
    emit currentLocationChanged();
    emit stateChanged(m_state);
}

void IdeDebugger::stepInto() {
    if (m_state != Paused && m_state != Stepping) return;
    m_state = Stepping;
    m_currentLine++;
    emit currentLocationChanged();
    emit stateChanged(m_state);
}

void IdeDebugger::stepOut() {
    if (m_state != Paused && m_state != Stepping) return;
    m_state = Stepping;
    m_currentFunction.clear();
    emit stackFrameChanged();
    emit stateChanged(m_state);
}

void IdeDebugger::runToLine(int line) {
    if (m_state != Paused) return;
    m_currentLine = line;
    m_state = Running;
    emit stateChanged(m_state);
}

// ============================================================================
// Variable inspection
// ============================================================================

QList<Variable> IdeDebugger::variables() const { return {}; }
QString IdeDebugger::variableValue(const QString& name) { Q_UNUSED(name); return QString(); }
void IdeDebugger::setVariable(const QString& name, const QString& value) { Q_UNUSED(name); Q_UNUSED(value); }
QString IdeDebugger::evaluateExpression(const QString& expr) { Q_UNUSED(expr); return QString(); }

// ============================================================================
// Stack access
// ============================================================================

QList<StackFrame> IdeDebugger::stackTrace() const {
    StackFrame frame;
    frame.functionName = m_currentFunction;
    frame.filePath = m_filePath;
    frame.line = m_currentLine;
    return {frame};
}

QString IdeDebugger::currentFile() const { return m_filePath; }
int IdeDebugger::currentLine() const { return m_currentLine; }
QString IdeDebugger::currentFunction() const { return m_currentFunction; }

// ============================================================================
// Configuration
// ============================================================================

void IdeDebugger::setBreakOnExceptions(bool enabled) { m_breakOnExceptions = enabled; }
void IdeDebugger::setStepSkipSystemCode(bool enabled) { m_stepSkipSystemCode = enabled; }

QString IdeDebugger::stateString() const {
    switch (m_state) {
        case Inactive: return "Inactive";
        case Starting: return "Starting";
        case Running: return "Running";
        case Paused: return "Paused";
        case Stepping: return "Stepping";
        case Terminated: return "Terminated";
    }
    return "Unknown";
}

} // namespace IDE
