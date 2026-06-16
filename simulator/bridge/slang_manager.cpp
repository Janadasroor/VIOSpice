/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "slang_manager.h"

#include <slang/syntax/SyntaxTree.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Scope.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/symbols/BlockSymbols.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Expression.h>
#include <slang/ast/Statement.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/LiteralExpressions.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/TextDiagnosticClient.h>
#include <slang/text/SourceManager.h>

#include <slang/ast/statements/MiscStatements.h>
#include <slang/ast/statements/ConditionalStatements.h>

#include <QDebug>
#include <QMap>
#include <cmath>
#include <cstring>
#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════════
// EvalNode implementations (global namespace — structs declared in header)
// ═══════════════════════════════════════════════════════════════════════════

double MultiBitInputNode::eval(const double* inputs) const {
    double v = 0.0;
    for (int i = 0; i < width; i++)
        if (inputs[baseIndex + i] > 2.5)
            v += std::pow(2.0, i);
    return v;
}

double BitSliceNode::eval(const double* inputs) const {
    double full = source->eval(inputs);
    int64_t bits = static_cast<int64_t>(std::llround(full));
    double extracted = static_cast<double>((bits >> lsb) & ((INT64_C(1) << count) - 1));
    return extracted;
}

double ElementSelectNode::eval(const double* inputs) const {
    double full = base->eval(inputs);
    int64_t bits = static_cast<int64_t>(std::llround(full));
    return static_cast<double>((bits >> bitIndex) & 1);
}

double ConcatNode::eval(const double* inputs) const {
    // Compute total width
    int totalWidth = 0;
    for (auto w : partWidths) totalWidth += w;

    // Leftmost part goes to MSB
    uint64_t result = 0;
    int bitOffset = totalWidth;
    for (size_t i = 0; i < parts.size(); i++) {
        double pv = parts[i]->eval(inputs);
        uint64_t pbits = static_cast<uint64_t>(std::llround(pv));
        bitOffset -= partWidths[i];
        for (int b = 0; b < partWidths[i]; b++) {
            if ((pbits >> b) & 1)
                result |= (1ULL << (bitOffset + b));
        }
    }
    return static_cast<double>(result);
}

double TernaryOpNode::eval(const double* inputs) const {
    double c = cond->eval(inputs);
    return (c > 0.5) ? trueExpr->eval(inputs) : falseExpr->eval(inputs);
}

double BinaryOpNode::eval(const double* inputs) const {
    double l = lhs->eval(inputs);
    double r = rhs->eval(inputs);
    switch (op) {
        case BinOp::And: return (double)((int64_t)l & (int64_t)r);
        case BinOp::Or:  return (double)((int64_t)l | (int64_t)r);
        case BinOp::Xor: return (double)((int64_t)l ^ (int64_t)r);
        case BinOp::Add: return l + r;
        case BinOp::Sub: return l - r;
        case BinOp::Mul: return l * r;
        case BinOp::Div: return r != 0.0 ? l / r : 0.0;
        case BinOp::Eq:  return (l == r) ? 1.0 : 0.0;
        case BinOp::Neq: return (l != r) ? 1.0 : 0.0;
        case BinOp::Gt:  return (l > r) ? 1.0 : 0.0;
        case BinOp::Lt:  return (l < r) ? 1.0 : 0.0;
    }
    return 0.0;
}

double UnaryOpNode::eval(const double* inputs) const {
    double v = operand->eval(inputs);
    switch (op) {
        case UnaryOp::Not:    return (v > 0.5) ? 0.0 : 1.0;
        case UnaryOp::Negate: return -v;
    }
    return 0.0;
}

// ── DffNode ────────────────────────────────────────────────────────────────

DffNode::DffNode(int clk, std::unique_ptr<EvalNode> d, int rst, int edge, double thresh)
    : clkIndex(clk), dExpr(std::move(d)), rstIndex(rst),
      edgeType(edge), threshold(thresh),
      savedClk(0.0), savedRst(0.0), capturedQ(0.0), lastTime(-1.0) {}

