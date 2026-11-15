extends "side_panel.gd"
## Ship Design (WP3.6, the owner's mockup B2): the legacy designer as one panel that flies out from
## the right over the galaxy, no sub-screens. The name beside the title, the hull SIZE drop-down;
## drive, armour, shield and computer in a column beside a large ship picture (◀ ▶ through the
## hull's pictures); the space bar (weapons, specials) and the cost; the weapon rows (a firing-arc
## glyph, − count +, the mods) and "+ Add a weapon", which opens in place: BEAM / MISSILE / BOMB /
## SPECIAL, the weapons, the arc glyphs (or a missile's rack), the mods, what it takes, ADD; the
## fitted specials and "+ Add a special", open in place: grouped as the tech review groups them,
## FIND, FITS NOW, ADD. CLEAR, CANCEL (Esc), BUILD (when the ship fits). Every edit is the game's
## (the design_* commands on a working copy); BUILD writes the slot.

signal command(dict: Dictionary)
signal width_changed
## BUILD wrote the slot (the design list shows it again).
signal built(slot: int)

const Themes := preload("../themes.gd")
const WIDTH := 640.0
const CATEGORIES := ["beam", "missile", "bomb", "special"]
const ARC_NAMES := ["FWD", "EXT", "B·EXT", "BACK", "360°"]
const ARC_TIPS := ["Forward", "Forward extended", "Back extended", "Back", "360 degrees"]
const WEAPON_COLOR := Color(0.91, 0.56, 0.16)
const SPECIAL_COLOR := Color(0.35, 0.63, 1.0)

var art: Node
var design := {}
var slot := -1
var _weapons: Array = [] # design_weapons
var _specials: Array = [] # design_specials
var _systems := {} # design_systems

## The weapon picker, open in place: its category, the picked weapon, its arc, rack and mods, and
## the row it replaces (-1: a new row).
var _adding_weapon := false
var _category := "beam"
var _pick_weapon := -1
var _pick_arc := 0
var _pick_rack := 1
var _pick_mods: Array[int] = []
var _replace_slot := -1
var _preview := {}
## The special picker, open in place.
var _adding_special := false
var _pick_special := -1
var _fits_only := true
var _find := ""

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
	_systems_box = VBoxContainer.new()
	_systems_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_systems_box.add_theme_constant_override("separation", 4)
	ship.add_child(_systems_box)
	var picture_frame := PanelContainer.new()
	picture_frame.theme_type_variation = &"Row"
	picture_frame.custom_minimum_size = Vector2(WIDTH / 3.0, 0)
	ship.add_child(picture_frame)
	var picture_box := VBoxContainer.new()
	picture_box.add_theme_constant_override("separation", 4)
	picture_frame.add_child(picture_box)
	_picture = preload("sprite_rect.gd").new()
	_picture.custom_minimum_size = Vector2(0, 118)
	_picture.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture_box.add_child(_picture)
	var arrows := HBoxContainer.new()
	picture_box.add_child(arrows)
	for step in [-1, 1]:
		var b := _button("◀" if step < 0 else "▶", "The hull's previous picture" if step < 0 else "The hull's next picture")
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.custom_minimum_size.y = 24
		var s: int = step
		b.pressed.connect(func(): _edit({"picture": s}))
		b.name = "Prev" if step < 0 else "Next"
		arrows.add_child(b)
	# Space and cost.
	var numbers := PanelContainer.new()
	numbers.theme_type_variation = &"Row"
	box.add_child(numbers)
	var numbers_row := HBoxContainer.new()
	numbers_row.add_theme_constant_override("separation", 14)
	numbers.add_child(numbers_row)
	var space_box := VBoxContainer.new()
	space_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	space_box.add_theme_constant_override("separation", 3)
	numbers_row.add_child(space_box)
	_space_label = Label.new()
	_space_label.add_theme_font_size_override("font_size", 13)
	space_box.add_child(_space_label)
	_space_bar = _SpaceBar.new()
	_space_bar.custom_minimum_size.y = 10
	space_box.add_child(_space_bar)
	var cost_box := VBoxContainer.new()
	cost_box.add_theme_constant_override("separation", 0)
	numbers_row.add_child(cost_box)
	var cost_title := Label.new()
	cost_title.theme_type_variation = &"Dim"
	cost_title.text = "COST"
	cost_title.add_theme_font_size_override("font_size", 11)
	cost_title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	cost_box.add_child(cost_title)
	_cost_label = Label.new()
	_cost_label.theme_type_variation = &"Name"
	_cost_label.add_theme_font_size_override("font_size", 20)
	cost_box.add_child(_cost_label)
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
				position.x = _rect.end.x - size.x
			width_changed.emit())


