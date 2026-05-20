#include "slang_manager.h"

#include <slang/syntax/SyntaxTree.h>
#include <slang/ast/Compilation.h>
#include <slang/ast/Scope.h>
#include <slang/ast/symbols/CompilationUnitSymbols.h>
#include <slang/ast/symbols/InstanceSymbols.h>
#include <slang/ast/symbols/PortSymbols.h>
#include <slang/ast/symbols/MemberSymbols.h>
#include <slang/ast/ASTVisitor.h>
#include <slang/ast/Expression.h>
#include <slang/ast/expressions/AssignmentExpressions.h>
#include <slang/ast/expressions/OperatorExpressions.h>
#include <slang/ast/expressions/LiteralExpressions.h>
#include <slang/ast/expressions/MiscExpressions.h>
#include <slang/diagnostics/DiagnosticEngine.h>
#include <slang/diagnostics/TextDiagnosticClient.h>
#include <slang/text/SourceManager.h>

#include <QDebug>
#include <QMap>
#include <cmath>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
// EvalNode implementations (global namespace — structs declared in header)
// ═══════════════════════════════════════════════════════════════════════════

double BinaryOpNode::eval(const double* inputs) const {
    double l = lhs->eval(inputs);
    double r = rhs->eval(inputs);
    switch (op) {
        case BinOp::And: return (l > 0.5 && r > 0.5) ? 1.0 : 0.0;
        case BinOp::Or:  return (l > 0.5 || r > 0.5) ? 1.0 : 0.0;
        case BinOp::Xor: return ((l > 0.5) != (r > 0.5)) ? 1.0 : 0.0;
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

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// EvalNode tree builder — visits Slang AST and builds EvalNode hierarchy
// ═══════════════════════════════════════════════════════════════════════════

class EvalNodeBuilder : public slang::ast::ASTVisitor<EvalNodeBuilder, slang::ast::VisitFlags::Expressions> {
public:
    std::unique_ptr<EvalNode> result;
    const QMap<std::string_view, int>& inputMap;

    EvalNodeBuilder(const QMap<std::string_view, int>& inputs)
        : inputMap(inputs) {}

    template<typename T>
    void visit(const T& t) {
        slang::ast::ASTVisitor<EvalNodeBuilder, slang::ast::VisitFlags::Expressions>::visit(t);
    }

    void handle(const slang::ast::BinaryExpression& expr) {
        auto leftBuilder = std::make_unique<EvalNodeBuilder>(inputMap);
        expr.left().visit(*leftBuilder);
        auto rightBuilder = std::make_unique<EvalNodeBuilder>(inputMap);
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
        auto operandBuilder = std::make_unique<EvalNodeBuilder>(inputMap);
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
        auto name = expr.symbol.name;
        if (inputMap.contains(name)) {
            result = std::make_unique<InputThresholdNode>(inputMap[name]);
        } else {
            result = std::make_unique<LiteralNode>(0.0);
        }
    }

    void handle(const slang::ast::IntegerLiteral& expr) {
        result = std::make_unique<LiteralNode>(
            std::stod(expr.getValue().toString()));
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
        // We continue if there are no fatal errors
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

    QMap<std::string_view, int> inputMap;
    int inIdx = 0;
    for (auto& symbol : targetModule->body.members()) {
        if (symbol.kind == slang::ast::SymbolKind::Port) {
            auto& p = symbol.as<slang::ast::PortSymbol>();
            if (p.direction == slang::ast::ArgumentDirection::In) {
                inputMap[p.name] = inIdx++;
                mod.inputPins << QString::fromStdString(std::string(p.name));
            }
        }
    }

    for (auto& symbol : targetModule->body.members()) {
        if (symbol.kind == slang::ast::SymbolKind::ContinuousAssign) {
            auto& assign = symbol.as<slang::ast::ContinuousAssignSymbol>();
            auto& expr = assign.getAssignment().as<slang::ast::AssignmentExpression>();

            QString outputName;
            if (expr.left().kind == slang::ast::ExpressionKind::NamedValue) {
                auto& named = expr.left().as<slang::ast::NamedValueExpression>();
                outputName = QString::fromStdString(std::string(named.symbol.name));
            }
            if (outputName.isEmpty())
                outputName = QString("out%1").arg(mod.outputs.size());

            auto nodeBuilder = std::make_unique<EvalNodeBuilder>(inputMap);
            expr.right().visit(*nodeBuilder);

            if (nodeBuilder->result) {
                CompiledOutput co;
                co.outputPin = outputName;
                co.expr = std::make_unique<VoltageOutputNode>(std::move(nodeBuilder->result));
                mod.outputs.push_back(std::move(co));
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
