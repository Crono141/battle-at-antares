extends "side_panel.gd"
## Planets (WP3.5b, the owner's empire panels in notes/antares/ui-concepts.md): the legacy
## Planets screen as a panel that flies out from the right, against the side tabs, with the
## native galaxy zoomed out beside it in place of the screen's small map. From the `planets`
## query: the planets the player could settle, one row each with the screen's own lines (the
## planet and who is there, climate and food, gravity and its penalty, minerals and production,
## size and population), filtered by the screen's five toggles (shown planets: in range, mineral
## abundance, non-hostile environment, normal gravity, no enemy presence) and sorted by a click on
## a column's title (again: the other way).
##
## SEND COLONY SHIP / SEND OUTPOST SHIP arm (lit while armed, available while a free ship of that
## kind exists); a click on a planet then sends the nearest free one (`planets_send`: the screen's
## own choice and questions). A ship on its way shows its marker and ETA in the row's last column;
## a click on it turns the ship back (`planets_recall`, the screen's question). A click on a star
## selects its planet's row, and the galaxy marks the hovered or selected row's star.

signal command(dict: Dictionary)
## The star the galaxy marks (the hovered or selected planet's), or -1.
signal star_marked(star: int)
## The panel's width changed: the galaxy fits the space left of it again.
signal width_changed

const Themes := preload("../themes.gd")
const COLUMNS := [
	{"key": "planet", "title": "PLANET", "width": 132.0},
	{"key": "climate", "title": "CLIMATE", "width": 94.0},
	{"key": "gravity", "title": "GRAVITY", "width": 94.0},
	{"key": "minerals", "title": "MINERALS", "width": 106.0},
	{"key": "size", "title": "SIZE", "width": 92.0},
	{"key": "ship", "title": "SHIPS", "width": 66.0},
]
## The screen's toggles, named short (owner, 2026-11-14; the mockup's long names in the tips): each shows only the planets that pass
## (the query's `hidden_by` flag hides it when on). Out of range is on by default, as the screen.
const FILTERS := [
	{"flag": "out_of_range", "title": "In Range", "tip": "Planets in range", "on": true},
	{"flag": "mineral_poor", "title": "Abundance", "tip": "Mineral abundance: no poor or ultra poor planets", "on": false},
	{"flag": "hostile", "title": "Environment", "tip": "Non-hostile environment: planets that grow food", "on": false},
	{"flag": "adverse_gravity", "title": "Gravity", "tip": "Normal gravity for your race: no production penalty", "on": false},
	{"flag": "enemy", "title": "No Hostiles", "tip": "No enemy presence: no other empire's colonies or ships there", "on": false},
]
const ROW_HEIGHT := 50.0
const ROW_PAD := Vector2(22, 2)
const SEPARATION := 4.0
const PICTURE := Vector2(34, 34)
const MARKER := Vector2(22, 16)
## Largest first except names (the screen's own sorts were climate, minerals and max pop, all
## largest first); gravity: the smallest penalty first.
const DESCENDING_FIRST := ["climate", "minerals", "size"]

var art: Node
var planets: Array = [] # the rows the toggles let through, in the shown order
var _all: Array = [] # every row of the last query
var _result := {}
var sort_key := "climate" # the screen's default sort
var sort_descending := true
var selected := -1 # the selected planet
var send_kind := "" # "colony" or "outpost" while a send button is armed
var _filters_on := {}
var _rows := {} # planet -> its row panel

@onready var rows_box: VBoxContainer = $Box/Table/Scroll/Rows
@onready var columns_box: HBoxContainer = $Box/Table/Columns
@onready var scroll: ScrollContainer = $Box/Table/Scroll
@onready var colony_button: Button = $Box/Send/Colony
@onready var outpost_button: Button = $Box/Send/Outpost
@onready var hint: Label = $Box/Hint


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)
	for f: Dictionary in FILTERS:
		_filters_on[f.flag] = f.on
	_show_filters()
	colony_button.toggled.connect(func(on: bool): _on_send_button("colony", on))
	outpost_button.toggled.connect(func(on: bool): _on_send_button("outpost", on))
	Themes.glowing(colony_button)
	Themes.glowing(outpost_button)
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				position.x = _rect.end.x - size.x
			width_changed.emit())


## Flies out into `area` (its right edge against the side tabs), as wide as the table.
func open_panel(area: Rect2) -> void:
	custom_minimum_size = Vector2.ZERO
	_clear(rows_box)
	_rows.clear()
	_show_columns()
	var width := _wanted_width(area)
	refresh()
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))


## As wide as its table (the columns are fixed), the scroll bar's room included.
## The Colonies panel's width (owner, 2026-11-15: a different width moved the galaxy's zoom when
## switching between them; `width_like` is that panel, its shared_width); the planet
## columns share what they don't need, each by its width.
var width_like: Control


