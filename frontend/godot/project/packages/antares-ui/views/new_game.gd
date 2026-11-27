extends PanelContainer
## New game (WP3.13, D30): the legacy setup screen, race selection, custom race, naming and
## banner as one stepped panel over the title backdrop. Nothing reaches the game until START: then
## one `create_game` carries every choice (the game does what the legacy screens do once they are
## made, and creates the galaxy). Single player only.
##
## Steps: GALAXY (the setup screen's options) -> RACE (13 races, or CUSTOM: the custom race
## screen's picks) -> LEADER (name and banner) -> START.

signal command(dict: Dictionary)
signal closed

var art: Node

const STEPS := ["galaxy", "race", "leader", "start"]
const STEP_TITLES := {"galaxy": "GALAXY", "race": "RACE", "leader": "LEADER", "start": "START"}
const SIZE_NAMES := ["Small", "Medium", "Large", "Huge", "Maximum"]
const AGE_NAMES := ["Young", "Average", "Old"]
const TECH_NAMES := ["Pre-Warp", "Average", "Post-Warp", "Advanced"]
const PLAYER_NAMES := ["2", "3", "4", "5", "6", "7", "8", "Random"]
const BANNER_NAMES := ["Red", "Yellow", "Green", "Silver", "Blue", "Brown", "Purple", "Orange"]

var _data := {} # the `new_game` query's answer
var _step := "galaxy"
var _choice := {"difficulty": 2, "size": 1, "age": 1, "players": 3, "tech": 1, "tactical": true, "events": true, "antarans": true}
var _race := 0
var _custom := false
var _picks := {} # custom: {primary: [9], government, specials: [22]}
var _custom_name := ""
var _leader := ""
var _leader_race := -1 # the race the leader name was suggested for
var _color := 0
var _busy := false

var _tabs: HBoxContainer
var _body: VBoxContainer
var _note: Label
var _back: Button
var _next: Button
var _cancel: Button
var _scroll: ScrollContainer


func _ready() -> void:
	theme_type_variation = &"Pane"
	var outer := VBoxContainer.new()
	outer.add_theme_constant_override("separation", 10)
	add_child(outer)
	var title := Label.new()
	title.theme_type_variation = &"Heading"
	title.text = "NEW GAME"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	outer.add_child(title)
	_tabs = HBoxContainer.new()
	_tabs.alignment = BoxContainer.ALIGNMENT_CENTER
	_tabs.add_theme_constant_override("separation", 4)
	outer.add_child(_tabs)
	_scroll = ScrollContainer.new()
	_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	outer.add_child(_scroll)
	_body = VBoxContainer.new()
	_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_body.add_theme_constant_override("separation", 10)
	_scroll.add_child(_body)
	_note = Label.new()
	_note.add_theme_color_override("font_color", Color(1, 0.55, 0.4))
	_note.autowrap_mode = TextServer.AUTOWRAP_WORD
	_note.hide()
	outer.add_child(_note)
	var foot := HBoxContainer.new()
	foot.add_theme_constant_override("separation", 8)
	outer.add_child(foot)
	_cancel = _footer_button("CANCEL", _cancel_game)
	foot.add_child(_cancel)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	foot.add_child(gap)
	_back = _footer_button("BACK", _go_back)
	foot.add_child(_back)
	_next = _footer_button("NEXT", _go_next)
	foot.add_child(_next)


