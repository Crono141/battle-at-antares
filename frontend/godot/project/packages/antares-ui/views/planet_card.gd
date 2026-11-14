extends RefCounted
## A planet's card (owner, 2026-11-13): what colonising it means, with icons instead of the game
## box's text lines (their font codes showed as zeros natively). Its picture, name and words (size,
## climate, minerals, gravity), the special found there, then the potential as icons:
## (colonist) up to N, (food) per (farmer), (industry) per (worker), (research) per (scientist),
## and the box's other lines (maintenance, worker bonus or penalty) when they apply.
## Built from a `system` planet entry and a `potential_colony` answer (either may be empty); used
## by the colony ship's window (hovering a planet), its confirmation and the right click's card.

## COLSYSDI.LBX: the colony screen's planet pictures (as the system pane).
const PLANET_ICONS := 11
const PLANET_SIZE_COUNT := 5
const GAS_GIANT_ICON := 0x3E
const ASTEROIDS_ICON := 0x3F
## RACEICON.LBX: 13 entries a race; a colonist doing a job is race * 13 + job * 2 + 1.
const RACE_STRIDE := 13
const SPECIAL_COLOR := Color(1.0, 0.82, 0.35)


static func build(art: Node, planet: Dictionary, potential: Dictionary, race_id: int, width := 340.0) -> Control:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	box.custom_minimum_size.x = width
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if not planet.is_empty():
		var head := HBoxContainer.new()
		head.add_theme_constant_override("separation", 10)
		head.mouse_filter = Control.MOUSE_FILTER_IGNORE
		head.add_child(_sprite(art, "COLSYSDI.LBX", planet_icon(planet), Vector2(40, 40)))
		var names := VBoxContainer.new()
		names.add_theme_constant_override("separation", 0)
		names.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		names.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var name_label := _label(String(planet.get("name", "")), 18)
		name_label.theme_type_variation = &"Name"
		names.add_child(name_label)
		if String(planet.get("type", "")) == "planet":
			var words: Array = [planet.get("size_name", ""), planet.get("climate_name", ""), planet.get("minerals_name", "")]
			if planet.has("gravity_name"):
				words.append(planet.gravity_name)
			var words_label := _label(" · ".join(words), 13)
			words_label.theme_type_variation = &"Dim"
			names.add_child(words_label)
		head.add_child(names)
		box.add_child(head)
		if planet.has("special_name"):
			box.add_child(special_line(planet, width))
	# Gas giants and asteroid fields can't be colonised, only outposted (owner, 2026-11-14: an
	# asteroid field's card showed food, industry and research per worker).
	match String(planet.get("type", "")):
		"gas_giant":
			box.add_child(_label("A gas giant: uninhabitable. Only an outpost can be built here.", 14, 0.8))
			return box
		"asteroids":
			box.add_child(_label("An asteroid field: uninhabitable. Only an outpost can be built here.", 14, 0.8))
			return box
	if potential.is_empty():
		return box
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 10)
	grid.add_theme_constant_override("v_separation", 4)
	grid.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var race := maxi(race_id, 0)
	# (colonist) up to N
	grid.add_child(_icons([_sprite(art, "RACEICON.LBX", race * RACE_STRIDE, Vector2(20, 26))]))
	grid.add_child(_value("Population up to %d" % int(potential.get("max_population", 0))))
	# (food) per (farmer): x, and the other two jobs
	var jobs := [["food", 0, "food_per_farmer", "Food per farmer"],
		["industry", 1, "industry_per_worker", "Industry per worker"],
		["research", 2, "research_per_scientist", "Research per scientist"]]
	for job: Array in jobs:
		var icons := _icons([_theme_icon(job[0]), _label("per", 14, 0.8),
			_sprite(art, "RACEICON.LBX", race * RACE_STRIDE + int(job[1]) * 2 + 1, Vector2(20, 26))])
		icons.tooltip_text = job[3]
		icons.mouse_filter = Control.MOUSE_FILTER_PASS
		grid.add_child(icons)
		grid.add_child(_value(str(potential.get(job[2], 0))))
	box.add_child(grid)
	# The box's other lines (maintenance, the worker bonus), when they say something.
	var lines: Array = potential.get("lines", [])
	for entry: Dictionary in lines.slice(4):
		if String(entry.get("value", "--")) != "--":
			var line := _label("%s: %s" % [String(entry.label).strip_edges().trim_suffix(":"), String(entry.value)], 14)
			line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			line.custom_minimum_size.x = width # (a wrapping label without a width measures a letter a line)
			box.add_child(line)
	return box


## The special found on a planet: its name in gold and what it does.
static func special_line(planet: Dictionary, width := 340.0) -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 2)
	column.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var name_label := _label("◆ " + String(planet.special_name), 15)
	name_label.add_theme_color_override("font_color", SPECIAL_COLOR)
	column.add_child(name_label)
	var text := String(planet.get("special_text", "")).strip_edges()
	if text != "":
		var body := _label(text, 13, 0.85)
		body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		body.custom_minimum_size.x = width
		column.add_child(body)
	return column


static func planet_icon(planet: Dictionary) -> int:
	match String(planet.get("type", "planet")):
		"gas_giant":
			return GAS_GIANT_ICON
		"asteroids":
			return ASTEROIDS_ICON
	return PLANET_ICONS + int(planet.get("climate", 0)) * PLANET_SIZE_COUNT + mini(int(planet.get("size", 0)), PLANET_SIZE_COUNT - 1)


static func _icons(parts: Array) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for part: Control in parts:
		row.add_child(part)
	return row


static func _value(text: String) -> Label:
	var label := _label(text, 17)
	label.theme_type_variation = &"Name"
	label.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	return label


static func _theme_icon(name: String) -> TextureRect:
	var rect := TextureRect.new()
	rect.texture = preload("../themes.gd").icon(name, 20)
	rect.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return rect


static func _sprite(art: Node, lbx: String, entry: int, min_size: Vector2) -> TextureRect:
	var rect: TextureRect = preload("sprite_rect.gd").new()
	rect.custom_minimum_size = min_size
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if art:
		rect.bind(art)
		rect.show_sprite(lbx, entry)
	return rect


static func _label(text: String, font_size: int, alpha := 1.0) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.modulate.a = alpha
	label.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label
