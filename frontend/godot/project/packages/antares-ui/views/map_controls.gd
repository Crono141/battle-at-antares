extends PanelContainer
## The galaxy's own controls along the bottom (WP3.3a; laid out by the owner, WP3.3f; themed,
## WP3.3g): the previous and next fleets (F1 / F2) outside the previous and next colony systems
## (F6 / F5), side by side in a pocket of the theme's screen frame. Each is a legacy key made
## visible (owner, 2026-11-05: every hidden key gets a control, its key in the tooltip); a button's
## key is its shortcut, set from its "key" metadata. Turn is on the top bar; influence, merge
## relocations, the distance tool and the game options are in the Options flyout; zoom is the
## mouse wheel.

signal command(dict: Dictionary)
signal action(name: String)

@onready var row: HBoxContainer = $Row


func _ready() -> void:
	for button: BaseButton in find_children("*", "BaseButton", true, false):
		if button.has_meta("key"):
			button.shortcut = preload("options_menu.gd").shortcut_for(String(button.get_meta("key")))
		if button.has_meta("action"):
			button.pressed.connect(func(): action.emit(String(button.get_meta("action"))))
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	row.add_theme_constant_override("separation", get_theme_constant("edge_gap", "Screen"))
	offset_bottom = -(inset - 3) if inset > 0 else -8.0
	offset_top = offset_bottom - 36.0
