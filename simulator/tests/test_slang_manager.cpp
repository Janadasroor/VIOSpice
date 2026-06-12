/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../bridge/slang_manager.h"
#include <iostream>
#include <cassert>
#include <QString>
#include <QList>
#include <QMap>

int main() {
    SlangManager& manager = SlangManager::instance();
    int failures = 0;

    // ---- Test 1: compileToInterpreter (AND gate) ----
    {
        QString svSource = 
            "module and2(input logic a, input logic b, output logic y);\n"
            "    assign y = a & b;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "and2", &error);
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: Interpreter compilation empty\n";
            return 1;
        }
        assert(compiled.outputs.size() == 1);
        assert(compiled.outputs[0].outputPin == "y");
        assert(compiled.inputPins.size() == 2);
        assert(compiled.inputPins[0] == "a");
        assert(compiled.inputPins[1] == "b");

        // Evaluate: a=5V, b=5V -> y should be 5V
        double inputs_high[] = {5.0, 5.0};
        double result = compiled.outputs[0].expr->eval(inputs_high);
        assert(result == 5.0);

        // Evaluate: a=5V, b=0V -> y should be 0V
        double inputs_low[] = {5.0, 0.0};
        result = compiled.outputs[0].expr->eval(inputs_low);
        assert(result == 0.0);

        std::cout << "Test 1 - Interpreter AND gate: OK" << std::endl;
    }

    // ---- Test 2: compileToInterpreter (inverter) ----
    {
        QString svSource = 
            "module inv(input logic a, output logic y);\n"
            "    assign y = ~a;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "inv", &error);
        assert(compiled.outputs.size() == 1);
        assert(compiled.outputs[0].outputPin == "y");

        double inputs_high[] = {5.0};
        assert(compiled.outputs[0].expr->eval(inputs_high) == 0.0);

        double inputs_low[] = {0.0};
        assert(compiled.outputs[0].expr->eval(inputs_low) == 5.0);

        std::cout << "Test 2 - Interpreter inverter: OK" << std::endl;
    }

    // ---- Test 3: TrampolineManager basic allocation and evaluation ----
    {
        auto inputNode = std::make_unique<InputThresholdNode>(0);
        auto wrapper = std::make_unique<VoltageOutputNode>(std::move(inputNode));

        double (*func)(double, const double*) =
            TrampolineManager::instance().allocate(std::move(wrapper));
        assert(func != nullptr);

        double inputs_high[] = {5.0};
        double inputs_low[] = {0.0};

        // Call via trampoline (real function pointer)
        double result = func(0.0, inputs_high);
        assert(result == 5.0);
        result = func(0.0, inputs_low);
        assert(result == 0.0);

        TrampolineManager::instance().free(func);
        std::cout << "Test 3 - TrampolineManager: OK" << std::endl;
    }

    // ---- Test 4: Full adder with multi-output ----
    {
        QString svSource =
            "module fa(input logic a, input logic b, input logic cin,\n"
            "          output logic sum, output logic cout);\n"
            "    assign sum = a ^ b ^ cin;\n"
            "    assign cout = (a & b) | (a & cin) | (b & cin);\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "fa", &error);
        assert(compiled.outputs.size() == 2);
        assert(compiled.inputPins.size() == 3);

        // Test: a=5V, b=5V, cin=0V -> sum=0V, cout=5V
        double inputs[] = {5.0, 5.0, 0.0};
        QString sumName = compiled.outputs[0].outputPin;
        QString coutName = compiled.outputs[1].outputPin;
        (void)sumName;
        (void)coutName;

        double sumVal = compiled.outputs[0].expr->eval(inputs);
        double coutVal = compiled.outputs[1].expr->eval(inputs);
        assert(sumVal == 0.0);
        assert(coutVal == 5.0);

        // Test: all low -> sum=0V, cout=0V
        double inputs_low[] = {0.0, 0.0, 0.0};
        sumVal = compiled.outputs[0].expr->eval(inputs_low);
        coutVal = compiled.outputs[1].expr->eval(inputs_low);
        assert(sumVal == 0.0);
        assert(coutVal == 0.0);

        // Test: a=5V, b=0V, cin=5V -> sum=0V, cout=5V
        double inputs_mix[] = {5.0, 0.0, 5.0};
        sumVal = compiled.outputs[0].expr->eval(inputs_mix);
        coutVal = compiled.outputs[1].expr->eval(inputs_mix);
        assert(sumVal == 0.0);
        assert(coutVal == 5.0);

        std::cout << "Test 4 - Full adder interpreter: OK" << std::endl;
    }

    // ---- Test 5: 4-bit adder with LHS concatenation {carry, sum} = a + b ----
    {
        QString svSource =
            "module adder4(input logic [3:0] a, input logic [3:0] b,\n"
            "              output logic carry, output logic [3:0] sum);\n"
            "    assign {carry, sum} = a + b;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "adder4", &error);
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: adder4 compiled empty\n";
            failures++;
        } else {
            // a, b each 4-bit: inputPins = [a0,a1,a2,a3, b0,b1,b2,b3]
            assert(compiled.inputPins.size() == 8);
            assert(compiled.inputPins.contains("a0"));
            assert(compiled.inputPins.contains("a3"));
            assert(compiled.inputPins.contains("b0"));
            assert(compiled.inputWidths.size() == 2);
            assert(compiled.inputWidths[0] == 4);
            assert(compiled.inputWidths[1] == 4);
            // Outputs: carry_0 (1 bit) + sum_0..sum_3 (4 bits) = 5
            // Concatenation operands are left-to-right: {carry, sum} → carry first
            assert(compiled.outputs.size() == 5);
            assert(compiled.outputs[0].outputPin == "carry_0");
            assert(compiled.outputs[1].outputPin == "sum_0");
            assert(compiled.outputs[4].outputPin == "sum_3");

            // Helper to build LSB-first input array for a and b dynamically
            auto makeInputs = [&](int aVal, int bVal) -> std::vector<double> {
                std::vector<double> v(8, 0.0);
                for (int i = 0; i < 4; i++) {
                    int aIdx = compiled.inputPins.indexOf(QString("a%1").arg(i));
                    int bIdx = compiled.inputPins.indexOf(QString("b%1").arg(i));
                    if (aIdx >= 0) v[aIdx] = (aVal >> i) & 1 ? 5.0 : 0.0;
                    if (bIdx >= 0) v[bIdx] = (bVal >> i) & 1 ? 5.0 : 0.0;
                }
                return v;
            };

            // a=0, b=0  →  carry=0, sum=0
            {
                auto in = makeInputs(0, 0);
                assert(compiled.outputs[0].expr->eval(in.data()) == 0.0); // carry
                for (int b = 1; b < 5; b++)
                    assert(compiled.outputs[b].expr->eval(in.data()) == 0.0); // sum bits
            }

            // a=1, b=1  →  1+1=2 → carry=0, sum=2 (binary 0010)
            // outputs[0]=carry=0V, outputs[1..4]=sum=bit0..bit3
            {
                auto in = makeInputs(1, 1);
                assert(compiled.outputs[0].expr->eval(in.data()) == 0.0); // carry
                assert(compiled.outputs[1].expr->eval(in.data()) == 0.0); // sum_0 (bit 0 of 2)
                assert(compiled.outputs[2].expr->eval(in.data()) == 5.0); // sum_1 (bit 1 of 2)
                assert(compiled.outputs[3].expr->eval(in.data()) == 0.0); // sum_2
                assert(compiled.outputs[4].expr->eval(in.data()) == 0.0); // sum_3
            }

            // a=15, b=1  →  15+1=16 → carry=1, sum=0
            {
                auto in = makeInputs(15, 1);
                assert(compiled.outputs[0].expr->eval(in.data()) == 5.0); // carry=1
                for (int b = 1; b < 5; b++)
                    assert(compiled.outputs[b].expr->eval(in.data()) == 0.0);
            }

            // a=5 (0101), b=10 (1010) →  5+10=15 → carry=0, sum=15 (all bits 1)
            {
                auto in = makeInputs(5, 10);
                assert(compiled.outputs[0].expr->eval(in.data()) == 0.0); // carry
                for (int b = 1; b < 5; b++)
                    assert(compiled.outputs[b].expr->eval(in.data()) == 5.0); // all sum bits
            }
        }
        std::cout << "Test 5 - 4-bit adder with LHS concat: OK" << std::endl;
    }

    // ---- Test 6: Ternary mux (assign y = sel ? a : b) ----
    {
        QString svSource =
            "module mux2(input logic sel, input logic [3:0] a,\n"
            "            input logic [3:0] b, output logic [3:0] y);\n"
            "    assign y = sel ? a : b;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "mux2", &error);
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: mux2 compiled empty\n";
            failures++;
        } else {
            // Inputs: sel (1) + a0..a3 (4) + b0..b3 (4) = 9
            assert(compiled.inputPins.size() == 9);
            assert(compiled.inputPins.contains("sel"));
            assert(compiled.inputPins.contains("a0"));
            assert(compiled.inputPins.contains("b0"));
            assert(compiled.inputWidths.size() == 3);
            assert(compiled.inputWidths[0] == 1);
            assert(compiled.inputWidths[1] == 4);
            assert(compiled.inputWidths[2] == 4);
            // Outputs: y_0..y_3
            assert(compiled.outputs.size() == 4);
            assert(compiled.outputs[0].outputPin == "y_0");
            assert(compiled.outputs[3].outputPin == "y_3");

            auto makeInputs = [&](int selVal, int aVal, int bVal) -> std::vector<double> {
                std::vector<double> v(9, 0.0);
                int selIdx = compiled.inputPins.indexOf("sel");
                if (selIdx >= 0) v[selIdx] = selVal ? 5.0 : 0.0;
                for (int i = 0; i < 4; i++) {
                    int aIdx = compiled.inputPins.indexOf(QString("a%1").arg(i));
                    int bIdx = compiled.inputPins.indexOf(QString("b%1").arg(i));
                    if (aIdx >= 0) v[aIdx] = (aVal >> i) & 1 ? 5.0 : 0.0;
                    if (bIdx >= 0) v[bIdx] = (bVal >> i) & 1 ? 5.0 : 0.0;
                }
                return v;
            };

            // sel=0 → y = b
            {
                auto in = makeInputs(0, 0xA, 0x5);
                // b = 0x5 = 0101 → y_0=5V, y_1=0V, y_2=5V, y_3=0V
                assert(compiled.outputs[0].expr->eval(in.data()) == 5.0);
                assert(compiled.outputs[1].expr->eval(in.data()) == 0.0);
                assert(compiled.outputs[2].expr->eval(in.data()) == 5.0);
                assert(compiled.outputs[3].expr->eval(in.data()) == 0.0);
            }

            // sel=1 → y = a
            {
                auto in = makeInputs(1, 0xA, 0x5);
                // a = 0xA = 1010 → y_0=0V, y_1=5V, y_2=0V, y_3=5V
                assert(compiled.outputs[0].expr->eval(in.data()) == 0.0);
                assert(compiled.outputs[1].expr->eval(in.data()) == 5.0);
                assert(compiled.outputs[2].expr->eval(in.data()) == 0.0);
                assert(compiled.outputs[3].expr->eval(in.data()) == 5.0);
            }
        }
        std::cout << "Test 6 - Ternary mux: OK" << std::endl;
    }

    // ---- Test 7: always_comb with multi-bit assignment ----
    {
        QString svSource =
            "module alu(input logic [3:0] a, input logic [3:0] b,\n"
            "           output logic [3:0] y);\n"
            "    always_comb begin\n"
            "        y = a ^ b;\n"
            "    end\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "alu", &error);
        if (!error.isEmpty())
            std::cerr << "  error: " << error.toStdString() << std::endl;

        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: alu (always_comb) compiled empty\n";
            failures++;
        } else {
            assert(compiled.outputs.size() == 4);
            auto makeInputs = [&](int aVal, int bVal) -> std::vector<double> {
                std::vector<double> v(8, 0.0);
                for (int i = 0; i < 4; i++) {
                    int aIdx = compiled.inputPins.indexOf(QString("a%1").arg(i));
                    int bIdx = compiled.inputPins.indexOf(QString("b%1").arg(i));
                    if (aIdx >= 0) v[aIdx] = (aVal >> i) & 1 ? 5.0 : 0.0;
                    if (bIdx >= 0) v[bIdx] = (bVal >> i) & 1 ? 5.0 : 0.0;
                }
                return v;
            };

            // a=0xF, b=0xF → XOR → 0x0
            {
                auto in = makeInputs(15, 15);
                for (int b = 0; b < 4; b++)
                    assert(compiled.outputs[b].expr->eval(in.data()) == 0.0);
            }

            // a=0xA, b=0x5 → XOR → 0xF
            {
                auto in = makeInputs(0xA, 0x5);
                for (int b = 0; b < 4; b++)
                    assert(compiled.outputs[b].expr->eval(in.data()) == 5.0);
            }
        }
        std::cout << "Test 7 - always_comb XOR: OK" << std::endl;
    }

    // ---- Test 8: always_comb with multiple statements and RHS concatenation ----
    {
        QString svSource =
            "module concat_test(input logic [1:0] a, input logic [1:0] b,\n"
            "                   output logic [3:0] y);\n"
            "    always_comb begin\n"
            "        y = {a, b};\n"
            "    end\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "concat_test", &error);
        if (!error.isEmpty())
            std::cerr << "  error: " << error.toStdString() << std::endl;
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: concat_test compiled empty\n";
            failures++;
        } else {
            assert(compiled.outputs.size() == 4);
            assert(compiled.inputPins.size() == 4);
            assert(compiled.inputWidths[0] == 2);
            assert(compiled.inputWidths[1] == 2);

            auto makeInputs = [&](int aVal, int bVal) -> std::vector<double> {
                std::vector<double> v(4, 0.0);
                for (int i = 0; i < 2; i++) {
                    int aIdx = compiled.inputPins.indexOf(QString("a%1").arg(i));
                    int bIdx = compiled.inputPins.indexOf(QString("b%1").arg(i));
                    if (aIdx >= 0) v[aIdx] = (aVal >> i) & 1 ? 5.0 : 0.0;
                    if (bIdx >= 0) v[bIdx] = (bVal >> i) & 1 ? 5.0 : 0.0;
                }
                return v;
            };

            // a=2'b10, b=2'b01  →  y = {10, 01} = 4'b1001
            // y_0=5V (bit 0 = 1), y_1=0V (bit 1 = 0), y_2=0V (bit 2 = 0), y_3=5V (bit 3 = 1)
            {
                auto in = makeInputs(0b10, 0b01);
                assert(compiled.outputs[0].expr->eval(in.data()) == 5.0);
                assert(compiled.outputs[1].expr->eval(in.data()) == 0.0);
                assert(compiled.outputs[2].expr->eval(in.data()) == 0.0);
                assert(compiled.outputs[3].expr->eval(in.data()) == 5.0);
            }
        }
        std::cout << "Test 8 - RHS concatenation in always_comb: OK" << std::endl;
    }

    // ---- Test 9: DFF (always @(posedge clk) q <= d) ----
    {
        QString svSource =
            "module dff(input logic clk, input logic d, output logic q);\n"
            "    always @(posedge clk) q <= d;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "dff", &error);
        if (!error.isEmpty())
            std::cerr << "  error: " << error.toStdString() << std::endl;
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: dff compiled empty\n";
            failures++;
        } else {
            // Input map: clk=0, d=1
            assert(compiled.inputPins.size() == 2);
            assert(compiled.outputs.size() == 1);
            // Simulate timesteps through the DffNode directly
            auto* dffNode = dynamic_cast<DffNode*>(compiled.outputs[0].expr.get());
            if (!dffNode) {
                std::cerr << "FAIL: dff output is not a DffNode\n";
                failures++;
            } else {
                int clkIdx = compiled.inputPins.indexOf("clk");
                int dIdx = compiled.inputPins.indexOf("d");
                assert(clkIdx >= 0 && dIdx >= 0);

                double inputs[2];
                // t0: clk=0, d=0 → Q should stay 0 (no edge)
                inputs[clkIdx] = 0.0; inputs[dIdx] = 0.0;
                double q0 = dffNode->eval(inputs);
                assert(q0 == 0.0);
                // t1: clk=5 (rising edge), d=5 → Q should capture 5V
                inputs[clkIdx] = 5.0; inputs[dIdx] = 5.0;
                double q1 = dffNode->eval(inputs);
                assert(q1 == 5.0);
                // t2: clk=5 (steady), d=0 → Q should hold 5V
                inputs[clkIdx] = 5.0; inputs[dIdx] = 0.0;
                double q2 = dffNode->eval(inputs);
                assert(q2 == 5.0);
                // t3: clk=0 (falling edge → no capture), d=0
                inputs[clkIdx] = 0.0; inputs[dIdx] = 0.0;
                double q3 = dffNode->eval(inputs);
                assert(q3 == 5.0); // still holding
                // t4: clk=5 (rising edge), d=0 → Q should capture 0V
                inputs[clkIdx] = 5.0; inputs[dIdx] = 0.0;
                double q4 = dffNode->eval(inputs);
                assert(q4 == 0.0);
                // reset internal state for next test
            }
        }
        std::cout << "Test 9 - DFF (always @(posedge clk)): OK" << std::endl;
    }

    // ---- Test 10: DFF with async reset (always @(posedge clk or posedge rst)) ----
    {
        QString svSource =
            "module dff_rst(input logic clk, input logic rst, input logic d, output logic q);\n"
            "    always @(posedge clk or posedge rst)\n"
            "        if (rst) q <= 0; else q <= d;\n"
            "endmodule\n";

        QString error;
        auto compiled = manager.compileToInterpreter(svSource, "dff_rst", &error);
        if (!error.isEmpty())
            std::cerr << "  error: " << error.toStdString() << std::endl;
        if (compiled.outputs.empty()) {
            std::cerr << "FAIL: dff_rst compiled empty\n";
            failures++;
        } else {
            assert(compiled.inputPins.size() == 3);
            assert(compiled.outputs.size() == 1);
            auto* dffNode = dynamic_cast<DffNode*>(compiled.outputs[0].expr.get());
            if (!dffNode) {
                std::cerr << "FAIL: dff_rst output is not a DffNode\n";
                failures++;
            } else {
                // Verify reset port is set
                assert(dffNode->rstIndex >= 0);

                int clkIdx = compiled.inputPins.indexOf("clk");
                int rstIdx = compiled.inputPins.indexOf("rst");
                int dIdx = compiled.inputPins.indexOf("d");
                assert(clkIdx >= 0 && rstIdx >= 0 && dIdx >= 0);

                double inputs[3];
                // t0: clk=0, rst=0, d=0
                inputs[clkIdx] = 0.0; inputs[rstIdx] = 0.0; inputs[dIdx] = 0.0;
                double q0 = dffNode->eval(inputs);
                assert(q0 == 0.0);
                // t1: clk=5 (rising edge), d=5 → capture
                inputs[clkIdx] = 5.0; inputs[rstIdx] = 0.0; inputs[dIdx] = 5.0;
                double q1 = dffNode->eval(inputs);
                assert(q1 == 5.0);
                // t2: rst=5 (rising edge) → reset to 0
                inputs[clkIdx] = 5.0; inputs[rstIdx] = 5.0; inputs[dIdx] = 5.0;
                double q2 = dffNode->eval(inputs);
                assert(q2 == 0.0);
                // t3: rst=0, clk=5 → holding 0
                inputs[clkIdx] = 5.0; inputs[rstIdx] = 0.0; inputs[dIdx] = 5.0;
                double q3 = dffNode->eval(inputs);
                assert(q3 == 0.0);
            }
        }
        std::cout << "Test 10 - DFF with async reset: OK" << std::endl;
    }

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED" << std::endl;
        return 1;
    }

    std::cout << "\nsimulator.slang_manager: all tests passed" << std::endl;
    return 0;
}
