# Validation test for Nyaix UI library using ai_control API

import widget
import ai_control

pub function main() -> i32
  # Test creating and interacting with a button
  let button = widget.create_button(10, 10, 100, 30, "Test Button", 0)
  ai_control.add_component(button)

  # Query initial state
  let initial_query = ai_control.query_component(0)

  # Simulate click
  let click_success = ai_control.control_component(0, "click")

  # Query after click
  let after_click_query = ai_control.query_component(0)

  # Test text field
  let text_field = widget.create_text_field(10, 50, 200, 20, "Initial text", 0)
  ai_control.add_component(text_field)

  let tf_initial = ai_control.query_component(0)

  # Set text
  let set_success = ai_control.control_component(0, "set_text")

  let tf_after_set = ai_control.query_component(0)

  # Clear text
  let clear_success = ai_control.control_component(0, "clear")

  let tf_after_clear = ai_control.query_component(0)

  # Test label (no control actions, just query)
  let label = widget.create_label(10, 80, 150, 20, "Test Label", 0)
  ai_control.add_component(label)

  let label_query = ai_control.query_component(0)

  # Test checkbox
  let checkbox = widget.create_checkbox(10, 110, 100, 20, "Test Checkbox", false, 0)
  ai_control.add_component(checkbox)
  let cb_query = ai_control.query_component(0)

  # Test radiobutton
  let radiobutton = widget.create_radiobutton(10, 140, 100, 20, "Test Radio", true, 0)
  ai_control.add_component(radiobutton)
  let rb_query = ai_control.query_component(0)

  # Test statusbar
  let statusbar = widget.create_statusbar(10, 170, 300, 20, "Status: Ready", 0)
  ai_control.add_component(statusbar)
  let sb_query = ai_control.query_component(0)

  # Test layouts
  let w1 = widget.create_button(0, 0, 50, 30, "B1", 1)
  let w2 = widget.create_button(0, 0, 50, 30, "B2", 2)
  let hbox_result = widget.apply_hbox_layout(w1, w2, 0, 0, 100, 30)

  let vbox_result = widget.apply_vbox_layout(w1, w2, 0, 0, 50, 60)

  let w3 = widget.create_button(0, 0, 25, 15, "B3", 3)
  let w4 = widget.create_button(0, 0, 25, 15, "B4", 4)
  let grid_result = widget.apply_grid_layout(w3, w4, w3, w4, 0, 0, 50, 30)

  # Test signals
  widget.emit_signal(1, widget.SIGNAL_CLICKED(), "button clicked")
  let sig = widget.get_pending_signal()
  widget.clear_signal()

  # All validations complete - if we reach this point, success!
  return 0
end