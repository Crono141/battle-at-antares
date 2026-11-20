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
## One of the player's colonies was picked (WP3.4: the colony pane opens beside), or -1.
signal colony_chosen(colony_id: int)

const SLIDE_TIME := 0.3
## COLSYSDI.LBX: the colony screen's planet pictures (COLSYSDI::Planet_Anims).
const PLANET_ICONS := 11
const PLANET_SIZE_COUNT := 5
const GAS_GIANT_ICON := 0x3E
const ASTEROIDS_ICON := 0x3F

@onready var fleets_row: HFlowContainer = $Box/Scroll/Margin/Content/Fleets
@onready var star_name: Label = $Box/Header/Names/Name
@onready var star_title: Label = $Box/Header/Names/Title
@onready var governor: PanelContainer = $Box/Scroll/Margin/Content/Governor
@onready var portrait: TextureRect = $Box/Scroll/Margin/Content/Governor/Row/Frame/Portrait
@onready var governor_name: Label = $Box/Scroll/Margin/Content/Governor/Row/Names/Name
@onready var governor_title: Label = $Box/Scroll/Margin/Content/Governor/Row/Names/Title
@onready var governor_skills: VBoxContainer = $Box/Scroll/Margin/Content/Governor/Row/Names/Skills
@onready var planets_box: VBoxContainer = $Box/Scroll/Margin/Content/Planets
@onready var description: Label = $Box/Scroll/Margin/Content/Description

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
	preload("../themes.gd").scroll_gap($Box/Scroll)
	$Box/Header/Close.pressed.connect(close)
	_restyle()
	$Box/Back.pressed.connect(close)
	# The governor opens a flyout of the colony leaders to the right (owner, 2026-11-13: not the
	# legacy leaders screen); a second click closes it.
	governor.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT:
			if _leaders_flyout and _leaders_flyout.visible:
				close_leaders()
			else:
				# The flyout takes the colony pane's place beside this one (owner, 2026-11-13).
				colony_chosen.emit(-1)
				command.emit({"verb": "query", "what": "colony_leaders", "star": star, "_reply_to": "show_leaders"}))


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
		close_leaders()
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


# ---- The governor's flyout (owner, 2026-11-13): the player's colony leaders, each with its
# portrait, skills and where it is; a click makes it this system's governor (the turns it takes
# to get there shown first), RELEASE sends the governor here back to the pool. ----

var _leaders_flyout: PanelContainer


## The `colony_leaders` answer: the flyout opens beside the pane, level with the governor block.
func show_leaders(result: Dictionary) -> void:
	if not result.get("ok", false) or not visible:
		return
	if _leaders_flyout == null:
		_leaders_flyout = PanelContainer.new()
		_leaders_flyout.theme_type_variation = &"Pane"
		_leaders_flyout.name = "LeadersFlyout"
		_leaders_flyout.top_level = true # the pane is a container: placed by hand, beside it
		add_child(_leaders_flyout)
	for child in _leaders_flyout.get_children():
		_leaders_flyout.remove_child(child)
		child.queue_free()
	var outer := VBoxContainer.new()
	outer.add_theme_constant_override("separation", 6)
	var header := HBoxContainer.new()
	var title := _label("Colony leaders", 18)
	title.theme_type_variation = &"Name"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var x := Button.new()
	x.text = "X"
	x.focus_mode = Control.FOCUS_NONE
	x.pressed.connect(close_leaders)
	header.add_child(x)
	outer.add_child(header)
	var current := int(system.get("governor", {}).get("leader", -1))
	var leaders: Array = result.get("leaders", [])
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 4)
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	if leaders.is_empty():
		list.add_child(_label("No colony leaders. Leaders offer to join between turns.", 14, 0.8))
	for leader: Dictionary in leaders:
		list.add_child(_leader_row(leader, int(leader.leader) == current))
	outer.add_child(scroll)
	_leaders_flyout.add_child(outer)
	_leaders_flyout.custom_minimum_size = Vector2(340, 0)
	scroll.custom_minimum_size.y = minf(560.0, 30.0 + 96.0 * maxi(leaders.size(), 1))
	_leaders_flyout.reset_size()
	# Right of this pane, where the colony pane would be (it closed for the flyout).
	var right := _shown_left + size.x
	_leaders_flyout.global_position = Vector2(right + 6.0, governor.global_position.y)
	_leaders_flyout.show()


