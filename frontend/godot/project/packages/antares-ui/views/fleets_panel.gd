extends "side_panel.gd"
## Fleets (WP3.5c, the owner's empire panels in notes/antares/ui-concepts.md): the legacy Fleets
## screen as a panel that flies out from the right, against the side tabs, with the native galaxy
## zoomed out beside it in place of the screen's small map. One of the player's fleets at a time
## (the `fleet` query): its ships as pictures in a grid, filtered by SUPPORT / COMBAT (the screen's
## radio buttons: one or neither), the fleet cycler (◀ ▶, `next_fleet`), and in a strip below the
## grid the hovered (else first selected) ship's detail (`ship_detail`, the screen's box).
##
## Ships are picked by clicking (ALL picks every one, again none). With ships picked a click on a
## star sends them there (`move`, the galaxy previews the route on the hovered star); without, a
## click on a star shows the player's fleet there. SCRAP scraps the picked ships (`scrap_ships`,
## the game's questions). RELOCATE arms the relocation mode: a star to relocate from, then the star
## to send its new ships to; the same star twice cancels (`relocate_star`). LEADERS opens the
## original Leaders screen until the Leaders panel exists.

signal command(dict: Dictionary)
## The fleet shown (its first ship), for the galaxy's mark; -1: none.
signal fleet_marked(ship: int)
## The panel's width changed: the galaxy fits the space left of it again.
signal width_changed

const Themes := preload("../themes.gd")
const TILE := Vector2(96, 72)
const COLUMNS := 4
const DETAIL_HEIGHT := 150.0

var art: Node
var player := -1 # the player's number (the galaxy's), for "own fleet at a star"
var fleets: Array = [] # the galaxy's `fleets` list (for a star click)
var fleet := {} # the `fleet` query
var filter := "" # "", "support" or "combat"
var relocating := false
var _tiles := {} # ship id -> its toggle button
var _detail_ship := -1

var _title: Label
var _cycler_label: Label
var _grid: GridContainer
var _scroll: ScrollContainer
var _detail: HBoxContainer
var _support: Button
var _combat: Button
var _all: Button
var _relocate: Button
var _scrap: Button
var _hint: Label


func _ready() -> void:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	box.add_child(header)
	_title = Label.new()
	_title.theme_type_variation = &"Heading"
	_title.text = "Fleets"
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_title)
	var leaders := _button("LEADERS", "The ship officers (the original Leaders screen for now)")
	leaders.pressed.connect(func(): command.emit({"verb": "open", "screen": 29}))
	header.add_child(leaders)
	var close_button := _button("X", "Close (Esc)")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(close)
	header.add_child(close_button)
	# The fleet cycler and the filters.
	var cycler := HBoxContainer.new()
	box.add_child(cycler)
	var previous := _button("◀", "The previous fleet")
	previous.pressed.connect(func(): _cycle(1))
	cycler.add_child(previous)
	_cycler_label = Label.new()
	_cycler_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_cycler_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cycler_label.clip_text = true
	cycler.add_child(_cycler_label)
	var next := _button("▶", "The next fleet")
	next.pressed.connect(func(): _cycle(0))
	cycler.add_child(next)
	var filters := HBoxContainer.new()
	filters.add_theme_constant_override("separation", 6)
	box.add_child(filters)
	_support = _toggle("SUPPORT", "Only transports, colony and outpost ships")
	_support.toggled.connect(func(on: bool): _set_filter("support" if on else ""))
	filters.add_child(_support)
	_combat = _toggle("COMBAT", "Only warships")
	_combat.toggled.connect(func(on: bool): _set_filter("combat" if on else ""))
	filters.add_child(_combat)
	# The ships.
	_scroll = ScrollContainer.new()
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(_scroll)
	_grid = GridContainer.new()
	_grid.columns = COLUMNS
	_grid.add_theme_constant_override("h_separation", 4)
	_grid.add_theme_constant_override("v_separation", 4)
	_scroll.add_child(_grid)
	# The detail strip (one size whatever the ship).
	var detail_panel := PanelContainer.new()
	detail_panel.theme_type_variation = &"Row"
	detail_panel.custom_minimum_size.y = DETAIL_HEIGHT
	detail_panel.clip_contents = true
	box.add_child(detail_panel)
	_detail = HBoxContainer.new()
	_detail.add_theme_constant_override("separation", 12)
	detail_panel.add_child(_detail)
	_hint = Label.new()
	_hint.theme_type_variation = &"Dim"
	_hint.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(_hint)
	var orders := HBoxContainer.new()
	orders.add_theme_constant_override("separation", 8)
	box.add_child(orders)
	_all = _button("ALL", "Pick every ship (again: none)")
	_all.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_all.pressed.connect(_on_all)
	orders.add_child(_all)
	_relocate = _toggle("RELOCATE", "Click the star to relocate new ships from, then the star they go to (the same star twice cancels)")
	_relocate.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_relocate.toggled.connect(_set_relocating)
	orders.add_child(_relocate)
	_scrap = _button("SCRAP", "Scrap the picked ships")
	_scrap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scrap.pressed.connect(func():
		var ships := selected_ships()
		if not ships.is_empty():
			command.emit({"verb": "scrap_ships", "ships": ships, "_reply_to": "_after_order"}))
	orders.add_child(_scrap)
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				position.x = _rect.end.x - size.x
			width_changed.emit())


