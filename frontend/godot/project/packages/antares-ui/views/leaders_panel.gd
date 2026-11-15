extends "side_panel.gd"
## Leaders (WP3.7, the owner's empire panels in notes/antares/ui-concepts.md): the legacy Leaders
## screen as a panel that flies out from the right, against the side tabs. Its top shows where the
## cycler (◀ place ▶ at the bottom) is: on Colony Leaders the colonised system (system_view.gd:
## its planets on their orbits, a hovered one's card in the corner), on Ship Officers the fleet's
## warships to pick one. Below, the tab's leaders as portrait cards (the `leaders` query: skills,
## where each serves, For Hire (turns) and the price). A click on a card picks the leader; HIRE,
## ASSIGN (to the shown system, or the picked warship), POOL and DISMISS act on it with the game's
## own questions. A click on a star moves the cycler to that system (the camera stays: owner).

signal command(dict: Dictionary)
## The panel's width changed: the galaxy fits the space left of it again.
signal width_changed
## The star the galaxy marks (the shown system), or -1.
signal star_marked(star: int)

const Themes := preload("../themes.gd")
const WIDTH := 430.0
const VIEW_HEIGHT := 200.0
const SHIP_TILE := Vector2(72, 56)

var art: Node
var stars: Array = [] # the galaxy's stars (sprites for the system view)
var tab := "colony"
var places: Array = [] # the `leaders` query's places (stars or fleets)
var place := 0 # the cycler's place
var leaders: Array = []
var picked := -1 # the picked leader
var picked_ship := -1 # the picked warship (Ship Officers)

var _view_holder: Control
var _system_view: Control
var _ships: HFlowContainer
var _cards: VBoxContainer
var _place_label: Label
var _colony_tab: Button
var _ship_tab: Button
var _hire: Button
var _assign: Button
var _pool: Button
var _dismiss: Button


func _ready() -> void:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	box.add_child(header)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "Leaders"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var close_button := _button("X", "Close (Esc)")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(close)
	header.add_child(close_button)
	# The top: the system, or the fleet's warships.
	_view_holder = PanelContainer.new()
	_view_holder.theme_type_variation = &"Row"
	_view_holder.custom_minimum_size.y = VIEW_HEIGHT
	box.add_child(_view_holder)
	_system_view = preload("system_view.gd").new()
	_system_view.art = art
	_view_holder.add_child(_system_view)
	_ships = HFlowContainer.new()
	_ships.add_theme_constant_override("h_separation", 4)
	_ships.add_theme_constant_override("v_separation", 4)
	_view_holder.add_child(_ships)
	var tabs := HBoxContainer.new()
	tabs.add_theme_constant_override("separation", 6)
	box.add_child(tabs)
	_colony_tab = _toggle("COLONY LEADERS", "Governors of your systems")
	_colony_tab.pressed.connect(func(): _set_tab("colony"))
	tabs.add_child(_colony_tab)
	_ship_tab = _toggle("SHIP OFFICERS", "Captains of your warships")
	_ship_tab.pressed.connect(func(): _set_tab("ship"))
	tabs.add_child(_ship_tab)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(scroll)
	_cards = VBoxContainer.new()
	_cards.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cards.add_theme_constant_override("separation", 4)
	scroll.add_child(_cards)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 6)
	box.add_child(buttons)
	_hire = _button("HIRE", "Hire the picked leader")
	_hire.pressed.connect(func(): _act("hire"))
	_assign = _button("ASSIGN", "Send the picked leader to the system or warship shown above")
	_assign.pressed.connect(func(): _act("assign"))
	_pool = _button("POOL", "Take the picked leader off its post, into the pool")
	_pool.pressed.connect(func(): _act("pool"))
	_dismiss = _button("DISMISS", "Let the picked leader go")
	_dismiss.pressed.connect(func(): _act("dismiss"))
	for b: Button in [_hire, _assign, _pool, _dismiss]:
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		buttons.add_child(b)
	var cycler := HBoxContainer.new()
	box.add_child(cycler)
	var previous := _button("◀", "The previous system or fleet")
	previous.pressed.connect(func(): _cycle(-1))
	cycler.add_child(previous)
	_place_label = Label.new()
	_place_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_place_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_place_label.clip_text = true
	cycler.add_child(_place_label)
	var next := _button("▶", "The next system or fleet")
	next.pressed.connect(func(): _cycle(1))
	cycler.add_child(next)
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				position.x = _rect.end.x - size.x
			width_changed.emit())