## Opens slot `design_slot` (0-5) for editing.
func open_panel(area: Rect2, design_slot: int) -> void:
	slot = design_slot
	design = {}
	_adding_weapon = false
	_adding_special = false
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
	var hull := int(result.hull_space)
	var used := int(result.space_used)
	_space_label.text = "SPACE  %d / %d  ·  %s" % [used, hull, ("%d free" % (hull - used)) if used <= hull else ("%d OVER" % (used - hull))]
	_space_label.add_theme_color_override("font_color", Color(1.0, 0.45, 0.35) if used > hull else get_theme_color("font_color", "Label"))
	_space_bar.set_parts(hull, used, int(result.weapon_space), int(result.special_space))
	_cost_label.text = "%d BC" % int(result.cost)
	_build.disabled = not result.get("buildable", false)
	_build.tooltip_text = "Save the design in its slot" if not _build.disabled else "The parts need more space than the hull has"
	Themes.glow(_build)
	command.emit({"verb": "query", "what": "design_weapons", "_reply_to": "_on_weapons"})
	command.emit({"verb": "query", "what": "design_specials", "_reply_to": "_on_specials"})
	if _systems.is_empty():
		command.emit({"verb": "query", "what": "design_systems", "_reply_to": "_on_systems"})
	_show_body()


func _on_weapons(result: Dictionary) -> void:
	if result.get("ok", false):
		_weapons = result.weapons
		if _adding_weapon:
			_show_body()


func _on_specials(result: Dictionary) -> void:
	if result.get("ok", false):
		_specials = result.specials
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
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	row.add_child(line)
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
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	row.add_child(line)
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
	pick.custom_minimum_size.x = 0
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


# ---- Weapons and specials ----

func _show_body() -> void:
	_clear(_body)
	if design.is_empty():
		return
	_body.add_child(_heading("Weapons"))
	for w: Dictionary in design.weapons:
		_body.add_child(_weapon_row(w))
	if _adding_weapon:
		_body.add_child(_weapon_picker())
	elif design.get("weapon_slot_free", true):
		_body.add_child(_add_line("+ Add a weapon", "", func():
			_adding_weapon = true
			_adding_special = false
			_replace_slot = -1
			_pick_weapon = -1
			_show_body()))
	_body.add_child(_heading("Specials"))
	for sp: Dictionary in design.specials:
		_body.add_child(_special_row(sp))
	if _adding_special:
		_body.add_child(_special_picker())
	elif design.get("special_slot_free", true):
		var fit := 0
		var need := 0
		for sp: Dictionary in _specials:
			if sp.get("fitted", false) or sp.has("excluded_by"):
				continue
			if sp.get("fits", false):
				fit += 1
			else:
				need += 1
		_body.add_child(_add_line("+ Add a special", "%d fit · %d more need room" % [fit, need], func():
			_adding_special = true
			_adding_weapon = false
			_pick_special = -1
			_show_body()))


