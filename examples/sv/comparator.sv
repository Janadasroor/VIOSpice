module comparator(input logic a, input logic b,
                  output logic gt, output logic eq, output logic lt);
    assign gt = a > b;
    assign eq = a == b;
    assign lt = a < b;
endmodule