func _footer_button(text: String, on_press: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.focus_mode = Control.FOCUS_NONE
	button.custom_minimum_size = Vector2(150, 40)
	button.pressed.connect(on_press)
	return button


## Opened when the game's screen is the new game: the options as they were last set.
func open() -> void:
	_busy = false
	_step = "galaxy"
	_custom = false
	_note.hide()
	show()
	_fit()
	command.emit({"verb": "query", "what": "new_game", "_reply_to": "show_data"})


func show_data(result: Dictionary) -> void:
	if not result.get("ok", false):
		_show_note(str(result.get("message", "The new game options could not be read.")))
		return
	_data = result
	var settings: Dictionary = result.get("settings", {})
	for key in _choice.keys():
		if settings.has(key):
			_choice[key] = settings[key]
	_race = clampi(_race, 0, 12)
	_rebuild()


func _fit() -> void:
	var view := get_viewport_rect().size
	custom_minimum_size = Vector2(minf(1240.0, view.x - 60.0), minf(820.0, view.y - 60.0))
	size = custom_minimum_size
	position = (view - size) * 0.5


# ---- Steps ----

func _rebuild() -> void:
	for child in _tabs.get_children():
		child.queue_free()
	for step: String in STEPS:
		var tab := Button.new()
		tab.text = STEP_TITLES[step] if not (step == "race" and _custom) else "CUSTOM RACE"
		tab.toggle_mode = true
		tab.focus_mode = Control.FOCUS_NONE
		tab.button_pressed = step == _step
		tab.custom_minimum_size = Vector2(170, 34)
		tab.disabled = STEPS.find(step) > STEPS.find(_step)
		tab.pressed.connect(func(): _go_to(step))
		_tabs.add_child(tab)
	for child in _body.get_children():
		_body.remove_child(child)
		child.queue_free()
	match _step:
		"galaxy":
			_build_galaxy()
		"race":
			if _custom:
				_build_custom()
			else:
				_build_race()
		"leader":
			_build_leader()
		"start":
			_build_start()
	_back.disabled = _step == "galaxy" and not _custom
	_next.text = "START GAME" if _step == "start" else "NEXT"
	_scroll.scroll_vertical = 0


func _go_to(step: String) -> void:
	if STEPS.find(step) <= STEPS.find(_step):
		_step = step
		_note.hide()
		_rebuild()


func _go_back() -> void:
	_note.hide()
	if _step == "race" and _custom:
		_custom = false
	elif STEPS.find(_step) > 0:
		_step = STEPS[STEPS.find(_step) - 1]
	_rebuild()


func _go_next() -> void:
	if _busy:
		return
	_note.hide()
	if _step == "race" and _custom:
		var problem := _custom_problem()
		if problem != "":
			_show_note(problem)
			return
	if _step == "leader" and _leader.strip_edges() == "":
		_show_note("Name your leader first.")
		return
	if _step == "start":
		_create()
		return
	_step = STEPS[STEPS.find(_step) + 1]
	_rebuild()


func _cancel_game() -> void:
	if _busy:
		return
	_busy = true
	command.emit({"verb": "cancel_new_game", "_reply_to": "_done"})


func _done(_result: Dictionary) -> void:
	_busy = false


func close() -> void:
	hide()
	closed.emit()


func _show_note(text: String) -> void:
	_note.text = text
	_note.visible = text != ""


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if not visible or key == null or not key.pressed or key.echo:
		return
	if key.keycode == KEY_ESCAPE:
		if _step == "galaxy" and not _custom:
			_cancel_game()
		else:
			_go_back()
		get_viewport().set_input_as_handled()
	elif key.keycode == KEY_SPACE and _step == "galaxy" and not (get_viewport().gui_get_focus_owner() is LineEdit):
		_go_next() # the legacy setup's Accept key
		get_viewport().set_input_as_handled()


# ---- Galaxy (newgame.cpp) ----

func _build_galaxy() -> void:
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 24)
	grid.add_theme_constant_override("v_separation", 14)
	_body.add_child(grid)
	var difficulties: Array = []
	for d: Dictionary in _data.get("difficulties", []):
		difficulties.append(str(d.name))
	if difficulties.size() != 5:
		difficulties = ["Tutor", "Easy", "Average", "Hard", "Impossible"]
	var sizes := SIZE_NAMES.slice(0, 5 if _data.get("has_maximum", false) else 4)
	grid.add_child(_option_group("Difficulty", "difficulty", difficulties, "NEWGAME.LBX", func(i: int) -> int: return 4 + i))
	grid.add_child(_option_group("Galaxy size", "size", sizes, "NEWGAME.LBX", func(i: int) -> int: return 9 + mini(i, 3)))
	grid.add_child(_option_group("Galaxy age", "age", AGE_NAMES, "NEWGAME.LBX", func(i: int) -> int: return 1 + i))
	grid.add_child(_option_group("Players", "players", PLAYER_NAMES, "NEWGAME.LBX", func(i: int) -> int: return 31 if i == 7 else 13 + i))
	grid.add_child(_option_group("Tech level", "tech", TECH_NAMES, "NEWGAME.LBX", func(i: int) -> int: return [20, 21, 32, 22][i]))
	var toggles := VBoxContainer.new()
	toggles.add_theme_constant_override("separation", 6)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = "Rules"
	toggles.add_child(heading)
	for item: Array in [["tactical", "Tactical combat"], ["events", "Random events"], ["antarans", "Antaran attacks"]]:
		var check := CheckButton.new()
		check.text = item[1]
		check.focus_mode = Control.FOCUS_NONE
		check.button_pressed = bool(_choice[item[0]])
		var key: String = item[0]
		check.toggled.connect(func(on: bool): _choice[key] = on)
		toggles.add_child(check)
	grid.add_child(toggles)


