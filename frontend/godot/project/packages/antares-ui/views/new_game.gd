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
var _custom_stage := "looks" # the custom race: "looks" (the portrait) then "picks"
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
	get_viewport().size_changed.connect(_on_window_resized)
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
	_body.size_flags_vertical = Control.SIZE_EXPAND_FILL # the pages fill the panel's height
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


## The panel fills the window less a margin, up to 21:9 (owner, 2026-11-27: the first pass kept a
## 4:3 box on wide screens); the pages lay out across that width.
func _fit() -> void:
	var view := get_viewport_rect().size
	var margin := clampf(view.y * 0.03, 12.0, 40.0)
	var h := view.y - margin * 2.0
	var w := minf(view.x - margin * 2.0, h * 21.0 / 9.0)
	custom_minimum_size = Vector2(w, h)
	size = custom_minimum_size
	position = (view - size) * 0.5


func _on_window_resized() -> void:
	if visible:
		_fit()
		if not _data.is_empty():
			_rebuild()


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
			if not _custom:
				_build_race()
			elif _custom_stage == "looks":
				_build_looks()
			else:
				_build_custom()
		"leader":
			_build_leader()
		"start":
			_build_start()
	_back.disabled = false # on the first page it goes back to the title (owner, 2026-11-27)
	_next.text = "START GAME" if _step == "start" else "NEXT"
	_scroll.scroll_vertical = 0


func _go_to(step: String) -> void:
	if STEPS.find(step) <= STEPS.find(_step):
		_step = step
		_note.hide()
		_rebuild()


func _go_back() -> void:
	_note.hide()
	if _step == "galaxy":
		_cancel_game()
		return
	if _step == "race" and _custom and _custom_stage == "picks":
		_custom_stage = "looks"
	elif _step == "race" and _custom:
		_custom = false
	elif STEPS.find(_step) > 0:
		_step = STEPS[STEPS.find(_step) - 1]
	_rebuild()


func _go_next() -> void:
	if _busy:
		return
	_note.hide()
	if _step == "race" and _custom and _custom_stage == "looks":
		_start_custom(_race_picks(_race), "")
		_rebuild()
		return
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


func _done(result: Dictionary) -> void:
	_busy = false
	if not result.get("ok", false):
		_show_note(str(result.get("message", result.get("error", "Could not go back to the title screen."))))


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
		_go_back()
		get_viewport().set_input_as_handled()
	elif key.keycode == KEY_SPACE and _step == "galaxy" and not (get_viewport().gui_get_focus_owner() is LineEdit):
		_go_next() # the legacy setup's Accept key
		get_viewport().set_input_as_handled()


# ---- Galaxy (newgame.cpp) ----

## Two rows of three (owner, 2026-11-27): difficulty, galaxy size and galaxy age above; players,
## tech level and the rules below. Each takes a third of the panel's width, so nothing scrolls.
func _build_galaxy() -> void:
	var difficulties: Array = []
	for d: Dictionary in _data.get("difficulties", []):
		difficulties.append(str(d.name))
	if difficulties.size() != 5:
		difficulties = ["Tutor", "Easy", "Average", "Hard", "Impossible"]
	var sizes := SIZE_NAMES.slice(0, 5 if _data.get("has_maximum", false) else 4)
	var rows := VBoxContainer.new()
	rows.size_flags_vertical = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 14)
	_body.add_child(rows)
	var top := _galaxy_row(rows)
	top.add_child(_framed(_option_group("Difficulty", "difficulty", difficulties, "NEWGAME.LBX", func(i: int) -> int: return 4 + i)))
	top.add_child(_framed(_option_group("Galaxy size", "size", sizes, "NEWGAME.LBX", func(i: int) -> int: return 9 + mini(i, 3))))
	top.add_child(_framed(_option_group("Galaxy age", "age", AGE_NAMES, "NEWGAME.LBX", func(i: int) -> int: return 1 + i)))
	var bottom := _galaxy_row(rows)
	bottom.add_child(_framed(_option_group("Players", "players", PLAYER_NAMES, "NEWGAME.LBX", func(i: int) -> int: return 31 if i == 7 else 13 + i)))
	bottom.add_child(_framed(_option_group("Tech level", "tech", TECH_NAMES, "NEWGAME.LBX", func(i: int) -> int: return [20, 21, 32, 22][i])))
	var toggles := VBoxContainer.new()
	toggles.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toggles.size_flags_stretch_ratio = 1.0
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
		check.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
		check.custom_minimum_size.x = 300
		var key: String = item[0]
		check.toggled.connect(func(on: bool): _choice[key] = on)
		toggles.add_child(check)
	bottom.add_child(_framed(toggles))


