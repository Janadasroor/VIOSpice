#ifndef VIOSPICE_SIMULATOR_BRIDGE_SLANG_MANAGER_H
#define VIOSPICE_SIMULATOR_BRIDGE_SLANG_MANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include <memory>
#include <vector>

// ── Interpreted expression node for SystemVerilog blocks ──────────────────
// Replaces the FluxScript JIT pipeline with direct C++ evaluation.
// Each node evaluates to a double (voltage) given a time and input array.

struct EvalNode {
    virtual ~EvalNode() = default;
    virtual double eval(const double* inputs) const = 0;
};

struct LiteralNode : EvalNode {
    double value;
    explicit LiteralNode(double v) : value(v) {}
    double eval(const double*) const override { return value; }
};

struct InputThresholdNode : EvalNode {
    int index;
    explicit InputThresholdNode(int idx) : index(idx) {}
    double eval(const double* inputs) const override {
        return inputs[index] > 2.5 ? 1.0 : 0.0;
    }
};

enum class BinOp { And, Or, Xor, Add, Sub, Mul, Div, Eq, Neq, Gt, Lt };

struct BinaryOpNode : EvalNode {
    std::unique_ptr<EvalNode> lhs, rhs;
    BinOp op;
    BinaryOpNode(std::unique_ptr<EvalNode> l, std::unique_ptr<EvalNode> r, BinOp o)
        : lhs(std::move(l)), rhs(std::move(r)), op(o) {}
    double eval(const double* inputs) const override;
};

enum class UnaryOp { Not, Negate };

struct UnaryOpNode : EvalNode {
    std::unique_ptr<EvalNode> operand;
    UnaryOp op;
    UnaryOpNode(std::unique_ptr<EvalNode> o, UnaryOp u)
        : operand(std::move(o)), op(u) {}
    double eval(const double* inputs) const override;
};

// Wraps a boolean-valued expression: returns 5.0V if expr > 0.5, else 0.0V
struct VoltageOutputNode : EvalNode {
    std::unique_ptr<EvalNode> expr;
    explicit VoltageOutputNode(std::unique_ptr<EvalNode> e) : expr(std::move(e)) {}
    double eval(const double* inputs) const override {
        return expr->eval(inputs) > 0.5 ? 5.0 : 0.0;
    }
};

// ── SlangManager ──────────────────────────────────────────────────────────

class SlangManager {
public:
    static SlangManager& instance();

    struct PortInfo {
        QString name;
        int width;
        bool isInput;
    };

    // Parse the SystemVerilog source and extract port information
    QList<PortInfo> extractPorts(const QString& svSource, const QString& moduleName, QString* error = nullptr);

    // Compile a SystemVerilog module into interpreted expression trees (no JIT/FluxScript)
    struct CompiledOutput {
        QString outputPin;
        std::unique_ptr<EvalNode> expr;
    };
    struct CompiledModule {
        QStringList inputPins; // in declaration order
        std::vector<CompiledOutput> outputs;
    };
    CompiledModule compileToInterpreter(const QString& svSource, const QString& moduleName, QString* error = nullptr);

private:
    SlangManager() = default;
    ~SlangManager() = default;
    SlangManager(const SlangManager&) = delete;
    SlangManager& operator=(const SlangManager&) = delete;
};

// ── Runtime trampoline manager for SV interpreter functions ───────────────
// Allocates executable memory pages containing tiny x86-64 stubs that embed
// an EvalNode index and jump to a shared dispatch function. Each stub can be
// registered with ngspice's viospice_jit code model via registerTargetsWithEngine.
// Removes the need for FluxScript JIT compilation of SystemVerilog blocks.

// Forward declaration needed by writeTrampoline
double trampolineDispatch(uint64_t slotIdx, double time, const double* inputs);

class TrampolineManager {
public:
    static TrampolineManager& instance();

    static constexpr int kMaxNodes = 512;

    // Allocate a trampoline for an EvalNode. Returns a function pointer
    // with signature double(double, const double*).
    double (*allocate(std::unique_ptr<EvalNode> node))(double, const double*);

    // Free a trampoline by its function pointer.
    void free(double (*func)(double, const double*));

    int count() const { return m_count; }

private:
    TrampolineManager() = default;
    ~TrampolineManager() = default;

    int m_count = 0;

    struct Slot {
        double (*func)(double, const double*) = nullptr;
        std::unique_ptr<EvalNode> node;
        bool inUse = false;
    };
    Slot m_slots[kMaxNodes];

    friend double trampolineDispatch(uint64_t slotIdx, double time, const double* inputs);

    TrampolineManager(const TrampolineManager&) = delete;
    TrampolineManager& operator=(const TrampolineManager&) = delete;
};

#endif // VIOSPICE_SIMULATOR_BRIDGE_SLANG_MANAGER_H
