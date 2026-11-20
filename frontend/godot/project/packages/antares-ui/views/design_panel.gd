extends "side_panel.gd"
## Ship Design (WP3.6; the owner, 2026-11-15: mockup B2's top half, C's bottom half and its parts
## list): the legacy designer as a panel that flies out from the right over the galaxy, no
## sub-screens. The name beside the title, the hull SIZE drop-down; drive, armour, shield and
## computer in a column beside a large ship picture (◀ ▶ through the hull's pictures); the space bar
## and the cost; then what is fitted (weapons with their arc glyph, − n× +, mods, ✕; specials, ✕)
## and + ADD WEAPON / + ADD SPECIAL, which bring the parts list (design_parts.gd) out to the left of
## the panel (a fitted part's name too, to change it). CLEAR, CANCEL (Esc), BUILD (when the ship
## fits). Every edit is the game's (the design_* commands on a working copy); BUILD writes the slot.
## The design theme plays while it is open (design_open holds the music).

signal command(dict: Dictionary)
signal width_changed
## BUILD wrote the slot (the design list shows it again).
signal built(slot: int)
## A help record for the frontend's help popup (the parts list's right clicks).
signal help_requested(topic: int, at: Control)

const Themes := preload("../themes.gd")
const ArcGlyph := preload("arc_glyph.gd")
const Parts := preload("design_parts.gd")
const WIDTH := 640.0
const ARC_TIPS := ["Forward", "Forward extended", "Back extended", "Back", "360 degrees"]

var art: Node
var design := {}
var slot := -1
var _weapons: Array = [] # design_weapons
var _specials: Array = [] # design_specials
var _systems := {} # design_systems

## The parts list (design_parts.gd), beside the panel while it is out.
var _parts: Control

var _name: LineEdit
var _size: OptionButton
var _combat: Label
var _systems_box: VBoxContainer
var _picture: TextureRect
var _space_bar: Control
var _space_label: Label
var _cost_label: Label
var _body: VBoxContainer
var _build: Button
var _message: Label
var _closing_quietly := false


