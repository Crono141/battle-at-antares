extends PanelContainer
## The Ship Design panel's parts list (WP3.6, the owner's mockup C, 2026-11-15): flies out to the
## left of the design panel when + ADD WEAPON or + ADD SPECIAL is clicked (or a fitted part), and
## folds away with its X. Every weapon and special the player can fit in one list (FIND, ALL /
## BEAM / MISSILE / BOMB / SPECIAL WPN / SYSTEMS, FITS NOW), each line marked by its kind; the picked
## part's card at the bottom: a weapon's arc dial and arc buttons (or a missile's shots), its mods,
## what it takes, ADD (REPLACE for a fitted one); a special's group, effect, space and cost, ADD.
## The design panel owns it: it keeps it against its left edge and carries its commands.

const Themes := preload("../themes.gd")
const ArcGlyph := preload("arc_glyph.gd")
const WIDTH := 500.0
const FILTERS := [["all", "ALL"], ["beam", "BEAM"], ["missile", "MISSILE"], ["bomb", "BOMB"], ["special", "SPECIAL WPN"], ["systems", "SYSTEMS"]]
const ARC_NAMES := ["FWD", "EXT", "B·EXT", "BACK", "360°"]
const ARC_TIPS := ["Forward", "Forward extended", "Back extended", "Back", "360 degrees"]
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

var _find: LineEdit
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
	box.add_child(_header_line())
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
	if visible:
		refresh()


func set_preview(result: Dictionary) -> void:
	if picked_kind == "weapon" and int(result.get("weapon", -1)) == picked:
		preview = result
		_show_card()


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
	_show_rows()
	_show_card()


# ---- The list ----

func _show_rows() -> void:
	_clear(_rows)
	var needle := find.strip_edges().to_lower()
	var shown := 0
	if filter != "systems":
		for w: Dictionary in weapons:
			var category := String(w.category)
			if filter != "all" and filter != category:
				continue
			if not _matches(needle, String(w.name), String(w.notes)):
				continue
			if fits_only and not w.get("fits", false):
				continue
			var id := int(w.weapon)
			_rows.add_child(_line(category, String(w.name), String(w.notes), String(w.damage), int(w.cost), int(w.space),
				picked_kind == "weapon" and picked == id, not w.get("fits", false) and not (picked_kind == "weapon" and replace_slot >= 0 and picked == id),
				func(): _pick_weapon(w)))
			shown += 1
	if filter == "all" or filter == "systems":
		for sp: Dictionary in specials:
			if sp.get("fitted", false) and not (picked_kind == "special" and picked == int(sp.special)):
				continue
			if not _matches(needle, String(sp.name), String(sp.description)):
				continue
			if fits_only and not sp.get("fits", false):
				continue
			var id := int(sp.special)
			var note := String(sp.description)
			if sp.has("excluded_by"):
				note = "⇄ " + String(sp.excluded_by)
			_rows.add_child(_line("systems", String(sp.name), note, "", int(sp.cost), int(sp.space),
				picked_kind == "special" and picked == id, sp.has("excluded_by") or not sp.get("fits", false),
				func(): _pick_special(id)))
			shown += 1
	if shown == 0:
		_rows.add_child(_dim("Nothing here" + (": turn FITS NOW off to see what needs more room" if fits_only else "")))


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
	if w.get("arcs", false):
		var dial := ArcGlyph.new()
		dial.arc = arc
		dial.custom_minimum_size = Vector2(88, 88)
		dial.tooltip_text = "Fires: %s" % ARC_TIPS[arc]
		row.add_child(dial)
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
	if w.get("arcs", false):
		var arcs := HBoxContainer.new()
		arcs.add_theme_constant_override("separation", 3)
		right.add_child(arcs)
		for i in 5:
			var b := _toggle(ARC_NAMES[i], i == arc)
			b.tooltip_text = ARC_TIPS[i]
			b.custom_minimum_size = Vector2(56, 26)
			b.icon_alignment = HORIZONTAL_ALIGNMENT_LEFT
			var a := i
			b.pressed.connect(func():
				arc = a
				_ask_preview()
				_show_card())
			arcs.add_child(b)
	elif w.has("racks"):
		var racks := HBoxContainer.new()
		racks.add_theme_constant_override("separation", 3)
		right.add_child(racks)
		racks.add_child(_dim("SHOTS"))
		for i in (w.racks as Array).size():
			var b := _toggle(str(int(w.racks[i])), i == rack)
			b.tooltip_text = "%d shots a combat" % int(w.racks[i])
			var r := i
			b.pressed.connect(func():
				rack = r
				_ask_preview()
				_show_card())
			racks.add_child(b)
	var mod_flow := HFlowContainer.new()
	mod_flow.add_theme_constant_override("h_separation", 3)
	mod_flow.add_theme_constant_override("v_separation", 3)
	right.add_child(mod_flow)
	for m: Dictionary in w.get("mods", []):
		var mod := int(m.mod)
		var b := _toggle(String(m.name).to_upper(), mod in mods)
		b.add_theme_font_size_override("font_size", 10)
		b.pressed.connect(func():
			if mod in mods:
				mods.erase(mod)
			else:
				mods.append(mod)
			_ask_preview()
			_show_card())
		mod_flow.add_child(b)
	var bottom := HBoxContainer.new()
	bottom.add_theme_constant_override("separation", 6)
	_card.add_child(bottom)
	var summary := _dim(_weapon_summary(w))
	summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bottom.add_child(summary)
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

