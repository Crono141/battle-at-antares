extends PanelContainer
## The fleet panel (WP3.3c): the legacy fleet box as a native panel over the galaxy, drawn from the
## `fleet` query. Ships are toggle buttons with their pictures (SHIPS.LBX), made from the hidden
## ShipTemplate button; the panel owns the selection and sends ship ids with each order. With ships
## selected, the galaxy previews a route on the hovered star (main.gd asks `move_preview` and shows
## the answer here) and a star click moves them. The order buttons show when the game's rules
## offer them (the query's `buttons`): Colonize, Outpost and Land troops ask which planet (Planets),
## Colonize then asks to confirm with the colony's numbers, as the legacy picker does.
##
## Commands go out through `command` (main.gd sends them); a command with "_reply_to" names the
## method here that gets its result.

signal command(dict: Dictionary)
signal closed
signal selection_changed

const WARNING_COLOR := Color(1.0, 0.45, 0.3)
const STATUS_COLOR := Color(0.45, 0.95, 0.45)

@onready var title: Label = $Box/Title
@onready var grid: GridContainer = $Box/Ships/Grid
@onready var template: Button = $Box/ShipTemplate
@onready var status: Label = $Box/Line/Status
@onready var all_button: Button = $Box/Line/All
@onready var planets: PopupMenu = $Planets
@onready var confirm_colony: ConfirmationDialog = $ConfirmColony

var art: Node # art.gd
var fleet := {} # the `fleet` query
var _buttons := {} # ship id -> Button
var _planet_order := "" # "colonize", "outpost" or "land_troops" while Planets is open
var _confirm_planet := -1
var _preview := false # the status line shows a route preview
var _drag_from := Vector2.INF


func _ready() -> void:
	all_button.pressed.connect(_on_all)
	$Box/Orders/Colonize.pressed.connect(func(): _choose_planet("colonize"))
	$Box/Orders/Outpost.pressed.connect(func(): _choose_planet("outpost"))
	$Box/Orders/LandTroops.pressed.connect(_on_land_troops)
	$Box/Orders/Engage.pressed.connect(func():
		command.emit({"verb": "engage", "fleet": int(fleet.ship), "_reply_to": "order_done"}))
	$Box/Orders/AttackAntares.pressed.connect(_on_attack_antares)
	$Box/Orders/Close.pressed.connect(close)
	planets.id_pressed.connect(_on_planet_chosen)
	confirm_colony.confirmed.connect(func():
		command.emit({"verb": "colonize", "fleet": int(fleet.ship), "planet": _confirm_planet, "_reply_to": "order_done"}))
	title.gui_input.connect(_on_title_input)
	if art:
		art.loaded.connect(func(_key): _refresh_icons())


## Shows a fleet (the `fleet` query). `auto_select`: select every ship that can take orders, as
## the legacy box does when it opens with that setting on.
func show_fleet(result: Dictionary, auto_select := false) -> void:
	if not result.get("ok", false):
		return
	var same := not fleet.is_empty() and int(fleet.ship) == int(result.ship)
	var keep: Array = selected_ships() if same else []
	fleet = result
	title.text = result.title
	title.label_settings.font_color = _owner_color()
	for child in grid.get_children():
		child.queue_free()
	_buttons.clear()
	for ship: Dictionary in result.ships:
		var button: Button = template.duplicate()
		button.visible = true
		button.disabled = not ship.orderable
		button.tooltip_text = "%s (%s)" % [ship.name, ship.type]
		button.button_pressed = ship.orderable and (int(ship.id) in keep or (auto_select and result.auto_select))
		button.toggled.connect(func(_on): _on_selection())
		button.mouse_entered.connect(func(): _show_status(ship.name, false))
		button.mouse_exited.connect(func(): _show_fleet_status())
		grid.add_child(button)
		_buttons[int(ship.id)] = button
	# Room for three rows of ships, then a scroll bar (the legacy box shows nine).
	$Box/Ships.custom_minimum_size.y = minf(3.0, ceilf(result.ships.size() / 3.0)) * (template.custom_minimum_size.y + 4.0)
	_refresh_icons()
	var buttons: Dictionary = result.buttons
	$Box/Orders/Colonize.visible = buttons.colonize
	$Box/Orders/Outpost.visible = buttons.outpost
	$Box/Orders/LandTroops.visible = buttons.land_troops
	$Box/Orders/Engage.visible = buttons.engage
	$Box/Orders/AttackAntares.visible = buttons.attack_antares
	all_button.visible = _orderable().size() > 0
	_preview = false
	_show_fleet_status()
	show()
	reset_size()
	selection_changed.emit()


## The selected ships' ids, in the fleet's order.
func selected_ships() -> Array:
	var out := []
	for ship: Dictionary in fleet.get("ships", []):
		var button: Button = _buttons.get(int(ship.id))
		if button and button.button_pressed and not button.disabled:
			out.append(int(ship.id))
	return out


func close() -> void:
	dismiss()
	command.emit({"verb": "close_fleet"}) # the saved fleet box record (fleet_orders.cpp)