func _weapon_row(w: Dictionary) -> Control:
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	row.add_child(line)
	var glyph := _ArcGlyph.new()
	glyph.arc = int(w.arc) if String(w.category) == "beam" else 4
	glyph.custom_minimum_size = Vector2(24, 24)
	glyph.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	glyph.tooltip_text = "Fires: %s" % ARC_TIPS[glyph.arc]
	glyph.mouse_filter = Control.MOUSE_FILTER_PASS
	line.add_child(glyph)
	var row_slot := int(w.slot)
	var minus := _button("−", "One fewer")
	minus.custom_minimum_size = Vector2(26, 24)
	minus.disabled = int(w.count) <= 1
	minus.pressed.connect(func(): _send({"verb": "design_weapon_count", "slot": row_slot, "delta": -1}))
	line.add_child(minus)
	var count := Label.new()
	count.theme_type_variation = &"Name"
	count.text = str(int(w.count))
	count.custom_minimum_size.x = 22
	count.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	line.add_child(count)
	var plus := _button("+", "One more")
	plus.custom_minimum_size = Vector2(26, 24)
	plus.disabled = not w.get("can_add", false)
	plus.tooltip_text = "One more" if not plus.disabled else "No room for one more"
	plus.pressed.connect(func(): _send({"verb": "design_weapon_count", "slot": row_slot, "delta": 1}))
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
	name_button.tooltip_text = "%s\nClick: change its arc or mods" % String(w.get("notes", ""))
	name_button.pressed.connect(func(): _open_weapon_picker_for(w))
	line.add_child(name_button)
	var detail := Label.new()
	detail.theme_type_variation = &"Dim"
	detail.text = "%s · %d BC · %d sp" % [String(w.damage), int(w.cost), int(w.space)]
	detail.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	detail.clip_text = true
	detail.custom_minimum_size.x = 120
	detail.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	detail.add_theme_font_size_override("font_size", 12)
	line.add_child(detail)
	var remove := _button("✕", "Remove this weapon")
	remove.custom_minimum_size = Vector2(26, 24)
	remove.pressed.connect(func(): _send({"verb": "design_weapon", "slot": row_slot, "weapon": 0}))
	line.add_child(remove)
	return row


func _open_weapon_picker_for(w: Dictionary) -> void:
	_adding_weapon = true
	_adding_special = false
	_replace_slot = int(w.slot)
	_pick_weapon = int(w.weapon)
	_category = String(w.category)
	_pick_arc = int(w.arc)
	_pick_mods.clear()
	for m: Dictionary in w.mods:
		_pick_mods.append(int(m.mod))
	_pick_rack = _rack_of(int(w.weapon), int(w.get("ammo", 0)))
	_request_preview()
	_show_body()


func _rack_of(weapon: int, ammo: int) -> int:
	var entry := _weapon_entry(weapon)
	var racks: Array = entry.get("racks", [])
	var found := racks.find(ammo)
	return found if found >= 0 else int(entry.get("rack", 1))


func _weapon_entry(weapon: int) -> Dictionary:
	for w: Dictionary in _weapons:
		if int(w.weapon) == weapon:
			return w
	return {}


## "+ Add a weapon", open: the categories, the weapons, then the picked one's arc (or rack), mods,
## what it takes and ADD (REPLACE for a fitted row).
func _weapon_picker() -> Control:
	var frame := _open_frame()
	var box: VBoxContainer = frame.get_child(0)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 4)
	box.add_child(top)
	var title := Label.new()
	title.theme_type_variation = &"Name"
	title.text = "+ Add a weapon" if _replace_slot < 0 else "Change the weapon"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(title)
	for c: String in CATEGORIES:
		var b := _small_toggle(c.to_upper(), _category == c)
		var cat := c
		b.pressed.connect(func():
			_category = cat
			_show_body())
		top.add_child(b)
	box.add_child(_pick_header(["WEAPON", "DMG", "BC", "SPACE"]))
	var listed := 0
	var picked := _weapon_entry(_pick_weapon)
	for w: Dictionary in _weapons:
		if String(w.category) != _category:
			continue
		listed += 1
		var id := int(w.weapon)
		var line := _pick_line([String(w.name), String(w.damage), str(int(w.cost)), str(int(w.space))], id == _pick_weapon,
			not w.get("fits", false) and _replace_slot < 0, String(w.notes))
		line.pressed.connect(func():
			_pick_weapon = id
			_pick_arc = 0
			_pick_mods.clear()
			_pick_rack = int(w.get("rack", 1))
			_request_preview()
			_show_body())
		box.add_child(line)
		if id == _pick_weapon:
			box.add_child(_weapon_options(picked)) # right under it, not past the end of a long list
	if listed == 0:
		box.add_child(_dim("None researched"))
	if picked.is_empty() or String(picked.category) != _category:
		var bottom := HBoxContainer.new()
		bottom.add_theme_constant_override("separation", 6)
		box.add_child(bottom)
		var hint := _dim("Pick a weapon")
		hint.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		bottom.add_child(hint)
		bottom.add_child(_picker_cancel())
	return frame


