extends VBoxContainer
## A ship's card in the battle scan's look (combat_view.gd, _show_scan; owner 2026-11-23: the fleets
## panel's ship info and a right click on a ship in a fleet, the same presentation, a little
## smaller): the systems with their glyphs beside the picture and its health bars, the combat
## values, then a row per weapon (its arc as a glyph) and per special. From `ship_detail`'s `card`.
## A right click on a part asks for its help record (`help_requested`).

signal help_requested(topic: int, at: Control)

const DIM := Color(0.58, 0.64, 0.72)
const TEXT := Color(0.85, 0.88, 0.93)
const AMBER := Color(0.95, 0.70, 0.24)
const SYSTEMS_COLOR := Color(0.95, 0.83, 0.42)
const ARMOR_COLOR := Color(0.79, 0.84, 0.92)
const STRUCTURE_COLOR := Color(0.94, 0.42, 0.42)
const TRACK_COLOR := Color(0.24, 0.29, 0.38, 0.75)
const BASE_FONT := 16

## 1.0 is the battle scan's size; the fleets panel uses a little less.
var scale_factor := 0.85
var art: Node # art.gd

var _card := {}


func show_card(card: Dictionary, title := "") -> void:
	_card = card
	for child in get_children():
		remove_child(child)
		child.queue_free()
	add_theme_constant_override("separation", _px(6))
	if title != "":
		var head := _label(title, 0, AMBER)
		head.theme_type_variation = &"Name"
		add_child(head)
	var sub := []
	for key in ["hull", "crew", "officer"]:
		if card.has(key) and String(card[key]) != "":
			sub.append(String(card[key]))
	if not sub.is_empty():
		var sub_label := _label("   ·   ".join(sub), 13, DIM)
		# Trimmed, not wrapped: a wrapping label asks for its whole height before it is laid out, and
		# the popup sized to that ran down the screen.
		sub_label.clip_text = true
		sub_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		sub_label.tooltip_text = sub_label.text
		sub_label.mouse_filter = Control.MOUSE_FILTER_PASS
		add_child(sub_label)
	# The systems' frame beside the picture's frame.
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", _px(8))
	add_child(top)
	var systems_frame := _panel(&"Row")
	systems_frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(systems_frame)
	var systems := VBoxContainer.new()
	systems.add_theme_constant_override("separation", 0)
	systems_frame.add_child(systems)
	var helps: Dictionary = card.get("help", {})
	var damaged: Dictionary = card.get("damaged", {})
	var armor: Dictionary = card.get("armor", {})
	var structure: Dictionary = card.get("structure", {})
	var shield_detail := "none" if int(card.get("shield_class", 0)) <= 0 else "blocks %d" % int(card.shield_class)
	if int(damaged.get("shield", 0)) > 0:
		shield_detail += " · %d%% hit" % int(damaged.shield)
	systems.add_child(_with_help(_line("shield", "SHIELD", String(card.get("shield", "")), shield_detail), int(helps.get("shield", -1))))
	systems.add_child(_with_help(_line("armor", "ARMOUR", String(card.get("armor_name", "")),
		"%d / %d" % [int(armor.get("now", 0)), int(armor.get("max", 0))]), int(helps.get("armor", -1))))
	systems.add_child(_line("structure", "STRUCTURE", "", "%d / %d" % [int(structure.get("now", 0)), int(structure.get("max", 0))]))
	var computer_detail := "%s %d" % [String(card.get("attack_label", "Beam OCV")).strip_edges(), int(card.get("attack", 0))]
	if damaged.get("computer", false):
		computer_detail = "damaged"
	systems.add_child(_with_help(_line("computer", "COMPUTER", String(card.get("computer", "")), computer_detail), int(helps.get("computer", -1))))
	var drive_detail := "speed %d" % int(card.get("speed", 0))
	if int(damaged.get("drive", 0)) > 0:
		drive_detail += " · %d%% hit" % int(damaged.drive)
	systems.add_child(_with_help(_line("drive", "DRIVE", String(card.get("drive", "")), drive_detail), int(helps.get("drive", -1))))
	var picture_frame := _panel(&"Row")
	picture_frame.custom_minimum_size = Vector2(_px(160), 0)
	top.add_child(picture_frame)
	var pv := VBoxContainer.new()
	pv.add_theme_constant_override("separation", _px(4))
	pv.alignment = BoxContainer.ALIGNMENT_CENTER
	picture_frame.add_child(pv)
	var picture := TextureRect.new()
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture.custom_minimum_size = Vector2(_px(144), _px(92))
	picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if art != null and card.has("picture"):
		picture.texture = art.texture(String(card.get("lbx", "SHIPS.LBX")), int(card.picture), 0)
		if picture.texture == null and art.has_signal("loaded"):
			var key_entry := int(card.picture)
			var refresh := func(_key: String):
				if is_instance_valid(picture) and picture.texture == null:
					picture.texture = art.texture(String(card.get("lbx", "SHIPS.LBX")), key_entry, 0)
			art.loaded.connect(refresh)
			picture.tree_exiting.connect(func():
				if art.loaded.is_connected(refresh):
					art.loaded.disconnect(refresh))
	pv.add_child(picture)
	pv.add_child(_bars(Vector2(_px(144), _px(9))))
	var values := ["%s %d" % [String(card.get("defense_label", "Beam DCV")).strip_edges(), int(card.get("defense", 0))],
		"Missile evasion %d" % int(card.get("evade", 0))]
	add_child(_label("      ".join(values), 12, DIM))
	for weapon: Dictionary in card.get("weapons", []):
		var row := _panel(&"Row")
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", _px(8))
		row.add_child(line)
		var glyph := preload("arc_glyph.gd").new()
		glyph.arc = _arc_glyph_index(int(weapon.get("arc", 0)))
		glyph.custom_minimum_size = Vector2(_px(22), _px(22))
		glyph.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		line.add_child(glyph)
		var n := _label("%d×" % int(weapon.get("count", 0)), 0, AMBER)
		n.custom_minimum_size.x = _px(30)
		line.add_child(n)
		var name := _label(String(weapon.get("name", "")))
		name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		name.clip_text = true
		name.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		name.tooltip_text = String(weapon.get("name", ""))
		line.add_child(name)
		var ammo := int(weapon.get("ammo", 0))
		if ammo > 0 and ammo < 99:
			line.add_child(_label("%d shots" % ammo, 12, DIM))
		add_child(_with_help(row, int(weapon.get("help", -1))))
	for special: Dictionary in card.get("specials", []):
		var row := _panel(&"Row")
		var line := HBoxContainer.new()
		row.add_child(line)
		var hurt := bool(special.get("damaged", false))
		var name := _label(String(special.get("name", "")), 0, SYSTEMS_COLOR if hurt else TEXT)
		name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(name)
		if hurt:
			line.add_child(_label("damaged", 12, SYSTEMS_COLOR))
		add_child(_with_help(row, int(special.get("help", -1))))
	if (card.get("weapons", []) as Array).is_empty() and (card.get("specials", []) as Array).is_empty():
		add_child(_label("No weapons or specials", 13, DIM))


