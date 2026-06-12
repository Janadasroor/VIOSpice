# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

def init_ext() {
    viora_flux_print("Demo Extension Activated!");
}

def show_info() {
    flux_qt_msg_box("Demo Extension", "This is a dynamically loaded FluxScript extension.");
}