func _picker_cancel() -> Button:
	var cancel := _button("CANCEL", "Close the weapon list")
	cancel.pressed.connect(func():
		_adding_weapon = false
		_show_body())
	return cancel


## The picked weapon's arc glyphs (beams) or shots (missiles), its mods, what it takes, ADD.
func _weapon_options(picked: Dictionary) -> Control:
	var holder := PanelContainer.new()
	holder.theme_type_variation = &"Row"
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	holder.add_child(box)
	var options := HBoxContainer.new()
	options.add_theme_constant_override("separation", 8)
	box.add_child(options)
	if picked.get("arcs", false):
		var arcs := HBoxContainer.new()
		arcs.add_theme_constant_override("separation", 3)
		options.add_child(arcs)
		for i in 5:
			var b := Button.new()
			b.toggle_mode = true
			b.focus_mode = Control.FOCUS_NONE
			b.custom_minimum_size = Vector2(46, 50)
			b.tooltip_text = ARC_TIPS[i]
			b.set_pressed_no_signal(i == _pick_arc)
			Themes.glowing(b)
			Themes.glow(b)
			var glyph := _ArcGlyph.new()
			glyph.arc = i
			glyph.mouse_filter = Control.MOUSE_FILTER_IGNORE
			glyph.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
			glyph.offset_left = 9
			glyph.offset_right = -9
			glyph.offset_top = 3
			glyph.offset_bottom = -17
			b.add_child(glyph)
			var label := Label.new()
			label.text = ARC_NAMES[i]
			label.add_theme_font_size_override("font_size", 9)
			label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			label.mouse_filter = Control.MOUSE_FILTER_IGNORE
			label.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE)
			label.offset_top = -16
			label.offset_bottom = -2
			b.add_child(label)
			var arc := i
			b.pressed.connect(func():
				_pick_arc = arc
				_request_preview()
				_show_body())
			arcs.add_child(b)
	elif picked.has("racks"):
		var racks := HBoxContainer.new()
		racks.add_theme_constant_override("separation", 3)
		options.add_child(racks)
		racks.add_child(_dim("SHOTS"))
		for i in (picked.racks as Array).size():
			var b := _small_toggle(str(int(picked.racks[i])), i == _pick_rack)
			b.tooltip_text = "%d shots a combat" % int(picked.racks[i])
			var r := i
			b.pressed.connect(func():
				_pick_rack = r
				_request_preview()
				_show_body())
			racks.add_child(b)
	var mods := HFlowContainer.new()
	mods.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mods.add_theme_constant_override("h_separation", 3)
	mods.add_theme_constant_override("v_separation", 3)
	options.add_child(mods)
	for m: Dictionary in picked.get("mods", []):
		var mod := int(m.mod)
		var b := _small_toggle(String(m.name).to_upper(), mod in _pick_mods)
		b.add_theme_font_size_override("font_size", 10)
		b.pressed.connect(func():
			if mod in _pick_mods:
				_pick_mods.erase(mod)
			else:
				_pick_mods.append(mod)
			_request_preview()
			_show_body())
		mods.add_child(b)
	if (picked.get("mods", []) as Array).is_empty() and not picked.get("arcs", false) and not picked.has("racks"):
		mods.add_child(_dim(String(picked.notes)))
	var bottom := HBoxContainer.new()
	bottom.add_theme_constant_override("separation", 6)
	box.add_child(bottom)
	var summary := _dim(_preview_text(picked))
	summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	bottom.add_child(summary)
	bottom.add_child(_picker_cancel())
	var add := _button("ADD" if _replace_slot < 0 else "REPLACE", "Fit the picked weapon")
	add.pressed.connect(func():
		_adding_weapon = false
		_send({"verb": "design_weapon", "slot": _replace_slot, "weapon": _pick_weapon, "arc": _pick_arc, "rack": _pick_rack, "mods": _pick_mods.duplicate()}))
	bottom.add_child(add)
	return holder


