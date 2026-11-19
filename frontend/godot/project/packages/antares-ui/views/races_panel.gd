extends "side_panel.gd"
## Race Relations (WP3.7, the owner's mockup "Native Races.png"): the legacy Races screen as a
## panel that flies out from the right, against the side tabs. The galaxy is not moved (owner).
## The other empires (up to 7) as one scrolling column of slots: the ruler's portrait in the
## empire's colour, the relations bar and word, the treaties (or No contact), and the player's
## spies against that empire as the screen's spy icons with ESPIONAGE / SABOTAGE / HIDE (their
## mission). Spies move as on the screen and as Colonies moves colonists (owner): a click on an icon
## picks it and the ones right of it up onto the pointer, a click on another empire's icons or the
## spies at home puts them down (Esc or a right click puts them back). Eliminated empires keep their slot:
## the portrait under the red Eliminated sign (races.lbx 31), No contact. A click on a slot picks the
## empire; the fixed bottom has BONUSES (spy and agent), the spies at home, and AUDIENCE (the
## native diplomacy window), REPORT (the original Races screen for now), DECLARE WAR (carried out
## next turn, the game's question) and IGNORE.

signal command(dict: Dictionary)
signal width_changed

const Themes := preload("../themes.gd")
const WIDTH := 430.0
const MISSIONS := ["ESPIONAGE", "SABOTAGE", "HIDE"]
const ELIMINATED_SIGN := 31 # races.lbx: the red Eliminated sign over a portrait
const HOME := -1 # a spy strip's owner: the spies at home

var art: Node
var races: Array = []
var result := {}
var picked := -1

var _slots: VBoxContainer
var _bonuses: Label
var _agents: Label
var _home_strip: Control
var _strips: Array[Control] = [] # every spy strip shown (the drop targets)
var _carry := {} # {from, n, view}: spies on the pointer
var _buttons := {}


func _ready() -> void:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	box.add_child(header)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "Race Relations"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var close_button := _button("X", "Close (Esc)")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(close)
	header.add_child(close_button)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(scroll)
	_slots = VBoxContainer.new()
	_slots.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_slots.add_theme_constant_override("separation", 6)
	scroll.add_child(_slots)
	var fixed := PanelContainer.new()
	fixed.theme_type_variation = &"Row"
	box.add_child(fixed)
	var bottom := VBoxContainer.new()
	bottom.add_theme_constant_override("separation", 4)
	fixed.add_child(bottom)
	_bonuses = Label.new()
	_bonuses.theme_type_variation = &"Name"
	bottom.add_child(_bonuses)
	var home := HBoxContainer.new()
	home.add_theme_constant_override("separation", 8)
	bottom.add_child(home)
	_agents = Label.new()
	_agents.theme_type_variation = &"Dim"
	home.add_child(_agents)
	_home_strip = _SpyStrip.new()
	_home_strip.owner_player = HOME
	_home_strip.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_home_strip.grabbed.connect(_pick_up)
	home.add_child(_home_strip)
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 6)
	grid.add_theme_constant_override("v_separation", 6)
	box.add_child(grid)
	for pair in [["AUDIENCE", "Talk with the picked empire's ambassador"], ["REPORT", "The picked empire's report (the original Races screen for now)"],
			["DECLARE WAR", "Declare war on the picked empire (carried out next turn)"], ["IGNORE", "Ignore the picked empire's messages (again: listen)"]]:
		var b := _button(pair[0], pair[1])
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var key := String(pair[0])
		b.pressed.connect(func(): _act(key))
		grid.add_child(b)
		_buttons[key] = b
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				position.x = _rect.end.x - size.x
			width_changed.emit())


func open_panel(area: Rect2) -> void:
	var width := _wanted_width(area)
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))
	refresh()


func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	return minf(maxf(WIDTH, get_combined_minimum_size().x), area.size.x - 16.0)


func panel_width() -> float:
	return _rect.end.x - rest_left()


## The galaxy isn't fitted for this panel (owner): no star clicks to answer.
func select_star(_star: int) -> bool:
	return false


func refresh() -> void:
	command.emit({"verb": "query", "what": "races", "_reply_to": "show_races"})