func close_leaders() -> void:
	if _leaders_flyout:
		_leaders_flyout.hide()


func _leader_row(leader: Dictionary, is_governor: bool) -> Control:
	var row := Button.new()
	row.theme_type_variation = &"ListItem"
	row.focus_mode = Control.FOCUS_NONE
	row.custom_minimum_size = Vector2(320, 0)
	# A Button doesn't lay out children: a margin box inside it does, and the button takes its height.
	var margin := MarginContainer.new()
	for side in ["left", "right", "top", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 6)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var box := HBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	margin.add_child(box)
	# Only a real change (a resize answered with a resize loops: owner, 2026-11-14, "Message
	# queue out of memory" while dragging the window).
	margin.minimum_size_changed.connect(func():
		var height := margin.get_combined_minimum_size().y
		if absf(height - row.custom_minimum_size.y) > 0.5:
			row.custom_minimum_size.y = height)
	var picture := _icon("OFFICER.LBX", int(leader.get("portrait", -1)), Vector2(56, 56))
	picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(picture)
	var lines := VBoxContainer.new()
	lines.add_theme_constant_override("separation", 1)
	lines.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var name_line := _label(String(leader.name), 15)
	name_line.add_theme_color_override("font_color", get_theme_color("font_color", "Name"))
	lines.add_child(name_line)
	var where := "In the pool"
	var here := int(leader.get("star", -1))
	var for_hire: bool = leader.get("for_hire", false)
	if for_hire:
		where = "For hire: %d BC" % int(leader.get("cost", 0))
	elif is_governor:
		where = "Governs here" if int(leader.get("eta", 0)) <= 0 else "On the way here: %d turns" % int(leader.eta)
	elif here >= 0:
		where = ("Governs " if int(leader.get("eta", 0)) <= 0 else "On the way to ") + String(leader.get("star_name", ""))
	if not is_governor and not for_hire:
		var eta := int(leader.get("eta_to", 0))
		where += "  ·  " + ("arrives at once" if eta <= 0 else "arrives in %d turns" % eta)
	lines.add_child(_label(where, 13, 0.8))
	for skill: Dictionary in leader.get("skills", []):
		var skill_line := HBoxContainer.new()
		skill_line.add_theme_constant_override("separation", 4)
		skill_line.mouse_filter = Control.MOUSE_FILTER_IGNORE
		var icon := _icon("OFFICER.LBX", int(skill.icon), Vector2(14, 14))
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		skill_line.add_child(icon)
		var skill_label := _label("%s %s" % [String(skill.name), String(skill.bonus).strip_edges()], 13)
		skill_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		skill_line.add_child(skill_label)
		lines.add_child(skill_line)
	box.add_child(lines)
	row.add_child(margin)
	if is_governor:
		var release := Button.new()
		release.text = "RELEASE"
		release.focus_mode = Control.FOCUS_NONE
		release.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		release.pressed.connect(func():
			command.emit({"verb": "release_governor", "star": star, "_reply_to": "governor_changed"}))
		box.add_child(release)
		row.disabled = true
	else:
		var leader_id := int(leader.leader)
		# One for hire: the hire offer first (owner, 2026-11-14), then it governs here.
		var verb := "hire_leader" if for_hire else "assign_governor"
		row.pressed.connect(func():
			command.emit({"verb": verb, "leader": leader_id, "star": star, "_reply_to": "governor_changed"}))
	return row


## A governor assigned or released: the flyout closes and the pane shows the system again.
func governor_changed(_result: Dictionary) -> void:
	close_leaders()
	command.emit({"verb": "query", "what": "system", "star": star, "_reply_to": "refresh_system"})


func refresh_system(result: Dictionary) -> void:
	if result.get("ok", false) and visible:
		show_system(result, star_name.text, star)