## A group in the theme's thin frame (owner, 2026-11-27: the pages' pictures and buttons floated on
## the panel), taking the content's size flags.
func _framed(content: Control) -> PanelContainer:
	var frame := PanelContainer.new()
	frame.theme_type_variation = &"Row"
	frame.size_flags_horizontal = content.size_flags_horizontal
	frame.size_flags_vertical = content.size_flags_vertical
	frame.size_flags_stretch_ratio = content.size_flags_stretch_ratio
	frame.add_child(content)
	return frame


func _galaxy_row(parent: Control) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", 24)
	parent.add_child(row)
	return row


## One of the setup screen's choices (owner, 2026-11-27): its picture (NEWGAME.LBX), as large as
## half the page's height allows, beside its name and a drop-down. A click on the picture steps to
## the next choice (a right click, the one before), as the original's picture buttons did.
func _option_group(title: String, key: String, names: Array, lbx: String, picture_of: Callable) -> Control:
	var row := HBoxContainer.new()
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_theme_constant_override("separation", 14)
	var side := clampf(minf((size.y - 340.0) * 0.5, (size.x - 180.0) / 3.0 - 250.0), 110.0, 480.0) # inside its frame
	var picture := TextureRect.new()
	picture.custom_minimum_size = Vector2(side, side)
	picture.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture.mouse_filter = Control.MOUSE_FILTER_STOP
	picture.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	picture.tooltip_text = "Click for the next choice (right click: the one before)"
	row.add_child(picture)
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 6)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = title
	column.add_child(heading)
	var pick := OptionButton.new()
	pick.focus_mode = Control.FOCUS_NONE
	pick.custom_minimum_size = Vector2(190, 36)
	for n in names:
		pick.add_item(str(n))
	column.add_child(pick)
	row.add_child(column)
	var choose := func(i: int):
		_choice[key] = i
		pick.select(i)
		if art:
			_set_texture(picture, lbx, int(picture_of.call(i)))
	pick.item_selected.connect(func(i: int): choose.call(i))
	picture.gui_input.connect(func(event: InputEvent):
		var press := event as InputEventMouseButton
		if press == null or not press.pressed:
			return
		if press.button_index == MOUSE_BUTTON_LEFT:
			choose.call((int(_choice[key]) + 1) % names.size())
		elif press.button_index == MOUSE_BUTTON_RIGHT:
			choose.call((int(_choice[key]) + names.size() - 1) % names.size())
		else:
			return
		command.emit({"verb": "play_sound"})
		picture.accept_event())
	choose.call(clampi(int(_choice[key]), 0, names.size() - 1))
	return row


# ---- Race (racesel.cpp) ----

func _races() -> Array:
	return _data.get("races", [])


## As the original screen (owner, 2026-11-27): the portrait as large as the page allows (its own
## shape kept), the race's traits beside it, and the races to pick from on the right.
func _build_race() -> void:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)
	row.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(row)
	var detail := HBoxContainer.new()
	detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail.add_theme_constant_override("separation", 20)
	row.add_child(_framed(detail))
	var list := GridContainer.new()
	list.columns = 2
	list.add_theme_constant_override("h_separation", 8)
	list.add_theme_constant_override("v_separation", 8)
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	row.add_child(_framed(list))
	var group := ButtonGroup.new()
	for race: Dictionary in _races():
		var button := _race_button(str(race.name))
		button.toggle_mode = true
		button.button_group = group
		button.button_pressed = int(race.race) == _race
		button.disabled = race.get("taken", false)
		var id := int(race.race)
		button.pressed.connect(func():
			_race = id
			_show_race(detail))
		list.add_child(button)
	var custom := _race_button("CUSTOM RACE...")
	custom.pressed.connect(func():
		_custom = true
		_custom_stage = "looks"
		_rebuild())
	list.add_child(custom)
	_show_race(detail)


func _race_button(text: String) -> Button:
	var button := Button.new()
	button.text = text
	button.focus_mode = Control.FOCUS_NONE
	button.custom_minimum_size = Vector2(190, 46)
	return button


func _race_data(race: int) -> Dictionary:
	for r: Dictionary in _races():
		if int(r.race) == race:
			return r
	return {}


