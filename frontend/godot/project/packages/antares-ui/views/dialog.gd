extends PanelContainer
## Native answer to the game's dialogs (requests, WP3.1b and WP3.2b): "message" (with an optional
## title) and "warning" have OK, "confirm" has Yes and No, "text" (the game's input box: a star's
## name) has a line to type in with OK and Cancel. "popup" (WP3.4, owner 2026-11-13) is one of the
## game's own windows (its "popup" field names it): a leader's card when it has one, its text and
## its own buttons, answered with the button's index. The game thread waits until answered.

signal answered(id: int, value: int, text: String)

var request_id := 0


var _kind := ""
var popup_name := "" # a popup's name ("leader_level", ...), for the smoke
var _slider_value := 0
var art: Node # the game's art (set by the frontend), for portraits and icons
var _extra: VBoxContainer # a popup's own content (a leader's card) between title and text
var _popup_buttons: HBoxContainer
var _help: Control
## A popup's options (planets to pick, targets...) answer this plus their index (the game's
## convention, mainpups.cpp); its buttons answer their own index.
const OPTION_ANSWER := 1000
## The options shown, for answers from the map (a planet clicked in the zoomed system).
var options: Array = []
## The player's race (empire query), for the planet card's colonists.
var race_id := -1
## A zoomed popup's planets (its `system`), by id, and what colonising each gives (its options'
## or its own `potential`): the planet card under the choices (owner, 2026-11-13).
var _planets := {}
var _potentials := {}
var _card_holder: PanelContainer
var _card_planet := -1


func _ready() -> void:
	# Owner, 2026-11-14: a window must follow the game window's size (a combat target window left
	# off the screen soft-locked the game until the window was sized back).
	get_viewport().size_changed.connect(func():
		if visible:
			_centre.call_deferred())
	_extra = VBoxContainer.new()
	_extra.add_theme_constant_override("separation", 6)
	$Fit/Box.add_child(_extra)
	$Fit/Box.move_child(_extra, $Fit/Box/Title.get_index() + 1)
	_popup_buttons = HBoxContainer.new()
	_popup_buttons.add_theme_constant_override("separation", 8)
	_popup_buttons.alignment = BoxContainer.ALIGNMENT_CENTER
	$Fit/Box.add_child(_popup_buttons)
	$Fit/Box/Buttons/Yes.pressed.connect(func(): _answer(1))
	$Fit/Box/Buttons/No.pressed.connect(func(): _answer(0))
	$Fit/Box/Entry.text_submitted.connect(func(_t: String): _answer(1))


## A live window (the bombing, owner 2026-11-20: "live"): its buttons, all but the last, and its
## timed step ("step_ms", answering "step_answer") answer without closing it; the game sends the
## window again at once.
var stepping := false