func close() -> void:
	close_leaders()
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
	var colony := _player_colony(planet_id)
	if colony >= 0:
		close_leaders() # the colony pane takes the flyout's place
	colony_chosen.emit(colony)


## The selected planet's row in a glow of the theme's "on" colour (owner): a wrapper whose own
## style is only the glow, drawn round the row.
func _glowing(row: Control, planet_id: int) -> Control:
	var wrapper := PanelContainer.new()
	wrapper.set_meta("planet", planet_id)
	wrapper.mouse_filter = Control.MOUSE_FILTER_PASS
	var style := StyleBoxFlat.new()
	style.draw_center = false
	style.bg_color = Color(0, 0, 0, 0)
	# The themes' amber: a list item's pressed colour where a theme sets one (Glass and Chrome
	# leave the button's pale, owner 2026-11-19: no highlight on Glass), else the button's.
	var glow := get_theme_color("font_pressed_color", "ListItem") if has_theme_color("font_pressed_color", "ListItem") \
		else get_theme_color("font_pressed_color", "Button")
	style.shadow_color = Color(glow, 0.7)
	style.border_color = Color(glow, 0.8) # and a thin ring, for themes whose rows are see-through
	style.set_border_width_all(1)
	style.shadow_size = 11
	style.set_corner_radius_all(6) # (the list has a margin all round, so the glow isn't clipped)
	wrapper.add_theme_stylebox_override("panel", style)
	wrapper.add_child(row)
	return wrapper


## A planet of the system hovered on the map (owner, 2026-11-13): its row lights up, -1 for none.
var hovered_planet := -1


func hover_planet(planet_id: int) -> void:
	hovered_planet = planet_id
	for row: Control in planets_box.get_children():
		if row.is_queued_for_deletion():
			continue
		row.modulate = Color(1.3, 1.3, 1.25) if planet_id >= 0 and row.get_meta("planet", -1) == planet_id else Color.WHITE
	if planet_id >= 0:
		var rect := planet_row_rect(planet_id)
		if rect.size == Vector2.ZERO:
			for row: Control in planets_box.get_children():
				if row.get_meta("planet", -1) == planet_id:
					$Box/Scroll.ensure_control_visible(row)


## The screen rect of a planet's row, clipped to the list's visible part (for markers by a row,
## WP3.4), or an empty rect when the row is scrolled out of sight.
func planet_row_rect(planet_id: int) -> Rect2:
	var scroll: Control = $Box/Scroll
	var shown := scroll.get_global_rect()
	for row: Control in planets_box.get_children():
		if row.get_meta("planet", -1) == planet_id and not row.is_queued_for_deletion():
			var r := row.get_global_rect()
			var mid := r.get_center().y
			if mid < shown.position.y or mid > shown.end.y:
				return Rect2()
			return r
	return Rect2()


## The colony id of a planet that is one of the player's colonies (not an outpost), or -1.
## The open colony's planet, selected and glowing without opening anything (owner, 2026-11-19: a
## colony opened from the to-do list or a landing left its row unmarked).
func mark_colony(colony_id: int) -> void:
	for planet: Dictionary in system.get("planets", []):
		var colony: Dictionary = planet.get("colony", {})
		if colony.has("id") and int(colony.id) == colony_id:
			if selected_planet != int(planet.planet):
				selected_planet = int(planet.planet)
				_expanded[selected_planet] = true
				_ask_potential(selected_planet)
				_show_planets()
			return


func _player_colony(planet_id: int) -> int:
	for planet: Dictionary in system.get("planets", []):
		if int(planet.planet) == planet_id:
			var colony: Dictionary = planet.get("colony", {})
			if colony.get("mine", false) and not colony.get("outpost", false):
				return int(colony.id)
	return -1


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


## A right click's help beside `at` (info_popup.gd: one at a time, a left click closes it).
func _show_help(title: String, text: String, at: Control) -> void:
	preload("info_popup.gd").open(self, at, title, text)


var _help_at: Control


func _help_on_right_click(control: Control, topic: int) -> void:
	control.mouse_filter = Control.MOUSE_FILTER_STOP
	control.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_RIGHT:
			_help_at = control
			command.emit({"verb": "query", "what": "help", "topic": topic, "_reply_to": "help_record"})
			control.accept_event())