func show_races(answer: Dictionary) -> void:
	if not answer.get("ok", false):
		return
	result = answer
	races = answer.races
	if picked < 0:
		for r: Dictionary in races:
			if r.contact and not r.eliminated:
				picked = int(r.player)
				break
	_bonuses.text = "BONUSES   %s %d%%   %s %d%%" % [String(answer.spy_bonus_label).strip_edges().trim_suffix(":"), int(answer.spy_bonus),
		String(answer.agent_bonus_label).strip_edges().trim_suffix(":"), int(answer.agent_bonus)]
	_agents.text = "Spies at home: %d" % int(answer.agents)
	_home_strip.setup(art, int(answer.get("spy_icon", -1)), int(answer.agents))
	_home_strip.held = int(_carry.n) if not _carry.is_empty() and int(_carry.from) == HOME else 0
	_clear(_slots)
	_strips = [_home_strip]
	# From the top down (owner, 2026-11-19: met empires sat below empty No-contact rows, in player
	# order): the empires met, then the eliminated, then those not met, each in player order.
	var rank := func(r: Dictionary) -> int:
		return 2 if not r.contact else (1 if r.eliminated else 0)
	var ordered: Array = races.duplicate()
	ordered.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return rank.call(a) < rank.call(b) or (rank.call(a) == rank.call(b) and int(a.player) < int(b.player)))
	for r: Dictionary in ordered:
		_slots.add_child(_slot(r))
	_update_buttons()


func _slot(r: Dictionary) -> Control:
	var p := int(r.player)
	var met: bool = r.contact and not r.eliminated
	var eliminated: bool = r.eliminated
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"RowOpen" if p == picked else &"Row"
	panel.mouse_filter = Control.MOUSE_FILTER_STOP
	panel.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT and met:
			picked = p
			show_races(result))
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(row)
	# The ruler's portrait in the empire's colour.
	var colors: Array = preload("../galaxy.gd").PLAYER_COLORS
	var frame := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0, 0, 0, 0.6)
	style.set_border_width_all(3)
	style.border_color = colors[int(r.color) % 8] if met else Color(0.3, 0.3, 0.3)
	frame.add_theme_stylebox_override("panel", style)
	frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(frame)
	var portrait: TextureRect = preload("sprite_rect.gd").new()
	portrait.custom_minimum_size = Vector2(84, 84)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.mouse_filter = Control.MOUSE_FILTER_IGNORE
	portrait.bind(art)
	if met or eliminated:
		portrait.show_sprite("RACES.LBX", int(r.portrait))
	frame.add_child(portrait)
	if eliminated:
		var sign_rect: TextureRect = preload("sprite_rect.gd").new()
		sign_rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		sign_rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		sign_rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
		sign_rect.bind(art)
		sign_rect.show_sprite("RACES.LBX", ELIMINATED_SIGN)
		frame.add_child(sign_rect)
	# The relations bar: red (hate) to green (harmony), the mark at the relation.
	var bar := _RelationBar.new()
	bar.value = float(int(r.relations) + 100) / 200.0
	bar.shown = met
	bar.custom_minimum_size = Vector2(10, 84)
	bar.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(bar)
	var info := VBoxContainer.new()
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info.add_theme_constant_override("separation", 2)
	info.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(info)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.clip_text = true
	var no_contact := String(result.get("no_contact", "No contact")).replace("^", "").strip_edges()
	name_label.text = ("%s (%s)" % [String(r.ruler), String(r.race)]) if met or eliminated else no_contact
	if p == picked:
		name_label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
	info.add_child(name_label)
	if eliminated:
		var gone := Label.new()
		gone.theme_type_variation = &"Dim"
		gone.text = no_contact
		gone.add_theme_font_size_override("font_size", 13)
		info.add_child(gone)
	if not met:
		return panel
	var treaty := Label.new()
	treaty.theme_type_variation = &"Dim"
	treaty.text = String(r.treaty) + "   ·   " + String(r.relation).strip_edges()
	if r.get("ignoring", false):
		treaty.text += "   ·   " + String(result.get("ignoring_label", "Ignoring")).strip_edges()
	if r.get("war_declared", false) and not r.get("at_war", false):
		treaty.text += "   ·   War next turn"
	treaty.add_theme_font_size_override("font_size", 13)
	treaty.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_child(treaty)
	# Spies against this empire: their icons, the mission switch.
	var spies := HBoxContainer.new()
	spies.add_theme_constant_override("separation", 4)
	info.add_child(spies)
	var strip := _SpyStrip.new()
	strip.owner_player = p
	strip.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	strip.setup(art, int(result.get("spy_icon", -1)), int(r.spies))
	strip.held = int(_carry.n) if not _carry.is_empty() and int(_carry.from) == p else 0
	strip.tooltip_text = "Your spies on them: %d (theirs on you: %d)\nClick spies to pick them up, then click another empire or home" % [int(r.spies), int(r.their_spies)]
	strip.grabbed.connect(_pick_up)
	_strips.append(strip)
	spies.add_child(strip)
	var missions := HBoxContainer.new()
	missions.add_theme_constant_override("separation", 2)
	info.add_child(missions)
	for m in MISSIONS.size():
		var b := _button(MISSIONS[m], "Their mission")
		b.toggle_mode = true
		b.add_theme_font_size_override("font_size", 11)
		b.custom_minimum_size.y = 24
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.set_pressed_no_signal(int(r.mission) == m)
		Themes.glow(b)
		var mission := m
		b.pressed.connect(func(): command.emit({"verb": "set_spies", "player": p, "mission": mission, "_reply_to": "_after_command"}))
		missions.add_child(b)
	return panel


