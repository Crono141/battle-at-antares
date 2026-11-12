extends VBoxContainer
## The galaxy's own controls along the bottom (WP3.3a; laid out by the owner, WP3.3f): the previous
## and next fleets (F1 / F2) outside the previous and next colony systems (F6 / F5), with the
## distance tool (F9) above them. Each one is a legacy key made visible (owner, 2026-11-05: every
## hidden key gets a control, its key in the tooltip); a button's key is its shortcut, set from its
## "key" metadata. View-only actions go to the frontend as `action`; the rest are commands.
## Turn is on the top bar; influence, merge relocations and the game options are in the options
## flyout (options_menu); zoom is the mouse wheel.

signal command(dict: Dictionary)
signal action(name: String)


func _ready() -> void:
	for button: BaseButton in find_children("*", "BaseButton", true, false):
		if button.has_meta("key"):
			button.shortcut = preload("options_menu.gd").shortcut_for(String(button.get_meta("key")))
		if button.has_meta("action"):
			button.pressed.connect(func(): action.emit(String(button.get_meta("action"))))
	$Distances.pressed.connect(func(): command.emit({"verb": "open", "screen": Screens.DISTANCE_BETWEEN_STARS}))
