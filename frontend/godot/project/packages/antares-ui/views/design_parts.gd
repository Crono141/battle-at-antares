extends PanelContainer
## The Ship Design panel's parts list (WP3.6, the owner's mockup C, 2026-11-15): flies out to the
## left of the design panel when + ADD WEAPON or + ADD SPECIAL is clicked (or a fitted part), and
## folds away with its X. Every weapon and special the player can fit in one list (FIND, ALL /
## BEAM / MISSILE / BOMB / SPECIAL WPN / SYSTEMS, FITS NOW), each line marked by its kind; the picked
## part's card at the bottom: a weapon's arc dial (a click turns it: left the next arc, right the
## one before) or a missile's shots, its mods, what it takes, ADD (REPLACE for a fitted one); a
## special's group, effect, space and cost, ADD. Columns: name, description, damage, BC, space;
## a click on a heading sorts by it (again: the other way). A right click on a line or a mod is
## its help, as the original picker's.
## The design panel owns it: it keeps it against its left edge and carries its commands.

const Themes := preload("../themes.gd")
const ArcGlyph := preload("arc_glyph.gd")
const WIDTH := 500.0
const FILTERS := [["all", "ALL"], ["beam", "BEAM"], ["missile", "MISSILE"], ["bomb", "BOMB"], ["special", "SPECIAL WPN"], ["systems", "SYSTEMS"]]
const MARK_WIDTH := 16.0
const NAME_WIDTH := 150.0
const COLUMN_WIDTHS := [52.0, 30.0, 42.0] # damage, BC, space
const ARC_TIPS := ["Forward", "Forward extended", "Back extended", "Back", "360 degrees"]
const ARC_LINES := ["FORWARD\n ", "FORWARD\nEXTENDED", "BACK\nEXTENDED", "BACK\n ", "360°\n "] # two lines each: one height
const MARKS := {"beam": ["●", Color(0.91, 0.56, 0.16)], "missile": ["▲", Color(1.0, 0.42, 0.6)], "bomb": ["■", Color(0.61, 0.55, 1.0)],
	"special": ["✦", Color(0.95, 0.85, 0.35)], "systems": ["◆", Color(0.35, 0.63, 1.0)]}

var panel: Control # the design panel
var art: Node
var design := {}
var weapons: Array = []
var specials: Array = []

var filter := "all"
var fits_only := false
var find := ""
## The picked part: "weapon" or "special" and its id; for a weapon its arc, rack and mods, and the
## fitted row it replaces (-1: a new one).
var picked_kind := ""
var picked := -1
var replace_slot := -1
var arc := 0
var rack := 1
var mods: Array[int] = []
var preview := {}
var sort_key := "" # "", "name", "damage", "cost", "space"
var sort_descending := false

var _find: LineEdit
var _header: HBoxContainer
var _summary: Label
var _dial: Control
var _dial_label: Label
var _filters: HBoxContainer
var _rows: VBoxContainer
var _card: VBoxContainer
var _card_frame: PanelContainer


func _ready() -> void:
	theme_type_variation = &"Pane"
	custom_minimum_size.x = WIDTH
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	add_child(box)
	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 8)
	box.add_child(header)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "Parts"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	_find = LineEdit.new()
	_find.placeholder_text = "find: name or effect"
	_find.custom_minimum_size.x = 170
	_find.text_changed.connect(func(t: String):
		find = t
		_show_rows())
	header.add_child(_find)
	var close_button := _button("X", "Fold the parts list away")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(fold)
	header.add_child(close_button)
	_filters = HBoxContainer.new()
	_filters.add_theme_constant_override("separation", 3)
	box.add_child(_filters)
	_header = HBoxContainer.new()
	_header.add_theme_constant_override("separation", 6)
	box.add_child(_header)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 2)
	scroll.add_child(_rows)
	_card_frame = PanelContainer.new()
	_card_frame.theme_type_variation = &"RowOpen"
	box.add_child(_card_frame)
	_card = VBoxContainer.new()
	_card.add_theme_constant_override("separation", 6)
	_card_frame.add_child(_card)


