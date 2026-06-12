/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

module comparator(input logic a, input logic b,
                  output logic gt, output logic eq, output logic lt);
    assign gt = a > b;
    assign eq = a == b;
    assign lt = a < b;
endmodule