func ask(id: int, kind: String, request: Dictionary) -> void:
	request_id = id
	var live: bool = request.get("live", false)
	if request.has("step_ms"):
		var step_value := int(request.get("step_answer", 0))
		get_tree().create_timer(float(request.step_ms) / 1000.0).timeout.connect(func():
			if request_id == id and visible:
				_step(step_value))
	# The bombing's next step (owner, 2026-11-20: rebuilt each bomb, the text and the buttons
	# swapped places and the buttons moved): the same window, its numbers and buttons updated.
	if live and kind == "popup" and String(request.get("popup", "")) == "colony_bombing" and visible and \
			popup_name in ["colony_bombing", "colony_combat"] and is_instance_valid(_bombing):
		popup_name = "colony_bombing"
		_kind = kind
		_update_bombing(request)
		return
	_apply_bare()
	$Fit/Box/Text.text = request.get("text", "")
	var title: String = request.get("title", "")
	if title == "":
		title = "Warning" if kind == "warning" else ("Confirm" if kind == "confirm" else "")
	$Fit/Box/Title.text = title
	$Fit/Box/Title.visible = $Fit/Box/Title.text != ""
	_kind = kind
	var entry: LineEdit = $Fit/Box/Entry
	entry.visible = kind == "text"
	$Fit/Box/Buttons/Yes.text = "YES" if kind == "confirm" else "OK"
	$Fit/Box/Buttons/No.text = "CANCEL" if kind == "text" else "NO"
	$Fit/Box/Buttons/No.visible = kind == "confirm" or kind == "text"
	if kind == "text":
		entry.max_length = int(request.get("max_chars", 15))
		entry.text = String(request.get("value", ""))
	options = []
	for child in _extra.get_children():
		_extra.remove_child(child)
		child.queue_free()
	for child in _popup_buttons.get_children():
		_popup_buttons.remove_child(child)
		child.queue_free()
	# "choose" (the game's pick-from-list window, fields::Get_List_Field) is laid out as a popup:
	# its items as options (answered with the item's index) and CANCEL (-1).
	var choose := kind == "choose"
	var popup := kind == "popup" or choose
	popup_name = String(request.get("popup", "")) if kind == "popup" else ("choose" if choose else "")
	$Fit/Box/Buttons.visible = not popup
	_popup_buttons.visible = popup
	if popup:
		if request.has("leader"):
			_extra.add_child(_leader_card(request.leader))
		if request.has("items"):
			_extra.add_child(_item_list(request.items))
		if request.has("special"):
			_extra.add_child(_special_box(request.special))
		_planets = {}
		_potentials = {}
		_card_planet = -1
		_card_holder = null
		for planet: Dictionary in request.get("system", {}).get("planets", []):
			_planets[int(planet.planet)] = planet
		_planet_rows.clear()
		_fleet_rows.clear()
		if popup_name == "new_system" and not _planets.is_empty():
			# The system's planets, the special's marked (owner, 2026-11-21: nothing said which
			# planet had the gem deposits, nor listed the planets), each lit with its planet.
			_extra.add_child(_system_planet_list(request.system.planets))
		for option: Dictionary in request.get("options", []):
			if option.has("potential") and option.has("planet"):
				_potentials[int(option.planet)] = option.potential
		if request.has("potential") and request.has("planet"):
			_potentials[int(request.planet)] = request.potential
		# The other sides' ships (the select-combat windows' `fleet`; owner, 2026-11-21: every enemy
		# fleet and colony on this side, the player's own on the other): under the side's target
		# row, or a block of their own.
		var enemies: Array = request.get("fleet", {}).get("enemies", [])
		var enemies_shown := {}
		var option_items: Array = request.get("options", [])
		if not enemies.is_empty() and request.has("options"):
			option_items = option_items.duplicate(true)
			for item: Dictionary in option_items:
				var side := _enemy_side(enemies, int(item.get("fleet_owner", -1)))
				if not side.is_empty():
					item["details"] = (item.get("details", []) as Array) + _enemy_lines(side)
					enemies_shown[int(side.player)] = true
		if request.has("options"):
			_extra.add_child(_option_list(option_items, 0 if choose else OPTION_ANSWER, int(request.get("selected", -1))))
		for side: Dictionary in enemies:
			if not enemies_shown.has(int(side.get("player", -1))):
				_extra.add_child(_enemy_fleet_block(side))
		if not _potentials.is_empty():
			# The planet card: the hovered planet's (a row, or the planet on the map), else the
			# first that can be picked, or the one asked about.
			_card_holder = PanelContainer.new()
			_card_holder.theme_type_variation = &"Pane"
			_extra.add_child(_card_holder)
			var first := int(request.get("planet", -1))
			if first < 0:
				for option: Dictionary in request.get("options", []):
					if bool(option.get("enabled", true)) and option.has("potential"):
						first = int(option.planet)
						break
			_show_card(first)
		if choose and bare_width > 0.0:
			request = request.merged({"buttons": []}, true) # Esc or a right click cancels
		elif choose:
			request = request.merged({"buttons": ["CANCEL"]}, true)
		if request.has("rows") and popup_name in ["colony_bombing", "colony_combat"]:
			_extra.add_child(_bombing_body(request))
			$Fit/Box/Text.text = ""
			request = request.merged({"buttons": []}, true) # its own, stacked on the right
		elif request.has("rows"):
			# The orbit's invade/bomb window (owner, 2026-11-13: pictures like the window's): only
			# the bombing's result stays as text under them.
			_extra.add_child(_combat_rows(request))
			$Fit/Box/Text.text = String(request.get("summary", ""))
		# A slider or a list to choose from goes under its question (owner, 2026-11-14: a research
		# treaty's ACCEPT / REJECT sat above "What say you?"); other content (a leader's card)
		# above the text.
		$Fit/Box.move_child(_extra, $Fit/Box/Text.get_index() + (1 if popup_name in ["slider", "choose"] else 0))
		if popup_name == "slider":
			_extra.add_child(_slider(int(request.get("min", 0)), int(request.get("max", 0)), int(request.get("value", 0))))
			request = request.merged({"buttons": ["OK", "CANCEL"]}, true)
		var buttons: Array = request.get("buttons", ["OK"])
		for i in buttons.size():
			var b := Button.new()
			b.text = String(buttons[i])
			b.focus_mode = Control.FOCUS_NONE
			b.custom_minimum_size = Vector2(140, 36)
			var value := i
			if popup_name == "slider":
				value = -1 if i == 1 else -2 # OK answers the slider's value
			elif choose:
				value = -1
			if live and i < buttons.size() - 1:
				b.pressed.connect(func(): _step(value))
			else:
				b.pressed.connect(func(): _answer(_slider_value if value == -2 else value))
			_popup_buttons.add_child(b)
	$Fit/Box/Text.visible = $Fit/Box/Text.text != "" or kind != "popup"
	show()
	if kind == "text":
		entry.grab_focus()
		entry.select_all()
	reset_size() # as tall as its text: OK as far from the bottom as the text from the top (owner)
	_centre.call_deferred()


## An answer that keeps the window up (a live window's step or button); once per request, so a
## step timer can't follow a button pressed first.
var _stepped_id := -1


func _step(value: int) -> void:
	if _stepped_id == request_id:
		return
	_stepped_id = request_id
	stepping = true
	answered.emit(request_id, value, "")
	stepping = false


func _answer(value: int) -> void:
	preload("info_popup.gd").close_current()
	hide()
	answered.emit(request_id, value, $Fit/Box/Entry.text if _kind == "text" else "")