func open_panel(area: Rect2, at_star := -1) -> void:
	var width := _wanted_width(area)
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))
	_set_tab(tab, at_star)


func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	return minf(maxf(WIDTH, get_combined_minimum_size().x), area.size.x - 16.0)


func panel_width() -> float:
	return _rect.end.x - rest_left()


func refresh() -> void:
	command.emit({"verb": "query", "what": "leaders", "type": tab, "_reply_to": "show_leaders"})


func _set_tab(kind: String, at_star := -1) -> void:
	tab = kind
	_colony_tab.set_pressed_no_signal(kind == "colony")
	_ship_tab.set_pressed_no_signal(kind == "ship")
	Themes.glow(_colony_tab)
	Themes.glow(_ship_tab)
	picked = -1
	picked_ship = -1
	set_meta("at_star", at_star)
	refresh()


func show_leaders(result: Dictionary) -> void:
	if not result.get("ok", false) or String(result.type) != tab:
		return
	leaders = result.leaders
	var old_star := int(places[place].get("star", -1)) if place < places.size() else -1
	places = result.places
	var wanted := int(get_meta("at_star", -1))
	set_meta("at_star", -1)
	var target := wanted if wanted >= 0 else old_star
	place = 0
	for i in places.size():
		if int(places[i].get("star", -1)) == target:
			place = i
	_show_cards()
	_show_place()


## A star clicked on the galaxy: the cycler goes to that system (or that fleet's).
func select_star(star: int) -> bool:
	for i in places.size():
		if int(places[i].get("star", -1)) == star:
			place = i
			_show_place()
			return true
	return false


func close() -> void:
	if visible and not _closing:
		star_marked.emit(-1)
	super.close()


# ---- The place (top and cycler) ----

func _cycle(step: int) -> void:
	if places.is_empty():
		return
	place = posmod(place + step, places.size())
	picked_ship = -1
	_show_place()


func _show_place() -> void:
	_system_view.visible = tab == "colony"
	_ships.visible = tab == "ship"
	_clear(_ships)
	if places.is_empty():
		_place_label.text = "No colonies" if tab == "colony" else "No warships"
		_system_view.show_system({}, {})
		star_marked.emit(-1)
		_update_buttons()
		return
	var p: Dictionary = places[place]
	_place_label.text = String(p.name)
	var star := int(p.get("star", -1))
	star_marked.emit(star)
	if tab == "colony" and star >= 0:
		var star_entry: Dictionary = stars[star] if star < stars.size() else {}
		command.emit({"verb": "query", "what": "system", "star": star, "_reply_to": "_on_system"})
		set_meta("star_entry", star_entry)
	elif tab == "ship":
		for ship: Dictionary in p.get("ships", []):
			var tile := Button.new()
			tile.toggle_mode = true
			tile.focus_mode = Control.FOCUS_NONE
			tile.custom_minimum_size = SHIP_TILE
			tile.expand_icon = true
			tile.icon_alignment = HORIZONTAL_ALIGNMENT_CENTER
			tile.tooltip_text = String(ship.name) + ("" if int(ship.officer) < 0 else "\n(has an officer)")
			if art != null:
				tile.icon = art.texture("SHIPS.LBX", int(ship.picture))
			for state in ["normal", "disabled", "focus"]:
				tile.add_theme_stylebox_override(state, preload("fleets_panel.gd")._tile_style(false, false))
			tile.add_theme_stylebox_override("hover", preload("fleets_panel.gd")._tile_style(false, true))
			tile.add_theme_stylebox_override("pressed", preload("fleets_panel.gd")._tile_style(true, false))
			tile.add_theme_stylebox_override("hover_pressed", preload("fleets_panel.gd")._tile_style(true, true))
			var id := int(ship.id)
			tile.set_pressed_no_signal(id == picked_ship)
			tile.toggled.connect(func(on: bool):
				picked_ship = id if on else -1
				for other: Button in _ships.get_children():
					if other != tile:
						other.set_pressed_no_signal(false)
				_update_buttons())
			_ships.add_child(tile)
	_update_buttons()


func _on_system(result: Dictionary) -> void:
	if result.get("ok", false) and tab == "colony":
		_system_view.art = art
		_system_view.show_system(result, get_meta("star_entry", {}))


# ---- The cards ----

