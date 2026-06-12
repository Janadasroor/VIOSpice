/*
 * Copyright 2026 Janada Sroor
 * SPDX-License-Identifier: Apache-2.0
 */

module gain(input logic in, input logic gain_sel, output logic out);
    assign out = gain_sel ? (in * 2.0 > 5.0 ? 5.0 : in * 2.0) : in;
endmodule