## A leader as the leaders screen shows one: portrait, name, title and level, then each skill's
## icon, name and bonus; a right click on a skill gives its help.
func _leader_card(leader: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	var portrait: TextureRect = preload("sprite_rect.gd").new()
	portrait.custom_minimum_size = Vector2(96, 96)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	if art:
		portrait.bind(art)
		portrait.show_sprite("OFFICER.LBX", int(leader.get("portrait", -1)))
	row.add_child(portrait)
	var names := VBoxContainer.new()
	names.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	names.add_theme_constant_override("separation", 2)
	row.add_child(names)
	var title := Label.new()
	title.theme_type_variation = &"Dim"
	title.text = "%s · level %d" % [String(leader.get("title", "")), int(leader.get("level", 0)) + 1] if String(leader.get("title", "")) != "" else "Level %d" % (int(leader.get("level", 0)) + 1)
	names.add_child(title)
	for skill: Dictionary in leader.get("skills", []):
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 6)
		line.mouse_filter = Control.MOUSE_FILTER_PASS
		var icon: TextureRect = preload("sprite_rect.gd").new()
		icon.custom_minimum_size = Vector2(18, 18)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		if art:
			icon.bind(art)
			icon.show_sprite("OFFICER.LBX", int(skill.icon))
		line.add_child(icon)
		var skill_name := Label.new()
		skill_name.text = String(skill.name)
		skill_name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(skill_name)
		var bonus := Label.new()
		bonus.theme_type_variation = &"Name"
		bonus.text = String(skill.bonus).strip_edges()
		line.add_child(bonus)
		var help_title := String(skill.name)
		var help_text := String(skill.get("description", ""))
		line.gui_input.connect(func(event: InputEvent):
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
				_help = preload("info_popup.gd").open(self, line, help_title, help_text)
				line.accept_event())
		names.add_child(line)
	return row


## A popup's list (the turn summary's messages): one row each, serious ones in red; a click
## answers 100 + the item's "msg".
func _item_list(items: Array) -> Control:
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size = Vector2(560, minf(420.0, 34.0 * items.size() + 8.0))
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list.add_theme_constant_override("separation", 2)
	scroll.add_child(list)
	for item: Dictionary in items:
		var row := Button.new()
		row.flat = true
		row.theme_type_variation = &"ListItem"
		row.focus_mode = Control.FOCUS_NONE
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		row.text = String(item.get("text", ""))
		row.custom_minimum_size.y = 30
		if item.get("serious", false):
			for state in ["font_color", "font_hover_color", "font_pressed_color"]:
				row.add_theme_color_override(state, Color(1.0, 0.45, 0.35))
		var value := 100 + int(item.get("msg", 0))
		row.pressed.connect(func(): _answer(value))
		list.add_child(row)
	return scroll


## The invade/bomb window's body (MAINPUPS::Draw_Colony_Combat_Popup): the defenders' rows down
## the left (buildings, best shield, colonists, troops), the planet's picture in the middle, the
## attacker's marines and bombardment down the right; each row its icon and line.
func _combat_rows(request: Dictionary) -> Control:
	var body := HBoxContainer.new()
	body.add_theme_constant_override("separation", 16)
	var columns := {}
	for side in ["left", "right"]:
		var column := VBoxContainer.new()
		column.add_theme_constant_override("separation", 8)
		column.custom_minimum_size.x = 210
		column.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		columns[side] = column
	body.add_child(columns.left)
	var picture: Dictionary = request.get("picture", {})
	var planet: TextureRect = preload("sprite_rect.gd").new()
	planet.custom_minimum_size = Vector2(150, 150)
	planet.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	planet.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	planet.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	if art:
		planet.bind(art)
		planet.show_sprite(String(picture.get("lbx", "")), int(picture.get("entry", -1)))
	body.add_child(planet)
	body.add_child(columns.right)
	for r: Dictionary in request.get("rows", []):
		var right := String(r.get("side", "left")) == "right"
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 8)
		line.alignment = BoxContainer.ALIGNMENT_END if right else BoxContainer.ALIGNMENT_BEGIN
		var icon: TextureRect = preload("sprite_rect.gd").new()
		icon.custom_minimum_size = Vector2(30, 30)
		icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		if art:
			icon.bind(art)
			icon.show_sprite(String(r.lbx), int(r.entry))
		var label := Label.new()
		label.text = String(r.text).strip_edges()
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.custom_minimum_size.x = 160
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT if right else HORIZONTAL_ALIGNMENT_LEFT
		label.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		if right:
			line.add_child(label)
			line.add_child(icon)
		else:
			line.add_child(icon)
			line.add_child(label)
		columns[("right" if right else "left")].add_child(line)
	return body