## One of the setup screen's choices: its picture (NEWGAME.LBX) beside a column of buttons.
func _option_group(title: String, key: String, names: Array, lbx: String, picture_of: Callable) -> Control:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	var picture := TextureRect.new()
	picture.custom_minimum_size = Vector2(150, 112)
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	row.add_child(picture)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 3)
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = title
	column.add_child(heading)
	var flow := HFlowContainer.new()
	flow.add_theme_constant_override("h_separation", 4)
	flow.add_theme_constant_override("v_separation", 4)
	column.add_child(flow)
	row.add_child(column)
	var group := ButtonGroup.new()
	var set_picture := func(i: int):
		if art:
			var entry: int = picture_of.call(i)
			_set_texture(picture, lbx, entry)
	for i in names.size():
		var button := Button.new()
		button.text = str(names[i])
		button.toggle_mode = true
		button.button_group = group
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(96, 30)
		button.button_pressed = int(_choice[key]) == i
		var index := i
		button.pressed.connect(func():
			_choice[key] = index
			set_picture.call(index))
		flow.add_child(button)
	set_picture.call(int(_choice[key]))
	return row


# ---- Race (racesel.cpp) ----

func _races() -> Array:
	return _data.get("races", [])


func _build_race() -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 18)
	row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(row)
	var list := GridContainer.new()
	list.columns = 2
	list.add_theme_constant_override("h_separation", 6)
	list.add_theme_constant_override("v_separation", 6)
	row.add_child(list)
	var group := ButtonGroup.new()
	var detail := VBoxContainer.new()
	detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail.add_theme_constant_override("separation", 8)
	row.add_child(detail)
	for race: Dictionary in _races():
		var button := Button.new()
		button.text = str(race.name)
		button.toggle_mode = true
		button.button_group = group
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(190, 40)
		button.button_pressed = int(race.race) == _race
		button.disabled = race.get("taken", false)
		var id := int(race.race)
		button.pressed.connect(func():
			_race = id
			_show_race(detail))
		list.add_child(button)
	var custom := Button.new()
	custom.text = "CUSTOM RACE..."
	custom.focus_mode = Control.FOCUS_NONE
	custom.custom_minimum_size = Vector2(190, 40)
	custom.pressed.connect(func():
		_start_custom(_race_picks(_race), "")
		_rebuild())
	list.add_child(custom)
	_show_race(detail)


func _race_data(race: int) -> Dictionary:
	for r: Dictionary in _races():
		if int(r.race) == race:
			return r
	return {}


## The race's portrait (RACESEL.LBX 15 + race) and its traits, as the selection screen shows them.
func _show_race(detail: VBoxContainer) -> void:
	for child in detail.get_children():
		child.queue_free()
	var race := _race_data(_race)
	var name := Label.new()
	name.theme_type_variation = &"Heading"
	name.text = str(race.get("name", ""))
	detail.add_child(name)
	var portrait := TextureRect.new()
	portrait.custom_minimum_size = Vector2(420, 330)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	detail.add_child(portrait)
	_set_texture(portrait, "RACESEL.LBX", 0x0F + _race)
	var traits := Label.new()
	traits.autowrap_mode = TextServer.AUTOWRAP_WORD
	traits.custom_minimum_size = Vector2(420, 0)
	var lines: PackedStringArray = []
	for line: Dictionary in race.get("traits", []):
		lines.append(str(line.text))
	traits.text = "\n".join(lines)
	detail.add_child(traits)


func _set_texture(target: TextureRect, lbx: String, entry: int, frame := 0) -> void:
	if art == null:
		return
	var tex: Texture2D = art.texture(lbx, entry, frame)
	target.texture = tex
	if tex == null:
		_waiting.append([target, lbx, entry, frame])
		if not art.loaded.is_connected(_on_art_loaded):
			art.loaded.connect(_on_art_loaded)


## Pictures still on their way: each takes its texture when it arrives (any sprite's arrival
## checks them all; a wait ends when its picture is there or its control is gone).
var _waiting: Array = []


func _on_art_loaded(_key: String) -> void:
	var still: Array = []
	for item: Array in _waiting:
		if not is_instance_valid(item[0]):
			continue # its page was rebuilt
		var target: TextureRect = item[0]
		var tex: Texture2D = art.texture(item[1], item[2], item[3])
		if tex == null:
			still.append(item)
		else:
			target.texture = tex
	_waiting = still