double DffNode::eval(const double* inputs) const {
    double clk = inputs[clkIndex];

    // Run edge detection on EVERY call.
    // savedClk is always updated to the latest clk value.
    // This is safe because:
    // 1. The derivative perturbation in cfunc.c changes clk by ~1e-6V,
    //    which doesn't affect future threshold comparisons.
    // 2. Multiple NR iterations within a timestep each call eval, but
    //    only the FIRST one sees a real edge (subsequent ones see clk
    //    already above/below threshold).

    bool activeEdge = false;
    if (edgeType == 0) // PosEdge
        activeEdge = (savedClk < threshold && clk >= threshold);
    else // NegEdge
        activeEdge = (savedClk >= threshold && clk < threshold);

    // Async reset: posedge rst → force Q=0
    if (rstIndex >= 0) {
        double rst = inputs[rstIndex];
        bool rstEdge = (savedRst < threshold && rst >= threshold);
        if (rstEdge) {
            capturedQ = 0.0;
            savedClk = clk;
            savedRst = rst;
            return capturedQ;
        }
        // On clock edge, if rst is high, also force Q=0 (synchronous reset)
        if (activeEdge && rst >= threshold) {
            capturedQ = 0.0;
            savedClk = clk;
            savedRst = rst;
            return capturedQ;
        }
        savedRst = rst;
    }

    if (activeEdge && dExpr) {
        capturedQ = dExpr->eval(inputs) > 0.5 ? 5.0 : 0.0;
    }

    savedClk = clk;
    return capturedQ;
}

std::unique_ptr<EvalNode> DffNode::clone() const {
    auto cloned = std::make_unique<DffNode>(clkIndex, dExpr ? dExpr->clone() : nullptr, rstIndex, edgeType, threshold);
    cloned->savedClk = savedClk;
    cloned->capturedQ = capturedQ;
    cloned->savedRst = savedRst;
    cloned->lastTime = lastTime;
    return cloned;
}

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// EvalNode tree builder — visits Slang AST and builds EvalNode hierarchy
// ═══════════════════════════════════════════════════════════════════════════

class EvalNodeBuilder : public slang::ast::ASTVisitor<EvalNodeBuilder, slang::ast::VisitFlags::Expressions> {
public:
    std::unique_ptr<EvalNode> result;
    const QMap<std::string, int>& inputMap;
    const QMap<std::string, int>& inputWidths; // port name → bit width

    EvalNodeBuilder(const QMap<std::string, int>& inputs,
                    const QMap<std::string, int>& widths = {})
        : inputMap(inputs), inputWidths(widths) {}

    template<typename T>
    void visit(const T& t) {
        slang::ast::ASTVisitor<EvalNodeBuilder, slang::ast::VisitFlags::Expressions>::visit(t);
    }

    void handle(const slang::ast::BinaryExpression& expr) {
        auto leftBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.left().visit(*leftBuilder);
        auto rightBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.right().visit(*rightBuilder);

        BinOp op;
        switch (expr.op) {
            case slang::ast::BinaryOperator::BinaryAnd:
            case slang::ast::BinaryOperator::LogicalAnd:
                op = BinOp::And; break;
            case slang::ast::BinaryOperator::BinaryOr:
            case slang::ast::BinaryOperator::LogicalOr:
                op = BinOp::Or; break;
            case slang::ast::BinaryOperator::BinaryXor:
                op = BinOp::Xor; break;
            case slang::ast::BinaryOperator::Add:
                op = BinOp::Add; break;
            case slang::ast::BinaryOperator::Subtract:
                op = BinOp::Sub; break;
            case slang::ast::BinaryOperator::Multiply:
                op = BinOp::Mul; break;
            case slang::ast::BinaryOperator::Divide:
                op = BinOp::Div; break;
            case slang::ast::BinaryOperator::Equality:
                op = BinOp::Eq; break;
            case slang::ast::BinaryOperator::Inequality:
                op = BinOp::Neq; break;
            case slang::ast::BinaryOperator::GreaterThan:
                op = BinOp::Gt; break;
            case slang::ast::BinaryOperator::LessThan:
                op = BinOp::Lt; break;
            default:
                result = std::make_unique<LiteralNode>(0.0);
                return;
        }
        result = std::make_unique<BinaryOpNode>(
            std::move(leftBuilder->result),
            std::move(rightBuilder->result),
            op);
    }