const PORTRAIT_ASPECT := 290.0 / 322.0 # RACESEL.LBX 15-27, width over height
const TRAITS_WIDTH := 300.0
const RACE_LIST_WIDTH := 2 * 190.0 + 8.0


## The portrait (RACESEL.LBX 15 + race) at the largest size that fits beside the traits and the
## list, then the race's name and traits to its right.
func _show_race(detail: HBoxContainer) -> void:
	for child in detail.get_children():
		child.queue_free()
	var race := _race_data(_race)
	var room := Vector2(size.x - 120.0 - RACE_LIST_WIDTH - 24.0 - TRAITS_WIDTH - 20.0, size.y - 280.0) # less the frames
	var height := minf(room.y, room.x / PORTRAIT_ASPECT)
	var portrait := TextureRect.new()
	portrait.custom_minimum_size = Vector2(height * PORTRAIT_ASPECT, height)
	portrait.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	detail.add_child(portrait)
	_set_texture(portrait, "RACESEL.LBX", 0x0F + _race)
	var text := VBoxContainer.new()
	text.custom_minimum_size.x = TRAITS_WIDTH
	text.add_theme_constant_override("separation", 8)
	detail.add_child(text)
	var name := Label.new()
	name.theme_type_variation = &"Heading"
	name.text = str(race.get("name", ""))
	text.add_child(name)
	for line: Dictionary in race.get("traits", []):
		var trait_label := Label.new()
		trait_label.text = str(line.text)
		trait_label.autowrap_mode = TextServer.AUTOWRAP_WORD
		trait_label.custom_minimum_size.x = TRAITS_WIDTH
		text.add_child(trait_label)


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

## The custom race's first page (owner, 2026-11-27: as the stock flow, the portrait first): the 13
## portraits; a click picks the look and goes on to the picks, starting from that race's own.
func _build_looks() -> void:
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = "Choose how your race looks"
	_body.add_child(heading)
	var grid := GridContainer.new()
	grid.columns = 7
	grid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	grid.add_theme_constant_override("h_separation", 10)
	grid.add_theme_constant_override("v_separation", 10)
	_body.add_child(_framed(grid))
	var cell_w := maxf(110.0, (size.x - 110.0 - 6 * 10.0) / 7.0)
	var cell_h := minf(cell_w * 1.1, maxf(120.0, (size.y - 330.0) * 0.5))
	for race: Dictionary in _races():
		var id := int(race.race)
		var box := VBoxContainer.new()
		var button := Button.new()
		button.toggle_mode = true
		button.focus_mode = Control.FOCUS_NONE
		button.button_pressed = id == _race
		button.custom_minimum_size = Vector2(cell_w, cell_h)
		var picture := TextureRect.new()
		picture.set_anchors_preset(Control.PRESET_FULL_RECT)
		picture.offset_left = 5
		picture.offset_top = 5
		picture.offset_right = -5
		picture.offset_bottom = -5
		picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		picture.mouse_filter = Control.MOUSE_FILTER_IGNORE
		button.add_child(picture)
		_set_texture(picture, "RACESEL.LBX", 0x0F + id)
		button.pressed.connect(func():
			_race = id
			_start_custom(_race_picks(id), "")
			_rebuild())
		box.add_child(button)
		var name := Label.new()
		name.text = str(race.name)
		name.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		box.add_child(name)
		grid.add_child(box)


func _race_picks(race: int) -> Dictionary:
	var picks: Dictionary = _race_data(race).get("picks", {})
	return {"primary": (picks.get("primary", [0, 0, 0, 0, 0, 0, 0, 0, 0]) as Array).duplicate(),
		"government": int(picks.get("government", 1)),
		"specials": (picks.get("specials", []) as Array).duplicate()}


func _start_custom(picks: Dictionary, name: String) -> void:
	_custom = true
	_custom_stage = "picks"
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


const PICKED_COLOR := Color(1.0, 0.62, 0.2) # a chosen pick's text
const Tiles := preload("fleets_panel.gd") # its ship tiles' look, for the banners