# ---- Custom race (raceopt.cpp; checks as Racial_Option_Screen) ----

func _race_picks(race: int) -> Dictionary:
	var picks: Dictionary = _race_data(race).get("picks", {})
	return {"primary": (picks.get("primary", [0, 0, 0, 0, 0, 0, 0, 0, 0]) as Array).duplicate(),
		"government": int(picks.get("government", 1)),
		"specials": (picks.get("specials", []) as Array).duplicate()}


func _start_custom(picks: Dictionary, name: String) -> void:
	_custom = true
	_picks = picks
	while (_picks.specials as Array).size() < 22:
		(_picks.specials as Array).append(0)
	_custom_name = name if name != "" else str(_race_data(_race).get("name", ""))


func _rules() -> Dictionary:
	return _data.get("pick_rules", {})


## Picks left and the negative total, as raceopt::Calculate_Picks.
func _count_picks() -> Vector2i:
	var rules := _rules()
	var picks := int(rules.get("starting", 10))
	var negative := 0
	var groups: Array = rules.get("primary", [])
	for g in mini(groups.size(), 9):
		var v := int(_picks.primary[g])
		if v > 0:
			var cost := int(groups[g].choices[v - 1].cost)
			picks -= cost
			if cost < 0:
				negative += cost
	var gov: Array = rules.get("government", {}).get("choices", [])
	if int(_picks.government) < gov.size():
		var cost := int(gov[int(_picks.government)].cost)
		picks -= cost
		if cost < 0:
			negative += cost
	var specials: Array = rules.get("specials", [])
	for s in mini(specials.size(), 22):
		if int(_picks.specials[s]) != 0:
			var cost := int(specials[s].cost)
			picks -= cost
			if cost < 0:
				negative += cost
	return Vector2i(picks, negative)


func _custom_problem() -> String:
	var counts := _count_picks()
	if counts.x < 0:
		return "You have used more picks than you have."
	if counts.y < int(_rules().get("negative_limit", -10)):
		return "That is more negative picks than allowed."
	if _custom_name.strip_edges() == "":
		return "Name your race first."
	return ""


