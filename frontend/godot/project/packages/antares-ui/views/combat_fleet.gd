extends PanelContainer
## The select-combat window's other side (owner, 2026-11-20): beside the zoomed system, right of
## it, opposite the target list. From the request's `fleet` (the combat_fleet query: the game
## answers no queries while the window waits):
##   - each other side there, as the player sees it from orbit: its ships by hull size, and with
##     Battle Scanner researched a deliberately rough rating against the player's ships;
##   - the player's own colonies there and their defences (bases, batteries, shields);
##   - the player's ships in orbit, a scrolling list: picture, name, hull, armour and structure
##     left, what is damaged;
##   - the hovered (or clicked) ship: its officer, weapons with mods and arcs, specials and systems.

const WIDTH := 470.0
const TEXT_WIDTH := 370.0 # wrapped lines' width (measured before layout, a wrapped label is one word wide)

var art: Node
var fleet := {}
var _colonies: VBoxContainer
var _rows: VBoxContainer
var _detail: VBoxContainer
var _picked := -1 # a clicked ship stays shown; -1: the hovered one, else the first


func _ready() -> void:
	theme_type_variation = &"Pane"
	mouse_filter = Control.MOUSE_FILTER_STOP
	custom_minimum_size.x = WIDTH
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	_colonies = VBoxContainer.new()
	_colonies.add_theme_constant_override("separation", 4)
	box.add_child(_colonies)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "Your Fleet"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(title)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size.y = 70
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	preload("../themes.gd").scroll_gap(scroll)
	box.add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 4)
	scroll.add_child(_rows)
	var detail_frame := PanelContainer.new()
	detail_frame.theme_type_variation = &"Row"
	detail_frame.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail_frame.size_flags_stretch_ratio = 1.2 # the ship's details a little more room than the list
	box.add_child(detail_frame)
	var detail_scroll := ScrollContainer.new() # short screens (200%): the details scroll too
	detail_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	detail_scroll.custom_minimum_size.y = 90
	preload("../themes.gd").scroll_gap(detail_scroll)
	detail_frame.add_child(detail_scroll)
	_detail = VBoxContainer.new()
	_detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_detail.add_theme_constant_override("separation", 4)
	detail_scroll.add_child(_detail)


## Shows `data` (the combat_fleet query's answer), its left edge at `left`, filling the panes'
## band from `top`, `height` tall (the ship list scrolls in what is left).
func open(data: Dictionary, left: float, top: float, height: float, right_limit := INF) -> void:
	fleet = data
	_picked = -1
	_fill_colonies()
	_fill_rows()
	var ships: Array = data.get("ships", [])
	_show_ship(ships[0] if not ships.is_empty() else {})
	show()
	var area := get_parent_area_size()
	position = Vector2(clampf(left, 0.0, maxf(minf(area.x, right_limit) - WIDTH, 0.0)), top).floor() # clear of the side tabs
	await get_tree().process_frame # laid out at its width first
	size = Vector2(WIDTH, height) # the band's height; the ship list scrolls in what is left


func close() -> void:
	hide()
	fleet = {}


func _fill_colonies() -> void:
	for child in _colonies.get_children():
		_colonies.remove_child(child)
		child.queue_free()
	var colonies: Array = fleet.get("colonies", [])
	if colonies.is_empty():
		return
	var heading := Label.new()
	heading.theme_type_variation = &"Heading"
	heading.text = "Your Defences"
	heading.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_colonies.add_child(heading)
	for colony: Dictionary in colonies:
		var name_label := Label.new()
		name_label.theme_type_variation = &"Name"
		name_label.text = String(colony.get("name", ""))
		_colonies.add_child(name_label)
		var names: Array = []
		for d: Dictionary in colony.get("defences", []):
			names.append(String(d.get("name", "")))
		var line := Label.new()
		line.theme_type_variation = &"Dim"
		line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		line.custom_minimum_size.x = TEXT_WIDTH
		line.text = ", ".join(names) if not names.is_empty() else "No defences"
		_colonies.add_child(line)
	_colonies.add_child(HSeparator.new())


func _fill_rows() -> void:
	for child in _rows.get_children():
		_rows.remove_child(child)
		child.queue_free()
	var ships: Array = fleet.get("ships", [])
	if ships.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "None of your warships are in orbit"
		_rows.add_child(none)
		return
	for ship: Dictionary in ships:
		_rows.add_child(_ship_row(ship))


