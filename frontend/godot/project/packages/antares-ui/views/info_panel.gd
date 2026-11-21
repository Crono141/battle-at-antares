extends "side_panel.gd"
## Info (WP3.7, the owner's "Native Info Panel.png"): the legacy Info screen as a wide panel that
## flies out from the right over the galaxy (the galaxy isn't moved). Across the top the star date
## and the screen's five tabs (History Graph, Tech Review, Race Statistics, Turn Summary,
## Reference); down the right a picture (the highlighted application's, a reference topic's own or
## its application's, else the ruler's portrait) over the net income chart at full height; the active tab fills the rest. The
## screen's NEXT PAGE arrows are gone: every list scrolls. The tab and the graph toggles are
## written back as the screen writes them (`info_set_tab`, `info_set_graphs`, in the save).

signal command(dict: Dictionary)
signal width_changed
## A turn summary message picked (the summary list's; `colony` >= 0 when it has one to open).
signal item_chosen(item: Dictionary)
## A trait's help (a click on it in Race Statistics), shown by the frontend's help popup.
signal help_requested(topic: int, at: Control)

const Themes := preload("../themes.gd")
const WIDTH := 980.0
const SIDE_WIDTH := 300.0
const PICTURE_HEIGHT := 230.0
const APPLICATIONS := 212 # TECH_APP_COUNT: the help records that are applications
const GRAPHS := ["Population", "Production", "Fleet", "Technology"]
## The income chart's bar colours (the screen's maintenance_bar_colors): income, buildings,
## freighters, ships, spies, tribute, leaders.
const BAR_COLORS := [Color(0.95, 0.62, 0.1), Color(0.62, 0.62, 0.62), Color(0.75, 0.12, 0.1), Color(0.55, 0.2, 0.75),
	Color(0.85, 0.8, 0.15), Color(0.15, 0.2, 0.85), Color(0.15, 0.6, 0.2)]

var art: Node
var info := {}
var tab := -1
var _tabs: Array[Button] = []
var _stardate: Label
var _content: Control
var _picture: TextureRect
var _picture_caption: Label
var _chart: Control
var _review_list := 0 # the Tech Review's list
var _review := {}
var _detail_title: Label
var _detail_text: RichTextLabel


func _ready() -> void:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 8)
	add_child(box)
	var header := HBoxContainer.new()
	header.add_theme_constant_override("separation", 6)
	box.add_child(header)
	var date_box := PanelContainer.new()
	date_box.theme_type_variation = &"Row"
	header.add_child(date_box)
	_stardate = Label.new()
	_stardate.theme_type_variation = &"Name"
	_stardate.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	date_box.add_child(_stardate)
	for i in 5:
		var b := _button("", "")
		b.toggle_mode = true
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.clip_text = true # the tabs share the width; a narrow window clips their names
		b.add_theme_font_size_override("font_size", 12)
		b.custom_minimum_size.x = 60
		Themes.glowing(b)
		var index := i
		b.pressed.connect(func(): _set_tab(index))
		header.add_child(b)
		_tabs.append(b)
	var close_button := _button("X", "Close (Esc)")
	close_button.custom_minimum_size = Vector2(34, 30)
	close_button.pressed.connect(close)
	header.add_child(close_button)
	var body := HBoxContainer.new()
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 8)
	box.add_child(body)
	var content_frame := PanelContainer.new()
	content_frame.theme_type_variation = &"Row"
	content_frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	body.add_child(content_frame)
	_content = MarginContainer.new()
	content_frame.add_child(_content)
	var side := VBoxContainer.new()
	side.custom_minimum_size.x = SIDE_WIDTH
	side.add_theme_constant_override("separation", 8)
	body.add_child(side)
	var picture_frame := PanelContainer.new()
	picture_frame.theme_type_variation = &"Row"
	picture_frame.custom_minimum_size.y = PICTURE_HEIGHT
	side.add_child(picture_frame)
	_picture = preload("sprite_rect.gd").new()
	_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	picture_frame.add_child(_picture)
	_picture_caption = Label.new()
	_picture_caption.theme_type_variation = &"Dim"
	_picture_caption.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_picture_caption.vertical_alignment = VERTICAL_ALIGNMENT_BOTTOM
	_picture_caption.add_theme_font_size_override("font_size", 12)
	_picture_caption.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.9))
	_picture_caption.add_theme_constant_override("outline_size", 4)
	picture_frame.add_child(_picture_caption)
	var chart_frame := PanelContainer.new()
	chart_frame.theme_type_variation = &"Row"
	chart_frame.size_flags_vertical = Control.SIZE_EXPAND_FILL
	side.add_child(chart_frame)
	_chart = _IncomeChart.new()
	chart_frame.add_child(_chart)
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				preload("../themes.gd").move_x(self, _rect.end.x - size.x)
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


