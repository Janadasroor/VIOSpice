# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

win = flux_qt_create_window("Test")
lbl = flux_qt_create_label("Hello")
flux_qt_add_widget(win, lbl)
viora_flux_print("minimal dashboard loaded")
