extends PanelContainer
## The system pane (WP3.3f, the owner's design in notes/antares/ui-concepts.md): slides out on the
## left when a star is opened, while the camera flies to the star and its planets fade in on the
## map. From the `system` query (v3): the fleets in orbit (their banner icons; a click opens the
## fleet panel), the star's name, the governor (the player's colony leader there, system-wide),
## and every planet with its picture, size, climate, minerals and owner. Each planet expands:
## its colony potential as icons and numbers (the right-click information), and for the player's
## colonies the morale row (government icon and masks) and a button to the colony (the legacy
## colony screen until WP3.4). Clicking a planet here or on the map selects it.

signal command(dict: Dictionary)
signal closed
signal fleet_chosen(ship_id: int)
signal planet_selected(planet_id: int)

const SLIDE_TIME := 0.3
## COLSYSDI.LBX: the colony screen's planet pictures (COLSYSDI::Planet_Anims).
const PLANET_ICONS := 11
const PLANET_SIZE_COUNT := 5
const GAS_GIANT_ICON := 0x3E
const ASTEROIDS_ICON := 0x3F

@onready var fleets_row: HBoxContainer = $Box/Fleets
@onready var star_name: Label = $Box/Header/Names/Name
@onready var star_title: Label = $Box/Header/Names/Title
@onready var governor: PanelContainer = $Box/Governor
@onready var portrait: TextureRect = $Box/Governor/Row/Frame/Portrait
@onready var governor_name: Label = $Box/Governor/Row/Names/Name
@onready var governor_title: Label = $Box/Governor/Row/Names/Title
@onready var governor_skills: VBoxContainer = $Box/Governor/Row/Names/Skills
@onready var planets_box: VBoxContainer = $Box/Scroll/Planets
@onready var description: Label = $Box/Description

var art: Node:
	set(value):
		art = value
		preload("sprite_rect.gd").bind_all(self, art)
		if art and not art.loaded.is_connected(_on_art_loaded):
			art.loaded.connect(_on_art_loaded)
var system := {}
var star := -1
var race_id := -1 # the player's race (empire query): colonist pictures
var selected_planet := -1
var _expanded := {} # planet id -> true
var _potential := {} # planet id -> the potential_colony answer
var _tween: Tween
var _shown_left := 0.0


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)
	_restyle()
	$Box/Back.pressed.connect(close)
	# The governor opens the colony leaders on this system (the legacy leaders screen for now).
	governor.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT:
			command.emit({"verb": "open", "screen": Screens.OFFICERS, "star": star}))


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


## Inside the screen frame, from under the top bar to the bottom (WP3.3g).
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_left = inset + gap
	offset_right = offset_left + 372.0
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -(inset + gap)
	_shown_left = offset_left
	if not system.is_empty():
		_show_planets()


## Shows a system (the `system` query's answer) under the star's name.
func show_system(result: Dictionary, name_text: String, star_id: int) -> void:
	var same := not system.is_empty() and star == star_id
	system = result
	star = star_id
	if not same:
		selected_planet = -1
		_expanded = {}
		_potential = {}
	star_name.text = name_text
	star_title.text = result.get("title", "")
	description.visible = result.get("view", "full") != "full" and result.has("description")
	description.text = result.get("description", "")
	_show_fleets()
	_show_governor()
	_show_planets()
	if not visible:
		_slide(true)


func close() -> void:
	if not visible:
		return
	_slide(false)
	command.emit({"verb": "close_system"}) # the saved system box record (galaxy_queries.cpp)
	system = {}
	closed.emit()


## Hides the pane without telling the game (opening a fleet closed the system box itself).
func dismiss() -> void:
	if visible:
		_slide(false)
		system = {}
		closed.emit()


func select_planet(planet_id: int) -> void:
	selected_planet = planet_id
	if planet_id >= 0:
		_expanded[planet_id] = true
		_ask_potential(planet_id)
	_show_planets()