func _build_custom() -> void:
	var rules := _rules()
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 12)
	_body.add_child(top)
	var base_label := Label.new()
	base_label.text = "Looks like:"
	top.add_child(base_label)
	var base := OptionButton.new()
	base.focus_mode = Control.FOCUS_NONE
	for race: Dictionary in _races():
		base.add_item(str(race.name), int(race.race))
	base.select(_race)
	base.item_selected.connect(func(i: int): _race = base.get_item_id(i))
	top.add_child(base)
	var name_label := Label.new()
	name_label.text = "Race name:"
	top.add_child(name_label)
	var name_edit := LineEdit.new()
	name_edit.max_length = 14
	name_edit.custom_minimum_size = Vector2(220, 0)
	name_edit.text = _custom_name
	name_edit.text_changed.connect(func(t: String): _custom_name = t)
	top.add_child(name_edit)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(gap)
	var picks_label := Label.new()
	picks_label.theme_type_variation = &"Name"
	top.add_child(picks_label)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 8)
	_body.add_child(buttons)
	var defaults := Button.new()
	defaults.text = "DEFAULT"
	defaults.focus_mode = Control.FOCUS_NONE
	defaults.pressed.connect(func():
		_picks = {"primary": [0, 0, 0, 0, 0, 0, 0, 0, 0], "government": 1, "specials": []}
		for _i in 22:
			(_picks.specials as Array).append(0)
		_rebuild())
	buttons.add_child(defaults)
	var last: Dictionary = _data.get("last_custom", {})
	if not last.is_empty():
		var load_last := Button.new()
		load_last.text = "LAST CUSTOM RACE: %s" % str(last.get("name", ""))
		load_last.focus_mode = Control.FOCUS_NONE
		load_last.pressed.connect(func():
			_race = int(last.get("race", _race))
			var p: Dictionary = last.get("picks", {})
			_start_custom({"primary": (p.get("primary", []) as Array).duplicate(), "government": int(p.get("government", 1)),
				"specials": (p.get("specials", []) as Array).duplicate()}, str(last.get("name", "")))
			_rebuild())
		buttons.add_child(load_last)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 22)
	_body.add_child(columns)
	var left := VBoxContainer.new()
	left.add_theme_constant_override("separation", 6)
	left.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	columns.add_child(left)
	var right := VBoxContainer.new()
	right.add_theme_constant_override("separation", 2)
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	columns.add_child(right)

	var update := func():
		var counts := _count_picks()
		picks_label.text = "Picks left: %d" % counts.x
		picks_label.add_theme_color_override("font_color", Color(1, 0.5, 0.4) if counts.x < 0 else Color(0.7, 1, 0.7))

	var groups: Array = rules.get("primary", [])
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 14)
	grid.add_theme_constant_override("v_separation", 8)
	left.add_child(grid)
	for g in groups.size():
		var box := VBoxContainer.new()
		var title := Label.new()
		title.theme_type_variation = &"Name"
		title.text = str(groups[g].title)
		box.add_child(title)
		var choices: Array = groups[g].choices
		for c in choices.size():
			var button := Button.new()
			button.toggle_mode = true
			button.focus_mode = Control.FOCUS_NONE
			button.text = "%s   %+d" % [str(choices[c].label), int(choices[c].cost)]
			button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			button.button_pressed = int(_picks.primary[g]) == c + 1
			var gi := g
			var value := c + 1
			button.pressed.connect(func():
				# Clicking the current choice clears it (the legacy screen does the same).
				if int(_picks.primary[gi]) == value:
					_picks.primary[gi] = 0
				else:
					if gi == int(rules.get("food_group", 1)) and int(_picks.specials[int(rules.get("lithovore", 9))]) != 0:
						_show_note("Lithovores eat rock: food picks don't apply to them.")
						_rebuild()
						return
					var before := int(_picks.primary[gi])
					_picks.primary[gi] = value
					if _count_picks().y < int(rules.get("negative_limit", -10)):
						_picks.primary[gi] = before
						_show_note("That is more negative picks than allowed.")
				_rebuild())
			box.add_child(button)
		grid.add_child(box)
	var gov: Dictionary = rules.get("government", {})
	var gov_box := VBoxContainer.new()
	var gov_title := Label.new()
	gov_title.theme_type_variation = &"Name"
	gov_title.text = str(gov.get("title", "Government"))
	gov_box.add_child(gov_title)
	var gov_group := ButtonGroup.new()
	var gov_choices: Array = gov.get("choices", [])
	for c in gov_choices.size():
		var button := Button.new()
		button.toggle_mode = true
		button.button_group = gov_group
		button.focus_mode = Control.FOCUS_NONE
		button.alignment = HORIZONTAL_ALIGNMENT_LEFT
		button.text = "%s   %+d" % [str(gov_choices[c].label), int(gov_choices[c].cost)]
		button.button_pressed = int(_picks.government) == c
		var index := c
		button.pressed.connect(func():
			var before := int(_picks.government)
			_picks.government = index
			if _count_picks().y < int(rules.get("negative_limit", -10)):
				_picks.government = before
				_show_note("That is more negative picks than allowed.")
			_rebuild())
		gov_box.add_child(button)
	grid.add_child(gov_box)

	var specials_title := Label.new()
	specials_title.theme_type_variation = &"Name"
	specials_title.text = str(rules.get("specials_title", "Special abilities"))
	right.add_child(specials_title)
	var specials: Array = rules.get("specials", [])
	var exclusive: Array = rules.get("exclusive", [])
	for s in specials.size():
		var check := CheckButton.new()
		check.focus_mode = Control.FOCUS_NONE
		check.text = "%s   %+d" % [str(specials[s].label), int(specials[s].cost)]
		check.button_pressed = int(_picks.specials[s]) != 0
		var index := s
		check.toggled.connect(func(on: bool):
			_picks.specials[index] = 1 if on else 0
			if on:
				if _count_picks().y < int(rules.get("negative_limit", -10)):
					_picks.specials[index] = 0
					_show_note("That is more negative picks than allowed.")
				else:
					for pair: Array in exclusive:
						if int(pair[0]) == index:
							_picks.specials[int(pair[1])] = 0
						elif int(pair[1]) == index:
							_picks.specials[int(pair[0])] = 0
					if index == int(rules.get("lithovore", 9)):
						_picks.primary[int(rules.get("food_group", 1))] = 0
			_rebuild())
		right.add_child(check)
	update.call()


# ---- Leader and banner (racesel.cpp, Naming_Popup and Flag_Screen) ----

