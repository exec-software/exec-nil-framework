@pragma once
# =============================================================================
# ny.ui.mimic — Mimic UI Framework Package
# =============================================================================
# Cross-platform windowed UI framework (SDL2/Win32 backend).
# Provides window management, event handling, and widget toolkit.
#
# Provides:
#   - Mimic: window creation, event loop, input (mimic.ny)
#   - App: application lifecycle (app.ny)
#   - Window: window struct and operations (window.ny)
#   - Widget base class (widget.ny)
#   - Controls: Button, Checkbox, Dropdown, Label, Listbox (button.ny etc.)
#   - Layout: Panel, GroupBox, Toolbar, Statusbar (panel.ny etc.)
#   - Input: TextField, Slider, Progressbar, Radio (textfield.ny etc.)
#   - Color: color types and constants (color.ny)
#
# Cross-refs:
#   ny.lang.core     — event types, text
#   ny.engine        — engine renderer may target mimic windows
#   nilaix            — IDE experiments built on ny.ui.mimic
# =============================================================================

package ny.ui.mimic

import ny.ui.mimic.mimic
import ny.ui.mimic.app
import ny.ui.mimic.window
import ny.ui.mimic.widget
import ny.ui.mimic.button
import ny.ui.mimic.checkbox
import ny.ui.mimic.color
import ny.ui.mimic.dropdown
import ny.ui.mimic.groupbox
import ny.ui.mimic.label
import ny.ui.mimic.listbox
import ny.ui.mimic.panel
import ny.ui.mimic.progressbar
import ny.ui.mimic.radio
import ny.ui.mimic.slider
import ny.ui.mimic.statusbar
import ny.ui.mimic.textfield
import ny.ui.mimic.toolbar
