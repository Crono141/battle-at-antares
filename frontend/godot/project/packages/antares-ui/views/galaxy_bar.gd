extends PanelContainer
## The galaxy's top bar (WP3.1b, WP3.3d; laid out for the design pass, WP3.3f): Game, the stardate
## and the empire's figures as the legacy side bar shows them, each with the game's own icon. The
## treasury opens the tax rates when the pointer rests on it (a click pins the menu, as any menu
## button), as the side bar's money box opens the tax popup; research opens research. The screen
## launchers and Turn moved to the right edge (side_tabs, map_controls).

signal command(dict: Dictionary)

const WARNING_COLOR := Color(1.0, 0.45, 0.35)
## How long the tax menu stays open after the pointer leaves the treasury and the menu.
const HOVER_CLOSE := 0.4

@onready var date_label: Label = $Row/Date
@onready var money: MenuButton = $Row/Money
@onready var command_label: Label = $Row/Command
@onready var food_label: Label = $Row/Food
@onready var freighters_label: Label = $Row/Freighters
@onready var research: Button = $Row/Research

var art: Node:
	set(value):
		art = value
		preload("sprite_rect.gd").bind_all(self, art)

var _hover_opened := false
var _away := 0.0


func _ready() -> void:
	$Row/Game.pressed.connect(func(): command.emit({"verb": "open", "screen": Screens.GAME}))
	money.get_popup().id_pressed.connect(func(rate: int):
		command.emit({"verb": "set_tax_rate", "rate": rate}))
	money.mouse_entered.connect(_open_on_hover)
	money.pressed.connect(func(): _hover_opened = false) # a click pins it
	research.pressed.connect(func(): command.emit({"verb": "open", "screen": int(research.get_meta("screen"))}))
	for button: Button in find_children("*", "Button", true, false):
		if button.has_meta("key"):
			var key := InputEventKey.new()
			key.keycode = OS.find_keycode_from_string(String(button.get_meta("key")))
			button.shortcut = Shortcut.new()
			button.shortcut.events = [key]


func _open_on_hover() -> void:
	var popup := money.get_popup()
	if popup.visible:
		return
	money.show_popup()
	_hover_opened = true
	_away = 0.0


## A menu opened by hovering closes once the pointer has left the treasury and the menu a moment.
func _process(delta: float) -> void:
	var popup := money.get_popup()
	if not _hover_opened or not popup.visible:
		_hover_opened = false
		return
	var mouse := get_viewport().get_mouse_position()
	var over := money.get_global_rect().has_point(mouse) or Rect2(Vector2(popup.position), Vector2(popup.size)).grow(6.0).has_point(mouse)
	_away = 0.0 if over else _away + delta
	if _away > HOVER_CLOSE:
		popup.hide()
		_hover_opened = false


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
	food_label.text = _signed(int(empire.surplus_food))
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