func _build_leader() -> void:
	if _leader_race != _race or _leader == "":
		var names: Array = _race_data(_race).get("leaders", [])
		if not names.is_empty():
			_leader = str(names[randi() % names.size()].name)
		_leader_race = _race
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	_body.add_child(row)
	var label := Label.new()
	label.theme_type_variation = &"Name"
	label.text = "Your name, leader of the %s:" % (_custom_name if _custom else str(_race_data(_race).get("name", "")))
	row.add_child(label)
	var edit := LineEdit.new()
	edit.max_length = 19
	edit.custom_minimum_size = Vector2(320, 0)
	edit.text = _leader
	edit.text_changed.connect(func(t: String): _leader = t)
	row.add_child(edit)
	var again := Button.new()
	again.text = "ANOTHER NAME"
	again.focus_mode = Control.FOCUS_NONE
	again.pressed.connect(func():
		var names: Array = _race_data(_race).get("leaders", [])
		if not names.is_empty():
			_leader = str(names[randi() % names.size()].name)
		_rebuild())
	row.add_child(again)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = "Banner"
	_body.add_child(heading)
	var banners := GridContainer.new()
	banners.columns = 4
	banners.add_theme_constant_override("h_separation", 12)
	banners.add_theme_constant_override("v_separation", 12)
	_body.add_child(banners)
	var group := ButtonGroup.new()
	for c in 8:
		var box := VBoxContainer.new()
		var button := Button.new()
		button.toggle_mode = true
		button.button_group = group
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(180, 150)
		button.button_pressed = _color == c
		button.tooltip_text = BANNER_NAMES[c]
		var picture := TextureRect.new()
		picture.set_anchors_preset(Control.PRESET_FULL_RECT)
		picture.offset_left = 8
		picture.offset_top = 8
		picture.offset_right = -8
		picture.offset_bottom = -8
		picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
		button.add_child(picture)
		_set_texture(picture, "RACESEL.LBX", 0x22 + _race * 8 + c)
		var index := c
		button.pressed.connect(func(): _color = index)
		box.add_child(button)
		var name := Label.new()
		name.text = BANNER_NAMES[c]
		name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		box.add_child(name)
		banners.add_child(box)


# ---- Start ----

func _build_start() -> void:
	var race_name := _custom_name if _custom else str(_race_data(_race).get("name", ""))
	var lines: PackedStringArray = []
	var difficulties: Array = _data.get("difficulties", [])
	var difficulty := str(difficulties[int(_choice.difficulty)].name) if int(_choice.difficulty) < difficulties.size() else ""
	lines.append("%s of the %s, under the %s banner." % [_leader, race_name, BANNER_NAMES[_color]])
	lines.append("")
	lines.append("Difficulty: %s" % difficulty)
	lines.append("Galaxy: %s, %s" % [SIZE_NAMES[int(_choice.size)], AGE_NAMES[int(_choice.age)]])
	lines.append("Players: %s" % PLAYER_NAMES[int(_choice.players)])
	lines.append("Tech level: %s" % TECH_NAMES[int(_choice.tech)])
	lines.append("Tactical combat: %s,  random events: %s,  Antaran attacks: %s" % [
		"on" if _choice.tactical else "off", "on" if _choice.events else "off", "on" if _choice.antarans else "off"])
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)
	_body.add_child(row)
	var portrait := TextureRect.new()
	portrait.custom_minimum_size = Vector2(360, 280)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_set_texture(portrait, "RACESEL.LBX", 0x0F + _race)
	row.add_child(portrait)
	var text := Label.new()
	text.text = "\n".join(lines)
	text.autowrap_mode = TextServer.AUTOWRAP_WORD
	text.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(text)


func _create() -> void:
	_busy = true
	_next.disabled = true
	_show_note("Creating the galaxy...")
	_note.add_theme_color_override("font_color", Color(0.7, 1, 0.7))
	var dict := {"verb": "create_game", "_reply_to": "created", "race": _race, "color": _color, "leader": _leader.strip_edges()}
	for key in _choice.keys():
		dict[key] = bool(_choice[key]) if key in ["tactical", "events", "antarans"] else int(_choice[key])
	if _custom:
		dict["custom"] = {"primary": (_picks.primary as Array).map(func(v): return int(v)), "government": int(_picks.government),
			"specials": (_picks.specials as Array).map(func(v): return int(v)), "name": _custom_name.strip_edges()}
	command.emit(dict)


func created(result: Dictionary) -> void:
	_busy = false
	_next.disabled = false
	_note.add_theme_color_override("font_color", Color(1, 0.55, 0.4))
	if result.get("ok", false):
		_note.hide()
		return
	_show_note(str(result.get("message", result.get("error", "The game could not be created."))))
