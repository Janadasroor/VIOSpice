// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 Janada Sroor

#include "waveform_expression_parser.h"
#include "waveform_math_processor.h"
#include <QRegularExpression>
#include <QDebug>
#include <limits>
#include <cmath>

bool WaveformExpressionParser::parseExpression(const QString &expression, const QStringList &availableSignalKeys, QStringList &signalNames, QString &error) {
    QRegularExpression vRe("V\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression iRe("I\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression pRe("P\\(([^)]+)\\)", QRegularExpression::CaseInsensitiveOption);
    
    QMap<QString, QStringList> exprPatterns;
    exprPatterns["V"] = QStringList();
    exprPatterns["I"] = QStringList();
    exprPatterns["P"] = QStringList();
    
    auto extractSignal = [&signalNames, &availableSignalKeys](const QString &type, const QString &net) -> bool {
        QString prefix = QString("%1(%2)").arg(type).arg(net);
        for (const QString &key : availableSignalKeys) {
            if (key.toLower() == prefix.toLower() || key.toLower() == net.toLower()) {
                QString storedNetName;
                QRegularExpression extractRe(QString("^%1\\((.+)\\)$").arg(type), QRegularExpression::CaseInsensitiveOption);
                QRegularExpressionMatch m = extractRe.match(key);
                if (m.hasMatch()) {
                    storedNetName = m.captured(1);
                } else {
                    storedNetName = key;
                }
                if (!signalNames.contains(storedNetName)) {
                    signalNames.append(storedNetName);
                    qDebug() << "parseExpression: added" << storedNetName << "to signalNames";
                }
                return true;
            }
        }
        return false;
    };
    
    QRegularExpressionMatchIterator itV = vRe.globalMatch(expression);
    while (itV.hasNext()) {
        QRegularExpressionMatch match = itV.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found V(" << net << ")";
        if (!extractSignal("V", net)) {
            error = QString("Signal 'V(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpressionMatchIterator itI = iRe.globalMatch(expression);
    while (itI.hasNext()) {
        QRegularExpressionMatch match = itI.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found I(" << net << ")";
        if (!extractSignal("I", net)) {
            error = QString("Signal 'I(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpressionMatchIterator itP = pRe.globalMatch(expression);
    while (itP.hasNext()) {
        QRegularExpressionMatch match = itP.next();
        QString net = match.captured(1);
        qDebug() << "parseExpression: found P(" << net << ")";
        if (!extractSignal("P", net)) {
            error = QString("Signal 'P(%1)' not found").arg(net);
            return false;
        }
    }
    
    QRegularExpression funcRe("(derivative|integral)\\(([VI])\\(([^)]+)\\)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator itFunc = funcRe.globalMatch(expression);
    while (itFunc.hasNext()) {
        QRegularExpressionMatch match = itFunc.next();
        QString func = match.captured(1).toLower();
        QString type = match.captured(2).toUpper();
        QString net = match.captured(3);
        qDebug() << "parseExpression: found" << func << "(" << type << "(" << net << "))";
        if (!extractSignal(type, net)) {
            error = QString("Signal '%1(%2)' not found").arg(type).arg(net);
            return false;
        }
    }
    
    qDebug() << "Parsed expression:" << expression << "found signals:" << signalNames;
    return true;
}

bool WaveformExpressionParser::evaluateExpression(const QString &expression, const QStringList &signalNames, const QMap<QString, SignalData> &allSignals, QVector<double> &time, QVector<double> &values) {
    qDebug() << "evaluateExpression: expression=" << expression << "signalNames=" << signalNames;
    if (signalNames.isEmpty()) {
        qDebug() << "evaluateExpression: no signal names provided, returning false";
        return false;
    }
    
    QMap<QString, QPair<QVector<double>, QVector<double>>> signalData;
    for (const QString &sig : signalNames) {
        QStringList prefixes = {sig, QString("V(%1)").arg(sig), QString("I(%1)").arg(sig), QString("P(%1)").arg(sig)};
        QString foundKey;
        for (const QString &key : prefixes) {
            if (allSignals.contains(key)) {
                foundKey = key;
                break;
            }
        }
        if (foundKey.isEmpty()) {
            qDebug() << "evaluateExpression: signal" << sig << "not found with any prefix";
            return false;
        }
        signalData[sig] = qMakePair(allSignals[foundKey].time, allSignals[foundKey].values);
        qDebug() << "evaluateExpression: found key" << foundKey << "with" << allSignals[foundKey].values.size() << "values";
    }
    
    int minSize = std::numeric_limits<int>::max();
    for (const QString &sig : signalNames) {
        if (!signalData.contains(sig)) {
            qDebug() << "evaluateExpression: signalData missing key" << sig;
            return false;
        }
        minSize = qMin(minSize, signalData[sig].first.size());
        minSize = qMin(minSize, signalData[sig].second.size());
    }
    
    if (minSize == 0 || minSize == std::numeric_limits<int>::max()) {
        qDebug() << "evaluateExpression: invalid minSize" << minSize;
        return false;
    }
    
    time = signalData[signalNames[0]].first.mid(0, minSize);
    values.resize(minSize);
    
    QMap<QString, QVector<double>> funcResults;
    QString expr = expression;
    
    QRegularExpression funcRe("(derivative|integral)\\(([VI])\\(([^)]+)\\)\\)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator itFunc = funcRe.globalMatch(expr);
    QVector<QPair<QString, QString>> funcPlaceholders;
    int funcIdx = 1000;
    while (itFunc.hasNext()) {
        QRegularExpressionMatch match = itFunc.next();
        QString func = match.captured(1).toLower();
        QString type = match.captured(2);
        QString net = match.captured(3);
        QString funcKey = match.captured(0);
        
        if (funcResults.contains(funcKey)) continue;
        
        QStringList prefixes = {net, QString("%1(%2)").arg(type).arg(net)};
        QString sigKey;
        for (const QString &k : prefixes) {
            if (signalData.contains(k)) {
                sigKey = k;
                break;
            }
        }
        if (!sigKey.isEmpty()) {
            QVector<double> result;
            if (func == "derivative") {
                result = WaveformMathProcessor::computeDerivative(signalData[sigKey].first, signalData[sigKey].second);
            } else if (func == "integral") {
                result = WaveformMathProcessor::computeIntegral(signalData[sigKey].first, signalData[sigKey].second);
            }
            if (!result.isEmpty()) {
                funcResults[funcKey] = result;
                QString placeholder = QString("f%1").arg(funcIdx++);
                funcPlaceholders.append({funcKey, placeholder});
            }
        }
    }
    
    for (const auto &p : funcPlaceholders) {
        expr.replace(p.first, p.second);
    }
    
    for (int i = 0; i < signalNames.size(); ++i) {
        QStringList patterns = {
            QString("V\\(%1\\)").arg(QRegularExpression::escape(signalNames[i])),
            QString("I\\(%1\\)").arg(QRegularExpression::escape(signalNames[i])),
            QString("P\\(%1\\)").arg(QRegularExpression::escape(signalNames[i]))
        };
        for (const QString &pattern : patterns) {
            QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
            expr.replace(re, QString("s%1").arg(i));
        }
    }
    
    qDebug() << "evaluateExpression: transformed expr=" << expr;
    qDebug() << "evaluateExpression: minSize=" << minSize << "time.size=" << time.size();
    
    QVector<QVector<double>> signalVectors;
    signalVectors.reserve(signalNames.size());
    for (int idx = 0; idx < signalNames.size(); ++idx) {
        const QString &sig = signalNames[idx];
        if (!signalData.contains(sig)) {
            qDebug() << "evaluateExpression: ERROR - signalData missing during vector creation";
            return false;
        }
        signalVectors.append(signalData[sig].second.mid(0, minSize));
        if (signalVectors.last().size() != minSize) {
            qDebug() << "evaluateExpression: WARNING - vector size mismatch";
        }
        qDebug() << "evaluateExpression: signal" << idx << "(" << sig << ") size=" << signalVectors.last().size() 
                 << "first 3 values:" << (signalVectors.last().size() > 0 ? signalVectors.last()[0] : 0) 
                 << (signalVectors.last().size() > 1 ? signalVectors.last()[1] : 0)
                 << (signalVectors.last().size() > 2 ? signalVectors.last()[2] : 0);
    }
    
    for (int i = 0; i < minSize; ++i) {
        QString eval = expr;
        
        // Simple left-to-right evaluation for basic arithmetic
        QStringList tokens;
        QList<QChar> operators;
        
        // Tokenize the expression
        QString current = "";
        for (int pos = 0; pos < eval.length(); ++pos) {
            QChar c = eval[pos];
            if (c == '+' || c == '-' || c == '*' || c == '/') {
                if (!current.isEmpty()) {
                    tokens.append(current.trimmed());
                    current = "";
                }
                operators.append(c);
            } else {
                current += c;
            }
        }
        if (!current.isEmpty()) {
            tokens.append(current.trimmed());
        }
        
        // Evaluate tokens
        if (tokens.isEmpty()) {
            values[i] = 0.0;
            continue;
        }
        
        // First pass: handle * and /
        QList<double> numbers;
        QList<QChar> remainingOps;
        
        // Validate token/operator consistency
        if (operators.size() >= tokens.size()) {
            qDebug() << "evaluateExpression: ERROR - operators.size()" << operators.size() 
                     << ">= tokens.size()" << tokens.size() << "for eval=" << eval;
            values[i] = 0.0;
            continue;
        }
        
        auto resolveToken = [&](const QString &token) -> double {
            bool ok;
            double val = token.toDouble(&ok);
            if (ok) return val;
            
            QRegularExpression sigRe("s(\\d+)");
            QRegularExpressionMatch match = sigRe.match(token);
            if (match.hasMatch()) {
                int idx = match.captured(1).toInt();
                if (idx >= 0 && idx < signalVectors.size() && i < signalVectors[idx].size()) {
                    return signalVectors[idx][i];
                }
            }
            
            QRegularExpression funcRe("f(\\d+)");
            match = funcRe.match(token);
            if (match.hasMatch()) {
                int idx = match.captured(1).toInt() - 1000;
                if (idx >= 0 && idx < funcResults.size()) {
                    return funcResults.values()[idx][qMin(i, funcResults.values()[idx].size() - 1)];
                }
            }
            return 0.0;
        };
        
        numbers.append(resolveToken(tokens[0]));
        
        for (int k = 0; k < operators.size(); ++k) {
            QChar op = operators[k];
            double nextNum = (k + 1 < tokens.size()) ? resolveToken(tokens[k+1]) : 0.0;
            
            if (op == '*') {
                double prev = numbers.last();
                numbers.removeLast();
                numbers.append(prev * nextNum);
            } else if (op == '/') {
                if (qFuzzyIsNull(nextNum)) {
                    numbers.append(0.0);
                } else {
                    double prev = numbers.last();
                    numbers.removeLast();
                    numbers.append(prev / nextNum);
                }
            } else {
                numbers.append(nextNum);
                remainingOps.append(op);
            }
        }
        
        // Second pass: handle + and -
        double result = numbers.isEmpty() ? 0.0 : numbers[0];
        for (int k = 0; k < remainingOps.size(); ++k) {
            if (k + 1 < numbers.size()) {
                if (remainingOps[k] == '+') {
                    result += numbers[k+1];
                } else if (remainingOps[k] == '-') {
                    result -= numbers[k+1];
                }
            }
        }
        
        if (!std::isfinite(result)) {
            result = 0.0;
        }
        
        if (i < 3) {
            qDebug() << "evaluateExpression: i=" << i << "eval=" << eval << "result=" << result;
        }
        values[i] = result;
    }
    
    return true;
}

double WaveformExpressionParser::evaluateSimpleMath(const QString &expr, bool &ok) {
    qDebug() << "evaluateSimpleMath called with:" << expr;
    ok = true;
    QString e = expr.trimmed();
    
    if (e.isEmpty()) {
        ok = false;
        qDebug() << "evaluateSimpleMath: empty expression";
        return 0;
    }
    
    // First, look for + or - (lowest precedence), leftmost for left-to-right associativity
    int pos = -1;
    QChar op;
    for (int i = 0; i < e.length(); ++i) {
        if (e[i] == '+' || e[i] == '-') {
            // Skip if it's the first character (could be a negative number)
            if (i == 0) continue;
            pos = i;
            op = e[i];
            break; // Take the first (leftmost) + or -
        }
    }
    
    if (pos != -1) {
        QString left = e.left(pos).trimmed();
        QString right = e.mid(pos + 1).trimmed();
        
        bool leftOk, rightOk;
        double leftVal = evaluateSimpleMath(left, leftOk);
        double rightVal = evaluateSimpleMath(right, rightOk);
        
        if (!leftOk || !rightOk) {
            ok = false;
            qDebug() << "evaluateSimpleMath: failed to evaluate left or right side of" << op;
            return 0;
        }
        
        if (op == '+') {
            double result = leftVal + rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " + " << rightVal << " = " << result;
            return result;
        } else { // '-'
            double result = leftVal - rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " - " << rightVal << " = " << result;
            return result;
        }
    }
    
    // No + or - found, look for * or / (higher precedence), leftmost for left-to-right associativity
    for (int i = 0; i < e.length(); ++i) {
        if (e[i] == '*' || e[i] == '/') {
            pos = i;
            op = e[i];
            break; // Take the first (leftmost) * or /
        }
    }
    
    if (pos != -1) {
        QString left = e.left(pos).trimmed();
        QString right = e.mid(pos + 1).trimmed();
        
        bool leftOk, rightOk;
        double leftVal = evaluateSimpleMath(left, leftOk);
        double rightVal = evaluateSimpleMath(right, rightOk);
        
        if (!leftOk || !rightOk) {
            ok = false;
            qDebug() << "evaluateSimpleMath: failed to evaluate left or right side of" << op;
            return 0;
        }
        
        if (op == '*') {
            double result = leftVal * rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " * " << rightVal << " = " << result;
            return result;
        } else { // '/'
            double denominator = rightVal;
            if (qFuzzyIsNull(denominator)) {
                ok = false;
                qDebug() << "evaluateSimpleMath: division by zero";
                return 0;
            }
            double result = leftVal / rightVal;
            qDebug() << "evaluateSimpleMath: " << leftVal << " / " << rightVal << " = " << result;
            return result;
        }
    }
    
    // No operators found, must be a number
    bool conversionOk;
    double val = e.toDouble(&conversionOk);
    if (!conversionOk) {
        ok = false;
        qDebug() << "evaluateSimpleMath: not a valid number:" << e;
        return 0;
    }
    qDebug() << "evaluateSimpleMath: parsed number" << val;
    return val;
}