## The orbit's window (owner, 2026-11-20: "a copy of the invade/bombard legacy window"): one
## window from the choice to the last bomb. The planet with its defenders and the result on the
## left, the attackers and the buttons stacked on the right: BOMB (STOP while bombs fall, BOMB
## again to go on), DROP ALL, INVADE, MIND CONTROL, DESTROY and IGNORE (CLOSE once bombing), each
## in its place throughout, greyed when not open. BOMB keeps the window for the bombing, which
## updates it in place (_update_bombing), each hit's explosion on the planet.
var _bombing: Control
var _bombing_rows: VBoxContainer
var _bombing_right: VBoxContainer
var _bombing_summary: Label
var _bombing_buttons: VBoxContainer
var _bombing_planet: TextureRect
var _bombing_picture := {}
var _blast_layer: Control
const PLANET_BOX := 220.0
const HUGE_PLANET_PX := 150.0 # CMBTPLNT's huge planets
var _blasts: Array = [] # {lbx, entry, dx, dy, born}
var _planet_width := 0.0
var _offered: Array = [] # the choice's buttons (the orbit's), kept for the bombing's layout
const BLAST_FPS := 15.0
var blast_count := 0 # explosions shown (the smoke)
signal released # a held window let go without an answer (BOMB with no bombing after it)
const BUTTON_ORDER := ["BOMB", "DROP ALL", "INVADE", "MIND CONTROL", "DESTROY", "IGNORE"]


func _bombing_body(request: Dictionary) -> Control:
	var body := HBoxContainer.new()
	body.add_theme_constant_override("separation", 24)
	_bombing = body
	_blasts.clear()
	var left := VBoxContainer.new()
	left.add_theme_constant_override("separation", 8)
	left.custom_minimum_size.x = 320
	body.add_child(left)
	_bombing_picture = request.get("picture", {})
	_bombing_planet = preload("sprite_rect.gd").new()
	_bombing_planet.custom_minimum_size = Vector2(PLANET_BOX, PLANET_BOX)
	_bombing_planet.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_bombing_planet.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_bombing_planet.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	_bombing_planet.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_bombing_planet.draw.connect(_fit_bombing_planet) # its art came (or changed): its size
	if art:
		_bombing_planet.bind(art)
		_bombing_planet.show_sprite(String(_bombing_picture.get("lbx", "")), int(_bombing_picture.get("entry", -1)))
	_fit_bombing_planet()
	# The planet at the original's scale (owner 2026-11-22: a tiny planet filled the box, and its
	# bombs' explosions were blown up with it): a huge planet (150 px) fills the box, the others
	# are as much smaller as in the original, in a box that keeps its size.
	var holder := CenterContainer.new()
	holder.custom_minimum_size = Vector2(PLANET_BOX, PLANET_BOX)
	holder.add_child(_bombing_planet)
	left.add_child(holder)
	_blast_layer = Control.new()
	_blast_layer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_blast_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	_blast_layer.draw.connect(_draw_blasts)
	_bombing_planet.add_child(_blast_layer)
	_bombing_rows = VBoxContainer.new()
	_bombing_rows.add_theme_constant_override("separation", 6)
	_bombing_rows.custom_minimum_size.y = 5 * 30 + 4 * 6 # up to five defenders' rows: the window keeps its size
	left.add_child(_bombing_rows)
	_bombing_summary = Label.new()
	_bombing_summary.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_bombing_summary.custom_minimum_size = Vector2(320, 44)
	_bombing_summary.add_theme_font_size_override("font_size", 14)
	left.add_child(_bombing_summary)
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 10)
	right.custom_minimum_size.x = 200
	body.add_child(right)
	var top_gap := Control.new()
	top_gap.custom_minimum_size.y = 40
	right.add_child(top_gap)
	_bombing_right = VBoxContainer.new()
	_bombing_right.add_theme_constant_override("separation", 6)
	_bombing_right.custom_minimum_size.y = 2 * 30 + 6 # the marines and the bombs
	right.add_child(_bombing_right)
	_bombing_buttons = VBoxContainer.new()
	_bombing_buttons.add_theme_constant_override("separation", 8)
	right.add_child(_bombing_buttons)
	if String(request.get("popup", "")) == "colony_combat":
		_offered = (request.get("buttons", []) as Array).duplicate()
	_update_bombing(request)
	return body


## Rows of icon and text, rebuilt when their count changes, else only their text.
func _fill_rows(box: VBoxContainer, rows: Array, right_side: bool) -> void:
	if box.get_child_count() != rows.size():
		for child in box.get_children():
			box.remove_child(child)
			child.queue_free()
		for r: Dictionary in rows:
			var line := HBoxContainer.new()
			line.add_theme_constant_override("separation", 8)
			line.alignment = BoxContainer.ALIGNMENT_END if right_side else BoxContainer.ALIGNMENT_BEGIN
			var icon: TextureRect = preload("sprite_rect.gd").new()
			icon.name = "Icon"
			icon.custom_minimum_size = Vector2(30, 30)
			icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			if art:
				icon.bind(art)
				icon.show_sprite(String(r.lbx), int(r.entry))
			var label := Label.new()
			label.name = "Text"
			label.size_flags_vertical = Control.SIZE_SHRINK_CENTER
			if right_side:
				line.add_child(label)
				line.add_child(icon)
			else:
				line.add_child(icon)
				line.add_child(label)
			box.add_child(line)
	for i in rows.size():
		(box.get_child(i).get_node("Text") as Label).text = String(rows[i].text).strip_edges()