func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	var natural := get_combined_minimum_size().x # the scroll bar's room is in the heading's tail
	var width := minf(clampf(area.size.x * 0.45, 700.0, 960.0), area.size.x - 16.0)
	if width_like != null:
		return width_like.shared_width(area) # the planets' columns fit in it and share the rest
	return minf(maxf(width, natural), area.size.x - 16.0)


func _col_width(i: int) -> float:
	return float(COLUMNS[i].width)


func panel_width() -> float:
	return _rect.end.x - rest_left()


func refresh() -> void:
	command.emit({"verb": "query", "what": "planets", "_reply_to": "show_planets"})


func show_planets(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	_result = result
	_all = result.planets
	_apply()
	_show_send_buttons()


## Selects the first shown planet at `star` (a click on the galaxy); false if none is shown there.
func select_star(star: int) -> bool:
	for p: Dictionary in planets:
		if int(p.star) == star:
			if send_kind != "":
				_send(p)
			else:
				_select(int(p.planet), int(p.star))
			var row: Control = _rows.get(int(p.planet))
			if row != null:
				scroll.ensure_control_visible.call_deferred(row)
			return true
	return false


func close() -> void:
	if visible and not _closing:
		star_marked.emit(-1)
	_arm("")
	super.close()


func dismiss() -> void:
	_arm("")
	super.dismiss()


# ---- Filters and sorting ----

func _show_filters() -> void:
	var box: HFlowContainer = $Box/Filters
	_clear(box)
	for f: Dictionary in FILTERS:
		var flag := String(f.flag)
		var b := Button.new()
		b.toggle_mode = true
		b.focus_mode = Control.FOCUS_NONE
		b.text = String(f.title).to_upper()
		b.add_theme_font_size_override("font_size", 12)
		b.custom_minimum_size.y = 28
		b.set_pressed_no_signal(bool(_filters_on.get(flag, false)))
		b.tooltip_text = "Show only: %s" % String(f.tip).to_lower()
		Themes.glowing(b)
		b.toggled.connect(func(on: bool):
			_filters_on[flag] = on
			_apply())
		box.add_child(b)


func _apply() -> void:
	planets = _all.filter(func(p: Dictionary) -> bool:
		for flag: String in _filters_on:
			if _filters_on[flag] and bool(p.hidden_by.get(flag, false)):
				return false
		return true)
	_sort()
	_show_rows()


func _show_columns() -> void:
	_clear(columns_box)
	columns_box.add_theme_constant_override("separation", int(SEPARATION))
	var lead := Control.new()
	lead.custom_minimum_size.x = maxf(_row_pad(SIDE_LEFT) - SEPARATION, 0.0)
	columns_box.add_child(lead)
	for i in COLUMNS.size():
		var column: Dictionary = COLUMNS[i]
		var key := String(column.key)
		var b := Button.new()
		b.flat = true
		b.theme_type_variation = &"ListItem"
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size = Vector2(_col_width(i), 28)
		# Every column takes a share of the spare width by its own (the rows' cells the same).
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.size_flags_stretch_ratio = _col_width(i)
		b.clip_text = true
		b.text = String(column.title)
		if key == sort_key:
			b.text += "  ▼" if sort_descending else "  ▲"
			b.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "ListItem"))
		if key != "ship":
			b.tooltip_text = "Sort by this column (again: the other way)"
			b.pressed.connect(_sort_by.bind(key))
		columns_box.add_child(b)
	var tail := Control.new()
	# The rows' right padding and the scroll bar beside them (always shown, so the expanding planet
	# column lines up with its heading).
	scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_ALWAYS
	tail.custom_minimum_size.x = _row_pad(SIDE_RIGHT) + scroll.get_v_scroll_bar().get_combined_minimum_size().x
	columns_box.add_child(tail)


func _sort_by(key: String) -> void:
	if key == sort_key:
		sort_descending = not sort_descending
	else:
		sort_key = key
		sort_descending = key in DESCENDING_FIRST
	_show_columns()
	_sort()
	_show_rows()


func _sort() -> void:
	planets.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var order := _compare(a, b)
		if order == 0:
			order = String(a.name).naturalnocasecmp_to(String(b.name))
		if order == 0:
			order = int(a.planet) - int(b.planet)
		return order > 0 if sort_descending else order < 0)


func _compare(a: Dictionary, b: Dictionary) -> int:
	match sort_key:
		"planet":
			return String(a.name).naturalnocasecmp_to(String(b.name))
		"climate":
			return int(a.climate_id) - int(b.climate_id)
		"gravity":
			return int(a.gravity_penalty) - int(b.gravity_penalty)
		"minerals":
			return int(a.mineral_value) - int(b.mineral_value)
		"size":
			return int(a.max_pop) - int(b.max_pop)
	return 0