func _show_fleets() -> void:
	for child in fleets_row.get_children():
		child.queue_free()
	var fleets: Array = system.get("fleets", [])
	fleets_row.visible = not fleets.is_empty()
	for fleet: Dictionary in fleets:
		var button := Button.new()
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(56, 40)
		# The picture is the button (owner): no frame round it, a little brighter under the pointer.
		button.mouse_entered.connect(func(): button.modulate = Color(1.25, 1.25, 1.25))
		button.mouse_exited.connect(func(): button.modulate = Color.WHITE)
		button.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		button.expand_icon = true
		button.tooltip_text = "\n".join(fleet.get("lines", []).map(func(line: Dictionary) -> String: return line.text))
		var ship := int(fleet.ship)
		button.pressed.connect(func(): fleet_chosen.emit(ship))
		fleets_row.add_child(button)
		button.set_meta("box_sprite", int(fleet.box_sprite))
		_fleet_icon(button, int(fleet.ships))


## A fleet's box picture, or its ship count until the picture arrives (_on_art_loaded).
func _fleet_icon(button: Button, ships: int) -> void:
	var texture: Texture2D = art.texture("BUFFER0.LBX", int(button.get_meta("box_sprite"))) if art else null
	button.icon = texture
	button.text = "" if texture else str(ships)
	button.flat = texture != null # framed only while it is a count


## Art arrived: fleet buttons still showing a count get their picture (owner, 2026-11-11: the
## first system opened showed numbers until clicked again).
func _on_art_loaded(_key: String) -> void:
	for button: Button in fleets_row.get_children():
		if button.icon == null and not button.is_queued_for_deletion():
			_fleet_icon(button, int(button.text))


func _show_governor() -> void:
	var leader: Dictionary = system.get("governor", {})
	# Shown on the player's systems, with or without a governor: either way it opens the leaders.
	var mine: bool = system.get("planets", []).any(func(planet: Dictionary) -> bool:
		return planet.get("colony", {}).get("mine", false) and not planet.get("colony", {}).get("outpost", false))
	governor.visible = not leader.is_empty() or mine
	if leader.is_empty():
		for child in governor_skills.get_children():
			child.queue_free()
		portrait.show_sprite("", -1)
		governor_name.text = "No governor"
		governor_title.text = "Assign a colony leader"
		return
	portrait.show_sprite("OFFICER.LBX", int(leader.portrait))
	governor_name.text = leader.name
	governor_title.text = leader.title
	# The bonuses (system v4): each skill's icon, name and bonus, as the leaders screen lists them.
	for child in governor_skills.get_children():
		child.queue_free()
	for skill: Dictionary in leader.get("skills", []):
		var line := HBoxContainer.new()
		line.mouse_filter = Control.MOUSE_FILTER_IGNORE
		line.add_theme_constant_override("separation", 4)
		line.add_child(_icon("OFFICER.LBX", int(skill.icon), Vector2(16, 16)))
		var name_label := _label(String(skill.name), 14)
		name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(name_label)
		var bonus := _label(String(skill.bonus).strip_edges(), 14)
		bonus.add_theme_color_override("font_color", get_theme_color("font_color", "Name"))
		line.add_child(bonus)
		governor_skills.add_child(line)


func _show_planets() -> void:
	for child in planets_box.get_children():
		child.queue_free()
	if system.get("view", "full") != "full":
		return
	for planet: Dictionary in system.get("planets", []):
		planets_box.add_child(_planet_row(planet))