## Over the galaxy, not beside it: no star clicks to answer.
func select_star(_star: int) -> bool:
	return false


func refresh() -> void:
	command.emit({"verb": "query", "what": "info", "_reply_to": "show_info"})


func show_info(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	info = result
	_stardate.text = "STAR DATE  %s" % String(result.stardate)
	var names: Array = result.get("tabs", [])
	for i in _tabs.size():
		_tabs[i].text = String(names[i].name).strip_edges() if i < names.size() else ""
		_tabs[i].tooltip_text = _tabs[i].text
	_chart.set_data(result)
	var wanted := int(result.get("tab", 0)) if tab < 0 else tab
	tab = -1
	_set_tab(clampi(wanted, 0, 4))


func _set_tab(index: int) -> void:
	for i in _tabs.size():
		_tabs[i].set_pressed_no_signal(i == index)
		Themes.glow(_tabs[i])
	if index == tab:
		return
	tab = index
	command.emit({"verb": "info_set_tab", "tab": index}) # the screen's choice, kept in the save
	_clear(_content)
	_show_portrait()
	match index:
		0:
			command.emit({"verb": "query", "what": "history", "_reply_to": "_show_history"})
		1:
			command.emit({"verb": "query", "what": "tech_review", "_reply_to": "_show_tech_review"})
		2:
			command.emit({"verb": "query", "what": "race_stats", "_reply_to": "_show_race_stats"})
		3:
			command.emit({"verb": "query", "what": "turn_summary", "all": 1, "_reply_to": "_show_turn_summary"})
		4:
			command.emit({"verb": "query", "what": "reference", "_reply_to": "_show_reference"})


func close() -> void:
	if visible and not _closing:
		tab = -1 # the saved tab again next time
	super.close()


# ---- The picture ----

func _show_portrait() -> void:
	var portrait: Dictionary = info.get("portrait", {})
	_picture.frame = 0
	_picture.bind(art)
	_picture.show_sprite(String(portrait.get("lbx", "")), int(portrait.get("entry", -1)))
	_picture_caption.text = ""


func _show_picture(lbx: String, entry: int, frame := 0, caption := "") -> void:
	if lbx == "" or entry < 0:
		_show_portrait()
		return
	_picture.frame = frame
	_picture.bind(art)
	_picture.show_sprite(lbx.to_upper(), entry)
	_picture.entry = entry # the frame changes with the same entry too
	_picture_caption.text = caption


# ---- History Graph ----

func _show_history(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 0:
		return
	_clear(_content)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	_content.add_child(box)
	var legend := HFlowContainer.new()
	legend.add_theme_constant_override("h_separation", 18)
	box.add_child(legend)
	var colors: Array = preload("../galaxy.gd").PLAYER_COLORS
	for p: Dictionary in result.players:
		var name_label := Label.new()
		name_label.theme_type_variation = &"Name"
		name_label.text = String(p.name)
		name_label.add_theme_color_override("font_color", colors[int(p.color) % 8])
		legend.add_child(name_label)
	var graph := _HistoryGraph.new()
	graph.size_flags_vertical = Control.SIZE_EXPAND_FILL
	graph.result = result
	box.add_child(graph)
	var toggles := HBoxContainer.new()
	toggles.add_theme_constant_override("separation", 6)
	box.add_child(toggles)
	var on: Array = result.get("graphs", [1, 1, 1, 1])
	for i in 4:
		var b := _button(GRAPHS[i].to_upper(), "Count %s in the graph" % GRAPHS[i].to_lower())
		b.toggle_mode = true
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.set_pressed_no_signal(int(on[i]) != 0)
		Themes.glowing(b)
		Themes.glow(b)
		toggles.add_child(b)
	for b: Button in toggles.get_children():
		b.toggled.connect(func(_pressed: bool):
			var args := {"verb": "info_set_graphs", "_reply_to": "_after_graphs"}
			var keys := ["pop", "prod", "fleet", "tech"]
			for i in 4:
				args[keys[i]] = 1 if (toggles.get_child(i) as Button).button_pressed else 0
			command.emit(args))


func _after_graphs(_result: Dictionary) -> void:
	if tab == 0:
		command.emit({"verb": "query", "what": "history", "_reply_to": "_show_history"})


# ---- Tech Review ----

func _show_tech_review(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 1:
		return
	_review = result
	_clear(_content)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	_content.add_child(box)
	var lists := HBoxContainer.new()
	lists.add_theme_constant_override("separation", 6)
	box.add_child(lists)
	for i in (result.lists as Array).size():
		var b := _button(String(result.lists[i].name).to_upper(), "")
		b.toggle_mode = true
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.set_pressed_no_signal(i == _review_list)
		Themes.glowing(b)
		Themes.glow(b)
		var index := i
		b.pressed.connect(func():
			_review_list = index
			_show_tech_review(_review))
		lists.add_child(b)
	var split := _split(box)
	var rows := _scroll_list(split)
	_detail(split)
	var first := -1
	var list: Dictionary = result.lists[clampi(_review_list, 0, (result.lists as Array).size() - 1)]
	for group: Dictionary in list.groups:
		rows.add_child(_heading(String(group.name)))
		for app: Dictionary in group.apps:
			var id := int(app.app)
			var row := _row(String(app.name), func(): _pick_app(id, String(app.name)))
			row.mouse_entered.connect(func(): _pick_app(id, String(app.name)))
			rows.add_child(row)
			if first < 0:
				first = id
				_pick_app(id, String(app.name))
	if first < 0:
		rows.add_child(_dim("Nothing researched here yet"))


func _pick_app(app: int, app_name: String) -> void:
	_detail_title.text = app_name
	_detail_text.text = ""
	_show_picture("APP_PICS.LBX", app)
	command.emit({"verb": "query", "what": "help", "topic": app, "_reply_to": "_show_app_help"})


func _show_app_help(result: Dictionary) -> void:
	if result.get("ok", false) and is_instance_valid(_detail_text):
		_detail_text.text = String(result.get("text", "")).strip_edges()


# ---- Race Statistics ----

func _show_race_stats(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 2:
		return
	_clear(_content)
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_content.add_child(scroll)
	var grid := GridContainer.new()
	grid.columns = 2
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_theme_constant_override("h_separation", 8)
	grid.add_theme_constant_override("v_separation", 8)
	scroll.add_child(grid)
	var colors: Array = preload("../galaxy.gd").PLAYER_COLORS
	for p: Dictionary in result.players:
		var card := PanelContainer.new()
		card.theme_type_variation = &"Row"
		card.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		grid.add_child(card)
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 10)
		card.add_child(row)
		var portrait: TextureRect = preload("sprite_rect.gd").new()
		portrait.custom_minimum_size = Vector2(64, 74)
		portrait.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		portrait.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
		portrait.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
		portrait.bind(art)
		portrait.show_sprite("RACES.LBX", int(p.portrait))
		row.add_child(portrait)
		var lines := VBoxContainer.new()
		lines.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		lines.add_theme_constant_override("separation", 0)
		row.add_child(lines)
		var title := Label.new()
		title.theme_type_variation = &"Name"
		title.text = String(p.name)
		title.add_theme_color_override("font_color", colors[int(p.color) % 8])
		lines.add_child(title)
		if p.get("eliminated", false):
			var gone := Label.new()
			gone.text = String(result.get("eliminated_label", "Eliminated")).strip_edges()
			gone.add_theme_color_override("font_color", Color(1.0, 0.3, 0.25))
			lines.add_child(gone)
			continue
		for trait_line: Dictionary in p.traits:
			var line := Label.new()
			line.text = String(trait_line.text).strip_edges()
			line.add_theme_font_size_override("font_size", 13)
			line.mouse_filter = Control.MOUSE_FILTER_STOP
			line.mouse_default_cursor_shape = Control.CURSOR_HELP
			var topic := int(trait_line.help)
			line.gui_input.connect(func(event: InputEvent):
				var mb := event as InputEventMouseButton
				if mb and mb.pressed and mb.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
					help_requested.emit(topic, line))
			lines.add_child(line)


# ---- Turn Summary ----

func _show_turn_summary(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 3:
		return
	_clear(_content)
	var rows := _scroll_list(_content)
	var items: Array = result.get("items", [])
	for item: Dictionary in items:
		var picked := item
		var row := _row(String(item.text), func(): item_chosen.emit(picked))
		row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		if item.get("serious", false):
			row.add_theme_color_override("font_color", Color(1.0, 0.45, 0.35))
		if int(item.get("colony", -1)) < 0:
			row.tooltip_text = "No colony to open for this message"
		else:
			row.tooltip_text = "Click: open the colony"
		rows.add_child(row)
	if items.is_empty():
		rows.add_child(_dim("Nothing to report this turn"))


# ---- Reference ----

func _show_reference(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 4:
		return
	_clear(_content)
	_show_portrait()
	var columns := HBoxContainer.new()
	columns.add_theme_constant_override("separation", 16)
	_content.add_child(columns)
	var left := _scroll_list(columns)
	left.get_parent().size_flags_horizontal = Control.SIZE_EXPAND_FILL
	left.add_child(_heading(String(result.categories_label).strip_edges()))
	for c: Dictionary in result.categories:
		var category := int(c.category)
		left.add_child(_row("•  " + String(c.name).strip_edges(), func():
			command.emit({"verb": "query", "what": "reference_category", "category": category, "_reply_to": "_show_category"})))
	var right := _scroll_list(columns)
	right.get_parent().size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_child(_heading(String(result.how_to_label).strip_edges()))
	for h: Dictionary in result.how_to:
		var topic := int(h.topic)
		var topic_name := String(h.name).strip_edges()
		right.add_child(_row("•  " + topic_name, func(): _show_how_to(topic, topic_name)))


func _show_category(result: Dictionary) -> void:
	if not result.get("ok", false) or tab != 4:
		return
	_clear(_content)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	_content.add_child(box)
	box.add_child(_back_bar(String(result.title).strip_edges()))
	var split := _split(box)
	var rows := _scroll_list(split)
	_detail(split)
	var first := true
	for t: Dictionary in result.topics:
		var topic := int(t.topic)
		var row := _row(String(t.name).strip_edges(), func(): _pick_topic(topic))
		row.mouse_entered.connect(func(): _pick_topic(topic))
		rows.add_child(row)
		if first:
			first = false
			_pick_topic(topic)


func _show_how_to(topic: int, topic_name: String) -> void:
	_clear(_content)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	_content.add_child(box)
	box.add_child(_back_bar(topic_name))
	_detail(box)
	_pick_topic(topic)


func _pick_topic(topic: int) -> void:
	command.emit({"verb": "query", "what": "help", "topic": topic, "_reply_to": "_show_topic"})


func _show_topic(result: Dictionary) -> void:
	if not result.get("ok", false) or not is_instance_valid(_detail_text):
		return
	_detail_title.text = String(result.get("title", "")).strip_edges()
	_detail_text.text = String(result.get("text", "")).strip_edges()
	var picture: Dictionary = result.get("picture", {})
	var topic := int(result.get("topic", -1))
	if picture.is_empty() and topic >= 0 and topic < APPLICATIONS:
		# The help records 0-211 are the applications': a building's or a tech's picture.
		picture = {"lbx": "APP_PICS.LBX", "entry": topic}
	_show_picture(String(picture.get("lbx", "")), int(picture.get("entry", -1)), int(picture.get("frame", 0)))


func _back_bar(title_text: String) -> Control:
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 8)
	var back := _button("◀ BACK", "Back to the reference's topics")
	back.pressed.connect(func():
		tab = -1
		_set_tab(4))
	bar.add_child(back)
	var title := _heading(title_text)
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	title.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	bar.add_child(title)
	return bar


# ---- Pieces ----

## A list and a text side by side (Tech Review, a reference category).
func _split(holder: Control) -> HBoxContainer:
	var split := HBoxContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	split.add_theme_constant_override("separation", 12)
	holder.add_child(split)
	return split


## A scrolling column in `holder`; returns the column the rows go in.
func _scroll_list(holder: Control) -> VBoxContainer:
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	holder.add_child(scroll)
	var rows := VBoxContainer.new()
	rows.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	rows.add_theme_constant_override("separation", 1)
	scroll.add_child(rows)
	return rows


## The selected item's title and text (scrolls).
func _detail(holder: Control) -> void:
	var box := VBoxContainer.new()
	box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_theme_constant_override("separation", 6)
	holder.add_child(box)
	_detail_title = _heading("")
	_detail_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(_detail_title)
	_detail_text = RichTextLabel.new()
	_detail_text.bbcode_enabled = false
	_detail_text.scroll_active = true
	_detail_text.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_detail_text.add_theme_font_size_override("normal_font_size", 14)
	box.add_child(_detail_text)


## A clickable line (hover highlights it).
func _row(text: String, on_click: Callable) -> Label:
	var row := Label.new()
	row.text = text
	row.mouse_filter = Control.MOUSE_FILTER_STOP
	row.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	row.add_theme_font_size_override("font_size", 15)
	row.mouse_entered.connect(func(): row.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button")))
	row.mouse_exited.connect(func(): row.remove_theme_color_override("font_color"))
	row.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
			on_click.call())
	return row


func _heading(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Name"
	label.text = text
	return label


func _dim(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Dim"
	label.text = text
	return label


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


## The net income chart (owner, 2026-11-15, in place of Draw_Maint_Income_Chart's bars side by
## side): income as one bar, the six costs stacked in one bar beside it in their colours, both on
## one scale, so the net income is the gap between the two tops (green above the costs, red when
## they are higher). The legend gives each part in BC and as a share of the income.
class _IncomeChart extends Control:
	var data := {}

	func set_data(result: Dictionary) -> void:
		data = result
		queue_redraw()

	func _draw() -> void:
		if data.is_empty():
			return
		var font := get_theme_font("font", "Label")
		var text_color := get_theme_color("font_color", "Label")
		var shadow := Color(0, 0, 0, 0.85)
		var net := int(data.get("net_income", 0))
		var top_text := String(data.get("net_income_text", "")).strip_edges()
		draw_string(font, Vector2(0, 18), top_text, HORIZONTAL_ALIGNMENT_CENTER, size.x, 16,
			Color(1.0, 0.35, 0.3) if net < 0 else text_color)
		var bars: Array = data.get("bars", [])
		if bars.is_empty():
			return
		var income := float(int(bars[0].value))
		var costs := 0.0
		for i in range(1, bars.size()):
			costs += float(int(bars[i].value))
		var strip_height := 22.0
		var strips_top := size.y - strip_height * float(bars.size()) - 4.0
		var label_y := strips_top - 8.0
		var base := label_y - 16.0
		var top := 34.0
		var scale := (base - top) / maxf(maxf(income, costs), 1.0)
		var bar_w := minf(size.x * 0.22, 56.0)
		var income_x := size.x * 0.3 - bar_w * 0.5
		var costs_x := size.x * 0.7 - bar_w * 0.5
		_bar(Rect2(income_x, base - income * scale, bar_w, income * scale), BAR_COLORS[0])
		var y := base
		for i in range(1, bars.size()):
			var h := float(int(bars[i].value)) * scale
			if h > 0.0:
				_bar(Rect2(costs_x, y - h, bar_w, h), BAR_COLORS[i])
				y -= h
		# The net income: the gap between the tops, bracketed and labelled.
		var income_top := base - income * scale
		var costs_top := base - costs * scale
		var gap_color := Color(0.3, 0.9, 0.4) if net >= 0 else Color(1.0, 0.35, 0.3)
		if absf(income_top - costs_top) >= 2.0:
			var gx := costs_x + bar_w * 0.5
			draw_dashed_line(Vector2(income_x + bar_w, income_top), Vector2(costs_x + bar_w + 6.0, income_top), gap_color, 1.5, 4.0)
			draw_line(Vector2(gx, income_top), Vector2(gx, costs_top), gap_color, 2.0)
			draw_line(Vector2(gx - 6, income_top), Vector2(gx + 6, income_top), gap_color, 2.0)
			draw_line(Vector2(gx - 6, costs_top), Vector2(gx + 6, costs_top), gap_color, 2.0)
			var mid := (income_top + costs_top) * 0.5 + 5.0
			var net_text := "%+d" % net
			draw_string_outline(font, Vector2(costs_x + bar_w + 8.0, mid), net_text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, 3, shadow)
			draw_string(font, Vector2(costs_x + bar_w + 8.0, mid), net_text, HORIZONTAL_ALIGNMENT_LEFT, -1, 13, gap_color)
		var dim := text_color * Color(1, 1, 1, 0.8)
		draw_string(font, Vector2(income_x - 30, label_y), String(data.get("income_label", "")).strip_edges(), HORIZONTAL_ALIGNMENT_CENTER, bar_w + 60, 11, dim)
		draw_string(font, Vector2(costs_x - 40, label_y), String(data.get("maintenance_label", "")).strip_edges(), HORIZONTAL_ALIGNMENT_CENTER, bar_w + 80, 11, dim)
		for i in bars.size():
			var row_y := strips_top + float(i) * strip_height
			var rect := Rect2(4, row_y + 1, size.x - 8, strip_height - 3)
			draw_rect(rect, BAR_COLORS[i] * Color(1, 1, 1, 0.85))
			draw_rect(Rect2(rect.position, Vector2(rect.size.x, 2)), Color(1, 1, 1, 0.25))
			var value := int(bars[i].value)
			var label := String(bars[i].label).strip_edges()
			if i == 0:
				label = label.trim_prefix("BC").strip_edges() # the game's text follows a number ("5235 BC INCOME")
			var share := "" if i == 0 or income <= 0.0 else "%d%%" % roundi(float(value) * 100.0 / income)
			var parts := [[str(value) + " BC", HORIZONTAL_ALIGNMENT_LEFT, rect.position.x + 6, 74.0], [label, HORIZONTAL_ALIGNMENT_CENTER, rect.position.x + 80, rect.size.x - 130],
				[share, HORIZONTAL_ALIGNMENT_RIGHT, rect.end.x - 50, 44.0]]
			for part: Array in parts:
				draw_string_outline(font, Vector2(float(part[2]), row_y + 16), String(part[0]), part[1], float(part[3]), 13, 3, shadow)
				draw_string(font, Vector2(float(part[2]), row_y + 16), String(part[0]), part[1], float(part[3]), 13, Color.WHITE)

	## A bar, lit on its left as the screen's.
	func _bar(rect: Rect2, color: Color) -> void:
		if rect.size.y <= 0.5:
			return
		draw_rect(rect, color)
		draw_rect(Rect2(rect.position, Vector2(rect.size.x * 0.35, rect.size.y)), color.lightened(0.25))
		draw_rect(Rect2(rect.position, Vector2(rect.size.x, 1.0)), color.lightened(0.5))


## The History Graph (Draw_The_History_Graph, Draw_Histories): the scale's top at the left, the
## time axis's labels under it, each player's line in its colour. The game's 350 columns of time
## fill the width; a point takes `columns_per_point` of them.
class _HistoryGraph extends Control:
	var result := {}

	func _draw() -> void:
		if result.is_empty():
			return
		var font := get_theme_font("font", "Label")
		var text_color := get_theme_color("font_color", "Label")
		var grid_color := text_color * Color(1, 1, 1, 0.18)
		var left := 46.0
		var bottom := size.y - 22.0
		var top := 10.0
		var width := size.x - left - 10.0
		var height := bottom - top
		draw_rect(Rect2(left, top, width, height), Color(0, 0, 0, 0.25))
		for i in 11:
			var y := bottom - height * float(i) / 10.0
			draw_line(Vector2(left, y), Vector2(left + width, y), grid_color)
		var axis: Array = result.get("axis", [])
		for i in axis.size():
			var x := left + width * float(i * 50) / 350.0
			draw_line(Vector2(x, top), Vector2(x, bottom + 4), grid_color)
			draw_string(font, Vector2(x - 30, bottom + 18), String(axis[i].text), HORIZONTAL_ALIGNMENT_CENTER, 60, 12, text_color)
		draw_string(font, Vector2(0, top + 12), str(int(result.get("top", 25))), HORIZONTAL_ALIGNMENT_RIGHT, left - 6, 12, text_color)
		draw_string(font, Vector2(0, bottom), "0", HORIZONTAL_ALIGNMENT_RIGHT, left - 6, 12, text_color)
		var scale_top := float(maxi(int(result.get("top", 25)), 1))
		var step := float(int(result.get("columns_per_point", 1)))
		var colors: Array = preload("../galaxy.gd").PLAYER_COLORS
		var players: Array = result.get("players", [])
		for pi in range(players.size() - 1, -1, -1): # the player's own line last, on top
			var p: Dictionary = players[pi]
			var points: Array = p.points
			if points.size() < 2:
				continue
			var line := PackedVector2Array()
			for t in points.size():
				var x := left + width * minf(float(t) * step / 350.0, 1.0)
				var y := bottom - height * minf(float(points[t]) / scale_top, 1.0)
				line.append(Vector2(x, y))
			draw_polyline(line, Color(0, 0, 0, 0.6), 3.0, true)
			draw_polyline(line, colors[int(p.color) % 8], 1.6, true)