    void handle(const slang::ast::UnaryExpression& expr) {
        auto operandBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.operand().visit(*operandBuilder);

        switch (expr.op) {
            case slang::ast::UnaryOperator::BitwiseNot:
            case slang::ast::UnaryOperator::LogicalNot:
                result = std::make_unique<UnaryOpNode>(
                    std::move(operandBuilder->result), UnaryOp::Not);
                break;
            case slang::ast::UnaryOperator::Minus:
                result = std::make_unique<UnaryOpNode>(
                    std::move(operandBuilder->result), UnaryOp::Negate);
                break;
            default:
                result = std::move(operandBuilder->result);
                break;
        }
    }

    void handle(const slang::ast::NamedValueExpression& expr) {
        std::string name(expr.symbol.name);
        auto wit = inputWidths.find(name);
        if (wit != inputWidths.end() && wit.value() > 1) {
            // Multi-bit input: reconstruct integer from individual bits
            auto it = inputMap.find(name);
            if (it != inputMap.end()) {
                result = std::make_unique<MultiBitInputNode>(it.value(), wit.value());
            } else {
                result = std::make_unique<LiteralNode>(0.0);
            }
        } else if (inputMap.contains(name)) {
            result = std::make_unique<InputThresholdNode>(inputMap[name]);
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    void handle(const slang::ast::IntegerLiteral& expr) {
        auto val = expr.getValue();
        double dval = 0.0;
        if (val.isSigned()) {
            dval = static_cast<double>(static_cast<int64_t>(val.getRawPtr()[0]));
        } else {
            dval = static_cast<double>(val.getRawPtr()[0]);
        }
        result = std::make_unique<LiteralNode>(dval);
    }

    // Conditional/ternary: cond ? left : right
    void handle(const slang::ast::ConditionalExpression& expr) {
        if (expr.conditions.empty()) {
            result = std::make_unique<LiteralNode>(0.0);
            return;
        }
        auto condBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.conditions[0].expr->visit(*condBuilder);
        auto trueBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.left().visit(*trueBuilder);
        auto falseBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.right().visit(*falseBuilder);

        if (condBuilder->result && trueBuilder->result && falseBuilder->result) {
            result = std::make_unique<TernaryOpNode>(
                std::move(condBuilder->result),
                std::move(trueBuilder->result),
                std::move(falseBuilder->result));
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    // Element select: a[idx] — extracts one bit from multi-bit value
    void handle(const slang::ast::ElementSelectExpression& expr) {
        auto baseBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.value().visit(*baseBuilder);
        auto idxBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.selector().visit(*idxBuilder);

        if (baseBuilder->result && idxBuilder->result) {
            // For constant index, use ElementSelectNode directly
            auto* lit = dynamic_cast<LiteralNode*>(idxBuilder->result.get());
            if (lit) {
                int bitIdx = static_cast<int>(lit->value);
                result = std::make_unique<ElementSelectNode>(
                    std::move(baseBuilder->result), bitIdx);
            } else {
                // Variable index: evaluate index at runtime
                // For simplicity, store the index expression and evaluate
                result = std::make_unique<ElementSelectNode>(
                    std::move(baseBuilder->result), 0);
            }
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    // Range select: a[3:0] — extracts bits [msb:lsb]
    void handle(const slang::ast::RangeSelectExpression& expr) {
        auto baseBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.value().visit(*baseBuilder);

        int lsb = 0, count = 1;
        // Try to get constant range
        auto* leftLit = dynamic_cast<LiteralNode*>(
            [&]() -> std::unique_ptr<EvalNode> {
                auto b = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
                expr.left().visit(*b);
                return std::move(b->result);
            }().get());
        auto* rightLit = dynamic_cast<LiteralNode*>(
            [&]() -> std::unique_ptr<EvalNode> {
                auto b = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
                expr.right().visit(*b);
                return std::move(b->result);
            }().get());

        if (leftLit && rightLit) {
            int left = static_cast<int>(leftLit->value);
            int right = static_cast<int>(rightLit->value);
            lsb = std::min(left, right);
            count = std::abs(left - right) + 1;
        }

        if (baseBuilder->result) {
            result = std::make_unique<BitSliceNode>(
                std::move(baseBuilder->result), lsb, count);
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    // Concatenation: {a, b} — combines values into wider word
    void handle(const slang::ast::ConcatenationExpression& expr) {
        std::vector<std::unique_ptr<EvalNode>> parts;
        std::vector<int> widths;

        for (auto* operand : expr.operands()) {
            auto partBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
            operand->visit(*partBuilder);
            if (partBuilder->result) {
                parts.push_back(std::move(partBuilder->result));
                int w = 1;
                if (operand->type)
                    w = std::max(1, (int)operand->type->getBitWidth());
                widths.push_back(w);
            }
        }

        if (!parts.empty()) {
            result = std::make_unique<ConcatNode>(std::move(parts), std::move(widths));
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    // Conversion expression — unwrap and recurse
    void handle(const slang::ast::ConversionExpression& expr) {
        expr.operand().visit(*this);
    }

    // For any unrecognized expression, fall back to a literal 0
    void handle(const slang::ast::Expression& expr) {
        result = std::make_unique<LiteralNode>(0.0);
    }
};

// ── Helper: process an assignment expression (from ContinuousAssign or always_comb) ──
// Builds the RHS eval node, then for each bit on the LHS creates a CompiledOutput.
struct AssignmentProcessor {
    const QMap<std::string, int>& inputMap;
    const QMap<std::string, int>& inputWidths;
    std::vector<SlangManager::CompiledOutput>& outputs;
    const QString& ref;  // component reference prefix for naming

    void process(const slang::ast::AssignmentExpression& expr) {
        auto rhsBuilder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.right().visit(*rhsBuilder);
        if (!rhsBuilder->result) return;

        auto baseExpr = std::move(rhsBuilder->result);

        // Parse LHS to determine output bit positions
        processLHS(expr.left(), std::move(baseExpr));
    }

    void processLHS(const slang::ast::Expression& lhsExpr, std::unique_ptr<EvalNode> rhsExpr) {
        switch (lhsExpr.kind) {
            case slang::ast::ExpressionKind::NamedValue: {
                auto& named = lhsExpr.as<slang::ast::NamedValueExpression>();
                QString portName = QString::fromStdString(std::string(named.symbol.name));
                int width = std::max(1, (int)named.symbol.getType().getBitWidth());

                if (width == 1) {
                    SlangManager::CompiledOutput co;
                    co.outputPin = portName;
                    co.outputPort = portName;
                    co.bitOffset = 0;
                    co.bitCount = 1;
                    co.expr = std::make_unique<VoltageOutputNode>(
                        std::make_unique<BitSliceNode>(
                            rhsExpr->clone(), 0, 1));
                    outputs.push_back(std::move(co));
                } else {
                    // Multi-bit output: one A-device per bit
                    for (int b = 0; b < width; b++) {
                        SlangManager::CompiledOutput co;
                        co.outputPin = QString("%1_%2").arg(portName).arg(b);
                        co.outputPort = portName;
                        co.bitOffset = b;
                        co.bitCount = 1;
                        co.expr = std::make_unique<VoltageOutputNode>(
                            std::make_unique<BitSliceNode>(
                                rhsExpr->clone(), b, 1));
                        outputs.push_back(std::move(co));
                    }
                }
                break;
            }
            case slang::ast::ExpressionKind::ElementSelect: {
                // led[0] or led[idx_expr]
                auto& esel = lhsExpr.as<slang::ast::ElementSelectExpression>();
                QString baseName = QString::fromStdString(
                    std::string(esel.value().as<slang::ast::NamedValueExpression>().symbol.name));
                // Evaluate index
                int bitIdx = 0;
                if (auto* idxLit = esel.selector().as_if<slang::ast::IntegerLiteral>())
                    bitIdx = static_cast<int>(idxLit->getValue().getRawPtr()[0]);
                int portWidth = std::max(1, (int)esel.value().type->getBitWidth());
                SlangManager::CompiledOutput co;
                if (portWidth == 1)
                    co.outputPin = baseName;
                else
                    co.outputPin = QString("%1_%2").arg(baseName).arg(bitIdx);
                co.outputPort = baseName;
                co.bitOffset = bitIdx;
                co.bitCount = 1;
                co.expr = std::make_unique<VoltageOutputNode>(std::move(rhsExpr));
                outputs.push_back(std::move(co));
                break;
            }
            case slang::ast::ExpressionKind::Concatenation: {
                // {carry, sum} = rhs  →  split by part widths
                // Leftmost part is MSB of RHS
                auto& concat = lhsExpr.as<slang::ast::ConcatenationExpression>();

                // First pass: compute total width
                int totalWidth = 0;
                for (auto* part : concat.operands())
                    totalWidth += std::max(1, (int)part->type->getBitWidth());

                // Second pass: assign bits starting from MSB downward
                int bitOffset = totalWidth; // starts past the MSB

                for (size_t i = 0; i < concat.operands().size(); i++) {
                    auto* part = concat.operands()[i];
                    int partWidth = std::max(1, (int)part->type->getBitWidth());
                    bitOffset -= partWidth; // move to MSB of this part

                    if (part->kind == slang::ast::ExpressionKind::NamedValue) {
                        auto& named = part->as<slang::ast::NamedValueExpression>();
                        QString partName = QString::fromStdString(std::string(named.symbol.name));

                        for (int b = 0; b < partWidth; b++) {
                            SlangManager::CompiledOutput co;
                            co.outputPin = QString("%1_%2").arg(partName).arg(b);
                            co.outputPort = partName;
                            co.bitOffset = b;
                            co.bitCount = 1;
                            co.expr = std::make_unique<VoltageOutputNode>(
                                std::make_unique<BitSliceNode>(
                                    rhsExpr->clone(), bitOffset + b, 1));
                            outputs.push_back(std::move(co));
                        }
                    }
                }
                break;
            }
            default:
                // Unsupported LHS expression kind — fallback: single output
                {
                    SlangManager::CompiledOutput co;
                    co.outputPin = QString("out%1").arg(outputs.size());
                    co.outputPort = co.outputPin;
                    co.bitOffset = 0;
                    co.bitCount = 1;
                    co.expr = std::make_unique<VoltageOutputNode>(std::move(rhsExpr));
                    outputs.push_back(std::move(co));
                }
                break;
        }
    }
};

} // namespace

SlangManager& SlangManager::instance() {
    static SlangManager inst;
    return inst;
}

QList<SlangManager::PortInfo> SlangManager::extractPorts(const QString& svSource, const QString& moduleName, QString* error) {
    QList<PortInfo> ports;
    
    slang::SourceManager sourceManager;
    auto tree = slang::syntax::SyntaxTree::fromText(svSource.toStdString(), sourceManager);
    
    slang::ast::Compilation compilation;
    compilation.addSyntaxTree(tree);
    
    auto diagnostics = compilation.getAllDiagnostics();
    if (!diagnostics.empty() && error) {
        slang::DiagnosticEngine diagEngine(sourceManager);
        auto client = std::make_shared<slang::TextDiagnosticClient>();
        diagEngine.addClient(client);
        for (auto& diag : diagnostics) {
            diagEngine.issue(diag);
        }
        *error = QString::fromStdString(client->getString());
    }

    auto& root = compilation.getRoot();
    const slang::ast::InstanceSymbol* targetModule = nullptr;
    for (auto* instance : root.topInstances) {
        if (instance->name == moduleName.toStdString()) {
            targetModule = instance;
            break;
        }
    }

    if (!targetModule) {
        if (error && (error->isEmpty())) *error = QString("Module %1 not found.").arg(moduleName);
        return ports;
    }

    for (auto& symbol : targetModule->body.members()) {
        if (symbol.kind == slang::ast::SymbolKind::Port) {
            auto& p = symbol.as<slang::ast::PortSymbol>();
            PortInfo info;
            info.name = QString::fromStdString(std::string(p.name));
            info.isInput = (p.direction == slang::ast::ArgumentDirection::In);
            info.width = (int)p.getType().getBitWidth();
            ports << info;
        }
    }

    return ports;
}

SlangManager::CompiledModule SlangManager::compileToInterpreter(const QString& svSource, const QString& moduleName, QString* error) {
    CompiledModule mod;

    slang::SourceManager sourceManager;
    auto tree = slang::syntax::SyntaxTree::fromText(svSource.toStdString(), sourceManager);
    slang::ast::Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto diagnostics = compilation.getAllDiagnostics();
    if (!diagnostics.empty() && error) {
        slang::DiagnosticEngine diagEngine(sourceManager);
        auto client = std::make_shared<slang::TextDiagnosticClient>();
        diagEngine.addClient(client);
        for (auto& diag : diagnostics) {
            diagEngine.issue(diag);
        }
        *error = QString::fromStdString(client->getString());
    }

    auto& root = compilation.getRoot();
    const slang::ast::InstanceSymbol* targetModule = nullptr;
    for (auto* instance : root.topInstances) {
        if (instance->name == moduleName.toStdString()) {
            targetModule = instance;
            break;
        }
    }

    if (!targetModule) {
        if (error && error->isEmpty()) *error = QString("Module %1 not found.").arg(moduleName);
        return mod;
    }

    // ── Phase 1: Build input map with multi-bit expansion ────────────────
    // Maps port name → first input index, and port name → bit width
    QMap<std::string, int> inputMap;
    QMap<std::string, int> inputWidths;
    QList<PortInfo> ports;

    int inIdx = 0;
    for (auto& symbol : targetModule->body.members()) {
        if (symbol.kind == slang::ast::SymbolKind::Port) {
            auto& p = symbol.as<slang::ast::PortSymbol>();
            PortInfo info;
            info.name = QString::fromStdString(std::string(p.name));
            info.isInput = (p.direction == slang::ast::ArgumentDirection::In);
            info.width = (int)p.getType().getBitWidth();
            ports << info;

            if (p.direction == slang::ast::ArgumentDirection::In) {
                inputMap[std::string(p.name)] = inIdx;
                inputWidths[std::string(p.name)] = info.width;
                // Expand multi-bit inputs: each bit occupies one input slot
                // Inputs are LSB-first: slot 0 = bit 0, slot 1 = bit 1, ...
                if (info.width > 1) {
                    for (int b = 0; b < info.width; b++) {
                        mod.inputPins << QString("%1%2").arg(QString::fromStdString(std::string(p.name))).arg(b);
                    }
                } else {
                    mod.inputPins << QString::fromStdString(std::string(p.name));
                }
                mod.inputWidths << info.width;
                inIdx += info.width;
            }
        }
    }

    // ── Phase 2: Process assignments ─────────────────────────────────────
    // Process ContinuousAssign, always_comb, always @(posedge ...), always_ff

    auto processAssignExpr = [&](const slang::ast::AssignmentExpression& expr) {
        AssignmentProcessor proc{inputMap, inputWidths, mod.outputs, QString()};
        proc.process(expr);
    };

    // Helper: get input index for a named signal from inputMap
    auto getInputIndex = [&](std::string_view name) -> int {
        auto it = inputMap.find(std::string(name));
        if (it != inputMap.end())
            return it.value();
        return -1;
    };

    // Helper: build an EvalNode for a data expression
    auto buildDataExpr = [&](const slang::ast::Expression& expr) -> std::unique_ptr<EvalNode> {
        auto builder = std::make_unique<EvalNodeBuilder>(inputMap, inputWidths);
        expr.visit(*builder);
        return std::move(builder->result);
    };

    // Process a sequential always block (always @(posedge clk), always_ff)
    auto processSequentialBlock = [&](const slang::ast::ProceduralBlockSymbol& pb) {
        const slang::ast::Statement& body = pb.getBody();
        if (body.kind != slang::ast::StatementKind::Timed)
            return;
        auto& timed = body.as<slang::ast::TimedStatement>();

        // Extract clock and reset info from timing control
        int clkIndex = -1, rstIndex = -1;
        int edgeType = 0; // 0=PosEdge

        auto processSignalEvent = [&](const slang::ast::SignalEventControl& ctrl) {
            if (ctrl.edge == slang::ast::EdgeKind::NegEdge) {
                edgeType = 1;
            }
            if (ctrl.expr.kind == slang::ast::ExpressionKind::NamedValue) {
                auto& nv = ctrl.expr.as<slang::ast::NamedValueExpression>();
                int idx = getInputIndex(nv.symbol.name);
                if (idx >= 0) {
                    if (clkIndex < 0) {
                        clkIndex = idx;
                        edgeType = (ctrl.edge == slang::ast::EdgeKind::NegEdge) ? 1 : 0;
                    } else if (rstIndex < 0) {
                        rstIndex = idx;
                    }
                }
            }
        };

        if (timed.timing.kind == slang::ast::TimingControlKind::SignalEvent) {
            processSignalEvent(timed.timing.as<slang::ast::SignalEventControl>());
        } else if (timed.timing.kind == slang::ast::TimingControlKind::EventList) {
            auto& evList = timed.timing.as<slang::ast::EventListControl>();
            for (auto* ev : evList.events) {
                if (ev && ev->kind == slang::ast::TimingControlKind::SignalEvent) {
                    processSignalEvent(ev->as<slang::ast::SignalEventControl>());
                }
            }
        }

        if (clkIndex < 0) return;

        // Process the inner statement: could be expression (direct assign) or conditional (if/else)
        std::function<void(const slang::ast::Statement&, int)> processInnerStmt;
        processInnerStmt = [&](const slang::ast::Statement& stmt, int forcedRstIdx) {
            if (stmt.kind == slang::ast::StatementKind::ExpressionStatement) {
                auto& es = stmt.as<slang::ast::ExpressionStatement>();
                if (es.expr.kind == slang::ast::ExpressionKind::Assignment) {
                    auto& assignExpr = es.expr.as<slang::ast::AssignmentExpression>();
                    auto rhsNode = buildDataExpr(assignExpr.right());
                    if (!rhsNode) return;

                    int useRst = (forcedRstIdx >= 0) ? forcedRstIdx : rstIndex;

                    // Process LHS: create per-bit DffNodes
                    int totalWidth = std::max(1, (int)assignExpr.left().type->getBitWidth());

                    auto assignRHS = [&](const QString& portName, int w, int bitOffset,
                                          std::unique_ptr<EvalNode>& dataExpr) {
                        for (int b = 0; b < w; b++) {
                            auto bitExpr = std::make_unique<BitSliceNode>(dataExpr->clone(), bitOffset + b, 1);
                            auto dff = std::make_unique<DffNode>(
                                clkIndex, std::move(bitExpr), useRst, edgeType, 2.5);

                            SlangManager::CompiledOutput co;
                            if (w == 1) {
                                co.outputPin = portName;
                            } else {
                                co.outputPin = QString("%1_%2").arg(portName).arg(b);
                            }
                            co.outputPort = portName;
                            co.bitOffset = b;
                            co.bitCount = 1;
                            co.expr = std::move(dff);
                            mod.outputs.push_back(std::move(co));
                        }
                    };

                    if (assignExpr.left().kind == slang::ast::ExpressionKind::NamedValue) {
                        auto& nv = assignExpr.left().as<slang::ast::NamedValueExpression>();
                        QString pn = QString::fromStdString(std::string(nv.symbol.name));
                        assignRHS(pn, totalWidth, 0, rhsNode);
                    } else if (assignExpr.left().kind == slang::ast::ExpressionKind::Concatenation) {
                        auto& concat = assignExpr.left().as<slang::ast::ConcatenationExpression>();
                        int bitOffset = totalWidth;
                        for (auto* part : concat.operands()) {
                            int pw = std::max(1, (int)part->type->getBitWidth());
                            bitOffset -= pw;
                            if (part->kind == slang::ast::ExpressionKind::NamedValue) {
                                auto& nv = part->as<slang::ast::NamedValueExpression>();
                                QString pn = QString::fromStdString(std::string(nv.symbol.name));
                                assignRHS(pn, pw, bitOffset, rhsNode);
                            }
                        }
                    }
                }
            } else if (stmt.kind == slang::ast::StatementKind::Conditional) {
                auto& cond = stmt.as<slang::ast::ConditionalStatement>();
                // Check if condition is a named signal (reset)
                if (!cond.conditions.empty()) {
                    auto& firstCond = cond.conditions[0];
                    if (firstCond.expr->kind == slang::ast::ExpressionKind::NamedValue) {
                        auto& condNv = firstCond.expr->as<slang::ast::NamedValueExpression>();
                        int condIdx = getInputIndex(condNv.symbol.name);
                        if (condIdx >= 0) {
                            // Condition is a known input — use as async reset
                            if (cond.ifFalse)
                                processInnerStmt(*cond.ifFalse, condIdx);
                        } else {
                            // Unknown condition — process both branches
                            processInnerStmt(cond.ifTrue, -1);
                            if (cond.ifFalse)
                                processInnerStmt(*cond.ifFalse, -1);
                        }
                    }
                }
            }
        };

        processInnerStmt(timed.stmt, -1);
    };

    for (auto& symbol : targetModule->body.members()) {
        // 2a. Continuous assign: assign x = expr;
        if (symbol.kind == slang::ast::SymbolKind::ContinuousAssign) {
            auto& assign = symbol.as<slang::ast::ContinuousAssignSymbol>();
            auto& expr = assign.getAssignment().as<slang::ast::AssignmentExpression>();
            processAssignExpr(expr);
        }
        // 2b. Procedural blocks
        else if (symbol.kind == slang::ast::SymbolKind::ProceduralBlock) {
            auto& pb = symbol.as<slang::ast::ProceduralBlockSymbol>();

            // Handle sequential blocks (always @(posedge ...), always_ff)
            if (pb.procedureKind == slang::ast::ProceduralBlockKind::Always ||
                pb.procedureKind == slang::ast::ProceduralBlockKind::AlwaysFF) {
                processSequentialBlock(pb);
            }
            // Handle combinational blocks (always_comb, always_latch)
            else if (pb.isSingleDriverBlock()) {
                const slang::ast::Statement& body = pb.getBody();
                const slang::ast::Statement* stmt = &body;

                while (stmt->kind == slang::ast::StatementKind::Block) {
                    stmt = &stmt->as<slang::ast::BlockStatement>().body;
                }

                auto processBody = [&](const slang::ast::Statement& s) {
                    if (s.kind == slang::ast::StatementKind::ExpressionStatement) {
                        auto& es = s.as<slang::ast::ExpressionStatement>();
                        if (es.expr.kind == slang::ast::ExpressionKind::Assignment) {
                            processAssignExpr(es.expr.as<slang::ast::AssignmentExpression>());
                        }
                    }
                };

                if (stmt->kind == slang::ast::StatementKind::List) {
                    for (auto* s : stmt->as<slang::ast::StatementList>().list)
                        processBody(*s);
                } else {
                    processBody(*stmt);
                }
            }
        }
    }

    return mod;
}

// ═══════════════════════════════════════════════════════════════════════════
// Pre-generated trampoline functions (one per slot index)
// ═══════════════════════════════════════════════════════════════════════════
#include "trampolines.inc"

// Dispatch function — called by all trampolines.
double trampolineDispatch(uint64_t slotIdx, double, const double* inputs) {
    auto& mgr = TrampolineManager::instance();
    if (slotIdx < static_cast<uint64_t>(TrampolineManager::kMaxNodes) &&
        mgr.m_slots[slotIdx].inUse && mgr.m_slots[slotIdx].node) {
        return mgr.m_slots[slotIdx].node->eval(inputs);
    }
    return 0.0;
}

TrampolineManager& TrampolineManager::instance() {
    static TrampolineManager mgr;
    return mgr;
}

double (*TrampolineManager::allocate(std::unique_ptr<EvalNode> node))(double, const double*) {
    if (!node) return nullptr;
    if (m_count >= kMaxNodes) return nullptr;

    for (int i = 0; i < kMaxNodes; ++i) {
        if (!m_slots[i].inUse) {
            m_slots[i].node = std::move(node);
            m_slots[i].inUse = true;
            m_slots[i].func = g_trampolines[i];
            m_count++;
            return m_slots[i].func;
        }
    }
    return nullptr;
}

void TrampolineManager::free(double (*func)(double, const double*)) {
    if (!func) return;
    for (int i = 0; i < kMaxNodes; ++i) {
        if (m_slots[i].inUse && m_slots[i].func == func) {
            m_slots[i].inUse = false;
            m_slots[i].node.reset();
            m_slots[i].func = nullptr;
            m_count--;
            return;
        }
    }
}