## Opens on `kind` ("weapons": the weapons, "systems": the specials), nothing picked.
func open_for(kind: String) -> void:
	filter = "all" if kind == "weapons" else "systems"
	picked_kind = ""
	picked = -1
	replace_slot = -1
	preview = {}
	show()
	refresh()


## Opens with a fitted weapon picked, to change its arc or mods (REPLACE).
func open_for_weapon(w: Dictionary) -> void:
	filter = String(w.category)
	picked_kind = "weapon"
	picked = int(w.weapon)
	replace_slot = int(w.slot)
	arc = int(w.arc)
	mods.clear()
	for m: Dictionary in w.mods:
		mods.append(int(m.mod))
	rack = _rack_of(int(w.weapon), int(w.get("ammo", 0)))
	show()
	_ask_preview()
	refresh()


## Opens with a fitted special picked (its card).
func open_for_special(special: int) -> void:
	filter = "systems"
	picked_kind = "special"
	picked = special
	replace_slot = -1
	show()
	refresh()


func fold() -> void:
	hide()
	picked = -1
	replace_slot = -1
	if panel:
		panel.parts_folded()


func set_data(design_now: Dictionary, weapons_now: Array, specials_now: Array) -> void:
	design = design_now
	weapons = weapons_now
	specials = specials_now
	# New data (a count changed, the weapons then the specials come back): the rows and the card
	# only, once a frame, and only the rows that changed (owner 2026-11-23: + and - lagged, each
	# reply rebuilt the whole list, about 0.2 s).
	if visible and not _data_queued:
		_data_queued = true
		_refresh_data.call_deferred()


var _data_queued := false


func _refresh_data() -> void:
	_data_queued = false
	if not visible or _filters == null:
		return
	_show_rows()
	_show_card()


func set_preview(result: Dictionary) -> void:
	if picked_kind == "weapon" and int(result.get("weapon", -1)) == picked:
		preview = result
		var w := _weapon(picked)
		if is_instance_valid(_summary) and not w.is_empty():
			_summary.text = _weapon_summary(w) # in place: rebuilding the card made it jump


func refresh() -> void:
	if _filters == null:
		return
	_clear(_filters)
	for f: Array in FILTERS:
		var b := _toggle(String(f[1]), filter == String(f[0]))
		var key := String(f[0])
		b.pressed.connect(func():
			filter = key
			refresh())
		_filters.add_child(b)
	var fits := _toggle("FITS NOW", fits_only)
	fits.tooltip_text = "Only the parts that fit the space left"
	fits.pressed.connect(func():
		fits_only = not fits_only
		refresh())
	_filters.add_child(fits)
	_show_header()
	_show_rows()
	_show_card()


# ---- The list ----

var _row_keys: Array = [] # each shown row's look (_row_key), in order; [] while "Nothing here" shows


## What a row shows: equal keys, the same row (its pick callable reads only the part's id and rack).
func _row_key(e: Dictionary) -> String:
	return "%s|%s|%s|%s|%d|%d|%s|%s|%d|%d" % [e.kind, e.name, e.note, e.damage, int(e.cost), int(e.space), e.picked, e.dim,
		int(e.get("app", -1)), int(e.get("weapon", {}).get("rack", 1)) if e.has("weapon") else int(e.get("special", -1))]