func _ready() -> void:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 10)
	box.add_child(header)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "Ship Design"
	header.add_child(title)
	_name = LineEdit.new()
	_name.max_length = 15
	_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_name.tooltip_text = "The design's name (15 letters; empty: the hull's own)"
	_name.add_theme_font_size_override("font_size", 18)
	_name.text_submitted.connect(func(_t: String): _send_name())
	_name.focus_exited.connect(_send_name)
	header.add_child(_name)
	var close_button := _button("X", "Cancel (Esc): the slot keeps its design")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(close)
	header.add_child(close_button)
	var size_row := HBoxContainer.new()
	size_row.add_theme_constant_override("separation", 10)
	box.add_child(size_row)
	var size_label := Label.new()
	size_label.theme_type_variation = &"Dim"
	size_label.text = "SIZE"
	size_row.add_child(size_label)
	_size = OptionButton.new()
	_size.focus_mode = Control.FOCUS_NONE
	_size.custom_minimum_size = Vector2(210, 30)
	_size.fit_to_longest_item = false
	_size.theme_type_variation = &"ListItem"
	_size.clip_text = true
	_size.item_selected.connect(func(index: int): _edit({"size": _size.get_item_id(index)}))
	size_row.add_child(_size)
	_combat = Label.new()
	_combat.theme_type_variation = &"Dim"
	_combat.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_combat.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_combat.add_theme_font_size_override("font_size", 13)
	_combat.clip_text = true # never wider than the panel lets it be
	_combat.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	size_row.add_child(_combat)
	# The systems beside the picture (a third of the width).
	var ship := HBoxContainer.new()
	ship.add_theme_constant_override("separation", 10)
	box.add_child(ship)
	# Owner, 2026-11-15: the top half no more than half the panel; one frame for the four systems,
	# the picture's arrows beside it.
	var systems_frame := PanelContainer.new()
	systems_frame.theme_type_variation = &"Row"
	systems_frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ship.add_child(systems_frame)
	_systems_box = VBoxContainer.new()
	_systems_box.add_theme_constant_override("separation", 0)
	systems_frame.add_child(_systems_box)
	var picture_frame := PanelContainer.new()
	picture_frame.theme_type_variation = &"Row"
	picture_frame.custom_minimum_size = Vector2(WIDTH / 3.0, 0)
	ship.add_child(picture_frame)
	var picture_box := HBoxContainer.new()
	picture_box.add_theme_constant_override("separation", 2)
	picture_frame.add_child(picture_box)
	for step in [-1, 1]:
		# Tall and narrow, the picture's full height (owner, 2026-11-15).
		var b := _button("◀" if step < 0 else "▶", "")
		b.custom_minimum_size = Vector2(14, 0)
		b.size_flags_vertical = Control.SIZE_EXPAND_FILL
		b.add_theme_font_size_override("font_size", 10)
		for state in ["normal", "hover", "pressed", "disabled", "focus"]:
			b.add_theme_stylebox_override(state, _arrow_style(state))
		b.add_theme_color_override("font_color", Color(0.85, 0.88, 0.93))
		b.add_theme_color_override("font_hover_color", Color(1.0, 0.75, 0.35))
		b.add_theme_color_override("font_pressed_color", Color(1.0, 0.65, 0.23))
		b.add_theme_color_override("font_disabled_color", Color(0.35, 0.38, 0.45))
		var s: int = step
		b.pressed.connect(func(): _edit({"picture": s}))
		b.name = "Prev" if step < 0 else "Next"
		picture_box.add_child(b)
		if step < 0:
			_picture = preload("sprite_rect.gd").new()
			_picture.custom_minimum_size = Vector2(0, 92)
			_picture.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			_picture.whole_frame = true # small hulls small, as the game draws them in the ship frame
			picture_box.add_child(_picture)
	# Space and cost.
	var numbers := PanelContainer.new()
	numbers.theme_type_variation = &"Row"
	box.add_child(numbers)
	var numbers_row := HBoxContainer.new()
	numbers_row.add_theme_constant_override("separation", 14)
	numbers.add_child(numbers_row)
	_space_label = Label.new()
	_space_label.add_theme_font_size_override("font_size", 13)
	_space_label.custom_minimum_size.x = 150
	numbers_row.add_child(_space_label)
	_space_bar = _SpaceBar.new()
	_space_bar.custom_minimum_size.y = 10
	_space_bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_space_bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	numbers_row.add_child(_space_bar)
	_cost_label = Label.new()
	_cost_label.theme_type_variation = &"Name"
	_cost_label.add_theme_font_size_override("font_size", 18)
	_cost_label.tooltip_text = "The design's cost"
	_cost_label.mouse_filter = Control.MOUSE_FILTER_PASS
	numbers_row.add_child(_cost_label)
	# Weapons and specials (scrolls).
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(scroll)
	_body = VBoxContainer.new()
	_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_body.add_theme_constant_override("separation", 5)
	scroll.add_child(_body)
	_message = Label.new()
	_message.add_theme_color_override("font_color", Color(1.0, 0.45, 0.35))
	_message.add_theme_font_size_override("font_size", 13)
	_message.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_message.visible = false
	box.add_child(_message)
	var footer := HBoxContainer.new()
	footer.add_theme_constant_override("separation", 8)
	box.add_child(footer)
	# AUTO: this design only, redone by the game's automatic designer for its hull (owner,
	# 2026-11-20); tweak it, BUILD or CANCEL as after any change. The design list's AUTO DESIGN
	# does every slot at once (Alt+A, D).
	var auto := _button("AUTO", "Design this ship automatically with your best technology: change it further, BUILD to keep it or CANCEL")
	auto.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	auto.pressed.connect(func(): _send({"verb": "design_auto"}))
	footer.add_child(auto)
	var clear := _button("CLEAR", "No weapons or specials; the best drive, armour and computer")
	clear.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	clear.pressed.connect(func(): _send({"verb": "design_clear"}))
	footer.add_child(clear)
	var cancel := _button("CANCEL", "Leave the slot as it was (Esc)")
	cancel.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cancel.pressed.connect(close)
	footer.add_child(cancel)
	_build = _button("BUILD", "Save the design in its slot")
	_build.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_build.size_flags_stretch_ratio = 2.0
	_build.pressed.connect(func(): command.emit({"verb": "design_build", "_reply_to": "_after_build"}))
	footer.add_child(_build)
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				preload("../themes.gd").move_x(self, _rect.end.x - size.x)
			width_changed.emit())


## Opens slot `design_slot` (0-5) for editing.
func open_panel(area: Rect2, design_slot: int) -> void:
	slot = design_slot
	design = {}
	if _parts != null:
		_parts.hide()
	_message.visible = false
	var width := _wanted_width(area)
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))
	command.emit({"verb": "design_open", "slot": slot, "_reply_to": "show_design"})