func _update_bombing(request: Dictionary) -> void:
	var all_rows: Array = request.get("rows", [])
	_fill_rows(_bombing_rows, all_rows.filter(func(r: Dictionary) -> bool: return String(r.get("side", "left")) == "left"), false)
	_fill_rows(_bombing_right, all_rows.filter(func(r: Dictionary) -> bool: return String(r.get("side", "")) == "right"), true)
	_bombing_summary.text = String(request.get("summary", ""))
	# This step's explosions on the planet.
	_planet_width = float(request.get("planet_width", _planet_width))
	var now := Time.get_ticks_msec() / 1000.0
	for b: Dictionary in request.get("blasts", []):
		blast_count += 1
		_blasts.append(b.merged({"born": now}))
		if art:
			art.sprite(String(b.lbx), int(b.entry))
	# The buttons, each in its place: the engine's (with what each answers) by label; the others
	# of the window's set greyed.
	var bombing := String(request.get("popup", "")) == "colony_bombing"
	var labels: Array = request.get("buttons", [])
	var answer_of := {}
	for i in labels.size():
		answer_of[String(labels[i])] = i
	var shown: Array = []
	for name: String in BUTTON_ORDER:
		var live_name := name
		if name == "BOMB" and answer_of.has("STOP"):
			live_name = "STOP"
		if name == "IGNORE" and bombing:
			live_name = "CLOSE"
		var offered: bool = _offered.has(name) or answer_of.has(live_name) or (name == "DROP ALL" and _offered.has("BOMB")) \
				or name == "IGNORE"
		if offered:
			shown.append([live_name, int(answer_of.get(live_name, -1))])
	var current: Array = _bombing_buttons.get_children().map(func(b: Node) -> String: return (b as Button).text)
	if current.size() != shown.size():
		for child in _bombing_buttons.get_children():
			_bombing_buttons.remove_child(child)
			child.queue_free()
		for k in shown.size():
			var b := Button.new()
			b.focus_mode = Control.FOCUS_NONE
			b.custom_minimum_size = Vector2(200, 36)
			b.pressed.connect(_bombing_pressed.bind(b))
			_bombing_buttons.add_child(b)
	for k in shown.size():
		var b := _bombing_buttons.get_child(k) as Button
		b.text = String(shown[k][0])
		b.set_meta("answer", int(shown[k][1]))
		b.disabled = int(shown[k][1]) < 0
	_bombing_buttons.set_meta("bombing", bombing)
	_bombing_buttons.set_meta("count", labels.size())


func _bombing_pressed(b: Button) -> void:
	var value := int(b.get_meta("answer", -1))
	if value < 0:
		return
	var bombing: bool = _bombing_buttons.get_meta("bombing", false)
	var last: bool = value == int(_bombing_buttons.get_meta("count", 0)) - 1
	if bombing:
		if last or not b.text in ["STOP", "BOMB", "DROP ALL"]:
			_answer(value) # CLOSE, or INVADE / MIND CONTROL / DESTROY: the bombing ends with it
		else:
			_step(value) # STOP / BOMB / DROP ALL: the window stays
	elif b.text == "BOMB":
		_hold_for_bombing(value)
	else:
		_answer(value)


## BOMB from the orbit's choice: the window stays for the bombing the game sends next (the same
## window, updated); if none comes, it goes.
func _hold_for_bombing(value: int) -> void:
	var id := request_id
	for b in _bombing_buttons.get_children():
		(b as Button).disabled = true
	_step(value)
	get_tree().create_timer(2.5).timeout.connect(func():
		if request_id == id and visible:
			hide()
			released.emit())


## The planet at the original's scale (a huge planet, 150 px, fills the box), as soon as its art
## is in (owner 2026-11-23: a small planet filled the box until the first bomb, which sized it).
func _fit_bombing_planet() -> void:
	if art == null or _bombing_planet == null:
		return
	var planet: Dictionary = art.sprite(String(_bombing_picture.get("lbx", "")), int(_bombing_picture.get("entry", -1)))
	if planet.is_empty():
		return
	var planet_size: Vector2 = planet.size
	var side := planet_size * (PLANET_BOX / HUGE_PLANET_PX) # screen px per original pixel, for every planet
	if _bombing_planet.custom_minimum_size != side:
		_bombing_planet.custom_minimum_size = side


func _draw_blasts() -> void:
	if art == null or _bombing_planet == null:
		return
	var planet: Dictionary = art.sprite(String(_bombing_picture.get("lbx", "")), int(_bombing_picture.get("entry", -1)))
	if planet.is_empty():
		return
	var area := _blast_layer.size
	_fit_bombing_planet()
	var k := PLANET_BOX / HUGE_PLANET_PX # screen px per original pixel, for every planet
	var centre := area * 0.5
	var now := Time.get_ticks_msec() / 1000.0
	for b: Dictionary in _blasts:
		var s: Dictionary = art.sprite(String(b.lbx), int(b.entry))
		if s.is_empty():
			continue
		var frame := int((now - float(b.born)) * BLAST_FPS)
		if frame >= (s.frames as Array).size():
			continue
		# Each hit is the explosion's top-left from 15 px up-left of the planet's centre (MAINPUPS:
		# base = centre - 30 / 2): drawn from the centre, every bomb landed low and right.
		art.draw(_blast_layer, String(b.lbx), int(b.entry), frame, centre + (Vector2(float(b.dx), float(b.dy)) - Vector2(15, 15)) * k, k)