func _show_rows() -> void:
	var needle := find.strip_edges().to_lower()
	var entries: Array = []
	if filter != "systems":
		for w: Dictionary in weapons:
			var category := String(w.category)
			if filter != "all" and filter != category:
				continue
			if not _matches(needle, String(w.name), String(w.notes)):
				continue
			if fits_only and not w.get("fits", false):
				continue
			entries.append({"kind": category, "name": String(w.name), "note": String(w.notes), "damage": String(w.damage),
				"cost": int(w.cost), "space": int(w.space), "app": int(w.get("app", -1)), "weapon": w,
				"picked": picked_kind == "weapon" and picked == int(w.weapon),
				"dim": not w.get("fits", false) and not (picked_kind == "weapon" and replace_slot >= 0 and picked == int(w.weapon))})
	if filter == "all" or filter == "systems":
		for sp: Dictionary in specials:
			var id := int(sp.special)
			if sp.get("fitted", false) and not (picked_kind == "special" and picked == id):
				continue
			if not _matches(needle, String(sp.name), String(sp.description)):
				continue
			if fits_only and not sp.get("fits", false):
				continue
			var note := String(sp.description)
			if sp.has("excluded_by"):
				note = "⇄ " + String(sp.excluded_by)
			entries.append({"kind": "systems", "name": String(sp.name), "note": note, "damage": "", "cost": int(sp.cost),
				"space": int(sp.space), "app": int(sp.get("app", -1)), "special": id, "picked": picked_kind == "special" and picked == id,
				"dim": sp.has("excluded_by") or not sp.get("fits", false)})
	if sort_key != "":
		entries.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			var va: Variant = _sort_value(a)
			var vb: Variant = _sort_value(b)
			if va == vb:
				return String(a.name) < String(b.name)
			return (va > vb) if sort_descending else (va < vb))
	var keys: Array = entries.map(_row_key)
	# The same rows in the same order: only those whose look changed are made again, in place.
	var reuse := not entries.is_empty() and keys.size() == _row_keys.size() and _rows.get_child_count() == keys.size()
	if not reuse:
		_clear(_rows)
	for i in entries.size():
		if reuse and keys[i] == _row_keys[i]:
			continue
		var e: Dictionary = entries[i]
		var pick: Callable
		if e.has("weapon"):
			var w: Dictionary = e.weapon
			pick = func(): _pick_weapon(w)
		else:
			var id := int(e.special)
			pick = func(): _pick_special(id)
		var line := _line(e, pick)
		if reuse:
			var old := _rows.get_child(i)
			_rows.add_child(line)
			_rows.move_child(line, i)
			_rows.remove_child(old)
			old.queue_free()
		else:
			_rows.add_child(line)
	_row_keys = keys
	if entries.is_empty():
		_row_keys = []
		_rows.add_child(_dim("Nothing here" + (": turn FITS NOW off to see what needs more room" if fits_only else "")))


## A line's value for the sort: names by letters, damage by its average ("special" last).
func _sort_value(e: Dictionary) -> Variant:
	match sort_key:
		"name":
			return String(e.name).to_lower()
		"damage":
			var parts := String(e.damage).replace("–", "-").split("-")
			if parts.is_empty() or not String(parts[0]).strip_edges().is_valid_int():
				return -1.0
			var low := float(String(parts[0]).strip_edges())
			var high := float(String(parts[parts.size() - 1]).strip_edges()) if String(parts[parts.size() - 1]).strip_edges().is_valid_int() else low
			return (low + high) * 0.5
		"cost":
			return int(e.cost)
		"space":
			return int(e.space)
	return 0


func _matches(needle: String, name_text: String, note: String) -> bool:
	return needle == "" or name_text.to_lower().contains(needle) or note.to_lower().contains(needle)


func _pick_weapon(w: Dictionary) -> void:
	picked_kind = "weapon"
	picked = int(w.weapon)
	replace_slot = -1
	arc = 0
	mods.clear()
	rack = int(w.get("rack", 1))
	preview = {}
	_ask_preview()
	refresh()


func _pick_special(id: int) -> void:
	picked_kind = "special"
	picked = id
	replace_slot = -1
	refresh()


func _ask_preview() -> void:
	if picked_kind == "weapon" and picked > 0 and panel:
		panel.ask_preview({"weapon": picked, "arc": arc, "rack": rack, "mods": mods.duplicate()})


