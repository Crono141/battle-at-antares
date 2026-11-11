extends PanelContainer
## The native galaxy's controls (WP3.1b): the turn button, the empire line and launchers for the
## screens that are still legacy. Each button is a command to the game thread.

signal command(dict: Dictionary)

@onready var empire_label: Label = $Row/Empire


func _ready() -> void:
	for button: Button in $Row/Screens.get_children():
		var screen: int = button.get_meta("screen", -1)
		button.pressed.connect(func(): command.emit({"verb": "open", "screen": screen}))
	$Row/Turn.pressed.connect(func(): command.emit({"verb": "end_turn"}))
	# The legacy galaxy's letter keys (WP3.3a), shown in each button's tooltip.
	for button: Button in find_children("*", "Button", true, false):
		if button.has_meta("key"):
			var key := InputEventKey.new()
			key.keycode = OS.find_keycode_from_string(String(button.get_meta("key")))
			button.shortcut = Shortcut.new()
			button.shortcut.events = [key]


func show_empire(empire: Dictionary) -> void:
	if not empire.get("ok", false):
		return
	var stardate := float(empire.stardate) / 10.0
	empire_label.text = "%s   Stardate %.1f   %d BC (%+d)   Command %d / %d   Freighters %d" % [
		empire.race, stardate, int(empire.bc), int(empire.bc_produced),
		int(empire.command_points_used), int(empire.command_points), int(empire.freighters)]