# ---- Rows ----

func _show_rows() -> void:
	_clear(rows_box)
	_rows.clear()
	for p: Dictionary in planets:
		rows_box.add_child(_row(p))
	if planets.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "No planets" if _all.is_empty() else "No planets pass the toggles above"
		rows_box.add_child(none)


func _row(p: Dictionary) -> Control:
	var id := int(p.planet)
	var panel := PanelContainer.new()
	var wrap := MarginContainer.new()
	wrap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(wrap)
	panel.mouse_filter = Control.MOUSE_FILTER_PASS
	panel.mouse_entered.connect(func(): star_marked.emit(int(p.star)))
	panel.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			if send_kind != "":
				_send(p)
			else:
				_select(id, int(p.star)))
	_rows[id] = panel
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", int(SEPARATION))
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	wrap.add_child(line)
	var name_cell := _planet_cell(p)
	panel.set_meta("name_label", name_cell.get_meta("name_label"))
	_add_fixed(line, name_cell, _col_width(0))
	_add_fixed(line, _two_lines(String(p.climate), String(p.food)), _col_width(1))
	_add_fixed(line, _two_lines(String(p.gravity), String(p.get("gravity_note", ""))), _col_width(2))
	_add_fixed(line, _two_lines(String(p.minerals), String(p.minerals_note)), _col_width(3))
	_add_fixed(line, _two_lines(String(p.size), String(p.size_note)), _col_width(4))
	_add_fixed(line, _ship_cell(p), _col_width(5))
	panel.ready.connect(func(): _style_row(panel, id == selected))
	return panel


## The planet's picture, its name, and who is there or its special.
func _planet_cell(p: Dictionary) -> Control:
	var box := HBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var picture: Dictionary = p.picture
	box.add_child(_sprite(String(picture.lbx), int(picture.entry), PICTURE))
	var text := VBoxContainer.new()
	text.alignment = BoxContainer.ALIGNMENT_CENTER
	text.add_theme_constant_override("separation", -2)
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(text)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(p.name)
	name_label.clip_text = true
	text.add_child(name_label)
	box.set_meta("name_label", name_label)
	var note := ""
	if p.has("owner"):
		note = "(%s)" % String(p.owner)
	elif String(p.get("special", "")) != "":
		note = String(p.special)
	if note != "":
		var label := Label.new()
		label.theme_type_variation = &"Dim"
		label.text = note
		label.clip_text = true
		label.add_theme_font_size_override("font_size", 12)
		if p.has("owner"):
			label.add_theme_color_override("font_color", Color(1.0, 0.45, 0.4))
		text.add_child(label)
	return box


func _two_lines(top: String, bottom: String) -> Control:
	var box := VBoxContainer.new()
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override("separation", -2)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var a := Label.new()
	a.text = top
	a.clip_text = true
	a.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(a)
	if bottom != "":
		var b := Label.new()
		b.theme_type_variation = &"Dim"
		b.text = bottom
		b.clip_text = true
		b.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		b.add_theme_font_size_override("font_size", 12)
		box.add_child(b)
	return box


## Ships on their way (the marker and ETA; a click turns the ship back), else while a send button
## is armed the nearest free ship's ETA.
func _ship_cell(p: Dictionary) -> Control:
	var box := VBoxContainer.new()
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override("separation", 0)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for kind in ["colony", "outpost"]:
		var eta := int(p.get(kind + "_eta", -1))
		if eta < 0:
			continue
		var b := Button.new()
		b.flat = true
		b.theme_type_variation = &"ListItem" # readable on the row (a plain flat button was dark)
		b.focus_mode = Control.FOCUS_NONE
		b.text = "%s %d" % ["C" if kind == "colony" else "O", eta]
		b.add_theme_font_size_override("font_size", 12)
		b.tooltip_text = "A%s ship is on its way: %d turn%s. Click to turn it back" % [" colony" if kind == "colony" else "n outpost", eta, "" if eta == 1 else "s"]
		var marker := _sprite("PLNTSUM.LBX", int(_result.get("ship_icon", -1)), MARKER)
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 2)
		row.alignment = BoxContainer.ALIGNMENT_CENTER
		row.mouse_filter = Control.MOUSE_FILTER_IGNORE
		row.add_child(marker)
		row.add_child(b)
		var planet_id := int(p.planet)
		b.pressed.connect(func(): command.emit({"verb": "planets_recall", "planet": planet_id, "kind": kind, "_reply_to": "_after_command"}))
		box.add_child(row)
	if box.get_child_count() == 0 and send_kind != "":
		var eta := int(p.get("best_%s_eta" % send_kind, -1))
		var label := Label.new()
		label.theme_type_variation = &"Dim"
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		label.add_theme_font_size_override("font_size", 12)
		label.text = ("%d turn%s" % [eta, "" if eta == 1 else "s"]) if eta >= 0 else ("Blocked" if p.get("blocked", false) else "Out of reach")
		box.add_child(label)
	return box