func _process(_delta: float) -> void:
	if _blasts.is_empty() or not is_instance_valid(_blast_layer):
		return
	var now := Time.get_ticks_msec() / 1000.0
	_blasts = _blasts.filter(func(b: Dictionary) -> bool:
		var s: Dictionary = art.sprite(String(b.lbx), int(b.entry)) if art else {}
		return s.is_empty() or (now - float(b.born)) * BLAST_FPS < (s.frames as Array).size())
	_blast_layer.queue_redraw()


## Shows a planet's card in the popup (a hovered row or map planet); -1 keeps the current one.
func _show_card(planet_id: int) -> void:
	if _card_holder == null or planet_id < 0 or planet_id == _card_planet or not _potentials.has(planet_id):
		return
	_card_planet = planet_id
	for child in _card_holder.get_children():
		_card_holder.remove_child(child)
		child.queue_free()
	_card_holder.add_child(preload("planet_card.gd").build(art, _planets.get(planet_id, {}), _potentials[planet_id], race_id))
	reset_size.call_deferred()


## A planet of the zoomed system hovered on the map (the frontend routes it here): its card, and
## its row lit (owner, 2026-11-14: the combat target window and the map hover together).
func hover_planet(planet_id: int) -> void:
	_show_card(planet_id)
	_light_row(planet_id)


## The option rows of planets, by planet (lit when their planet is hovered on the map).
var _planet_rows := {}
## A row of a planet hovered: the frontend lights the planet on the map (-1: none).
signal planet_row_hovered(planet_id: int)
## A row of a side's ships hovered (select combat): the frontend frames its fleets (-1: none).
signal fleet_row_hovered(owner: int)
## The option rows of a side's ships, by owner.
var _fleet_rows := {}


## A fleet of the zoomed system hovered on the map: its side's row lit (-1: none).
func hover_fleet_owner(owner: int) -> void:
	var lit := get_theme_color("font_pressed_color", "ListItem")
	for id: int in _fleet_rows:
		var row: Button = _fleet_rows[id]
		if not is_instance_valid(row):
			continue
		if id == owner:
			row.add_theme_color_override("font_color", lit)
		elif row.has_meta("color"):
			row.add_theme_color_override("font_color", row.get_meta("color"))
		else:
			row.remove_theme_color_override("font_color")


func _light_row(planet_id: int) -> void:
	var lit := get_theme_color("font_pressed_color", "ListItem")
	for id: int in _planet_rows:
		var row: Button = _planet_rows[id]
		if not is_instance_valid(row):
			continue
		if id == planet_id:
			row.add_theme_color_override("font_color", lit)
		else:
			row.remove_theme_color_override("font_color")


## The right click's card for a planet this popup knows (a row, or the planet on the map); false
## when it has nothing on it.
func show_planet_card(planet_id: int, at: Rect2) -> bool:
	if not _planets.has(planet_id) and not _potentials.has(planet_id):
		return false
	var card := preload("planet_card.gd").build(art, _planets.get(planet_id, {}), _potentials.get(planet_id, {}), race_id)
	_help = preload("info_popup.gd").open_content(self, at, "", card)
	return true


## A popup's choices, one row each (a planet to colonise, a target to attack, a list item): a
## click answers `base` + the row's index; disabled rows (the game would refuse them) are greyed.
func _option_list(items: Array, base: int, selected: int) -> Control:
	options = items
	_planet_rows.clear()
	_fleet_rows.clear()
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 2)
	for i in items.size():
		var item: Dictionary = items[i]
		var row := Button.new()
		row.theme_type_variation = &"ListItem"
		row.focus_mode = Control.FOCUS_NONE
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.text = String(item.get("text", ""))
		row.custom_minimum_size = Vector2(320, 30)
		row.disabled = not bool(item.get("enabled", true))
		if bare_width > 0.0:
			row.flat = true
			row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			row.custom_minimum_size = Vector2(bare_width, 26)
			_outline(row)
			row.add_theme_color_override("font_disabled_color", Color(0.62, 0.62, 0.66))
		if i == selected:
			row.add_theme_color_override("font_color", Color(1.0, 0.8, 0.35))
		var value := base + i
		row.pressed.connect(func(): _answer(value))
		var fleet_owner := int(item.get("fleet_owner", -1))
		if fleet_owner >= 0:
			_fleet_rows[fleet_owner] = row
			row.mouse_entered.connect(func(): fleet_row_hovered.emit(fleet_owner))
			row.mouse_exited.connect(func(): fleet_row_hovered.emit(-1))
		var planet_id := int(item.get("planet", -1))
		if planet_id >= 0:
			# Hovering a planet's row shows its card and lights the planet on the map; a right
			# click opens the card beside the row.
			_planet_rows[planet_id] = row
			row.mouse_entered.connect(func():
				_show_card(planet_id)
				planet_row_hovered.emit(planet_id))
			row.mouse_exited.connect(func(): planet_row_hovered.emit(-1))
			row.gui_input.connect(func(event: InputEvent):
				var mb := event as InputEventMouseButton
				if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
					if show_planet_card(planet_id, row.get_global_rect()):
						row.accept_event())
		list.add_child(row)
		# Lines under the choice (an attack target's planet and defences, as the original's
		# planet box shows them).
		for line in item.get("details", []):
			var detail := Label.new()
			detail.theme_type_variation = &"Dim"
			if line is Dictionary: # a coloured line (the Battle Scanner's rating)
				detail.add_theme_color_override("font_color", line.get("color", Color.WHITE))
				line = String(line.get("text", ""))
			detail.text = "    " + String(line)
			detail.add_theme_font_size_override("font_size", 13)
			detail.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			detail.custom_minimum_size.x = row.custom_minimum_size.x
			if bare_width > 0.0:
				_outline(detail)
			list.add_child(detail)
	if items.size() <= 8:
		return list
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size = Vector2(bare_width + 20.0 if bare_width > 0.0 else 340.0, 8 * 32)
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	return scroll