## RANDOMIZE (owner, 2026-11-27): a legal race at random. A random government, up to three random
## negative picks (within the negative limit), then random positive ones while the picks last;
## each pick is kept only if the race stays legal (picks left >= 0, the negative limit, the
## exclusive pairs, no food picks for Lithovores), as the pick buttons check them.
func _randomize_picks() -> void:
	var rules := _rules()
	var groups: Array = rules.get("primary", [])
	var specials: Array = rules.get("specials", [])
	var gov: Array = rules.get("government", {}).get("choices", [])
	var limit := int(rules.get("negative_limit", -10))
	var food := int(rules.get("food_group", 1))
	var lithovore := int(rules.get("lithovore", 9))
	var exclusive: Array = rules.get("exclusive", [])
	_picks = {"primary": [0, 0, 0, 0, 0, 0, 0, 0, 0], "government": 0, "specials": []}
	for _i in 22:
		(_picks.specials as Array).append(0)
	var governments := range(gov.size())
	governments.shuffle()
	for g in governments:
		_picks.government = g
		if _count_picks().x >= 0 and _count_picks().y >= limit:
			break
	# Every pick as [kind, index, value, cost].
	var options: Array = []
	for g in mini(groups.size(), 9):
		var choices: Array = groups[g].choices
		for c in choices.size():
			options.append(["primary", g, c + 1, int(choices[c].cost)])
	for i in mini(specials.size(), 22):
		options.append(["special", i, 1, int(specials[i].cost)])
	var legal := func() -> bool:
		var counts := _count_picks()
		if counts.x < 0 or counts.y < limit:
			return false
		if int(_picks.specials[lithovore]) != 0 and int(_picks.primary[food]) != 0:
			return false
		for pair: Array in exclusive:
			if int(_picks.specials[int(pair[0])]) != 0 and int(_picks.specials[int(pair[1])]) != 0:
				return false
		return true
	var try_pick := func(option: Array) -> bool:
		var key := "primary" if option[0] == "primary" else "specials"
		var at := int(option[1])
		if int(_picks[key][at]) != 0:
			return false # that group (or ability) is taken
		_picks[key][at] = int(option[2])
		if legal.call():
			return true
		_picks[key][at] = 0
		return false
	var negatives := options.filter(func(o: Array) -> bool: return int(o[3]) < 0)
	var positives := options.filter(func(o: Array) -> bool: return int(o[3]) > 0)
	negatives.shuffle()
	var wanted := randi_range(0, 3)
	for option: Array in negatives:
		if wanted <= 0:
			break
		if try_pick.call(option):
			wanted -= 1
	positives.shuffle()
	for option: Array in positives:
		if _count_picks().x <= 0:
			break
		try_pick.call(option)
	_show_note("")


## The picks' rows, smaller than the theme's buttons so the page fits without scrolling.
func _compact(button: Button) -> void:
	button.add_theme_font_size_override("font_size", 15)
	button.custom_minimum_size.y = 26 # the theme's own boxes (a copy made before the theme applies lost them)


## A pick: a radio button whose text lights when chosen (owner, 2026-11-27).
func _radio(text: String, group: ButtonGroup, on: bool) -> CheckBox:
	var radio := CheckBox.new()
	_compact(radio)
	radio.text = text
	radio.button_group = group
	radio.focus_mode = Control.FOCUS_NONE
	radio.button_pressed = on
	for state in ["font_pressed_color", "font_hover_pressed_color"]:
		radio.add_theme_color_override(state, PICKED_COLOR)
	return radio


