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


func ask(id: int, kind: String, request: Dictionary) -> void:
	request_id = id
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
		for option: Dictionary in request.get("options", []):
			if option.has("potential") and option.has("planet"):
				_potentials[int(option.planet)] = option.potential
		if request.has("potential") and request.has("planet"):
			_potentials[int(request.planet)] = request.potential
		if request.has("options"):
			_extra.add_child(_option_list(request.options, 0 if choose else OPTION_ANSWER, int(request.get("selected", -1))))
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
		if request.has("rows"):
			# The orbit's invade/bomb window (owner, 2026-11-13: pictures like the window's): only
			# the bombing's result stays as text under them.
			_extra.add_child(_combat_rows(request))
			$Fit/Box/Text.text = String(request.get("summary", ""))
		# A slider goes under its question; other content (a leader's card) above the text.
		$Fit/Box.move_child(_extra, $Fit/Box/Text.get_index() + (1 if popup_name == "slider" else 0))
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
			b.pressed.connect(func(): _answer(_slider_value if value == -2 else value))
			_popup_buttons.add_child(b)
	$Fit/Box/Text.visible = $Fit/Box/Text.text != "" or kind != "popup"
	show()
	if kind == "text":
		entry.grab_focus()
		entry.select_all()
	reset_size() # as tall as its text: OK as far from the bottom as the text from the top (owner)
	_centre.call_deferred()


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


## A planet of the zoomed system hovered on the map (the frontend routes it here).
func hover_planet(planet_id: int) -> void:
	_show_card(planet_id)


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
		var planet_id := int(item.get("planet", -1))
		if planet_id >= 0:
			# Hovering a planet's row shows its card; a right click opens it beside the row.
			row.mouse_entered.connect(func(): _show_card(planet_id))
			row.gui_input.connect(func(event: InputEvent):
				var mb := event as InputEventMouseButton
				if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
					if show_planet_card(planet_id, row.get_global_rect()):
						row.accept_event())
		list.add_child(row)
		# Lines under the choice (an attack target's planet and defences, as the original's
		# planet box shows them).
		for line: String in item.get("details", []):
			var detail := Label.new()
			detail.theme_type_variation = &"Dim"
			detail.text = "    " + line
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
		rect.custom_minimum_size = Vector2(380, 150)
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


## Centred on the screen once sized: a tall one (the turn summary) grew down from the middle.
func _centre() -> void:
	var area := get_parent_area_size()
	position = ((area - size) * 0.5).floor()
	match placement:
		"left": # beside a system the camera flew into, where the system pane sits
			position.x = 24.0
		"top": # over the map while a star is picked on it
			position.y = 56.0
		"at": # a scene's list (diplomacy), where the legacy screen had it, kept on the screen
			position = placement_at.clamp(Vector2.ZERO, (area - size).max(Vector2.ZERO)).floor()


## Where the next popup sits: "" centred, "left", "top" or "at" `placement_at` (set by the
## frontend before `ask`).
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