## The planet a map click picks, as this popup's answer, or -1 (not one of its enabled options).
func answer_for_planet(planet_id: int) -> int:
	for i in options.size():
		var item: Dictionary = options[i]
		if int(item.get("planet", -1)) == planet_id and bool(item.get("enabled", true)):
			return OPTION_ANSWER + i
	return -1


## The special a ship found in a system (SYS::Draw_System_Special_Popup): its name and what it does.
func _special_box(special: Dictionary) -> Control:
	var box := PanelContainer.new()
	box.theme_type_variation = &"Pane"
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 4)
	# The window's picture of it (MAINPUPS.LBX; owner, 2026-11-14: a vault of gold for a pirate
	# cache), as the legacy window shows it.
	var picture: Dictionary = special.get("picture", {})
	if not picture.is_empty() and art:
		var rect: TextureRect = preload("sprite_rect.gd").new()
		rect.custom_minimum_size = Vector2(380, 120)
		rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
		rect.bind(art)
		rect.show_sprite(String(picture.lbx), int(picture.entry))
		column.add_child(rect)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(special.get("name", ""))
	column.add_child(name_label)
	var text := Label.new()
	text.text = String(special.get("text", ""))
	text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	text.custom_minimum_size.x = 380
	column.add_child(text)
	box.add_child(column)
	return box


const STRENGTH_TEXT := {"outmatched": "We are outmatched", "even": "An even fight", "superior": "We are superior"}
const STRENGTH_COLOR := {"outmatched": Color(0.95, 0.4, 0.35), "even": Color(0.95, 0.85, 0.45), "superior": Color(0.5, 0.9, 0.5)}


func _enemy_side(enemies: Array, owner: int) -> Dictionary:
	if owner < 0:
		return {}
	for side: Dictionary in enemies:
		if int(side.get("player", -1)) == owner:
			return side
	return {}


## A side's ships as the player sees them from orbit: by hull size, and with Battle Scanner a
## rough rating (ships only).
func _enemy_lines(side: Dictionary) -> Array:
	var parts: Array = []
	for hull: Dictionary in side.get("hulls", []):
		parts.append("%d %s" % [int(hull.count), String(hull.name)])
	var count := int(side.get("ships", 0))
	var lines: Array = ["%d %s in orbit: %s" % [count, "ship" if count == 1 else "ships", ", ".join(parts)] if not parts.is_empty() else "No ships in orbit"]
	var strength := String(side.get("strength", ""))
	if strength != "":
		lines.append({"text": "Battle Scanner, ships only: %s" % STRENGTH_TEXT.get(strength, strength),
			"color": STRENGTH_COLOR.get(strength, Color.WHITE)})
	return lines


## An enemy side's fleet with no target row of its own (its colony's rows, or an attacker's): its
## name in its colour and its ships; hovering it frames its fleets on the map, and back.
func _enemy_fleet_block(side: Dictionary) -> Control:
	var block := VBoxContainer.new()
	block.add_theme_constant_override("separation", 2)
	var owner := int(side.get("player", -1))
	var head := Button.new()
	head.theme_type_variation = &"ListItem"
	head.focus_mode = Control.FOCUS_NONE
	head.alignment = HORIZONTAL_ALIGNMENT_LEFT
	head.text = "%s fleet" % String(side.get("name", ""))
	head.custom_minimum_size = Vector2(bare_width if bare_width > 0.0 else 320.0, 28)
	head.flat = bare_width > 0.0
	if bare_width > 0.0:
		_outline(head)
	var color := int(side.get("color", -1))
	if color >= 0:
		head.add_theme_color_override("font_color", preload("../galaxy.gd").PLAYER_COLORS[color % 8])
		head.set_meta("color", preload("../galaxy.gd").PLAYER_COLORS[color % 8])
	if owner >= 0:
		_fleet_rows[owner] = head
		head.mouse_entered.connect(func(): fleet_row_hovered.emit(owner))
		head.mouse_exited.connect(func(): fleet_row_hovered.emit(-1))
	block.add_child(head)
	for line in _enemy_lines(side):
		var label := Label.new()
		label.theme_type_variation = &"Dim"
		if line is Dictionary:
			label.add_theme_color_override("font_color", line.get("color", Color.WHITE))
			line = String(line.get("text", ""))
		label.text = "    " + String(line)
		label.add_theme_font_size_override("font_size", 13)
		label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		label.custom_minimum_size.x = head.custom_minimum_size.x
		if bare_width > 0.0:
			_outline(label)
		block.add_child(label)
	return block