## A help record's answer (the `help` query), beside what was right-clicked.
func help_record(result: Dictionary) -> void:
	if result.get("ok", false) and is_instance_valid(_help_at) and visible:
		# The help file's markup: "Cost[n]:" (a race pick's cost) and "^" (a new line).
		var text := RegEx.create_from_string("^Cost\\[-?\\d+\\]:\\s*").sub(String(result.text).strip_edges(), "")
		text = text.replace("^", "\n").strip_edges()
		_show_help(String(result.title).strip_edges(), text, _help_at)


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
		governor_title.visible = true
		governor_name.text = "No governor"
		governor_title.text = "Assign a colony leader"
		return
	portrait.show_sprite("OFFICER.LBX", int(leader.portrait))
	governor_name.text = leader.name
	governor_title.text = leader.title
	# A leader from the pool takes 5 turns to reach any system but the home one (owner, 2026-11-14:
	# it looked instant); its bonuses start when it arrives.
	var eta := int(leader.get("eta", 0))
	if eta > 0:
		governor_title.text = "Arrives in %d turn%s" % [eta, "" if eta == 1 else "s"]
	governor_title.visible = governor_title.text != "" # an empty line left a gap (owner)
	# The bonuses (system v4): each skill's icon, name and bonus, as the leaders screen lists them.
	for child in governor_skills.get_children():
		child.queue_free()
	for skill: Dictionary in leader.get("skills", []):
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 4)
		# A right click shows the skill's help, as on the leaders screen (owner); a left click
		# still goes to the governor block (the leaders).
		line.mouse_filter = Control.MOUSE_FILTER_PASS
		var help_title := String(skill.name)
		var help_text := String(skill.get("description", ""))
		line.gui_input.connect(func(event: InputEvent):
			var click := event as InputEventMouseButton
			if click and click.pressed and click.button_index == MOUSE_BUTTON_RIGHT:
				_show_help(help_title, help_text, line)
				line.accept_event())
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
		var row := _planet_row(planet)
		var id := int(planet.planet)
		var shown := _glowing(row, id) if id == selected_planet else row
		if id == hovered_planet:
			shown.modulate = Color(1.3, 1.3, 1.25)
		planets_box.add_child(shown)


func _planet_row(planet: Dictionary) -> Control:
	var id := int(planet.planet)
	var panel := PanelContainer.new()
	panel.set_meta("planet", id)
	# The theme's row; the selected planet's is the open one (WP3.3g).
	panel.theme_type_variation = &"RowOpen" if id == selected_planet else &"Row"

	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	panel.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT:
			_on_planet_clicked(id)
		elif click and click.pressed and click.button_index == MOUSE_BUTTON_RIGHT:
			# The system window's right click: the planet's card, what colonising it gives as
			# icons (owner, 2026-11-13), beside the row.
			show_planet_card(id, panel.get_global_rect())
			panel.accept_event())
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
	if planet.has("special_name"):
		# The special found there (owner, 2026-11-13: gem deposits were missing), its text on
		# the tooltip and in the right click's card.
		var special := _label("◆ " + String(planet.special_name), 13)
		special.add_theme_color_override("font_color", preload("planet_card.gd").SPECIAL_COLOR)
		special.tooltip_text = String(planet.get("special_text", ""))
		special.mouse_filter = Control.MOUSE_FILTER_PASS
		names.add_child(special)
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
	if planet.has("colonize_fleet") or planet.has("outpost_fleet"):
		# A colony or outpost ship of the player's in orbit can settle it (owner, 2026-11-19: no way
		# to settle from the system view): COLONIZE asks the game's question, then the landing.
		var settle := HBoxContainer.new()
		settle.add_theme_constant_override("separation", 6)
		for order: String in ["colonize", "outpost"]:
			if not planet.has(order + "_fleet"):
				continue
			var b := Button.new()
			b.text = order.to_upper()
			b.focus_mode = Control.FOCUS_NONE
			b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			var fleet := int(planet[order + "_fleet"])
			b.pressed.connect(func(): _settle(order, fleet, id))
			settle.add_child(b)
		rows.add_child(settle)
	if _expanded.has(id) and planet.type != "gas_giant":
		rows.add_child(_details(planet, colony))
	return panel