func _race(p: int) -> Dictionary:
	for r: Dictionary in races:
		if int(r.player) == p:
			return r
	return {}


func _update_buttons() -> void:
	var r := _race(picked)
	var met: bool = not r.is_empty() and r.contact and not r.eliminated
	for key: String in _buttons:
		(_buttons[key] as Button).disabled = not met
	if met:
		(_buttons["DECLARE WAR"] as Button).disabled = r.get("at_war", false) or r.get("war_declared", false)
		(_buttons["IGNORE"] as Button).text = "LISTEN" if r.get("ignoring", false) else "IGNORE"


func _act(key: String) -> void:
	if picked < 0:
		return
	match key:
		"AUDIENCE":
			command.emit({"verb": "audience", "player": picked, "_reply_to": "_after_command"})
		"REPORT":
			command.emit({"verb": "open", "screen": 6}) # the original Races screen (its REPORT)
		"DECLARE WAR":
			command.emit({"verb": "declare_war", "player": picked, "_reply_to": "_after_command"})
		"IGNORE":
			command.emit({"verb": "toggle_ignore", "player": picked, "_reply_to": "_after_command"})


func _after_command(_answer: Dictionary) -> void:
	refresh()


## Spies dragged from one strip to another: back home first, then out to the new empire (the
## screen's commit, Update_Spy_Stuff, does the same through the agents' count).
func _move_spies(from: int, to: int, n: int) -> void:
	if from == to or n <= 0:
		return
	if from != HOME:
		var src := _race(from)
		var home_cmd := {"verb": "set_spies", "player": from, "count": int(src.get("spies", 0)) - n}
		if to == HOME:
			home_cmd["_reply_to"] = "_after_command"
		command.emit(home_cmd)
	if to != HOME:
		var dst := _race(to)
		command.emit({"verb": "set_spies", "player": to, "count": int(dst.get("spies", 0)) + n, "_reply_to": "_after_command"})


# ---- Spies on the pointer ----

func _pick_up(from: int, n: int) -> void:
	if not _carry.is_empty() or n <= 0:
		return
	var view := HBoxContainer.new()
	view.top_level = true
	view.z_index = 100
	view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	view.add_theme_constant_override("separation", -14 if n > 4 else 0)
	for i in mini(n, 8):
		var icon: TextureRect = preload("sprite_rect.gd").new()
		icon.custom_minimum_size = Vector2(_SpyStrip.ICON, _SpyStrip.ICON)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		icon.mouse_filter = Control.MOUSE_FILTER_IGNORE
		icon.bind(art)
		icon.show_sprite("RACES.LBX", int(result.get("spy_icon", -1)))
		view.add_child(icon)
	if n > 1:
		var label := Label.new()
		label.text = str(n)
		label.add_theme_color_override("font_outline_color", Color.BLACK)
		label.add_theme_constant_override("outline_size", 4)
		view.add_child(label)
	add_child(view)
	_carry = {"from": from, "n": n, "view": view}
	for strip: Control in _strips:
		if strip.owner_player == from:
			strip.held = n
			strip.queue_redraw()
	Input.set_default_cursor_shape(Input.CURSOR_DRAG)
	_follow_pointer()


func _follow_pointer() -> void:
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.global_position = get_global_mouse_position() + Vector2(10, 6)
	var at := get_global_mouse_position()
	for strip: Control in _strips:
		if is_instance_valid(strip):
			strip.set_target(not _carry.is_empty() and strip.owner_player != int(_carry.from) \
				and strip.is_visible_in_tree() and strip.get_global_rect().has_point(at))


func _drop_carry() -> void:
	if _carry.is_empty():
		return
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.queue_free()
	_carry = {}
	for strip: Control in _strips:
		if is_instance_valid(strip):
			strip.held = 0
			strip.set_target(false)
			strip.queue_redraw()
	Input.set_default_cursor_shape(Input.CURSOR_ARROW)


func _input(event: InputEvent) -> void:
	if _carry.is_empty():
		return
	if event is InputEventMouseMotion:
		_follow_pointer()
		return
	var key := event as InputEventKey
	if key and key.pressed and key.keycode == KEY_ESCAPE:
		_drop_carry()
		get_viewport().set_input_as_handled()
		return
	var mb := event as InputEventMouseButton
	if mb == null or not mb.pressed or mb.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		return
	get_viewport().set_input_as_handled() # the click is the drop, nothing else
	var from := int(_carry.from)
	var n := int(_carry.n)
	var to := from
	if mb.button_index == MOUSE_BUTTON_LEFT:
		for strip: Control in _strips:
			if is_instance_valid(strip) and strip.is_visible_in_tree() and strip.get_global_rect().has_point(mb.global_position):
				to = strip.owner_player
	if to != from:
		_show_moved(from, to, n)
	_drop_carry()
	_move_spies(from, to, n)