## A list line: the kind's mark, the name with its note, damage, BC, space; picked lights it,
## `dim` greys it (no room, or ruled out).
func _line(kind: String, name_text: String, note: String, damage: String, cost: int, space: int, is_picked: bool, dim: bool, on_pick: Callable) -> Button:
	var b := Button.new()
	b.toggle_mode = true
	b.flat = true
	b.focus_mode = Control.FOCUS_NONE
	b.theme_type_variation = &"ListItem"
	b.custom_minimum_size.y = 26
	b.set_pressed_no_signal(is_picked)
	b.tooltip_text = note
	b.pressed.connect(on_pick)
	var line := HBoxContainer.new()
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.offset_left = 6
	line.offset_right = -6
	line.add_theme_constant_override("separation", 6)
	b.add_child(line)
	var mark: Array = MARKS.get(kind, MARKS.systems)
	var mark_label := _cell(String(mark[0]), 16.0, HORIZONTAL_ALIGNMENT_CENTER)
	mark_label.add_theme_color_override("font_color", mark[1] if not dim else Color(0.4, 0.42, 0.48))
	line.add_child(mark_label)
	var name_label := Label.new()
	name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_label.clip_text = true
	name_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	name_label.add_theme_font_size_override("font_size", 14)
	name_label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	name_label.text = name_text + ("   " + note if note != "" else "")
	line.add_child(name_label)
	line.add_child(_cell(damage, 54.0, HORIZONTAL_ALIGNMENT_RIGHT))
	line.add_child(_cell(str(cost), 34.0, HORIZONTAL_ALIGNMENT_RIGHT))
	line.add_child(_cell(str(space), 44.0, HORIZONTAL_ALIGNMENT_RIGHT))
	for label: Label in line.get_children():
		if is_picked:
			label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
		elif dim and label != mark_label:
			label.add_theme_color_override("font_color", Color(0.45, 0.48, 0.55))
	return b


func _header_line() -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	var pad := Control.new()
	pad.custom_minimum_size.x = 6 + 16
	line.add_child(pad)
	var titles := [["NAME", 0.0], ["DMG", 54.0], ["BC", 34.0], ["SPACE", 44.0]]
	for t: Array in titles:
		var label := _cell(String(t[0]), float(t[1]), HORIZONTAL_ALIGNMENT_RIGHT if float(t[1]) > 0.0 else HORIZONTAL_ALIGNMENT_LEFT)
		label.theme_type_variation = &"Name"
		label.add_theme_font_size_override("font_size", 11)
		if float(t[1]) == 0.0:
			label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(label)
	var right := Control.new()
	right.custom_minimum_size.x = 6 + 12 # the rows' padding and the scroll bar
	line.add_child(right)
	return line


func _cell(text: String, width: float, align: HorizontalAlignment) -> Label:
	var label := Label.new()
	label.text = text
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.custom_minimum_size.x = width
	label.horizontal_alignment = align
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
