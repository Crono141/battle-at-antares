extends PanelContainer
## The native galaxy's controls (WP3.1b): the turn button, the empire line and launchers for the
## screens that are still legacy. Each button is a command to the game thread. The empire line
## shows what the legacy side bar shows (WP3.3d): treasury and income (its menu sets the tax rate,
## as the side bar's money box does), command points, food, freighters and the research box
## (click: research).

signal command(dict: Dictionary)

const WARNING_COLOR := Color(1.0, 0.45, 0.35)

@onready var date_label: Label = $Rows/Empire/Date
@onready var money: MenuButton = $Rows/Empire/Money
@onready var command_label: Label = $Rows/Empire/Command
@onready var food_label: Label = $Rows/Empire/Food
@onready var freighters_label: Label = $Rows/Empire/Freighters
@onready var research: Button = $Rows/Empire/Research


func _ready() -> void:
	for button: Button in $Rows/Row/Screens.get_children():
		var screen: int = button.get_meta("screen", -1)
		button.pressed.connect(func(): command.emit({"verb": "open", "screen": screen}))
	$Rows/Row/Turn.pressed.connect(func(): command.emit({"verb": "end_turn"}))
	# The legacy side bar's money box opens the tax rate popup (WP3.3d): here, its menu.
	money.get_popup().id_pressed.connect(func(rate: int):
		command.emit({"verb": "set_tax_rate", "rate": rate}))
	# The legacy side bar's research box opens research.
	research.pressed.connect(func(): command.emit({"verb": "open", "screen": int(research.get_meta("screen"))}))
	# The legacy galaxy's letter keys (WP3.3a), shown in each button's tooltip.
	for button: Button in find_children("*", "Button", true, false):
		if button.has_meta("key"):
			var key := InputEventKey.new()
			key.keycode = OS.find_keycode_from_string(String(button.get_meta("key")))
			button.shortcut = Shortcut.new()
			button.shortcut.events = [key]


## The `empire` snapshot: what the legacy side bar shows (WP3.3d).
func show_empire(empire: Dictionary) -> void:
	if not empire.get("ok", false):
		return
	date_label.text = "%s   Stardate %.1f" % [empire.race, float(empire.stardate) / 10.0]
	money.text = "%s BC (%s)   Tax %d%%" % [_thousands(int(empire.bc)), _signed(int(empire.surplus_bc)), int(empire.tax_rate)]
	_warn(money, int(empire.bc) < 0 or int(empire.surplus_bc) < 0)
	var popup := money.get_popup()
	for i in popup.item_count:
		popup.set_item_checked(i, popup.get_item_id(i) == int(empire.tax_rate))
	var spare := int(empire.command_points) - int(empire.command_points_used)
	command_label.text = "Command %s (%d)" % [_signed(spare), int(empire.command_points)]
	_warn(command_label, spare < 0)
	food_label.text = "Food %s" % _signed(int(empire.surplus_food))
	_warn(food_label, int(empire.surplus_food) < 0)
	freighters_label.text = "Freighters %s (%d)" % [_signed(int(empire.surplus_freighters)), int(empire.freighters)]
	_warn(freighters_label, int(empire.surplus_freighters) < 0)
	research.text = _research_text(empire.research)


## As the side bar's research box: a breakthrough, nothing researched, or the research points with
## the turns to the next discovery (and the chance of it next turn).
func _research_text(box: Dictionary) -> String:
	var points := "%s %s" % [_thousands(int(box.produced)), box.unit]
	match box.state:
		"breakthrough", "none":
			return box.text
		"turns":
			var chance := "   %d%%" % int(box.chance) if int(box.chance) > 0 else ""
			return "%s   %s%s" % [points, box.text, chance]
		"complete":
			return "%s   %d%%" % [points, int(box.chance)]
		_:
			return "0 %s" % box.unit


func _warn(control: Control, on: bool) -> void:
	if on:
		control.add_theme_color_override("font_color", WARNING_COLOR)
	else:
		control.remove_theme_color_override("font_color")


static func _signed(value: int) -> String:
	return ("+" if value >= 0 else "-") + _thousands(absi(value))


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return ("-" if value < 0 else "") + digits + out
