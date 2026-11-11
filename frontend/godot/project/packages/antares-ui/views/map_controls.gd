extends PanelContainer
## The galaxy's own controls (WP3.3a), each one a legacy key made visible (owner, 2026-11-05: every
## hidden key gets a control, its key in the tooltip): zoom (+ / -), the previous and next colony
## systems (F6 / F5), the previous and next fleets (F1 / F2, WP3.3c), merging relocations (F4),
## the influence view (Alt+V), the distance tool (F9) and the game options (Alt+F1..F8). A
## button's key is its shortcut, set from its "key" metadata ("F5", "Alt+V").
## View-only actions go to main.gd as `action`; the rest are commands to the game.

signal command(dict: Dictionary)
signal action(name: String)

const INFLUENCE_NAMES := ["Influence: off", "Influence: filled", "Influence: borders"]

@onready var influence_button: Button = $Box/Influence
@onready var options_button: MenuButton = $Box/Options


func _ready() -> void:
	for button: BaseButton in find_children("*", "BaseButton", true, false):
		if button.has_meta("key"):
			button.shortcut = _shortcut(String(button.get_meta("key")))
		if button.has_meta("action"):
			button.pressed.connect(func(): action.emit(String(button.get_meta("action"))))
	$Box/Distances.pressed.connect(func(): command.emit({"verb": "open", "screen": Screens.DISTANCE_BETWEEN_STARS}))
	options_button.get_popup().id_pressed.connect(func(option: int):
		command.emit({"verb": "toggle_option", "option": option}))


## The Alt+F1..F8 options (the `options` snapshot) as check items.
func show_options(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	var popup := options_button.get_popup()
	popup.clear()
	for option: Dictionary in result.options:
		var id := int(option.option)
		popup.add_check_item(option.name, id)
		var index := popup.get_item_index(id)
		popup.set_item_checked(index, option.on)
		popup.set_item_shortcut(index, _shortcut("Alt+F%d" % (id + 1)), true)


func show_influence(mode: int) -> void:
	influence_button.text = INFLUENCE_NAMES[mode]


## "F5", "Alt+V", "Plus" as a Shortcut.
static func _shortcut(text: String) -> Shortcut:
	var key := InputEventKey.new()
	for part in text.split("+", false):
		match part:
			"Alt":
				key.alt_pressed = true
			"Ctrl":
				key.ctrl_pressed = true
			"Shift":
				key.shift_pressed = true
			_:
				key.keycode = OS.find_keycode_from_string(part)
	var shortcut := Shortcut.new()
	shortcut.events = [key]
	return shortcut