# ---- The picked part's card ----

func _show_card() -> void:
	_clear(_card)
	if picked_kind == "weapon":
		var w := _weapon(picked)
		if not w.is_empty():
			_weapon_card(w)
			return
	elif picked_kind == "special":
		var sp := _special(picked)
		if not sp.is_empty():
			_special_card(sp)
			return
	_card.add_child(_dim("Pick a part to see it here and fit it."))


func _weapon_card(w: Dictionary) -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	_card.add_child(row)
	_dial = null
	if w.get("arcs", false):
		# The arc: a click on the dial turns it (owner, 2026-11-15: no arc buttons).
		var dial_box := VBoxContainer.new()
		dial_box.add_theme_constant_override("separation", 0)
		row.add_child(dial_box)
		_dial = ArcGlyph.new()
		_dial.arc = arc
		_dial.custom_minimum_size = Vector2(84, 84)
		_dial.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		_dial.gui_input.connect(func(event: InputEvent):
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
				arc = posmod(arc + (1 if mb.button_index == MOUSE_BUTTON_LEFT else -1), 5)
				_dial.arc = arc
				_dial_label.text = ARC_LINES[arc]
				_dial.accept_event()
				_ask_preview())
		dial_box.add_child(_dial)
		_dial_label = Label.new()
		_dial_label.text = ARC_LINES[arc]
		# Two lines, always (owner: FORWARD EXTENDED, the widest, moved the card when picked).
		_dial_label.custom_minimum_size = Vector2(84, 28)
		_dial_label.vertical_alignment = VERTICAL_ALIGNMENT_TOP
		_dial_label.theme_type_variation = &"Name"
		_dial_label.add_theme_font_size_override("font_size", 11)
		_dial_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		dial_box.add_child(_dial_label)
	var right := VBoxContainer.new()
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 5)
	row.add_child(right)
	var top := HBoxContainer.new()
	right.add_child(top)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(w.name)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(name_label)
	var notes := _dim(String(w.notes))
	notes.clip_text = true
	notes.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	notes.custom_minimum_size.x = 180
	notes.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	top.add_child(notes)
	if w.has("racks"):
		var racks := HBoxContainer.new()
		racks.add_theme_constant_override("separation", 3)
		right.add_child(racks)
		racks.add_child(_dim("SHOTS"))
		var rack_buttons: Array[Button] = []
		for i in (w.racks as Array).size():
			var b := _toggle(str(int(w.racks[i])), i == rack)
			b.tooltip_text = "%d shots a combat" % int(w.racks[i])
			var r := i
			rack_buttons.append(b)
			b.pressed.connect(func():
				rack = r
				for other: Button in rack_buttons:
					other.set_pressed_no_signal(other == b)
					Themes.glow(other)
				_ask_preview())
			racks.add_child(b)
	var mod_flow := HFlowContainer.new()
	mod_flow.add_theme_constant_override("h_separation", 3)
	mod_flow.add_theme_constant_override("v_separation", 3)
	right.add_child(mod_flow)
	for m: Dictionary in w.get("mods", []):
		var mod := int(m.mod)
		var b := _toggle(String(m.name).to_upper(), mod in mods)
		b.add_theme_font_size_override("font_size", 10)
		var help := int(m.get("help", -1))
		b.gui_input.connect(func(event: InputEvent):
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT and help >= 0 and panel:
				panel.show_help(help, b)
				b.accept_event())
		b.toggled.connect(func(on: bool):
			if on and not mod in mods:
				mods.append(mod)
			elif not on:
				mods.erase(mod)
			Themes.glow(b)
			_ask_preview())
		mod_flow.add_child(b)
	var bottom := HBoxContainer.new()
	bottom.add_theme_constant_override("separation", 6)
	_card.add_child(bottom)
	_summary = _dim(_weapon_summary(w))
	_summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bottom.add_child(_summary)
	if replace_slot >= 0:
		var less := _button("− 1", "One fewer on the ship")
		less.pressed.connect(func(): panel.weapon_count(replace_slot, -1))
		bottom.add_child(less)
	var add := _button("REPLACE" if replace_slot >= 0 else "ADD", "Fit it with these choices" if replace_slot >= 0 else "Fit one on the ship")
	add.custom_minimum_size.x = 90
	Themes.glowing(add)
	add.pressed.connect(func():
		panel.add_weapon(replace_slot, picked, arc, rack, mods.duplicate())
		replace_slot = -1)
	bottom.add_child(add)