func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	return minf(maxf(WIDTH, get_combined_minimum_size().x), area.size.x - 16.0)


func panel_width() -> float:
	return _rect.end.x - rest_left()


## Over the galaxy: no star clicks to answer.
func select_star(_star: int) -> bool:
	return false


## CANCEL / Esc / X: the working copy goes, the slot keeps its design.
func close() -> void:
	if visible and not _closing and not _closing_quietly:
		command.emit({"verb": "design_cancel"})
	_closing_quietly = false
	if _parts != null:
		_parts.hide()
	super.close()


func _send(cmd: Dictionary) -> void:
	cmd["_reply_to"] = "show_design"
	command.emit(cmd)


func _edit(args: Dictionary) -> void:
	args["verb"] = "design_edit"
	_send(args)


func _send_name() -> void:
	if not design.is_empty() and _name.text.strip_edges() != String(design.get("name", "")):
		_edit({"name": _name.text})


func _after_build(result: Dictionary) -> void:
	if not result.get("ok", false):
		_show_message(String(result.get("message", "Can't build this design")))
		return
	_closing_quietly = true
	close()
	built.emit(slot)


func _show_message(text: String) -> void:
	_message.text = text
	_message.visible = text != ""


# ---- The design ----

func show_design(result: Dictionary) -> void:
	if not result.get("ok", false):
		_show_message(String(result.get("message", "")))
		return
	_show_message("")
	design = result
	if not _name.has_focus():
		_name.text = String(result.name)
	_size.clear()
	for s: Dictionary in result.sizes:
		_size.add_item("%s · %d space" % [String(s.name), int(s.space)], int(s.size))
	_size.select(int(result.size))
	_combat.text = "BEAM ATT +%d · BEAM DEF %+d · MISSILE EVASION %d%%" % [int(result.beam_attack), int(result.beam_defense), int(result.missile_evasion)]
	_picture.bind(art)
	_picture.show_sprite("SHIPS.LBX", int(result.picture))
	for b: Button in [find_child("Prev", true, false), find_child("Next", true, false)]:
		if b:
			b.disabled = not result.get("picture_cycles", true)
	_show_systems()
	# Battle Pods take negative space (the game subtracts it from what is used): shown as the hull
	# growing by it, and what is used the same, so the free space is the game's and the bar adds
	# up (owner, 2026-11-15: 19/31 with 12 free drew a full bar).
	var extra := 0
	var specials_space := 0
	for sp: Dictionary in result.specials:
		if int(sp.space) < 0:
			extra -= int(sp.space)
		else:
			specials_space += int(sp.space)
	var hull := int(result.hull_space) + extra
	var used := int(result.space_used) + extra
	_space_label.text = "SPACE %d / %d · %s" % [used, hull, ("%d free" % (hull - used)) if used <= hull else ("%d OVER" % (used - hull))]
	_space_label.tooltip_text = ("The hull's %d, and %d more from Battle Pods" % [int(result.hull_space), extra]) if extra > 0 else ""
	_space_label.add_theme_color_override("font_color", Color(1.0, 0.45, 0.35) if used > hull else get_theme_color("font_color", "Label"))
	_space_bar.set_parts(hull, used, int(result.weapon_space), specials_space)
	_cost_label.text = "COST %d BC" % int(result.cost)
	_build.disabled = not result.get("buildable", false)
	_build.tooltip_text = "Save the design in its slot" if not _build.disabled else "The parts need more space than the hull has"
	Themes.glow(_build)
	command.emit({"verb": "query", "what": "design_weapons", "_reply_to": "_on_weapons"})
	command.emit({"verb": "query", "what": "design_specials", "_reply_to": "_on_specials"})
	# Asked each time, like the weapons and specials: what is researched changes between openings
	# (and with a loaded save or a new game), and the costs and spaces follow the hull size.
	command.emit({"verb": "query", "what": "design_systems", "_reply_to": "_on_systems"})
	_show_body()


func _on_weapons(result: Dictionary) -> void:
	if result.get("ok", false):
		_weapons = result.weapons
		if _parts != null:
			_parts.set_data(design, _weapons, _specials)


func _on_specials(result: Dictionary) -> void:
	if result.get("ok", false):
		_specials = result.specials
		if _parts != null:
			_parts.set_data(design, _weapons, _specials)
		_show_body()


func _on_systems(result: Dictionary) -> void:
	if result.get("ok", false):
		_systems = result
		_show_systems()