func _build_custom() -> void:
	var rules := _rules()
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 12)
	_body.add_child(top)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = "Race name:"
	top.add_child(name_label)
	var name_edit := LineEdit.new()
	name_edit.max_length = 14
	name_edit.custom_minimum_size = Vector2(240, 0)
	name_edit.text = _custom_name
	name_edit.text_changed.connect(func(t: String): _custom_name = t)
	top.add_child(name_edit)
	# DEFAULT: the chosen look's own race; CLEAR: no picks at all (owner, 2026-11-27).
	var defaults := Button.new()
	defaults.text = "DEFAULT"
	defaults.focus_mode = Control.FOCUS_NONE
	defaults.tooltip_text = "The picks of the race whose portrait you chose"
	defaults.pressed.connect(func():
		var own := _race_picks(_race)
		_picks = own
		while (_picks.specials as Array).size() < 22:
			(_picks.specials as Array).append(0)
		_show_note("")
		_rebuild())
	top.add_child(defaults)
	var clear := Button.new()
	clear.text = "CLEAR"
	clear.focus_mode = Control.FOCUS_NONE
	clear.tooltip_text = "No picks: every point to spend"
	clear.pressed.connect(func():
		_picks = {"primary": [0, 0, 0, 0, 0, 0, 0, 0, 0], "government": 1, "specials": []}
		for _i in 22:
			(_picks.specials as Array).append(0)
		_show_note("")
		_rebuild())
	top.add_child(clear)
	var random := Button.new()
	random.text = "RANDOMIZE"
	random.focus_mode = Control.FOCUS_NONE
	random.tooltip_text = "A random race within the picks and the rules"
	random.pressed.connect(func():
		_randomize_picks()
		_rebuild())
	top.add_child(random)
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
		top.add_child(load_last)
	var gap := Control.new()
	gap.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(gap)
	# Picks left, and the race's total (owner, 2026-11-27: "need total score").
	var counts := _count_picks()
	var starting := int(rules.get("starting", 10))
	var total := Label.new()
	total.text = "Total: %d of %d    Negative: %d of %d" % [starting - counts.x, starting, counts.y, int(rules.get("negative_limit", -10))]
	top.add_child(total)
	var picks_label := Label.new()
	picks_label.theme_type_variation = &"Name"
	picks_label.text = "Picks left: %d" % counts.x
	picks_label.add_theme_color_override("font_color", Color(1, 0.5, 0.4) if counts.x < 0 else Color(0.7, 1, 0.7))
	top.add_child(picks_label)

	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 26)
	columns.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_body.add_child(columns)
	# The stats in four columns (owner, 2026-11-27: no scrolling), the government last.
	var stat_columns: Array[VBoxContainer] = []
	for _c in 4:
		var column := VBoxContainer.new()
		column.add_theme_constant_override("separation", 10)
		column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		columns.add_child(column)
		stat_columns.append(column)
	var groups: Array = rules.get("primary", [])
	var boxes: Array[Control] = []
	for g in groups.size():
		var box := VBoxContainer.new()
		box.add_theme_constant_override("separation", 0)
		var title := Label.new()
		title.theme_type_variation = &"Name"
		title.text = str(groups[g].title)
		box.add_child(title)
		var group := ButtonGroup.new()
		group.allow_unpress = true
		var choices: Array = groups[g].choices
		for c in choices.size():
			var radio := _radio("%s   %+d" % [str(choices[c].label), int(choices[c].cost)], group, int(_picks.primary[g]) == c + 1)
			var gi := g
			var value := c + 1
			radio.pressed.connect(func():
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
			box.add_child(radio)
		boxes.append(box)
	var gov: Dictionary = rules.get("government", {})
	var gov_box := VBoxContainer.new()
	gov_box.add_theme_constant_override("separation", 0)
	var gov_title := Label.new()
	gov_title.theme_type_variation = &"Name"
	gov_title.text = str(gov.get("title", "Government"))
	gov_box.add_child(gov_title)
	var gov_group := ButtonGroup.new()
	var gov_choices: Array = gov.get("choices", [])
	for c in gov_choices.size():
		var radio := _radio("%s   %+d" % [str(gov_choices[c].label), int(gov_choices[c].cost)], gov_group, int(_picks.government) == c)
		var index := c
		radio.pressed.connect(func():
			var before := int(_picks.government)
			_picks.government = index
			if _count_picks().y < int(rules.get("negative_limit", -10)):
				_picks.government = before
				_show_note("That is more negative picks than allowed.")
			_rebuild())
		gov_box.add_child(radio)
	boxes.append(gov_box)
	# Three groups in each of the first two columns, two in the others (the government has four
	# choices, so it shares the last with one group).
	var column_of := func(i: int) -> int: return 0 if i < 3 else (1 if i < 6 else (2 if i < 8 else 3))
	for i in boxes.size():
		stat_columns[mini(int(column_of.call(i)), 3)].add_child(_framed(boxes[i]))

	# The special abilities, two columns beside them.
	var specials_box := VBoxContainer.new()
	specials_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	specials_box.size_flags_stretch_ratio = 2.0
	columns.add_child(_framed(specials_box))
	var specials_title := Label.new()
	specials_title.theme_type_variation = &"Name"
	specials_title.text = str(rules.get("specials_title", "Special abilities"))
	specials_box.add_child(specials_title)
	var specials_grid := GridContainer.new()
	specials_grid.columns = 2
	specials_grid.add_theme_constant_override("h_separation", 18)
	specials_grid.add_theme_constant_override("v_separation", 0)
	specials_box.add_child(specials_grid)
	var specials: Array = rules.get("specials", [])
	var exclusive: Array = rules.get("exclusive", [])
	for s in specials.size():
		var check := CheckBox.new()
		_compact(check)
		check.focus_mode = Control.FOCUS_NONE
		check.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		check.text = "%s   %+d" % [str(specials[s].label), int(specials[s].cost)]
		check.button_pressed = int(_picks.specials[s]) != 0
		for state in ["font_pressed_color", "font_hover_pressed_color"]:
			check.add_theme_color_override(state, PICKED_COLOR)
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
		specials_grid.add_child(check)


# ---- Leader and banner (racesel.cpp, Naming_Popup and Flag_Screen) ----

func _build_leader() -> void:
	if _leader_race != _race or _leader == "":
		_random_leader()
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 10)
	_body.add_child(_framed(row))
	var label := Label.new()
	label.theme_type_variation = &"Name"
	label.text = "Your name, leader of the %s:" % (_custom_name if _custom else str(_race_data(_race).get("name", "")))
	row.add_child(label)
	var edit := LineEdit.new()
	edit.max_length = 19
	edit.custom_minimum_size = Vector2(360, 0)
	edit.text = _leader
	edit.text_changed.connect(func(t: String): _leader = t)
	row.add_child(edit)
	var again := Button.new()
	again.text = "RANDOM"
	again.focus_mode = Control.FOCUS_NONE
	again.custom_minimum_size.x = 140
	again.pressed.connect(func():
		_random_leader()
		_rebuild())
	row.add_child(again)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = "Banner"
	_body.add_child(heading)
	# Four across the whole panel, as large as two rows allow (owner, 2026-11-27).
	var banners := GridContainer.new()
	banners.columns = 4
	banners.size_flags_vertical = Control.SIZE_EXPAND_FILL
	banners.add_theme_constant_override("h_separation", 16)
	banners.add_theme_constant_override("v_separation", 12)
	_body.add_child(_framed(banners))
	var cell_w := maxf(160.0, (size.x - 110.0 - 3 * 16.0) / 4.0)
	var cell_h := clampf((size.y - 420.0) * 0.5, 120.0, cell_w * 0.85)
	var group := ButtonGroup.new()
	for c in 8:
		var box := VBoxContainer.new()
		box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		var button := Button.new()
		button.toggle_mode = true
		button.button_group = group
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(cell_w, cell_h)
		button.button_pressed = _color == c
		button.tooltip_text = BANNER_NAMES[c]
		# As the fleets' ship tiles (owner, 2026-11-27): clear, blue when chosen, no button face.
		for state in ["normal", "disabled", "focus"]:
			button.add_theme_stylebox_override(state, Tiles._tile_style(false, false))
		button.add_theme_stylebox_override("hover", Tiles._tile_style(false, true))
		button.add_theme_stylebox_override("pressed", Tiles._tile_style(true, false))
		button.add_theme_stylebox_override("hover_pressed", Tiles._tile_style(true, true))
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