## A just-visited system's planets (the scouts' window): name, size, climate and minerals, a
## planet's special in gold. Hovering a row lights its planet on the map, and the map's planet
## lights its row.
func _system_planet_list(planets: Array) -> Control:
	var box := PanelContainer.new()
	box.theme_type_variation = &"Pane"
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 2)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = "Planets"
	list.add_child(heading)
	for planet: Dictionary in planets:
		var planet_id := int(planet.get("planet", -1))
		var row := Button.new()
		row.theme_type_variation = &"ListItem"
		row.focus_mode = Control.FOCUS_NONE
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.custom_minimum_size = Vector2(380, 0)
		row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		var parts: Array = []
		for key in ["size_name", "climate_name", "minerals_name"]:
			var part := String(planet.get(key, ""))
			if part != "":
				parts.append(part)
		row.text = String(planet.get("name", "")) + ("  -  " + ", ".join(parts) if not parts.is_empty() else "")
		list.add_child(row)
		var special_name := String(planet.get("special_name", ""))
		if int(planet.get("special", 0)) > 0 and special_name != "":
			var special := Label.new()
			special.text = "    \u2605 " + special_name
			special.add_theme_color_override("font_color", Color(1.0, 0.8, 0.35))
			special.mouse_filter = Control.MOUSE_FILTER_IGNORE
			list.add_child(special)
		if planet_id >= 0:
			_planet_rows[planet_id] = row
			row.mouse_entered.connect(func(): planet_row_hovered.emit(planet_id))
			row.mouse_exited.connect(func(): planet_row_hovered.emit(-1))
			row.gui_input.connect(func(event: InputEvent):
				var mb := event as InputEventMouseButton
				if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
					if show_planet_card(planet_id, row.get_global_rect()):
						row.accept_event())
	box.add_child(list)
	return box


## Centred on the screen once sized: a tall one (the turn summary) grew down from the middle.
func _centre() -> void:
	var area := get_parent_area_size()
	position = ((area - size) * 0.5).floor()
	match placement:
		"left": # beside a system the camera flew into, where the system pane sits
			position.x = 24.0
		"beside": # just left of a system the camera flew into: its right edge at placement_at.x
			position.x = placement_at.x - size.x
			position.y = placement_at.y - size.y * 0.5
		"top": # over the map while a star is picked on it
			position.y = 56.0
		"at": # a scene's list (diplomacy), where the legacy screen had it, kept on the screen
			position = placement_at
	position = position.clamp(Vector2.ZERO, (area - size).max(Vector2.ZERO)).floor() # always on the screen


## Where the next popup sits: "" centred, "left", "top", "at" `placement_at`, or "beside" (its right
## edge at `placement_at.x`, centred on its y) (set by the frontend before `ask`).
var placement := ""
var placement_at := Vector2.ZERO
## A scene's list (diplomacy; owner, 2026-11-14): no frame and no backdrop, just outlined text
## over the scene, this wide (0: an ordinary dialog). Esc or a right click cancels it.
var bare_width := 0.0


func _apply_bare() -> void:
	var bare := bare_width > 0.0
	if bare:
		add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	else:
		remove_theme_stylebox_override("panel")
	custom_minimum_size.x = bare_width if bare else 460.0
	$Fit/Box/Text.custom_minimum_size.x = bare_width if bare else 420.0
	for label: Label in [$Fit/Box/Title, $Fit/Box/Text]:
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT if bare else HORIZONTAL_ALIGNMENT_CENTER
		if bare:
			_outline(label)
		else:
			label.remove_theme_constant_override("outline_size")
			label.remove_theme_color_override("font_outline_color")


func _outline(control: Control) -> void:
	control.add_theme_constant_override("outline_size", 6)
	control.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.9))


# _input: a right click on the scene behind would be taken by the scene window first.
func _input(event: InputEvent) -> void:
	if not visible or bare_width <= 0.0 or _kind != "choose":
		return
	var mb := event as InputEventMouseButton
	var key := event as InputEventKey
	if (mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT) or (key and key.pressed and key.keycode == KEY_ESCAPE):
		get_viewport().set_input_as_handled()
		_answer(-1)


## The game's slider (scrapping freighters): a bar and the number, from min to max.
func _slider(low: int, high: int, start: int) -> Control:
	_slider_value = clampi(start, low, high)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	var bar := HSlider.new()
	bar.min_value = low
	bar.max_value = high
	bar.step = 1
	bar.value = _slider_value
	bar.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	bar.custom_minimum_size.x = 300
	bar.focus_mode = Control.FOCUS_NONE
	row.add_child(bar)
	var number := Label.new()
	number.theme_type_variation = &"Name"
	number.custom_minimum_size.x = 48
	number.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	number.text = str(_slider_value)
	row.add_child(number)
	bar.value_changed.connect(func(v: float):
		_slider_value = int(v)
		number.text = str(_slider_value))
	return row