func _request_preview() -> void:
	if _pick_weapon > 0:
		command.emit({"verb": "query", "what": "design_weapon_preview", "weapon": _pick_weapon, "arc": _pick_arc, "rack": _pick_rack,
			"mods": _pick_mods.duplicate(), "_reply_to": "_on_preview"})


func _on_preview(result: Dictionary) -> void:
	if result.get("ok", false) and int(result.weapon) == _pick_weapon:
		_preview = result
		if _adding_weapon:
			_show_body()


func _preview_text(picked: Dictionary) -> String:
	if picked.is_empty():
		return "Pick a weapon"
	if _preview.is_empty() or int(_preview.get("weapon", -1)) != _pick_weapon:
		return String(picked.notes)
	var text := "%s: %s dmg · %d BC · %d space" % [String(picked.name), String(_preview.damage), int(_preview.cost), int(_preview.space)]
	if picked.get("arcs", false):
		text = "%s, %s" % [text, ARC_TIPS[_pick_arc].to_lower()]
	if _replace_slot < 0:
		text += " · %d free after" % int(_preview.free_after) if int(_preview.free_after) >= 0 else " · %d too many" % -int(_preview.free_after)
	return text


func _special_row(sp: Dictionary) -> Control:
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 8)
	row.add_child(line)
	var dot := Label.new()
	dot.text = "◆"
	dot.add_theme_color_override("font_color", SPECIAL_COLOR)
	line.add_child(dot)
	var name_label := Label.new()
	name_label.text = String(sp.name)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_label.clip_text = true
	name_label.tooltip_text = String(sp.description)
	name_label.mouse_filter = Control.MOUSE_FILTER_PASS
	line.add_child(name_label)
	var detail := _dim("%s · %d sp" % [String(sp.description), int(sp.space)])
	detail.add_theme_font_size_override("font_size", 12)
	detail.clip_text = true
	detail.custom_minimum_size.x = 220
	detail.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	line.add_child(detail)
	var row_slot := int(sp.slot)
	var remove := _button("✕", "Remove this special")
	remove.custom_minimum_size = Vector2(26, 24)
	remove.pressed.connect(func(): _send({"verb": "design_special", "slot": row_slot, "special": 0}))
	line.add_child(remove)
	return row


## "+ Add a special", open: FIND and FITS NOW, the researched specials in the tech review's groups
## (dimmed: no room, or ruled out by a fitted one), the picked one's line and ADD.
func _special_picker() -> Control:
	var frame := _open_frame()
	var box: VBoxContainer = frame.get_child(0)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 6)
	box.add_child(top)
	var title := Label.new()
	title.theme_type_variation = &"Name"
	title.text = "+ Add a special"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(title)
	var find := LineEdit.new()
	find.placeholder_text = "find: name or effect"
	find.custom_minimum_size.x = 130
	find.text = _find
	find.text_changed.connect(func(t: String):
		_find = t
		_refresh_special_rows(box))
	top.add_child(find)
	var fits := _small_toggle("FITS NOW", _fits_only)
	fits.tooltip_text = "Only the specials that fit the space left"
	fits.pressed.connect(func():
		_fits_only = not _fits_only
		_show_body())
	top.add_child(fits)
	box.add_child(_pick_header(["SYSTEM", "EFFECT", "SPACE", "BC"], true))
	var rows := VBoxContainer.new()
	rows.name = "Rows"
	rows.add_theme_constant_override("separation", 2)
	box.add_child(rows)
	_fill_special_rows(rows)
	var bottom := HBoxContainer.new()
	box.add_child(bottom)
	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom.add_child(spacer)
	var cancel := _button("CANCEL", "Close the special list")
	cancel.pressed.connect(func():
		_adding_special = false
		_show_body())
	bottom.add_child(cancel)
	return frame