## Hides the panel without telling the game (it closed the box itself: an order used up the
## fleet, or the system panel replaced it).
func dismiss() -> void:
	hide()
	fleet = {}
	closed.emit()


## The route preview for a hovered star (the `move_preview` query), or {} to go back to the
## fleet's own line.
func show_preview(result: Dictionary) -> void:
	if result.is_empty() or not result.get("ok", false) or result.get("text", "") == "":
		_preview = false
		_show_fleet_status()
		return
	_preview = true
	_show_status(result.text, result.get("warning", false))


## An order's result: show the fleet the box follows (the ships moved, or those left), or close.
func order_done(result: Dictionary) -> void:
	if not result.get("ok", false) or not result.get("done", true):
		return
	var follow := int(result.get("fleet", fleet.get("ship", -1)))
	if follow < 0:
		dismiss()
		return
	fleet = {} # the legacy box clears the selection after an order
	command.emit({"verb": "query", "what": "fleet", "ship": follow, "_reply_to": "show_fleet"})


## The colony's numbers for the confirmation (the `potential_colony` query).
func confirm_with(result: Dictionary) -> void:
	var lines: Array = []
	if result.get("ok", false):
		lines.append(result.title)
		for line: Dictionary in result.lines:
			lines.append("%s  %s" % [line.label, line.value])
	confirm_colony.dialog_text = "\n".join(lines) if not lines.is_empty() else "Colonize this planet?"
	confirm_colony.popup_centered()


# ---- Inside ----

func _orderable() -> Array:
	return fleet.get("ships", []).filter(func(ship: Dictionary) -> bool: return ship.orderable)


func _on_selection() -> void:
	_preview = false
	_show_fleet_status()
	selection_changed.emit()


## ALL: everything that can take orders, or nothing when all of it is selected already.
func _on_all() -> void:
	var all_on := selected_ships().size() == _orderable().size()
	for ship: Dictionary in _orderable():
		_buttons[int(ship.id)].set_pressed_no_signal(not all_on)
	_on_selection()


func _choose_planet(order: String) -> void:
	var targets: Array = fleet.get("targets", {}).get(order, [])
	if targets.is_empty():
		return
	_planet_order = order
	if targets.size() == 1:
		_on_planet_chosen(int(targets[0].planet))
		return
	planets.clear()
	for target: Dictionary in targets:
		planets.add_item(target.name, int(target.planet))
	planets.popup(Rect2i(Vector2i(get_screen_position() + Vector2(size.x, 0)), Vector2i.ZERO))


func _on_planet_chosen(planet: int) -> void:
	match _planet_order:
		"colonize":
			_confirm_planet = planet
			command.emit({"verb": "query", "what": "potential_colony", "planet": planet, "_reply_to": "confirm_with"})
		"outpost":
			command.emit({"verb": "outpost", "fleet": int(fleet.ship), "planet": planet, "_reply_to": "order_done"})
		"land_troops":
			command.emit({"verb": "land_troops", "ships": _selected_of_type("transport"), "planet": planet,
				"_reply_to": "order_done"})


func _on_land_troops() -> void:
	if _selected_of_type("transport").is_empty():
		_show_status("No transport ships selected", true) # the legacy box's message 0xE3
		return
	_choose_planet("land_troops")


func _on_attack_antares() -> void:
	var ships := selected_ships()
	if ships.is_empty():
		_show_status("No ships selected", true) # the legacy box's message 0xE6
		return
	command.emit({"verb": "attack_antares", "ships": ships, "_reply_to": "order_done"})


func _selected_of_type(type: String) -> Array:
	var ids := selected_ships()
	return fleet.get("ships", []).filter(func(ship: Dictionary) -> bool:
		return ship.type == type and int(ship.id) in ids).map(func(ship: Dictionary) -> int: return int(ship.id))


func _show_fleet_status() -> void:
	if not _preview:
		_show_status(fleet.get("status", ""), false)


func _show_status(text: String, warning: bool) -> void:
	status.text = text
	status.tooltip_text = text
	status.add_theme_color_override("font_color", WARNING_COLOR if warning else STATUS_COLOR)


func _refresh_icons() -> void:
	if art == null:
		return
	for ship: Dictionary in fleet.get("ships", []):
		var button: Button = _buttons.get(int(ship.id))
		if button:
			button.icon = art.texture("SHIPS.LBX", int(ship.picture))


func _owner_color() -> Color:
	var galaxy_script := preload("res://galaxy.gd")
	var index := int(fleet.get("color", -1))
	return galaxy_script.PLAYER_COLORS[index % 8] if index >= 0 else galaxy_script.NEUTRAL_COLOR


func _on_title_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button and button.button_index == MOUSE_BUTTON_LEFT:
		_drag_from = button.global_position if button.pressed else Vector2.INF
	var motion := event as InputEventMouseMotion
	if motion and _drag_from != Vector2.INF:
		position += motion.global_position - _drag_from
		_drag_from = motion.global_position


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