func _settle(order: String, fleet: int, planet_id: int) -> void:
	var cmd := {"verb": order, "fleet": fleet, "planet": planet_id, "_reply_to": "_settled"}
	if order == "colonize":
		cmd["confirm"] = 1
	command.emit(cmd)


func _settled(_result: Dictionary) -> void:
	command.emit({"verb": "query", "what": "system", "star": star, "_reply_to": "refresh_system"})


## The expanded part: potential as icons and numbers, then the colony's morale and its button.
func _details(planet: Dictionary, colony: Dictionary) -> Control:
	var box := VBoxContainer.new()
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var potential: Dictionary = _potential.get(int(planet.planet), {})
	if planet.type == "asteroids":
		box.add_child(_label("Uninhabitable: only an outpost can be built here.", 13))
	if not potential.is_empty() and planet.type == "planet":
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
		# A right click on the government or the masks shows its help, as the colony screen and the
		# race statistics do (owner, 2026-11-19); the row's own right click (the planet card) is
		# for the rest of it. Help records: government / 2 + 474 (Race_Statistics_Specials_Help),
		# morale 490 (colony_screen_help_list's morale box).
		var government := _icon("COLONY2.LBX", int(colony.government_sprite), Vector2(22, 22), "Government")
		_help_on_right_click(government, int(colony.government) / 2 + 474)
		morale.add_child(government)
		morale.add_child(_gap())
		var masks := absi(int(colony.morale_icons))
		# The masks overlap, as the colony screen stacks its icons (owner).
		var stack := HBoxContainer.new()
		stack.mouse_filter = Control.MOUSE_FILTER_IGNORE
		stack.add_theme_constant_override("separation", -6)
		morale.add_child(stack)
		for i in mini(masks, 10):
			var mask := _icon("COLONY2.LBX", int(colony.morale_sprite), Vector2(18, 18), "Morale")
			_help_on_right_click(mask, 490)
			stack.add_child(mask)
		box.add_child(morale)
	return box


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
		if int(planet.planet) == planet_id and planet.type == "planet" and not _potential.has(planet_id):
			command.emit({"verb": "query", "what": "potential_colony", "planet": planet_id, "_reply_to": "show_potential"})


## The `potential_colony` answer (main.gd routes it here).
func show_potential(result: Dictionary) -> void:
	if not result.get("ok", false) or system.is_empty():
		return
	_potential[int(result.planet)] = result
	_show_planets()
	if _card_pending == int(result.planet):
		_card_pending = -1
		_open_card(int(result.planet))


## A planet's card (planet_card.gd) beside `at` (a row, or the point clicked on the map): its
## picture and words, its special, and what colonising it gives; asks the potential first.
var _card_pending := -1
var _card_rect := Rect2()


func show_planet_card(planet_id: int, at: Rect2) -> void:
	_card_rect = at
	var planet := _planet_entry(planet_id)
	if planet.is_empty():
		return
	if planet.get("type", "") != "planet" or _potential.has(planet_id):
		_open_card(planet_id)
		return
	_card_pending = planet_id
	_ask_potential(planet_id)


func _open_card(planet_id: int) -> void:
	var card := preload("planet_card.gd").build(art, _planet_entry(planet_id), _potential.get(planet_id, {}), race_id)
	preload("info_popup.gd").open_content(self, _card_rect, "", card)


func _planet_entry(planet_id: int) -> Dictionary:
	for planet: Dictionary in system.get("planets", []):
		if int(planet.planet) == planet_id:
			return planet
	return {}


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
		preload("../themes.gd").move_x(self, hidden_x)
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_method(func(x: float): preload("../themes.gd").move_x(self, x), position.x, _shown_left if opening else hidden_x, SLIDE_TIME)
	if not opening:
		_tween.tween_callback(hide)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	var colony_pane := get_parent().get_node_or_null("ColonyPane")
	if colony_pane != null and colony_pane.visible:
		return # Esc closes the colony pane first
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