func _show_cards() -> void:
	_clear(_cards)
	for leader: Dictionary in leaders:
		_cards.add_child(_card(leader))
	if leaders.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "No leaders"
		_cards.add_child(none)
	_update_buttons()


func _card(leader: Dictionary) -> Control:
	var id := int(leader.leader)
	var card: Dictionary = leader.card
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"RowOpen" if id == picked else &"Row"
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	panel.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			picked = -1 if picked == id else id
			_show_cards())
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(row)
	var left := VBoxContainer.new()
	left.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(left)
	var portrait: TextureRect = preload("sprite_rect.gd").new()
	portrait.custom_minimum_size = Vector2(72, 72)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	portrait.bind(art)
	portrait.show_sprite("OFFICER.LBX", int(card.get("portrait", -1)))
	left.add_child(portrait)
	var state := Label.new()
	state.text = String(leader.get("state", ""))
	state.add_theme_font_size_override("font_size", 12)
	state.custom_minimum_size.x = 72
	state.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	state.clip_text = true
	if leader.get("for_hire", false):
		state.add_theme_color_override("font_color", Color(1.0, 0.4, 0.35))
	left.add_child(state)
	var info := VBoxContainer.new()
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info.add_theme_constant_override("separation", 0)
	info.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(info)
	var top := HBoxContainer.new()
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	info.add_child(top)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = "%s %s" % [String(card.get("title", "")), String(card.get("name", ""))]
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	name_label.clip_text = true
	if id == picked:
		name_label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
	top.add_child(name_label)
	if leader.get("for_hire", false):
		var cost := Label.new()
		cost.theme_type_variation = &"Dim"
		cost.text = "%d BC to hire" % int(leader.cost)
		cost.add_theme_font_size_override("font_size", 12)
		top.add_child(cost)
	for skill: Dictionary in card.get("skills", []):
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 6)
		line.mouse_filter = Control.MOUSE_FILTER_PASS
		line.tooltip_text = String(skill.get("description", ""))
		var icon: TextureRect = preload("sprite_rect.gd").new()
		icon.custom_minimum_size = Vector2(16, 16)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		icon.bind(art)
		icon.show_sprite("OFFICER.LBX", int(skill.icon))
		line.add_child(icon)
		var skill_name := Label.new()
		skill_name.text = String(skill.name)
		skill_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		skill_name.add_theme_font_size_override("font_size", 13)
		line.add_child(skill_name)
		var bonus := Label.new()
		bonus.theme_type_variation = &"Name"
		bonus.text = String(skill.bonus).strip_edges()
		bonus.add_theme_font_size_override("font_size", 13)
		line.add_child(bonus)
		info.add_child(line)
	return panel


func _leader(id: int) -> Dictionary:
	for leader: Dictionary in leaders:
		if int(leader.leader) == id:
			return leader
	return {}


func _update_buttons() -> void:
	var l := _leader(picked)
	var for_hire: bool = l.get("for_hire", false)
	_hire.disabled = l.is_empty() or not for_hire
	_assign.disabled = l.is_empty() or for_hire or places.is_empty() or (tab == "ship" and picked_ship < 0)
	_pool.disabled = l.is_empty() or int(l.get("status", -1)) != 1
	_dismiss.disabled = l.is_empty() or for_hire


func _act(what: String) -> void:
	var l := _leader(picked)
	if l.is_empty():
		return
	var star := int(places[place].get("star", -1)) if place < places.size() else -1
	match what:
		"hire":
			var cmd := {"verb": "hire_leader", "leader": picked, "_reply_to": "_after_command"}
			if tab == "colony" and star >= 0:
				cmd["star"] = star
			command.emit(cmd)
		"assign":
			if tab == "colony":
				command.emit({"verb": "assign_governor", "leader": picked, "star": star, "_reply_to": "_after_command"})
			elif picked_ship >= 0:
				command.emit({"verb": "assign_officer", "leader": picked, "ship": picked_ship, "_reply_to": "_after_command"})
		"pool":
			command.emit({"verb": "leader_pool", "leader": picked, "_reply_to": "_after_command"})
		"dismiss":
			command.emit({"verb": "leader_dismiss", "leader": picked, "_reply_to": "_after_command"})


func _after_command(_result: Dictionary) -> void:
	refresh()


# ---- Helpers ----

func _button(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size.y = 32
	return b


func _toggle(text: String, tip: String) -> Button:
	var b := _button(text, tip)
	b.toggle_mode = true
	b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	Themes.glowing(b)
	return b


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()