## A leader's name from the race's eight, not the one shown.
func _random_leader() -> void:
	var names: Array = _race_data(_race).get("leaders", [])
	if not names.is_empty():
		var pick := str(names[randi() % names.size()].name)
		if names.size() > 1:
			while pick == _leader:
				pick = str(names[randi() % names.size()].name)
		_leader = pick
	_leader_race = _race


# ---- Start ----

## The summary (owner, 2026-11-27): the portrait, and beside it the chosen banner above the text.
func _build_start() -> void:
	var race_name := _custom_name if _custom else str(_race_data(_race).get("name", ""))
	var lines: PackedStringArray = []
	var difficulties: Array = _data.get("difficulties", [])
	var difficulty := str(difficulties[int(_choice.difficulty)].name) if int(_choice.difficulty) < difficulties.size() else ""
	lines.append("%s of the %s" % [_leader, race_name])
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
	var height := minf(size.y - 290.0, (size.x * 0.45) / PORTRAIT_ASPECT)
	portrait.custom_minimum_size = Vector2(height * PORTRAIT_ASPECT, height)
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_set_texture(portrait, "RACESEL.LBX", 0x0F + _race)
	row.add_child(_framed(portrait))
	var column := VBoxContainer.new()
	column.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	column.add_theme_constant_override("separation", 12)
	row.add_child(_framed(column))
	var banner := TextureRect.new()
	banner.custom_minimum_size = Vector2(200, 200 * 94.0 / 84.0)
	banner.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	banner.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	banner.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	banner.tooltip_text = BANNER_NAMES[_color]
	_set_texture(banner, "RACESEL.LBX", 0x22 + _race * 8 + _color)
	column.add_child(banner)
	var text := Label.new()
	text.text = "\n".join(lines)
	text.autowrap_mode = TextServer.AUTOWRAP_WORD
	column.add_child(text)


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