# ---- Pieces (the battle scan's) ----

func _px(v: float) -> int:
	return int(roundf(v * scale_factor))


func _label(text: String, font_size := 0, color := TEXT) -> Label:
	var l := Label.new()
	l.text = text
	if color == DIM:
		l.theme_type_variation = &"Dim"
	elif color == AMBER:
		l.theme_type_variation = &"Name"
	elif color != TEXT:
		l.add_theme_color_override("font_color", color)
	l.add_theme_font_size_override("font_size", _px(font_size if font_size > 0 else BASE_FONT))
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


func _panel(variation := &"Pane") -> PanelContainer:
	var p := PanelContainer.new()
	p.mouse_filter = Control.MOUSE_FILTER_PASS
	p.theme_type_variation = variation
	return p


## One system line: glyph, small dim title, the part, the detail at the right.
func _line(kind: String, title: String, value: String, detail: String) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", _px(6))
	line.custom_minimum_size.y = _px(24)
	line.add_child(_glyph(kind))
	var t := _label(title, 11, DIM)
	t.custom_minimum_size.x = _px(72)
	line.add_child(t)
	var v := _label(value)
	v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	v.clip_text = true
	v.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	line.add_child(v)
	var d := _label(detail, 12, DIM)
	d.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(d)
	return line


func _with_help(control: Control, topic: int) -> Control:
	if topic < 0:
		return control
	control.mouse_filter = Control.MOUSE_FILTER_STOP
	control.tooltip_text = "Right click: what it does"
	control.gui_input.connect(func(event: InputEvent):
		var b := event as InputEventMouseButton
		if b != null and b.pressed and b.button_index == MOUSE_BUTTON_RIGHT:
			help_requested.emit(topic, control)
			control.accept_event())
	return control