## Flies out into `area` with the fleet of `ship` (the first ship of a fleet; -1: the first of the
## player's).
func open_panel(area: Rect2, ship := -1) -> void:
	var width := _wanted_width(area)
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))
	if ship < 0:
		for f: Dictionary in fleets:
			if int(f.owner) == player:
				ship = int(f.ship)
				break
	if ship >= 0:
		_show(ship)
	else:
		_cycler_label.text = "No fleets"


func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	var grid_width := COLUMNS * (TILE.x + 4.0) + 40.0 + 14.0 # the pane's padding, the scroll bar
	return minf(maxf(grid_width, get_combined_minimum_size().x), area.size.x - 16.0)


func panel_width() -> float:
	return _rect.end.x - rest_left()


func refresh() -> void:
	if not fleet.is_empty():
		_show(int(fleet.ship))


## The fleet of `ship` (its first ship).
func _show(ship: int) -> void:
	command.emit({"verb": "query", "what": "fleet", "ship": ship, "_reply_to": "show_fleet"})


func show_fleet(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	var same := not fleet.is_empty() and int(fleet.ship) == int(result.ship)
	var keep: Array = selected_ships() if same else []
	fleet = result
	_cycler_label.text = String(result.title)
	_cycler_label.tooltip_text = String(result.get("status", ""))
	_fill_grid(keep)
	fleet_marked.emit(int(result.ship))
	var first: int = keep[0] if not keep.is_empty() else (int(result.ships[0].id) if not (result.ships as Array).is_empty() else -1)
	_show_detail(first)


## A click on the galaxy's star: moves the picked ships there, sets a relocation, or shows the
## player's fleet at that star. False: nothing to do there.
func star_clicked(star: int) -> bool:
	if relocating:
		command.emit({"verb": "relocate_star", "star": star, "_reply_to": "_after_relocate"})
		return true
	var ships := selected_ships()
	if not ships.is_empty():
		command.emit({"verb": "move", "ships": ships, "star": star, "_reply_to": "_after_order"})
		return true
	for f: Dictionary in fleets:
		if int(f.owner) == player and int(f.get("star", -1)) == star:
			_show(int(f.ship))
			return true
	return false


## The side panels' common answer to a star click (frontend._on_star_clicked).
func select_star(star: int) -> bool:
	return star_clicked(star)


## The picked ships, in the fleet's order.
func selected_ships() -> Array:
	var out := []
	for ship: Dictionary in fleet.get("ships", []):
		var tile: Button = _tiles.get(int(ship.id))
		if tile != null and tile.button_pressed:
			out.append(int(ship.id))
	return out


## The route preview's answer for the hovered star (as the fleet box's status line).
func show_preview(result: Dictionary) -> void:
	if relocating:
		return
	if result.is_empty() or not result.get("ok", false):
		_hint.text = ""
		return
	_hint.text = String(result.get("text", result.get("status", "")))


func close() -> void:
	if visible and not _closing:
		fleet_marked.emit(-1)
	_set_relocating(false)
	super.close()


func dismiss() -> void:
	_set_relocating(false)
	super.dismiss()


# ---- The grid ----

func _fill_grid(keep: Array) -> void:
	for child in _grid.get_children():
		_grid.remove_child(child)
		child.queue_free()
	_tiles.clear()
	for ship: Dictionary in fleet.get("ships", []):
		if filter == "support" and not ship.get("support", false):
			continue
		if filter == "combat" and not ship.get("combat", false):
			continue
		var tile := Button.new()
		tile.toggle_mode = true
		tile.focus_mode = Control.FOCUS_NONE
		tile.custom_minimum_size = TILE
		tile.expand_icon = true
		tile.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
		tile.disabled = not ship.get("orderable", false)
		tile.tooltip_text = "%s (%s)" % [ship.name, ship.type]
		tile.set_pressed_no_signal(int(ship.id) in keep)
		# As the fleet box's ships (owner, 2026-11-15): clear, blue when picked, no button face.
		for state in ["normal", "disabled", "focus"]:
			tile.add_theme_stylebox_override(state, _tile_style(false, false))
		tile.add_theme_stylebox_override("hover", _tile_style(false, true))
		tile.add_theme_stylebox_override("pressed", _tile_style(true, false))
		tile.add_theme_stylebox_override("hover_pressed", _tile_style(true, true))
		var lbx := "SHIPS.LBX@%d" % int(ship.palette) if ship.has("palette") else "SHIPS.LBX"
		if art != null:
			tile.icon = art.texture(lbx, int(ship.picture))
		var id := int(ship.id)
		tile.mouse_entered.connect(func(): _show_detail(id))
		tile.toggled.connect(func(_on: bool): _on_pick())
		_grid.add_child(tile)
		_tiles[id] = tile
	_scrap.disabled = selected_ships().is_empty()
	_all.disabled = _tiles.is_empty()


func _on_pick() -> void:
	_scrap.disabled = selected_ships().is_empty()
	var picked := selected_ships()
	if not picked.is_empty():
		_show_detail(picked[0])


func _on_all() -> void:
	var any_off := false
	for id: int in _tiles:
		var tile: Button = _tiles[id]
		if not tile.disabled and not tile.button_pressed:
			any_off = true
	for id: int in _tiles:
		var tile: Button = _tiles[id]
		if not tile.disabled:
			tile.set_pressed_no_signal(any_off)
	_on_pick()


func _set_filter(kind: String) -> void:
	filter = kind
	_support.set_pressed_no_signal(kind == "support")
	_combat.set_pressed_no_signal(kind == "combat")
	Themes.glow(_support)
	Themes.glow(_combat)
	_fill_grid(selected_ships())


func _cycle(dir: int) -> void:
	if fleet.is_empty():
		return
	command.emit({"verb": "query", "what": "next_fleet", "fleet": int(fleet.ship), "dir": dir, "_reply_to": "_after_cycle"})


func _after_cycle(result: Dictionary) -> void:
	if result.get("ok", false) and int(result.ship) >= 0:
		_show(int(result.ship))


## After a move or a scrap: the fleet again (its first ship may have changed: the first of the
## player's fleets then).
func _after_order(_result: Dictionary) -> void:
	if fleet.is_empty():
		return
	var ship := int(fleet.ship)
	fleet = {}
	command.emit({"verb": "query", "what": "fleet", "ship": ship, "_reply_to": "_after_reload"})


func _after_reload(result: Dictionary) -> void:
	if result.get("ok", false):
		show_fleet(result)
	else:
		open_panel(_area_for_reopen())


func _area_for_reopen() -> Rect2:
	return Rect2(0.0, _rect.position.y, _rect.end.x, _rect.size.y)


# ---- Relocation ----

func _set_relocating(on: bool) -> void:
	if relocating and not on:
		command.emit({"verb": "relocate_reset"})
	relocating = on
	if _relocate != null:
		_relocate.set_pressed_no_signal(on)
		Themes.glow(_relocate)
	if _hint != null:
		_hint.text = "Click the star to relocate new ships from" if on else ""
	Input.set_default_cursor_shape(Input.CURSOR_CROSS if on else Input.CURSOR_ARROW)


func _after_relocate(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	if int(result.from) >= 0:
		_hint.text = "Now click the star its new ships go to (the same star cancels)"
	elif result.get("set", false):
		_hint.text = "Relocation set" if int(result.relocation) >= 0 else "Relocation cancelled"
	else:
		_hint.text = "Click the star to relocate new ships from"


# ---- The detail strip ----

func _show_detail(ship: int) -> void:
	if ship < 0:
		_clear(_detail)
		_detail_ship = -1
		return
	if ship == _detail_ship:
		return
	_detail_ship = ship
	command.emit({"verb": "query", "what": "ship_detail", "ship": ship, "_reply_to": "_on_detail"})


func _on_detail(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.ship) != _detail_ship:
		return
	_clear(_detail)
	if result.has("officer"):
		var officer: Dictionary = result.officer
		var portrait: TextureRect = preload("sprite_rect.gd").new()
		portrait.custom_minimum_size = Vector2(64, 80)
		portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		portrait.tooltip_text = String(officer.name)
		portrait.bind(art)
		portrait.show_sprite(String(officer.lbx), int(officer.entry))
		_detail.add_child(portrait)
	var left := VBoxContainer.new()
	left.add_theme_constant_override("separation", 0)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail.add_child(left)
	_line(left, String(result.name), &"Name")
	if result.has("help"): # colony, outpost and transport ships: their help text
		var help := _line(left, String(result.help), &"Dim")
		help.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		help.clip_text = false
		return
	_line(left, String(result.crew), &"Dim")
	_line(left, String(result.shield), &"Dim")
	_line(left, "%s %+d" % [String(result.attack_label).strip_edges(), int(result.attack)], &"Dim")
	_line(left, "%s %+d" % [String(result.defense_label).strip_edges(), int(result.defense)], &"Dim")
	if result.has("officer"):
		_line(left, String(result.officer.name), &"Dim")
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 0)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail.add_child(right)
	var design: Dictionary = result.get("design", {})
	_line(right, "Weapons", &"Name")
	var weapons: Array = design.get("weapons", [])
	for w: Dictionary in weapons:
		_line(right, String(w.text), &"Dim")
	if weapons.is_empty():
		_line(right, "None", &"Dim")
	var specials: Array = design.get("specials", [])
	if not specials.is_empty():
		_line(right, "Specials", &"Name")
		for sp: Dictionary in specials:
			_line(right, String(sp.text), &"Dim")


func _line(holder: Control, text: String, variation: StringName) -> Label:
	var label := Label.new()
	label.theme_type_variation = variation
	label.text = text
	label.clip_text = true
	label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	label.add_theme_font_size_override("font_size", 13 if variation == &"Dim" else 15)
	holder.add_child(label)
	return label


# ---- Helpers ----

static var _tile_styles := {}


## A ship tile's look, the fleet box's (fleet_panel.tscn "selected_ship"): picked a blue fill with a
## light edge, otherwise clear with a faint edge (lighter under the pointer).
static func _tile_style(picked: bool, hovered: bool) -> StyleBox:
	var key := "%s%s" % [picked, hovered]
	if not _tile_styles.has(key):
		var style := StyleBoxFlat.new()
		style.set_border_width_all(2)
		if picked:
			style.bg_color = Color(0.1, 0.25, 0.75, 0.9)
			style.border_color = Color(0.55, 0.75, 1, 1) if not hovered else Color(0.8, 0.9, 1, 1)
		else:
			style.bg_color = Color(0, 0, 0, 0.0)
			style.border_color = Color(0.55, 0.75, 1, 0.55) if hovered else Color(1, 1, 1, 0.08)
		_tile_styles[key] = style
	return _tile_styles[key]


func _button(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size.y = 32
	return b


func _toggle(text: String, tip: String) -> Button:
	var b := _button(text, tip)
	b.toggle_mode = true
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	Themes.glowing(b)
	return b


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if relocating and key and key.pressed and key.keycode == KEY_ESCAPE:
		_set_relocating(false) # Esc leaves the relocation mode first
		get_viewport().set_input_as_handled()
		return
	super._unhandled_key_input(event)