## Drive and armour (always the best), shield and computer (drop-downs of what is researched).
func _show_systems() -> void:
	_clear(_systems_box)
	if design.is_empty():
		return
	var drive: Dictionary = design.drive
	_systems_box.add_child(_system_line("DRIVE", String(drive.name), "%d pc · %d speed" % [int(drive.parsecs), int(drive.combat_speed)]))
	var armor: Dictionary = design.armor
	_systems_box.add_child(_system_line("ARMOR", String(armor.name), "%d structure · %d armor" % [int(armor.structure), int(armor.armor)]))
	var shield: Dictionary = design.shield
	_systems_box.add_child(_system_choice("SHIELD", "shield", _systems.get("shields", []), int(shield.shield),
		"%d · blocks %d" % [int(shield.strength), int(shield.blocked)]))
	var computer: Dictionary = design.computer
	_systems_box.add_child(_system_choice("COMPUTER", "computer", _systems.get("computers", []), int(computer.computer),
		"%+d beam attack" % int(design.beam_attack)))


func _system_line(title: String, value: String, detail: String) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	line.custom_minimum_size.y = 24
	var row := line
	var t := Label.new()
	t.theme_type_variation = &"Dim"
	t.text = title
	t.custom_minimum_size.x = 74
	t.add_theme_font_size_override("font_size", 11)
	line.add_child(t)
	var v := Label.new()
	v.text = value
	v.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	v.clip_text = true
	line.add_child(v)
	var d := Label.new()
	d.theme_type_variation = &"Dim"
	d.text = detail
	d.add_theme_font_size_override("font_size", 12)
	d.clip_text = true
	d.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	d.custom_minimum_size.x = 110
	d.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(d)
	return row


func _system_choice(title: String, key: String, items: Array, current: int, detail: String) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	line.custom_minimum_size.y = 24
	var row := line
	var t := Label.new()
	t.theme_type_variation = &"Dim"
	t.text = title
	t.custom_minimum_size.x = 74
	t.add_theme_font_size_override("font_size", 11)
	line.add_child(t)
	var pick := OptionButton.new()
	pick.focus_mode = Control.FOCUS_NONE
	pick.theme_type_variation = &"ListItem" # readable on the row (a flat button's text was dark)
	pick.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	pick.fit_to_longest_item = false
	pick.clip_text = true
	pick.custom_minimum_size = Vector2(0, 24)
	pick.add_theme_font_size_override("font_size", 14)
	for item: Dictionary in items:
		pick.add_item(String(item.name), int(item[key]))
		if int(item.get("cost", 0)) > 0:
			pick.set_item_tooltip(pick.item_count - 1, "%d BC%s" % [int(item.cost), (", %d space" % int(item.space)) if item.has("space") else ""])
	if pick.item_count == 0:
		pick.add_item(String(design[key].name), current)
	pick.select(maxi(pick.get_item_index(current), 0))
	pick.disabled = pick.item_count < 2
	pick.item_selected.connect(func(index: int): _send({"verb": "design_system", key: pick.get_item_id(index)}))
	line.add_child(pick)
	var d := Label.new()
	d.theme_type_variation = &"Dim"
	d.text = detail
	d.add_theme_font_size_override("font_size", 12)
	d.clip_text = true
	d.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	d.custom_minimum_size.x = 110
	d.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(d)
	return row


# ---- What is fitted (the owner's mockup C) ----

func _show_body() -> void:
	_clear(_body)
	if design.is_empty():
		return
	_body.add_child(_heading("Fitted"))
	for w: Dictionary in design.weapons:
		_body.add_child(_weapon_row(w))
	for sp: Dictionary in design.specials:
		_body.add_child(_special_row(sp))
	if (design.weapons as Array).is_empty() and (design.specials as Array).is_empty():
		_body.add_child(_dim("Nothing fitted yet: + ADD WEAPON or + ADD SPECIAL opens the parts list."))
	var adds := HBoxContainer.new()
	adds.add_theme_constant_override("separation", 8)
	_body.add_child(adds)
	var add_weapon := _button("+ ADD WEAPON", "The parts list, its weapons")
	add_weapon.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_weapon.disabled = not design.get("weapon_slot_free", true)
	add_weapon.pressed.connect(func(): _open_parts("weapons"))
	adds.add_child(add_weapon)
	var fit := 0
	for sp: Dictionary in _specials:
		if not sp.get("fitted", false) and not sp.has("excluded_by") and sp.get("fits", false):
			fit += 1
	var add_special := _button("+ ADD SPECIAL", "The parts list, its specials (%d fit now)" % fit)
	add_special.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	add_special.disabled = not design.get("special_slot_free", true)
	add_special.pressed.connect(func(): _open_parts("systems"))
	adds.add_child(add_special)


