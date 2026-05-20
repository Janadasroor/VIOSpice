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

    if (failures > 0) {
        std::cerr << "\n" << failures << " test(s) FAILED" << std::endl;
        return 1;
    }

    std::cout << "\nsimulator.slang_manager: all tests passed" << std::endl;
    return 0;
}
