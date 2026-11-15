extends "drop_panel.gd"
## Research (WP3.3g, owner: the research figure drops this down and it stays in native): the
## change-research screen's choices from the `research` query, the eight areas in its order, each
## with the field it would research, the RP still to go and the applications on offer. The one
## being researched is lit, with the original's branching arrow pointing at the chosen
## application, or at all of them where the field gives every one (creative races, and a few
## fields for everyone); the arrow follows the pointer over the applications, showing what a click
## would give. A click sends `choose_research` (a new field loses the breakthrough).

signal command(dict: Dictionary)
## Pick mode (the research window, owner 2026-11-14): a click answers the game's question
## instead of sending `choose_research`; no close button. -1, -1: nothing left to choose.
signal picked(field: int, app: int)

var pick_mode := false

const Themes := preload("../themes.gd")
const AREA_WIDTH := 340.0

@onready var areas_box: GridContainer = $Fit/Box/Areas
@onready var progress: Label = $Fit/Box/Header/Titles/Progress

var _research_text := ""


func _ready() -> void:
	$Fit/Box/Header/Close.pressed.connect(close)


## The bar's research figure, under the title ("3,358 RP   11 turns").
func set_progress(text: String) -> void:
	_research_text = text
	progress.text = text


func refresh() -> void:
	command.emit({"verb": "query", "what": "research", "_reply_to": "show_research"})


func show_research(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	for child in areas_box.get_children():
		child.queue_free()
	var current_field := int(result.field)
	var current_app := int(result.app)
	var choices := 0
	for area: Dictionary in result.areas:
		areas_box.add_child(_area(area, current_field, current_app))
		choices += (area.get("apps", []) as Array).size()
	progress.text = _research_text
	$Fit/Box/Header/Close.visible = not pick_mode
	if pick_mode and choices == 0:
		picked.emit.call_deferred(-1, -1)


func _area(area: Dictionary, current_field: int, current_app: int) -> Control:
	var field := int(area.field)
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"RowOpen" if field != 0 and field == current_field else &"Row"
	panel.custom_minimum_size.x = AREA_WIDTH
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 2)
	panel.add_child(box)
	var head := HBoxContainer.new()
	box.add_child(head)
	var area_label := Label.new()
	area_label.theme_type_variation = &"Section"
	area_label.text = String(area.area).to_upper()
	area_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(area_label)
	if field == 0:
		var done := Label.new()
		done.theme_type_variation = &"Dim"
		done.text = "All researched"
		box.add_child(done)
		return panel
	var cost := Label.new()
	cost.theme_type_variation = &"Dim"
	cost.text = "%s RP" % _thousands(int(area.cost))
	head.add_child(cost)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(area.field_name)
	box.add_child(name_label)
	var apps: Array = area.get("apps", [])
	if apps.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = String(area.get("none", ""))
		box.add_child(none)
		return panel
	# The applications beside the branching arrow: in the field being researched it points at the
	# chosen one, or at all of them where the field gives every one (creative races).
	var gives_all: bool = area.get("all", false)
	var is_current := field == current_field
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 4)
	box.add_child(line)
	var branch: Control = preload("research_branch.gd").new()
	line.add_child(branch)
	var list := VBoxContainer.new()
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	list.add_theme_constant_override("separation", 2)
	line.add_child(list)
	branch.gives_all = gives_all
	var chosen: Array[bool] = []
	var picked_color := get_theme_color("font_pressed_color", "ListItem")
	for app: Dictionary in apps:
		var id := int(app.app)
		# Clickable text, not buttons (owner): the chosen one lit, the arrow following the pointer.
		var b := Button.new()
		b.flat = true
		b.theme_type_variation = &"ListItem"
		b.focus_mode = Control.FOCUS_NONE
		b.text = String(app.name)
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.custom_minimum_size.y = 26
		b.add_theme_font_size_override("font_size", 15)
		var on := is_current and (gives_all or id == current_app)
		if on:
			for state in ["font_color", "font_hover_color", "font_pressed_color"]:
				b.add_theme_color_override(state, picked_color)
		var row := list.get_child_count()
		b.mouse_entered.connect(func(): branch.hover(row))
		b.button_down.connect(func(): branch.hover(row)) # held, it still shows what it would give
		b.mouse_exited.connect(func():
			if not b.is_pressed():
				branch.hover(-1))
		b.button_up.connect(func():
			if not b.is_hovered():
				branch.hover(-1))
		# Picked: the choice is sent and the drop-down goes back up (owner, 2026-11-13).
		b.pressed.connect(func():
			if _flashing:
				return
			if not pick_mode:
				command.emit({"verb": "choose_research", "field": field, "app": id, "_reply_to": "_chosen"})
			# The pick flashes three times in half a second, then the drop-down goes up (owner).
			var lit_rows: Array = branch.rows if gives_all else [b]
			_flashing = true
			var flash := create_tween()
			flash.tween_method(func(t: float):
				var alpha := 0.15 + 0.85 * (0.5 + 0.5 * cos(t * 3.0 * TAU))
				for lit in lit_rows:
					if is_instance_valid(lit):
						(lit as Control).modulate.a = alpha, 0.0, 1.0, 0.5)
			flash.tween_callback(func():
				_flashing = false
				if pick_mode:
					picked.emit(field, id)
				else:
					close()))
		# A right click: the application's help, as the game's research screen gives it.
		var app_name := String(app.name)
		var help := String(app.get("description", ""))
		b.gui_input.connect(func(event: InputEvent):
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
				_show_info(b, app_name, help)
				b.accept_event())
		list.add_child(b)
		branch.rows.append(b)
		# Drawn again once laid out: drawn before, every arrowhead sat on the first row (owner,
		# 2026-11-13: one arrow until hovered, after the drop-down reopened).
		b.item_rect_changed.connect(branch.queue_redraw)
		chosen.append(on)
	branch.set_chosen(chosen)
	branch.color = picked_color
	list.resized.connect(branch.queue_redraw)
	return panel


var _info_popup: Control


func _show_info(at: Control, title: String, text: String) -> void:
	_info_popup = preload("info_popup.gd").open(self, at, title, text)


var _flashing := false # a pick flashing before the drop-down goes up: no rebuild meanwhile


func _chosen(result: Dictionary) -> void:
	if result.get("ok", false) and not _flashing:
		refresh()


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return digits + out