func _weapon_summary(w: Dictionary) -> String:
	if preview.is_empty() or int(preview.get("weapon", -1)) != picked:
		return "%s dmg · %d BC · %d space" % [String(w.damage), int(w.cost), int(w.space)]
	var text := "%s dmg · %d BC · %d space" % [String(preview.damage), int(preview.cost), int(preview.space)]
	if replace_slot < 0:
		var free_after := int(preview.free_after)
		text += " · %s" % (("%d free after" % free_after) if free_after >= 0 else ("%d too many" % -free_after))
	return text


func _special_card(sp: Dictionary) -> void:
	var top := HBoxContainer.new()
	_card.add_child(top)
	var mark := Label.new()
	mark.text = "◆ "
	mark.add_theme_color_override("font_color", MARKS.systems[1])
	top.add_child(mark)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(sp.name)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(name_label)
	var group := _dim(String(sp.get("group", "")).to_upper())
	top.add_child(group)
	var description := Label.new()
	description.text = String(sp.description)
	description.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_card.add_child(description)
	var bottom := HBoxContainer.new()
	bottom.add_theme_constant_override("separation", 6)
	_card.add_child(bottom)
	var free_after := int(design.get("hull_space", 0)) - int(design.get("space_used", 0)) - int(sp.space)
	var text := "%d space · %d BC · %s" % [int(sp.space), int(sp.cost), ("%d free after" % free_after) if free_after >= 0 else ("%d too many" % -free_after)]
	if sp.get("fitted", false):
		text = "Fitted · %d space · %d BC" % [int(sp.space), int(sp.cost)]
	elif sp.has("excluded_by"):
		text = "Can't go with %s, which is fitted" % String(sp.excluded_by)
	var summary := _dim(text)
	summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bottom.add_child(summary)
	var add := _button("ADD", "Fit it on the ship")
	add.custom_minimum_size.x = 90
	add.disabled = sp.get("fitted", false) or sp.has("excluded_by")
	Themes.glowing(add)
	add.pressed.connect(func(): panel.add_special(picked))
	bottom.add_child(add)


func _weapon(id: int) -> Dictionary:
	for w: Dictionary in weapons:
		if int(w.weapon) == id:
			return w
	return {}


func _special(id: int) -> Dictionary:
	for sp: Dictionary in specials:
		if int(sp.special) == id:
			return sp
	return {}


func _rack_of(weapon: int, ammo: int) -> int:
	var w := _weapon(weapon)
	var found := (w.get("racks", []) as Array).find(ammo)
	return found if found >= 0 else int(w.get("rack", 1))


# ---- Pieces ----