func _refresh_special_rows(box: VBoxContainer) -> void:
	var rows: VBoxContainer = box.get_node("Rows")
	_clear(rows)
	_fill_special_rows(rows)


func _fill_special_rows(rows: VBoxContainer) -> void:
	var groups := {}
	var order: Array[String] = []
	var needle := _find.strip_edges().to_lower()
	for sp: Dictionary in _specials:
		if sp.get("fitted", false):
			continue
		if _fits_only and not sp.get("fits", false):
			continue
		if needle != "" and not (String(sp.name).to_lower().contains(needle) or String(sp.description).to_lower().contains(needle)):
			continue
		var group := String(sp.get("group", "")).strip_edges()
		if group == "":
			group = "Other"
		if not groups.has(group):
			groups[group] = []
			order.append(group)
		(groups[group] as Array).append(sp)
	for group: String in order:
		var heading := _dim(group.to_upper())
		heading.add_theme_font_size_override("font_size", 11)
		rows.add_child(heading)
		for sp: Dictionary in groups[group]:
			var id := int(sp.special)
			var note := String(sp.description)
			if sp.has("excluded_by"):
				note = "⇄ " + String(sp.excluded_by)
			elif not sp.get("fits", false):
				note = "needs %d · %s" % [int(sp.space), note]
			var line := _pick_line([String(sp.name), note, str(int(sp.space)), str(int(sp.cost))], id == _pick_special,
				sp.has("excluded_by") or not sp.get("fits", false), String(sp.description), true)
			line.pressed.connect(func():
				_pick_special = id
				_show_body())
			rows.add_child(line)
			if id == _pick_special:
				rows.add_child(_special_options(sp)) # right under it
	if order.is_empty():
		rows.add_child(_dim("Nothing here" + (" fits the space left: turn FITS NOW off to see all" if _fits_only else "")))


## The picked special's line: what it does and takes, ADD.
func _special_options(sp: Dictionary) -> Control:
	var holder := PanelContainer.new()
	holder.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	holder.add_child(line)
	var free_after := int(design.hull_space) - int(design.space_used) - int(sp.space)
	var text := "%s · %d space · %d BC · %s" % [String(sp.description), int(sp.space), int(sp.cost),
		("%d free after" % free_after) if free_after >= 0 else ("%d too many" % -free_after)]
	if sp.has("excluded_by"):
		text = "Can't go with %s, which is fitted" % String(sp.excluded_by)
	var summary := _dim(text)
	summary.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	line.add_child(summary)
	var add := _button("ADD", "Fit this special")
	add.disabled = sp.has("excluded_by")
	add.pressed.connect(func():
		_adding_special = false
		_send({"verb": "design_special", "slot": -1, "special": _pick_special}))
	line.add_child(add)
	return holder


func _special_entry(special: int) -> Dictionary:
	for sp: Dictionary in _specials:
		if int(sp.special) == special:
			return sp
	return {}


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


## A collapsed "+ Add ..." line (the button opens its picker in place).
func _add_line(text: String, detail: String, on_open: Callable) -> Control:
	var b := Button.new()
	b.focus_mode = Control.FOCUS_NONE
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.text = text + (("     " + detail) if detail != "" else "")
	b.custom_minimum_size.y = 32
	b.clip_text = true
	b.pressed.connect(on_open)
	return b