func _ship_row(ship: Dictionary) -> Control:
	var row := Button.new()
	row.toggle_mode = true
	row.focus_mode = Control.FOCUS_NONE
	row.custom_minimum_size = Vector2(0, 82)
	row.set_meta("ship", int(ship.ship))
	var line := HBoxContainer.new()
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.offset_left = 6
	line.offset_right = -6
	line.add_theme_constant_override("separation", 8)
	row.add_child(line)
	var picture: TextureRect = preload("sprite_rect.gd").new()
	picture.custom_minimum_size = Vector2(64, 56)
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if art:
		picture.bind(art)
		picture.show_sprite("SHIPS.LBX", int(ship.get("picture", -1)))
	line.add_child(picture)
	var text := VBoxContainer.new()
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	text.alignment = BoxContainer.ALIGNMENT_CENTER
	text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	text.add_theme_constant_override("separation", 2)
	line.add_child(text)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = "%s  ·  %s" % [String(ship.get("name", "")), String(ship.get("hull_name", ""))]
	name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	text.add_child(name_label)
	text.add_child(_bar("Armor", int(ship.get("armor", 0)), int(ship.get("armor_max", 0)), Color(0.45, 0.7, 0.95)))
	text.add_child(_bar("Structure", int(ship.get("structure", 0)), int(ship.get("structure_max", 0)), Color(0.55, 0.85, 0.5)))
	var damaged: Array = ship.get("damaged", [])
	if not damaged.is_empty():
		var hurt := Label.new()
		hurt.text = ", ".join(damaged.map(func(d: Dictionary) -> String: return String(d.text)))
		hurt.add_theme_color_override("font_color", Color(0.95, 0.45, 0.35))
		hurt.add_theme_font_size_override("font_size", 12)
		hurt.mouse_filter = Control.MOUSE_FILTER_IGNORE
		text.add_child(hurt)
		row.custom_minimum_size.y = 98
	row.mouse_entered.connect(func():
		if _picked < 0:
			_show_ship(ship))
	row.pressed.connect(func():
		_picked = int(ship.ship) if row.button_pressed else -1
		for other in _rows.get_children():
			if other != row and other is Button:
				(other as Button).set_pressed_no_signal(false)
		_show_ship(ship))
	return row


## A thin bar: what is left of the most, with the numbers.
func _bar(label_text: String, left: int, most: int, color: Color) -> Control:
	var line := HBoxContainer.new()
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.add_theme_constant_override("separation", 6)
	var label := Label.new()
	label.text = label_text
	label.custom_minimum_size.x = 70
	label.add_theme_font_size_override("font_size", 12)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.add_child(label)
	var bar := ProgressBar.new()
	bar.show_percentage = false
	bar.max_value = maxi(most, 1)
	bar.value = left
	bar.custom_minimum_size = Vector2(150, 8)
	bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var fill := StyleBoxFlat.new()
	fill.bg_color = color if left * 4 > most else Color(0.95, 0.4, 0.35)
	bar.add_theme_stylebox_override("fill", fill)
	line.add_child(bar)
	var numbers := Label.new()
	numbers.text = "%d / %d" % [left, most]
	numbers.add_theme_font_size_override("font_size", 12)
	numbers.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.add_child(numbers)
	return line


func _show_ship(ship: Dictionary) -> void:
	for child in _detail.get_children():
		_detail.remove_child(child)
		child.queue_free()
	if ship.is_empty():
		return
	var officer: Dictionary = ship.get("officer", {})
	if not officer.is_empty():
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 10)
		var portrait: TextureRect = preload("sprite_rect.gd").new()
		portrait.custom_minimum_size = Vector2(64, 64)
		portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		if art:
			portrait.bind(art)
			portrait.show_sprite("OFFICER.LBX", int(officer.get("portrait", -1)))
		row.add_child(portrait)
		var names := VBoxContainer.new()
		names.add_theme_constant_override("separation", 1)
		names.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(names)
		var who := Label.new()
		who.theme_type_variation = &"Name"
		who.text = "%s, level %d" % [String(officer.get("name", "")), int(officer.get("level", 0)) + 1]
		names.add_child(who)
		for skill: Dictionary in officer.get("skills", []):
			var line := Label.new()
			line.text = "%s %s" % [String(skill.get("name", "")), String(skill.get("bonus", "")).strip_edges()]
			line.add_theme_font_size_override("font_size", 13)
			names.add_child(line)
		_detail.add_child(row)
	var heading := Label.new()
	heading.theme_type_variation = &"Section"
	heading.text = "WEAPONS"
	_detail.add_child(heading)
	for weapon: Dictionary in ship.get("weapons", []):
		var w := Label.new()
		w.text = "%d %s (%s)" % [int(weapon.get("count", 0)), String(weapon.get("text", "")).strip_edges(), String(weapon.get("arc", ""))]
		w.add_theme_font_size_override("font_size", 13)
		w.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		w.custom_minimum_size.x = TEXT_WIDTH
		_detail.add_child(w)
	var design: Dictionary = ship.get("design", {})
	var specials: Array = (design.get("specials", []) as Array).map(func(s: Dictionary) -> String: return String(s.text))
	if not specials.is_empty():
		var sh := Label.new()
		sh.theme_type_variation = &"Section"
		sh.text = "SPECIALS"
		_detail.add_child(sh)
		var sp := Label.new()
		sp.text = ", ".join(specials)
		sp.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		sp.custom_minimum_size.x = TEXT_WIDTH
		sp.add_theme_font_size_override("font_size", 13)
		_detail.add_child(sp)
	var systems := Label.new()
	systems.theme_type_variation = &"Dim"
	systems.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	systems.custom_minimum_size.x = TEXT_WIDTH
	systems.add_theme_font_size_override("font_size", 13)
	systems.text = "%s · %s · %s · %s" % [String(design.get("shield", "")), String(design.get("computer", "")), String(design.get("armor", "")),
		String(design.get("drive", ""))]
	_detail.add_child(systems)