func _mark(kind: String) -> Label:
	var marks: Dictionary = Parts.MARKS
	var mark: Array = marks.get(kind, marks.systems)
	var label := Label.new()
	label.text = String(mark[0])
	label.add_theme_color_override("font_color", mark[1])
	label.custom_minimum_size.x = 14
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	return label


func _weapon_row(w: Dictionary) -> Control:
	var row := PanelContainer.new()
	var editing: bool = _parts != null and _parts.visible and _parts.replace_slot == int(w.slot)
	row.theme_type_variation = &"RowOpen" if editing else &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	row.add_child(line)
	line.add_child(_mark(String(w.category)))
	var glyph := ArcGlyph.new()
	glyph.arc = int(w.arc) if String(w.category) == "beam" else 4
	glyph.custom_minimum_size = Vector2(22, 22)
	glyph.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	glyph.mouse_filter = Control.MOUSE_FILTER_PASS
	line.add_child(glyph)
	var row_slot := int(w.slot)
	var minus := _button("−", "One fewer")
	minus.custom_minimum_size = Vector2(26, 24)
	minus.disabled = int(w.count) <= 1
	minus.pressed.connect(func(): weapon_count(row_slot, -1))
	line.add_child(minus)
	var count := Label.new()
	count.theme_type_variation = &"Name"
	count.text = "%d×" % int(w.count)
	count.custom_minimum_size.x = 30
	count.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	line.add_child(count)
	var plus := _button("+", "One more")
	plus.custom_minimum_size = Vector2(26, 24)
	plus.disabled = not w.get("can_add", false)
	plus.tooltip_text = "One more" if not plus.disabled else "No room for one more"
	plus.pressed.connect(func(): weapon_count(row_slot, 1))
	line.add_child(plus)
	var name_button := Button.new()
	name_button.flat = true
	name_button.theme_type_variation = &"ListItem"
	name_button.focus_mode = Control.FOCUS_NONE
	name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	name_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_button.clip_text = true
	var mods: Array = (w.mods as Array).map(func(m: Dictionary) -> String: return String(m.name))
	name_button.text = String(w.name) + ((" (%d)" % int(w.ammo)) if int(w.get("ammo", 0)) > 0 else "") + (("  · " + ", ".join(mods)) if not mods.is_empty() else "")
	name_button.pressed.connect(func(): _open_parts_on_weapon(w))
	line.add_child(name_button)
	var detail := _dim("%s · %d BC · %d sp" % [String(w.damage), int(w.cost), int(w.space)])
	detail.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	detail.clip_text = true
	detail.custom_minimum_size.x = 120
	detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(detail)
	var remove := _button("✕", "Remove this weapon")
	remove.custom_minimum_size = Vector2(26, 24)
	remove.pressed.connect(func(): _send({"verb": "design_weapon", "slot": row_slot, "weapon": 0}))
	line.add_child(remove)
	return row


func _special_row(sp: Dictionary) -> Control:
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	row.add_child(line)
	line.add_child(_mark("systems"))
	var name_button := Button.new()
	name_button.flat = true
	name_button.theme_type_variation = &"ListItem"
	name_button.focus_mode = Control.FOCUS_NONE
	name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	name_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_button.clip_text = true
	name_button.text = String(sp.name)
	name_button.tooltip_text = String(sp.description)
	var id := int(sp.special)
	name_button.pressed.connect(func(): _open_parts_on_special(id))
	line.add_child(name_button)
	var detail := _dim("%s · %d sp" % [String(sp.description), int(sp.space)])
	detail.clip_text = true
	detail.custom_minimum_size.x = 220
	detail.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(detail)
	var row_slot := int(sp.slot)
	var remove := _button("✕", "Remove this special")
	remove.custom_minimum_size = Vector2(26, 24)
	remove.pressed.connect(func(): _send({"verb": "design_special", "slot": row_slot, "special": 0}))
	line.add_child(remove)
	return row


# ---- The parts list beside the panel ----

func _ensure_parts() -> void:
	if _parts != null:
		return
	_parts = Parts.new()
	_parts.panel = self
	_parts.art = art
	_parts.visible = false
	get_parent().add_child(_parts)
	_parts.set_data(design, _weapons, _specials)