func _planet_row(planet: Dictionary) -> Control:
	var id := int(planet.planet)
	var panel := PanelContainer.new()
	# The theme's row; the selected planet's is the open one (WP3.3g).
	panel.theme_type_variation = &"RowOpen" if id == selected_planet else &"Row"
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	panel.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT:
			_on_planet_clicked(id))
	var rows := VBoxContainer.new()
	rows.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(rows)
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_theme_constant_override("separation", 10)
	rows.add_child(row)
	row.add_child(_icon("COLSYSDI.LBX", _planet_icon(planet), Vector2(34, 34)))
	var names := VBoxContainer.new()
	names.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	names.mouse_filter = Control.MOUSE_FILTER_IGNORE
	names.add_theme_constant_override("separation", 0)
	row.add_child(names)
	var name_label := _label(planet.get("name", ""), 18)
	name_label.add_theme_font_override("font", get_theme_font("font", "Name"))
	names.add_child(name_label)
	if planet.type == "planet":
		var words: Array = [planet.get("size_name", ""), planet.get("climate_name", ""), planet.get("minerals_name", "")]
		if planet.has("gravity_name"):
			words.append(planet.gravity_name)
		var words_label := _label(" · ".join(words), 13)
		words_label.theme_type_variation = &"Dim"
		words_label.add_theme_font_size_override("font_size", 13)
		names.add_child(words_label)
	var colony: Dictionary = planet.get("colony", {})
	var right := VBoxContainer.new()
	right.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(right)
	if not colony.is_empty():
		var owner_line := _label(_colony_text(planet, colony), 13)
		owner_line.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		owner_line.add_theme_color_override("font_color", _player_color(int(colony.color)))
		right.add_child(owner_line)
	if planet.type != "gas_giant":
		var arrow := _label("▾" if _expanded.has(id) else "▸", 14)
		arrow.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		right.add_child(arrow)
	if _expanded.has(id) and planet.type != "gas_giant":
		rows.add_child(_details(planet, colony))
	return panel


## The expanded part: potential as icons and numbers, then the colony's morale and its button.
func _details(planet: Dictionary, colony: Dictionary) -> Control:
	var box := VBoxContainer.new()
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var potential: Dictionary = _potential.get(int(planet.planet), {})
	if not potential.is_empty():
		var line := HBoxContainer.new()
		line.mouse_filter = Control.MOUSE_FILTER_IGNORE
		line.add_theme_constant_override("separation", 4)
		if race_id >= 0:
			line.add_child(_icon("RACEICON.LBX", race_id * 13, Vector2(20, 26), "Maximum population"))
		line.add_child(_label(str(int(potential.max_population)), 17))
		line.add_child(_gap())
		line.add_child(_bar_icon("food", "Food per farmer"))
		line.add_child(_label(str(potential.food_per_farmer), 17))
		line.add_child(_gap())
		line.add_child(_bar_icon("industry", "Industry per worker"))
		line.add_child(_label(str(int(potential.industry_per_worker)), 17))
		line.add_child(_gap())
		line.add_child(_bar_icon("research", "Research per scientist"))
		line.add_child(_label(str(int(potential.research_per_scientist)), 17))
		box.add_child(line)
		var extras: Array = []
		for entry: Dictionary in potential.get("lines", []).slice(4):
			if entry.value != "--":
				extras.append("%s %s" % [entry.label, entry.value])
		if not extras.is_empty():
			box.add_child(_label("   ".join(extras), 12, 0.7))
	if colony.has("government"):
		var morale := HBoxContainer.new()
		morale.mouse_filter = Control.MOUSE_FILTER_IGNORE
		morale.add_theme_constant_override("separation", 2)
		morale.add_child(_icon("COLONY2.LBX", int(colony.government_sprite), Vector2(22, 22), "Government"))
		morale.add_child(_gap())
		var masks := absi(int(colony.morale_icons))
		for i in mini(masks, 10):
			morale.add_child(_icon("COLONY2.LBX", int(colony.morale_sprite), Vector2(18, 18), "Morale"))
		var spacer := Control.new()
		spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
		morale.add_child(spacer)
		morale.add_child(_colony_button(planet, colony))
		box.add_child(morale)
	elif colony.get("mine", false):
		box.add_child(_colony_button(planet, colony))
	return box