func _open_frame() -> PanelContainer:
	var frame := PanelContainer.new()
	frame.theme_type_variation = &"RowOpen"
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	frame.add_child(box)
	return frame


## A list line of the pickers: name, then columns; picked lights it, `dim` greys it.
func _pick_line(cells: Array, picked: bool, dim: bool, tip: String, wide_second := false) -> Button:
	var b := Button.new()
	b.toggle_mode = true
	b.flat = true
	b.focus_mode = Control.FOCUS_NONE
	b.theme_type_variation = &"ListItem"
	b.custom_minimum_size.y = 24
	b.set_pressed_no_signal(picked)
	b.tooltip_text = tip
	var line := HBoxContainer.new()
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.offset_left = 6
	line.offset_right = -6
	line.add_theme_constant_override("separation", 6)
	b.add_child(line)
	var widths := [0.0, 200.0 if wide_second else 52.0, 40.0, 40.0]
	for i in cells.size():
		var label := Label.new()
		label.text = String(cells[i])
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		label.clip_text = true
		label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
		label.add_theme_font_size_override("font_size", 13 if i == 0 else 12)
		label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
		if i == 0:
			label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		else:
			label.custom_minimum_size.x = widths[mini(i, 3)]
			label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT if i > 1 or not wide_second else HORIZONTAL_ALIGNMENT_LEFT
		if picked:
			label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
		elif dim:
			label.add_theme_color_override("font_color", Color(0.45, 0.48, 0.55))
		line.add_child(label)
	return b


## The column titles over a picker's lines (the same widths as _pick_line's).
func _pick_header(titles: Array, wide_second := false) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	var widths := [0.0, 200.0 if wide_second else 52.0, 40.0, 40.0]
	var pad := Control.new()
	pad.custom_minimum_size.x = 0
	line.add_child(pad)
	for i in titles.size():
		var label := Label.new()
		label.theme_type_variation = &"Name"
		label.text = String(titles[i])
		label.add_theme_font_size_override("font_size", 11)
		if i == 0:
			label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		else:
			label.custom_minimum_size.x = widths[mini(i, 3)]
			label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT if i > 1 or not wide_second else HORIZONTAL_ALIGNMENT_LEFT
		line.add_child(label)
	var right := Control.new()
	right.custom_minimum_size.x = 6
	line.add_child(right)
	return line


func _small_toggle(text: String, on: bool) -> Button:
	var b := _button(text, "")
	b.toggle_mode = true
	b.custom_minimum_size = Vector2(0, 24)
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


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


## A firing arc as the mockup's glyph (the original's arc pictures, DESIGN.LBX 33-37): a dial with
## the ship's arrow and the arc lit; 0 forward, 1 forward extended, 2 back extended, 3 back, 4 360.
class _ArcGlyph extends Control:
	var arc := 0
	const SPANS := [[-0.75, -0.25], [-1.125, 0.125], [-0.125, 1.125], [0.25, 0.75], [0.0, 2.0]]

	func _draw() -> void:
		var r := minf(size.x, size.y) * 0.5 - 1.0
		var c := size * 0.5
		draw_circle(c, r, Color(0.03, 0.04, 0.08))
		var span: Array = SPANS[clampi(arc, 0, 4)]
		var points := PackedVector2Array()
		if arc != 4:
			points.append(c)
		var steps := 24
		for i in steps + 1:
			var a := PI * (float(span[0]) + (float(span[1]) - float(span[0])) * float(i) / float(steps))
			points.append(c + Vector2(cos(a), sin(a)) * r)
		draw_colored_polygon(points, Color(0.16, 0.36, 0.84))
		draw_arc(c, r, 0.0, TAU, 32, Color(0.22, 0.27, 0.37), 1.0, true)
		var s := r * 0.45
		draw_colored_polygon(PackedVector2Array([c + Vector2(-s * 0.55, s * 0.6), c + Vector2(0, -s), c + Vector2(s * 0.55, s * 0.6)]), Color(0.79, 0.82, 0.86))


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