func _select(planet_id: int, star: int) -> void:
	selected = planet_id
	for id: int in _rows:
		_style_row(_rows[id], id == selected)
	star_marked.emit(star)


# ---- Sending ----

func _show_send_buttons() -> void:
	var flux: bool = _result.get("flux", false)
	for pair in [[colony_button, "colony", "colony_ships"], [outpost_button, "outpost", "outpost_ships"]]:
		var b: Button = pair[0]
		var available: bool = _result.get(pair[2], false) and not flux
		b.disabled = not available
		if not available and send_kind == pair[1]:
			_arm("")
		b.tooltip_text = ("Ship movement is not possible during the hyperspace flux" if flux else
			("No free %s ship" % pair[1]) if not available else
			"Arm, then click a planet: the nearest free %s ship goes there" % pair[1])


## A send button pressed: it arms; with a planet already selected the ship goes there at once
## (owner, 2026-11-20: select, then send, as well as arm, then click), and the button stays armed
## for the next planet, as after a click.
func _on_send_button(kind: String, on: bool) -> void:
	_arm(kind if on else "")
	if not on or selected < 0:
		return
	for p: Dictionary in planets:
		if int(p.planet) == selected:
			_send(p)
			return


## A send button armed ("colony" / "outpost") or disarmed (""); the other goes off.
func _arm(kind: String) -> void:
	send_kind = kind
	colony_button.set_pressed_no_signal(kind == "colony")
	outpost_button.set_pressed_no_signal(kind == "outpost")
	Themes.glow(colony_button)
	Themes.glow(outpost_button)
	hint.text = "" if kind == "" else "Click a planet: the nearest free %s ship goes there" % kind
	if is_node_ready() and visible:
		_show_rows() # the ships column shows the nearest ship's ETA while armed


func _send(p: Dictionary) -> void:
	command.emit({"verb": "planets_send", "planet": int(p.planet), "kind": send_kind, "_reply_to": "_after_command"})


func _after_command(_result_unused: Dictionary) -> void:
	refresh()


# ---- Helpers (as the Colonies panel's) ----

func _style_row(panel: PanelContainer, lit: bool) -> void:
	panel.theme_type_variation = &"RowOpen" if lit else &"Row"
	var style := panel.get_theme_stylebox("panel", panel.theme_type_variation)
	var wrap := panel.get_child(0) as MarginContainer if panel.get_child_count() > 0 else null
	if wrap == null or style == null:
		return
	wrap.add_theme_constant_override("margin_left", -int(maxf(style.get_margin(SIDE_LEFT) - ROW_PAD.x, 0.0)))
	wrap.add_theme_constant_override("margin_right", -int(maxf(style.get_margin(SIDE_RIGHT) - ROW_PAD.x, 0.0)))
	wrap.add_theme_constant_override("margin_top", -int(maxf(style.get_margin(SIDE_TOP) - ROW_PAD.y, 0.0)))
	wrap.add_theme_constant_override("margin_bottom", -int(maxf(style.get_margin(SIDE_BOTTOM) - ROW_PAD.y, 0.0)))
	var name_label: Label = panel.get_meta("name_label", null)
	if name_label != null:
		if lit:
			name_label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
		else:
			name_label.remove_theme_color_override("font_color")


func _row_pad(side: Side) -> float:
	var style := get_theme_stylebox("panel", &"Row")
	return minf(style.get_margin(side), ROW_PAD.x) if style != null else ROW_PAD.x


func _add_fixed(line: HBoxContainer, content: Control, width: float) -> Control:
	var holder := Control.new()
	holder.custom_minimum_size = Vector2(width, ROW_HEIGHT)
	holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL # a share of the spare width, as the heading's
	holder.size_flags_stretch_ratio = width
	holder.clip_contents = true
	holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
	holder.add_child(content)
	content.custom_minimum_size.x = 0.0
	content.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.add_child(holder)
	return holder


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


func _sprite(lbx: String, entry: int, min_size: Vector2) -> TextureRect:
	var rect: TextureRect = preload("sprite_rect.gd").new()
	rect.custom_minimum_size = min_size
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	rect.bind(art)
	if lbx != "" and entry >= 0:
		rect.show_sprite(lbx, entry)
	return rect
