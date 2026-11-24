extends PanelContainer
## The Races report (owner, 2026-11-20: REPORT fell back to the legacy screen): a flyout from the
## Races panel, laid out as the Info panel. Across the top the Tech Review's four filters
## (ACHIEVEMENTS, COLONY, WEAPONS, EQUIPMENT); below, that empire's researched applications by group
## with the hovered one's picture and help; down the right the ruler's portrait, name and
## personality, that empire's spies in the player's empire, and its alliances and wars in a box.
## The left of RACERPRT::Race_Report_Screen (`race_report`) and its Tech Review subscreen
## (`tech_review` with the player), with the buttons at the top.

signal command(dict: Dictionary)
signal closed

const Themes := preload("../themes.gd")
const WIDTH := 820.0
const SIDE_WIDTH := 250.0
const PORTRAIT_HEIGHT := 230.0
const SPY_ICONS_SHOWN := 12

var art: Node
var player := -1
var report := {}
var review := {}
var _list := 0

var _title: Label
var _filters: HBoxContainer
var _rows: VBoxContainer
var _app_title: Label
var _app_picture: TextureRect
var _app_text: RichTextLabel
var _side: VBoxContainer


func _ready() -> void:
	theme_type_variation = &"Pane"
	top_level = true
	mouse_filter = Control.MOUSE_FILTER_STOP
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	_title = Label.new()
	_title.theme_type_variation = &"Heading"
	_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_title)
	var x := Button.new()
	x.text = "X"
	x.focus_mode = Control.FOCUS_NONE
	x.tooltip_text = "Close (Esc)"
	x.custom_minimum_size = Vector2(34, 30)
	x.pressed.connect(close)
	header.add_child(x)
	box.add_child(header)
	_filters = HBoxContainer.new()
	_filters.add_theme_constant_override("separation", 6)
	box.add_child(_filters)
	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 12)
	box.add_child(body)
	# The list and the hovered application.
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	Themes.scroll_gap(scroll)
	body.add_child(scroll)
	_rows = VBoxContainer.new()
	_rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows.add_theme_constant_override("separation", 1)
	scroll.add_child(_rows)
	var detail := VBoxContainer.new()
	detail.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail.size_flags_vertical = Control.SIZE_EXPAND_FILL
	detail.add_theme_constant_override("separation", 6)
	body.add_child(detail)
	_app_title = Label.new()
	_app_title.theme_type_variation = &"Name"
	_app_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	detail.add_child(_app_title)
	_app_picture = preload("sprite_rect.gd").new()
	_app_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_app_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_app_picture.custom_minimum_size = Vector2(0, 170)
	detail.add_child(_app_picture)
	_app_text = RichTextLabel.new()
	_app_text.scroll_active = true
	_app_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_app_text.add_theme_font_size_override("normal_font_size", 14)
	detail.add_child(_app_text)
	# The ruler, the spies and the relations.
	_side = VBoxContainer.new()
	_side.custom_minimum_size.x = SIDE_WIDTH
	_side.add_theme_constant_override("separation", 8)
	body.add_child(_side)


## Opens on `who`'s report, beside `rect` (the Races panel), as tall as it.
func open_for(who: int, rect: Rect2) -> void:
	player = who
	_list = 0
	report = {}
	review = {}
	place(rect)
	show()
	move_to_front()
	command.emit({"verb": "query", "what": "race_report", "player": who, "_reply_to": "show_report"})
	command.emit({"verb": "query", "what": "tech_review", "player": who, "_reply_to": "show_review"})


## Beside `rect` (the Races panel), as tall; narrower where the screen has less room.
func place(rect: Rect2) -> void:
	var width := minf(WIDTH, rect.position.x - 14.0)
	custom_minimum_size = Vector2(width, rect.size.y)
	size = custom_minimum_size
	global_position = Vector2(rect.position.x - width - 6.0, rect.position.y)


func close() -> void:
	if visible:
		hide()
		closed.emit()


