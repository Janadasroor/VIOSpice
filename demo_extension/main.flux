# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

def init_ext() {
    var x = sin(1.0);
    var t = timer();
    viora_flux_print("Demo Extension Activated!");
     show_info();
}

def show_info() {
    flux_qt_msg_box("Demo Extension", "This is a dynamically loaded FluxScript extension.");
}