func _open_parts(kind: String) -> void:
	_ensure_parts()
	_parts.set_data(design, _weapons, _specials)
	_parts.open_for(kind)
	_place_parts()
	_show_body()


func _open_parts_on_weapon(w: Dictionary) -> void:
	_ensure_parts()
	_parts.set_data(design, _weapons, _specials)
	_parts.open_for_weapon(w)
	_place_parts()
	_show_body()


func _open_parts_on_special(id: int) -> void:
	_ensure_parts()
	_parts.set_data(design, _weapons, _specials)
	_parts.open_for_special(id)
	_place_parts()


## Against the panel's left edge, as tall as the panel.
func _place_parts() -> void:
	if _parts == null or not _parts.visible:
		return
	var width := maxf(Parts.WIDTH, _parts.get_combined_minimum_size().x)
	_parts.size = Vector2(width, size.y)
	_parts.position = Vector2(position.x - width - 8.0, position.y)


func _process(_delta: float) -> void:
	if _parts != null and _parts.visible:
		if not visible:
			_parts.hide()
		else:
			_place_parts()


## The parts list's X (it folds away; the rows show nothing being changed).
func parts_folded() -> void:
	_show_body()


func show_help(topic: int, at: Control) -> void:
	help_requested.emit(topic, at)


func ask_preview(args: Dictionary) -> void:
	args["verb"] = "query"
	args["what"] = "design_weapon_preview"
	args["_reply_to"] = "_on_preview"
	command.emit(args)


func _on_preview(result: Dictionary) -> void:
	if result.get("ok", false) and _parts != null:
		_parts.set_preview(result)


func add_weapon(replace_slot: int, weapon: int, arc: int, rack: int, mods: Array) -> void:
	_send({"verb": "design_weapon", "slot": replace_slot, "weapon": weapon, "arc": arc, "rack": rack, "mods": mods})


func add_special(special: int) -> void:
	_send({"verb": "design_special", "slot": -1, "special": special})


func weapon_count(row_slot: int, delta: int) -> void:
	_send({"verb": "design_weapon_count", "slot": row_slot, "delta": delta})


# ---- Pieces ----

func _heading(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Name"
	label.text = text
	label.add_theme_font_size_override("font_size", 17)
	return label


func _dim(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Dim"
	label.text = text
	label.add_theme_font_size_override("font_size", 12)
	return label


func _button(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size.y = 30
	return b


## A slim stylebox for the picture's arrows: no side margins, a faint edge, lighter under the pointer.
static func _arrow_style(state: String) -> StyleBox:
	var style := StyleBoxFlat.new()
	style.content_margin_left = 0
	style.content_margin_right = 0
	style.set_corner_radius_all(3)
	style.set_border_width_all(1)
	style.bg_color = Color(0.09, 0.11, 0.17, 0.9) if state != "hover" else Color(0.16, 0.2, 0.3, 0.95)
	style.border_color = Color(0.3, 0.35, 0.47) if state != "pressed" else Color(0.91, 0.56, 0.16)
	if state == "disabled":
		style.bg_color = Color(0.06, 0.07, 0.1, 0.6)
	return style


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


## The space bar: weapons and specials in their colours, the rest of the used space (drive, shield,
## armour) grey, red past the hull's end.
class _SpaceBar extends Control:
	var hull := 1
	var used := 0
	var weapons := 0
	var specials := 0

	func set_parts(hull_space: int, used_space: int, weapon_space: int, special_space: int) -> void:
		hull = maxi(hull_space, 1)
		used = used_space
		weapons = weapon_space
		specials = special_space
		queue_redraw()

	func _draw() -> void:
		draw_rect(Rect2(Vector2.ZERO, size), Color(0.03, 0.04, 0.08))
		var total := float(maxi(hull, used))
		var x := 0.0
		for part in [[used - weapons - specials, Color(0.45, 0.48, 0.55)], [weapons, Color(0.91, 0.56, 0.16)], [specials, Color(0.35, 0.63, 1.0)]]:
			var w := size.x * maxf(float(part[0]), 0.0) / total
			draw_rect(Rect2(x, 0, w, size.y), part[1])
			x += w
		if used > hull:
			var end := size.x * float(hull) / total
			draw_rect(Rect2(end, 0, size.x - end, size.y), Color(1.0, 0.3, 0.25, 0.55))
			draw_line(Vector2(end, -2), Vector2(end, size.y + 2), Color.WHITE, 2.0)