## The strips show the move at once (owner: the stack flashed back home until the game's answer
## came); the refresh after the commands brings the game's own counts.
func _show_moved(from: int, to: int, n: int) -> void:
	var moved := n
	for strip: Control in _strips:
		if is_instance_valid(strip) and strip.owner_player == to and to != HOME:
			moved = mini(n, 63 - strip.count) # an empire holds 63 at most; the rest stay
	for strip: Control in _strips:
		if not is_instance_valid(strip):
			continue
		if strip.owner_player == from:
			strip.count -= moved
		elif strip.owner_player == to:
			strip.count += moved
		strip.queue_redraw()
	if from == HOME or to == HOME:
		_agents.text = "Spies at home: %d" % _home_strip.count


func _unhandled_key_input(event: InputEvent) -> void:
	if not _carry.is_empty():
		return
	super._unhandled_key_input(event)


func close() -> void:
	_drop_carry()
	super.close()


func _button(text: String, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size.y = 32
	return b


func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


## The relations bar (the screen's: red at the bottom to green at the top, the slider at the
## relation).
class _RelationBar extends Control:
	var value := 0.5
	var shown := true

	func _draw() -> void:
		var steps := 24
		for i in steps:
			var t := float(i) / float(steps - 1)
			var color := Color(1.0, 0.15, 0.1).lerp(Color(1.0, 0.85, 0.1), minf(t * 2.0, 1.0)) if t < 0.5 else \
				Color(1.0, 0.85, 0.1).lerp(Color(0.2, 0.9, 0.3), (t - 0.5) * 2.0)
			if not shown:
				color = Color(0.25, 0.25, 0.25)
			var h := size.y / float(steps)
			draw_rect(Rect2(0, size.y - (i + 1) * h, size.x, h + 0.5), color)
		if shown:
			var y := size.y * (1.0 - value)
			draw_rect(Rect2(-2, y - 2, size.x + 4, 4), Color.WHITE)


## A row of spy icons (the screen's icon group): drawn side by side, overlapping when there are
## many. A click on an icon picks it and every icon to its right up (Draw_Icon_Group_Cursor);
## the held ones are left out while they ride on the pointer.
class _SpyStrip extends Control:
	signal grabbed(from: int, n: int)
	const ICON := 26.0
	var owner_player := -1
	var count := 0
	var held := 0 # picked up, on the pointer
	var _texture: Texture2D
	var _art: Node
	var _icon := -1
	var _target := false

	func _init() -> void:
		custom_minimum_size = Vector2(ICON * 3.0, ICON + 4.0)
		mouse_filter = Control.MOUSE_FILTER_STOP
		mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND

	func setup(art: Node, icon: int, n: int) -> void:
		count = n
		_art = art
		_icon = icon
		if art != null and not art.loaded.is_connected(_on_loaded):
			art.loaded.connect(_on_loaded)
		if tooltip_text == "":
			tooltip_text = "Click spies to pick them up, then click an empire to send them"
		_on_loaded("")
		queue_redraw()

	## The icon once the art has it (pictures arrive in the background, as sprite_rect.gd's do).
	func _on_loaded(_key: String) -> void:
		if _texture == null and _art != null and _icon >= 0:
			_texture = _art.texture("RACES.LBX", _icon)
			queue_redraw()

	func set_target(on: bool) -> void:
		if on != _target:
			_target = on
			queue_redraw()

	func _spacing() -> float:
		if count <= 1:
			return ICON + 2.0
		return minf(ICON + 2.0, (size.x - ICON) / float(count - 1))

	func _draw() -> void:
		var box := Rect2(Vector2.ZERO, size)
		draw_rect(box, Color(0, 0, 0, 0.35))
		if _target:
			draw_rect(box, Color(1.0, 0.85, 0.3, 0.9), false, 2.0)
		var step := _spacing()
		for i in count - held:
			var at := Rect2(i * step, (size.y - ICON) * 0.5, ICON, ICON)
			if _texture != null:
				draw_texture_rect(_texture, at, false)
			else:
				draw_circle(at.get_center(), ICON * 0.4, Color(0.7, 0.8, 1.0))

	func _gui_input(event: InputEvent) -> void:
		var mb := event as InputEventMouseButton
		if mb == null or not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT or count - held <= 0:
			return
		if mb.position.x > (count - held - 1) * _spacing() + ICON:
			return # past the last icon
		var index := clampi(int(mb.position.x / _spacing()), 0, count - 1)
		accept_event()
		grabbed.emit(owner_player, count - index)
