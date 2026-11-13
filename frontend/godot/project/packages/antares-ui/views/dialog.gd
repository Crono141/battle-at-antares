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
	for child in _extra.get_children():
		_extra.remove_child(child)
		child.queue_free()
	for child in _popup_buttons.get_children():
		_popup_buttons.remove_child(child)
		child.queue_free()
	var popup := kind == "popup"
	popup_name = String(request.get("popup", "")) if popup else ""
	$Fit/Box/Buttons.visible = not popup
	_popup_buttons.visible = popup
	if popup:
		if request.has("leader"):
			_extra.add_child(_leader_card(request.leader))
		if request.has("items"):
			_extra.add_child(_item_list(request.items))
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
			b.pressed.connect(func(): _answer(_slider_value if value == -2 else value))
			_popup_buttons.add_child(b)
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


## Centred on the screen once sized: a tall one (the turn summary) grew down from the middle.
func _centre() -> void:
	var area := get_parent_area_size()
	position = ((area - size) * 0.5).floor()


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