## The weapon's arc as the arc glyph's (0 forward, 1 forward wide, 2 back wide, 3 back, 4 all round).
func _arc_glyph_index(arc: int) -> int:
	match arc:
		1:
			return 0
		2, 3:
			return 1
		4, 12:
			return 2
		8:
			return 3
	return 4


func _glyph(kind: String) -> Control:
	var g := Control.new()
	g.custom_minimum_size = Vector2(_px(20), _px(20))
	g.mouse_filter = Control.MOUSE_FILTER_IGNORE
	g.draw.connect(_draw_glyph.bind(g, kind))
	return g


func _draw_glyph(g: Control, kind: String) -> void:
	var c := g.size / 2.0
	var r := minf(g.size.x, g.size.y) * 0.42
	var ink := g.get_theme_color("font_color", "Label")
	match kind:
		"shield":
			var pts := PackedVector2Array([c + Vector2(-r, -r * 0.8), c + Vector2(0, -r), c + Vector2(r, -r * 0.8), c + Vector2(r, 0),
				c + Vector2(0, r), c + Vector2(-r, 0), c + Vector2(-r, -r * 0.8)])
			g.draw_polyline(pts, ink, 1.5, true)
		"armor":
			for k in 3:
				var y := c.y - r + k * r * 0.75
				g.draw_rect(Rect2(Vector2(c.x - r, y), Vector2(r * 2.0, r * 0.55)), ink, false, 1.5)
		"structure":
			g.draw_rect(Rect2(c - Vector2(r, r * 0.6), Vector2(r * 2.0, r * 1.2)), ink, false, 1.5)
			g.draw_line(c + Vector2(-r, -r * 0.6), c + Vector2(0, r * 0.6), ink, 1.5, true)
			g.draw_line(c + Vector2(0, r * 0.6), c + Vector2(r, -r * 0.6), ink, 1.5, true)
		"computer":
			g.draw_rect(Rect2(c - Vector2(r * 0.6, r * 0.6), Vector2(r * 1.2, r * 1.2)), ink, false, 1.5)
			for k in [-0.3, 0.3]:
				for side in [-1.0, 1.0]:
					g.draw_line(c + Vector2(r * k, side * r * 0.6), c + Vector2(r * k, side * r), ink, 1.5)
					g.draw_line(c + Vector2(side * r * 0.6, r * k), c + Vector2(side * r, r * k), ink, 1.5)
		"drive":
			g.draw_colored_polygon(PackedVector2Array([c + Vector2(-r, -r * 0.5), c + Vector2(0, -r * 0.5), c + Vector2(0, r * 0.5),
				c + Vector2(-r, r * 0.5)]), Color(ink, 0.25))
			g.draw_polyline(PackedVector2Array([c + Vector2(0, -r * 0.5), c + Vector2(r, 0), c + Vector2(0, r * 0.5)]), ink, 1.5, true)


## The battle's health bars: armour across the top, structure and systems from the middle below.
func _bars(min_size: Vector2) -> Control:
	var c := Control.new()
	c.custom_minimum_size = min_size
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	c.draw.connect(func():
		var w := c.size.x
		var row := maxf(2.0, (c.size.y - 2.0) / 2.0)
		var a := _fraction(_card.get("armor", {}))
		var s := _fraction(_card.get("structure", {}))
		var y := _fraction(_card.get("systems", {}))
		c.draw_rect(Rect2(0, 0, w, row), TRACK_COLOR)
		c.draw_rect(Rect2(w * (1.0 - a) / 2.0, 0, w * a, row), ARMOR_COLOR)
		var half := w / 2.0 - 1.0
		c.draw_rect(Rect2(0, row + 2.0, half, row), TRACK_COLOR)
		c.draw_rect(Rect2(w / 2.0 + 1.0, row + 2.0, half, row), TRACK_COLOR)
		c.draw_rect(Rect2(half - half * s, row + 2.0, half * s, row), STRUCTURE_COLOR)
		c.draw_rect(Rect2(w / 2.0 + 1.0, row + 2.0, half * y, row), SYSTEMS_COLOR))
	return c


func _fraction(pool: Dictionary) -> float:
	var top := float(pool.get("max", 0))
	if top <= 0.0:
		return 0.0
	return clampf(float(pool.get("now", 0)) / top, 0.0, 1.0)
