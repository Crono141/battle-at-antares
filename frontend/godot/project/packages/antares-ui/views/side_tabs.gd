extends VBoxContainer
## The screen tabs on the galaxy's right edge (WP3.3f, owner's layout): each tab opens its screen
## (still the legacy one) or, for Ship Design and Options, a flyout that slides out from the right.
## A tab's "screen" metadata is the SCREEN_* number it opens, "action" a view action for main.gd,
## "key" the legacy galaxy's letter key.

signal command(dict: Dictionary)
signal action(name: String)


func _ready() -> void:
	for tab: Button in get_children():
		if tab.has_meta("screen"):
			var screen: int = tab.get_meta("screen")
			tab.pressed.connect(func(): command.emit({"verb": "open", "screen": screen}))
		elif tab.has_meta("action"):
			var action_name := String(tab.get_meta("action"))
			tab.pressed.connect(func(): action.emit(action_name))
		if tab.has_meta("key"):
			var key := InputEventKey.new()
			key.keycode = OS.find_keycode_from_string(String(tab.get_meta("key")))
			tab.shortcut = Shortcut.new()
			tab.shortcut.events = [key]
			tab.shortcut_in_tooltip = true


## Lights or darkens the tab of one flyout.
func show_open(action_name: String, open: bool) -> void:
	for tab: Button in get_children():
		if tab.get_meta("action", "") == action_name:
			tab.set_pressed_no_signal(open)
			tab.queue_redraw()