## The colony's button (the legacy colony screen until WP3.4); outposts have none to open.
func _colony_button(planet: Dictionary, colony: Dictionary) -> Button:
	var open_colony := Button.new()
	open_colony.text = "OUTPOST" if colony.get("outpost", false) else "COLONY"
	open_colony.focus_mode = Control.FOCUS_NONE
	open_colony.disabled = colony.get("outpost", false) or not colony.get("mine", false)
	open_colony.visible = colony.get("mine", false)
	open_colony.custom_minimum_size = Vector2(96, 26)
	open_colony.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	open_colony.add_theme_font_size_override("font_size", 13)
	var id := int(planet.planet)
	open_colony.pressed.connect(func(): command.emit({"verb": "open_colony", "planet": id}))
	return open_colony


## A bar icon (the theme art's: food, industry, research), at a row's size.
func _bar_icon(name: String, tip: String) -> TextureRect:
	var rect := TextureRect.new()
	rect.texture = preload("../themes.gd").icon(name, 20)
	rect.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.tooltip_text = tip
	rect.mouse_filter = Control.MOUSE_FILTER_PASS
	return rect


func _on_planet_clicked(planet_id: int) -> void:
	if selected_planet == planet_id and _expanded.has(planet_id):
		_expanded.erase(planet_id) # a second click folds it
		_show_planets()
		return
	select_planet(planet_id)
	planet_selected.emit(planet_id)


func _ask_potential(planet_id: int) -> void:
	for planet: Dictionary in system.get("planets", []):
		if int(planet.planet) == planet_id and planet.type != "gas_giant" and not _potential.has(planet_id):
			command.emit({"verb": "query", "what": "potential_colony", "planet": planet_id, "_reply_to": "show_potential"})


## The `potential_colony` answer (main.gd routes it here).
func show_potential(result: Dictionary) -> void:
	if not result.get("ok", false) or system.is_empty():
		return
	_potential[int(result.planet)] = result
	_show_planets()


func _colony_text(planet: Dictionary, colony: Dictionary) -> String:
	if colony.outpost:
		return "Outpost"
	return "%d / %d" % [int(colony.population), int(planet.max_population)]


func _planet_icon(planet: Dictionary) -> int:
	match planet.type:
		"gas_giant":
			return GAS_GIANT_ICON
		"asteroids":
			return ASTEROIDS_ICON
	return PLANET_ICONS + int(planet.climate) * PLANET_SIZE_COUNT + mini(int(planet.size), PLANET_SIZE_COUNT - 1)


func _icon(lbx: String, entry: int, min_size: Vector2, tip := "") -> TextureRect:
	var rect: TextureRect = preload("sprite_rect.gd").new()
	rect.custom_minimum_size = min_size
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.tooltip_text = tip
	rect.mouse_filter = Control.MOUSE_FILTER_PASS if tip != "" else Control.MOUSE_FILTER_IGNORE
	rect.bind(art)
	rect.show_sprite(lbx, entry)
	return rect


func _label(text: String, font_size: int, alpha := 1.0) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", font_size)
	label.modulate.a = alpha
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return label


func _gap() -> Control:
	var gap := Control.new()
	gap.custom_minimum_size = Vector2(10, 0)
	gap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return gap


func _player_color(color_index: int) -> Color:
	var galaxy_script := preload("../galaxy.gd")
	return galaxy_script.PLAYER_COLORS[color_index % 8] if color_index >= 0 else galaxy_script.NEUTRAL_COLOR


## Slides in from (or out to) the left edge.
func _slide(opening: bool) -> void:
	if _tween:
		_tween.kill()
	var hidden_x := -size.x - 8.0
	if opening:
		show()
		position.x = hidden_x
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:x", _shown_left if opening else hidden_x, SLIDE_TIME)
	if not opening:
		_tween.tween_callback(hide)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