## A list line: the kind's mark, the name, the description, damage, BC, space; picked lights it,
## dimmed: no room, or ruled out. A right click is its help.
func _line(e: Dictionary, on_pick: Callable) -> Button:
	var b := Button.new()
	b.toggle_mode = true
	b.flat = true
	b.focus_mode = Control.FOCUS_NONE
	b.theme_type_variation = &"ListItem"
	b.custom_minimum_size.y = 26
	b.set_pressed_no_signal(bool(e.picked))
	b.pressed.connect(on_pick)
	var app := int(e.get("app", -1))
	b.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT and app >= 0 and panel:
			panel.show_help(app, b)
			b.accept_event())
	var line := HBoxContainer.new()
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.offset_left = 6
	line.offset_right = -6
	line.add_theme_constant_override("separation", 6)
	b.add_child(line)
	var dim := bool(e.dim)
	var mark: Array = MARKS.get(String(e.kind), MARKS.systems)
	var mark_label := _cell(String(mark[0]), MARK_WIDTH)
	mark_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	mark_label.add_theme_color_override("font_color", mark[1] if not dim else Color(0.4, 0.42, 0.48))
	line.add_child(mark_label)
	var name_label := _cell(String(e.name), NAME_WIDTH)
	name_label.add_theme_font_size_override("font_size", 14)
	line.add_child(name_label)
	var note := _cell(String(e.note), 0.0)
	note.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	note.add_theme_font_size_override("font_size", 12)
	line.add_child(note)
	line.add_child(_cell(String(e.damage), COLUMN_WIDTHS[0]))
	line.add_child(_cell(str(int(e.cost)), COLUMN_WIDTHS[1]))
	line.add_child(_cell(str(int(e.space)), COLUMN_WIDTHS[2]))
	for label: Label in line.get_children():
		if bool(e.picked):
			label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
		elif dim and label != mark_label:
			label.add_theme_color_override("font_color", Color(0.45, 0.48, 0.55))
		elif label == note:
			label.add_theme_color_override("font_color", get_theme_color("font_color", "Dim"))
	return b


## The headings over the list: NAME, DMG, BC, SPACE sort it (again: the other way).
func _show_header() -> void:
	_clear(_header)
	var pad := Control.new()
	pad.custom_minimum_size.x = 6 + MARK_WIDTH
	_header.add_child(pad)
	_header.add_child(_sort_heading("NAME", "name", NAME_WIDTH))
	var description := _cell("DESCRIPTION", 0.0)
	description.theme_type_variation = &"Name"
	description.add_theme_font_size_override("font_size", 11)
	description.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_header.add_child(description)
	_header.add_child(_sort_heading("DMG", "damage", COLUMN_WIDTHS[0]))
	_header.add_child(_sort_heading("BC", "cost", COLUMN_WIDTHS[1]))
	_header.add_child(_sort_heading("SPACE", "space", COLUMN_WIDTHS[2]))
	var right := Control.new()
	right.custom_minimum_size.x = 6 + 12 # the rows' padding and the scroll bar
	_header.add_child(right)


func _sort_heading(text: String, key: String, width: float) -> Button:
	var b := Button.new()
	b.flat = true
	b.focus_mode = Control.FOCUS_NONE
	b.theme_type_variation = &"ListItem"
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.custom_minimum_size = Vector2(width, 22)
	b.add_theme_font_size_override("font_size", 11)
	b.text = text + ((" ▼" if sort_descending else " ▲") if sort_key == key else "")
	b.tooltip_text = "Sort by this column (again: the other way)"
	if sort_key == key:
		b.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
	b.pressed.connect(func():
		if sort_key == key:
			sort_descending = not sort_descending
		else:
			sort_key = key
			sort_descending = key in ["damage", "cost", "space"] # the biggest first
		_show_header()
		_show_rows())
	return b


func _cell(text: String, width: float) -> Label:
	var label := Label.new()
	label.text = text
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.custom_minimum_size.x = width
	label.clip_text = true
	label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.add_theme_font_size_override("font_size", 13)
	return label


func _toggle(text: String, on: bool) -> Button:
	var b := _button(text, "")
	b.toggle_mode = true
	b.custom_minimum_size = Vector2(0, 26)
	b.add_theme_font_size_override("font_size", 11)
	b.set_pressed_no_signal(on)
	Themes.glowing(b)
	Themes.glow(b)
	return b


func _button(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size.y = 30
	return b


func _dim(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Dim"
	label.text = text
	label.add_theme_font_size_override("font_size", 12)
	return label


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()