func show_report(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.get("player", -1)) != player:
		return
	report = result
	_title.text = "%s Report" % String(result.get("race", ""))
	for child in _side.get_children():
		_side.remove_child(child)
		child.queue_free()
	var portrait: TextureRect = preload("sprite_rect.gd").new()
	portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	portrait.custom_minimum_size = Vector2(SIDE_WIDTH, PORTRAIT_HEIGHT)
	portrait.bind(art)
	portrait.show_sprite(String(result.portrait.lbx), int(result.portrait.entry))
	_side.add_child(portrait)
	var name_label := _centred(String(result.get("name", "")), &"Name")
	name_label.add_theme_color_override("font_color", preload("../galaxy.gd").PLAYER_COLORS[int(result.get("color", 0)) % 8])
	_side.add_child(name_label)
	var personality := String(result.get("personality", ""))
	if result.has("objective"):
		personality += "\n" + String(result.objective)
	_side.add_child(_centred(personality, &"Dim"))
	# Their spies among the player's colonies, as the screen draws them.
	var spies := int(result.get("spies", 0))
	if spies > 0:
		var icons := HBoxContainer.new()
		icons.alignment = BoxContainer.ALIGNMENT_CENTER
		icons.add_theme_constant_override("separation", -10 if spies > 6 else 0)
		for i in mini(spies, SPY_ICONS_SHOWN):
			var icon: TextureRect = preload("sprite_rect.gd").new()
			icon.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
			icon.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
			icon.custom_minimum_size = Vector2(24, 32)
			icon.bind(art)
			icon.show_sprite("RACES.LBX", int(result.spy_icon))
			icons.add_child(icon)
		_side.add_child(icons)
	_side.add_child(_centred(String(result.get("spies_text", "")), &""))
	# Alliances and wars, in a box.
	var relations := PanelContainer.new()
	relations.theme_type_variation = &"Row"
	relations.size_flags_vertical = Control.SIZE_EXPAND_FILL
	var lines := VBoxContainer.new()
	lines.add_theme_constant_override("separation", 4)
	relations.add_child(lines)
	for part in [["alliances_label", "alliances", "none_allied"], ["wars_label", "wars", "none_at_war"]]:
		lines.add_child(_centred(String(result.get(part[0], "")).strip_edges(), &"Name"))
		var names: Array = result.get(part[1], [])
		if names.is_empty():
			lines.add_child(_centred(String(result.get(part[2], "")).strip_edges(), &"Dim"))
		for n: Dictionary in names:
			lines.add_child(_centred(String(n.name), &""))
		var gap := Control.new()
		gap.custom_minimum_size.y = 8
		lines.add_child(gap)
	_side.add_child(relations)


func show_review(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.get("player", -1)) != player:
		return
	review = result
	for child in _filters.get_children():
		_filters.remove_child(child)
		child.queue_free()
	var lists: Array = result.get("lists", [])
	for i in lists.size():
		var b := Button.new()
		b.text = String(lists[i].name).to_upper()
		b.toggle_mode = true
		b.focus_mode = Control.FOCUS_NONE
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.custom_minimum_size.y = 32
		b.set_pressed_no_signal(i == _list)
		Themes.glowing(b)
		Themes.glow(b)
		var index := i
		b.pressed.connect(func():
			_list = index
			show_review(review))
		_filters.add_child(b)
	for child in _rows.get_children():
		_rows.remove_child(child)
		child.queue_free()
	if lists.is_empty():
		return
	var first := -1
	for group: Dictionary in lists[clampi(_list, 0, lists.size() - 1)].groups:
		var heading := Label.new()
		heading.theme_type_variation = &"Name"
		heading.text = String(group.name)
		_rows.add_child(heading)
		for app: Dictionary in group.apps:
			var id := int(app.app)
			var app_name := String(app.name)
			var row := Label.new()
			row.text = "   " + app_name
			row.mouse_filter = Control.MOUSE_FILTER_STOP
			var missing: bool = not app.get("player_has", true)
			if missing:
				# Theirs, not ours: the legacy report drew these brighter (info_jim_hack_color, near
				# the list's highlight); here bright white and bold.
				row.add_theme_color_override("font_color", Color.WHITE)
				var bold := FontVariation.new()
				bold.base_font = get_theme_font("font", "Label") # the theme's (the row isn't in the tree yet)
				bold.variation_embolden = 0.8
				row.add_theme_font_override("font", bold)
				row.tooltip_text = "You don't have this"
			row.add_theme_font_size_override("font_size", 15)
			row.mouse_entered.connect(func():
				row.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
				_pick(id, app_name))
			row.mouse_exited.connect(func():
				if missing:
					row.add_theme_color_override("font_color", Color.WHITE)
				else:
					row.remove_theme_color_override("font_color"))
			_rows.add_child(row)
			if first < 0:
				first = id
				_pick(id, app_name)
	if first < 0:
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "Nothing researched here yet"
		_rows.add_child(none)
		_app_title.text = ""
		_app_text.text = ""
		_app_picture.texture = null


func _pick(app: int, app_name: String) -> void:
	_app_title.text = app_name
	_app_text.text = ""
	_app_picture.bind(art)
	_app_picture.show_sprite("APP_PICS.LBX", app)
	command.emit({"verb": "query", "what": "help", "topic": app, "_reply_to": "_show_help"})


func _show_help(result: Dictionary) -> void:
	if result.get("ok", false):
		_app_text.text = String(result.get("text", "")).strip_edges()


func _centred(text: String, variation: StringName) -> Label:
	var label := Label.new()
	label.text = text
	if variation != &"":
		label.theme_type_variation = variation
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size.x = SIDE_WIDTH - 20.0
	return label


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
