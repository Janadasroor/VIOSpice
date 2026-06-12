/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

module mux2(input logic sel, input logic a, input logic b, output logic y);
    assign y = sel ? b : a;
endmodule
