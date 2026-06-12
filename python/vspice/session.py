# Copyright 2026 Janada Sroor
# SPDX-License-Identifier: Apache-2.0

"""
vspice.session — GUI Session Introspection and Awareness
This module provides utilities for querying the active state of the VioSpice GUI.
It is designed to be called from the internal Python bridge by external agents.
"""

import json
from PySide6.QtWidgets import QApplication, QTabWidget

def get_active_tab_info():
    """Returns metadata about the currently focused editor tab."""
    for w in QApplication.topLevelWidgets():
        if not w.isVisible():
            continue
            
        # Prioritize the main workspace tabs
        tabs = w.findChild(QTabWidget, "m_workspaceTabs") or w.findChild(QTabWidget)
        
        if tabs:
            idx = tabs.currentIndex()
            if idx < 0:
                continue
                
            widget = tabs.widget(idx)
            info = {
                "title": tabs.tabText(idx),
                "index": idx,
                "type": widget.__class__.__name__,
                "window": w.windowTitle()
            }
            
            # Common property patterns for VioraEDA editors
            if hasattr(widget, "filePath"):
                info["filePath"] = widget.filePath()
            elif hasattr(widget, "property") and widget.property("filePath"):
                info["filePath"] = widget.property("filePath")
                
            return info
            
    return {"error": "No active editor found"}

def get_session_state():
    """Returns a full map of all open windows and tabs in the current session."""
    state = {"windows": []}
    
    for w in QApplication.topLevelWidgets():
        if not w.isVisible():
            continue
            
        win_info = {
            "title": w.windowTitle(),
            "type": w.__class__.__name__,
            "tabs": []
        }
        
        # Look for the primary workspace tab widget
        tabs_widget = w.findChild(QTabWidget, "m_workspaceTabs") or w.findChild(QTabWidget)
        
        if tabs_widget:
            active_idx = tabs_widget.currentIndex()
            for i in range(tabs_widget.count()):
                widget = tabs_widget.widget(i)
                tab_info = {
                    "title": tabs_widget.tabText(i),
                    "active": (i == active_idx),
                    "type": widget.__class__.__name__
                }
                
                if hasattr(widget, "filePath"):
                    tab_info["filePath"] = widget.filePath()
                elif hasattr(widget, "property") and widget.property("filePath"):
                    tab_info["filePath"] = widget.property("filePath")
                    
                win_info["tabs"].append(tab_info)
        
        state["windows"].append(win_info)
        
    return state

def print_active_tab_json():
    """Helper for remote bridge calls."""
    print(json.dumps(get_active_tab_info()))

def print_session_state_json():
    """Helper for remote bridge calls."""
    print(json.dumps(get_session_state()))
