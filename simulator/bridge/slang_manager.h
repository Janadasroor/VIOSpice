/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VIOSPICE_SIMULATOR_BRIDGE_SLANG_MANAGER_H
#define VIOSPICE_SIMULATOR_BRIDGE_SLANG_MANAGER_H

#include <QString>
#include <QList>
#include <QMap>
#include <memory>
#include <vector>
#include <cstdint>

// ── Interpreted expression node for SystemVerilog blocks ──────────────────
// Replaces the FluxScript JIT pipeline with direct C++ evaluation.
// Each node evaluates to a double (voltage) given a time and input array.
// For multi-bit expressions, eval() returns the reconstructed integer value.

struct EvalNode {
    virtual ~EvalNode() = default;
    virtual double eval(const double* inputs) const = 0;
    virtual std::unique_ptr<EvalNode> clone() const = 0;
};

struct LiteralNode : EvalNode {
    double value;
    explicit LiteralNode(double v) : value(v) {}
    double eval(const double*) const override { return value; }
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<LiteralNode>(value);
    }
};

// Single-bit input with 2.5V threshold
struct InputThresholdNode : EvalNode {
    int index;
    explicit InputThresholdNode(int idx) : index(idx) {}
    double eval(const double* inputs) const override {
        return inputs[index] > 2.5 ? 1.0 : 0.0;
    }
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<InputThresholdNode>(index);
    }
};

// Multi-bit input: reconstructs integer from N consecutive thresholded bits
// Inputs are LSB-first: index 0 = bit 0, index 1 = bit 1, ...
struct MultiBitInputNode : EvalNode {
    int baseIndex;
    int width;
    MultiBitInputNode(int idx, int w) : baseIndex(idx), width(w) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<MultiBitInputNode>(baseIndex, width);
    }
};

// Extracts bits [lsb+count-1 : lsb] from a multi-bit expression result
struct BitSliceNode : EvalNode {
    std::unique_ptr<EvalNode> source;
    int lsb;
    int count;
    BitSliceNode(std::unique_ptr<EvalNode> s, int l, int c)
        : source(std::move(s)), lsb(l), count(c) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<BitSliceNode>(source->clone(), lsb, count);
    }
};

// Selects a single element from a multi-bit input value
struct ElementSelectNode : EvalNode {
    std::unique_ptr<EvalNode> base;
    int bitIndex;
    ElementSelectNode(std::unique_ptr<EvalNode> b, int i)
        : base(std::move(b)), bitIndex(i) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<ElementSelectNode>(base->clone(), bitIndex);
    }
};

// Concatenates multiple values into a wider word (for RHS concat)
struct ConcatNode : EvalNode {
    std::vector<std::unique_ptr<EvalNode>> parts;
    std::vector<int> partWidths;
    ConcatNode(std::vector<std::unique_ptr<EvalNode>> p, std::vector<int> w)
        : parts(std::move(p)), partWidths(std::move(w)) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        std::vector<std::unique_ptr<EvalNode>> clonedParts;
        for (auto& p : parts) clonedParts.push_back(p->clone());
        return std::make_unique<ConcatNode>(std::move(clonedParts), partWidths);
    }
};

// Conditional/ternary: sel ? a : b
struct TernaryOpNode : EvalNode {
    std::unique_ptr<EvalNode> cond, trueExpr, falseExpr;
    TernaryOpNode(std::unique_ptr<EvalNode> c, std::unique_ptr<EvalNode> t, std::unique_ptr<EvalNode> f)
        : cond(std::move(c)), trueExpr(std::move(t)), falseExpr(std::move(f)) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<TernaryOpNode>(cond->clone(), trueExpr->clone(), falseExpr->clone());
    }
};

enum class BinOp { And, Or, Xor, Add, Sub, Mul, Div, Eq, Neq, Gt, Lt };

struct BinaryOpNode : EvalNode {
    std::unique_ptr<EvalNode> lhs, rhs;
    BinOp op;
    BinaryOpNode(std::unique_ptr<EvalNode> l, std::unique_ptr<EvalNode> r, BinOp o)
        : lhs(std::move(l)), rhs(std::move(r)), op(o) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<BinaryOpNode>(lhs->clone(), rhs->clone(), op);
    }
};

enum class UnaryOp { Not, Negate };

struct UnaryOpNode : EvalNode {
    std::unique_ptr<EvalNode> operand;
    UnaryOp op;
    UnaryOpNode(std::unique_ptr<EvalNode> o, UnaryOp u)
        : operand(std::move(o)), op(u) {}
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<UnaryOpNode>(operand->clone(), op);
    }
};

// Edge-triggered D flip-flop with optional async reset
// Detects rising (PosEdge) or falling (NegEdge) clock edge,
// evaluates the data expression on the active edge, stores in mutable state.
// For async reset, detects posedge of reset signal and forces Q=0.
// IMPORTANT: uses mutable state that persists across timesteps.
// Uses time-based gating to prevent derivative perturbation calls
// (from cfunc.c numerical partials) from corrupting edge detection.
struct DffNode : EvalNode {
    int clkIndex;
    std::unique_ptr<EvalNode> dExpr;
    int rstIndex;           // -1 = no reset
    int edgeType;           // 0=PosEdge, 1=NegEdge
    double threshold;
    mutable double savedClk;    // clock voltage from last eval (any call)
    mutable double savedRst;    // reset voltage from last eval
    mutable double capturedQ;   // current Q value
    mutable double lastTime;    // timestep of last edge detection (-1 = initial)

    DffNode(int clk, std::unique_ptr<EvalNode> d, int rst, int edge, double thresh = 2.5);
    double eval(const double* inputs) const override;
    std::unique_ptr<EvalNode> clone() const override;
};

// Wraps a boolean-valued expression: returns 5.0V if expr > 0.5, else 0.0V
struct VoltageOutputNode : EvalNode {
    std::unique_ptr<EvalNode> expr;
    explicit VoltageOutputNode(std::unique_ptr<EvalNode> e) : expr(std::move(e)) {}
    double eval(const double* inputs) const override {
        return expr->eval(inputs) > 0.5 ? 5.0 : 0.0;
    }
    std::unique_ptr<EvalNode> clone() const override {
        return std::make_unique<VoltageOutputNode>(expr->clone());
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
        QString outputPin;     // "SUM_3", "SUM_2", ...  or "CARRY_0"
        QString outputPort;    // base port name, e.g. "SUM"
        int bitOffset;         // bit position within the port (0 = LSB)
        int bitCount;          // number of bits this output covers (1 for single bit)
        std::unique_ptr<EvalNode> expr;
    };
    struct CompiledModule {
        QStringList inputPins;     // expanded per-bit input names, e.g. ["A_0","A_1","A_2","A_3","B_0",...]
        QList<int> inputWidths;    // original width of each input, e.g. [4, 4, 1]
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
