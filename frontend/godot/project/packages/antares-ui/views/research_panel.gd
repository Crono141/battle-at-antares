extends "drop_panel.gd"
## Research (WP3.3g, owner: the research figure drops this down and it stays in native): the
## change-research screen's choices from the `research` query, the eight areas in its order, each
## with the field it would research, the RP still to go and the applications on offer. The one
## being researched is armed, with the original's branching arrow pointing at the chosen
## application, or at all of them where the field gives every one (creative races, and a few
## fields for everyone); another sends `choose_research` (a new field loses the breakthrough).

signal command(dict: Dictionary)

const Themes := preload("../themes.gd")
const AREA_WIDTH := 340.0

@onready var areas_box: GridContainer = $Box/Areas
@onready var progress: Label = $Box/Header/Titles/Progress

var _research_text := ""


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)


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
	for area: Dictionary in result.areas:
		areas_box.add_child(_area(area, current_field, current_app))
	progress.text = _research_text


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
	for app: Dictionary in apps:
		var id := int(app.app)
		var b := Button.new()
		b.theme_type_variation = &"Toggle"
		b.toggle_mode = true
		b.focus_mode = Control.FOCUS_NONE
		b.text = String(app.name)
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.custom_minimum_size.y = 28
		b.add_theme_font_size_override("font_size", 13)
		b.tooltip_text = "Research %s: all of its applications come with it" % area.field_name if gives_all \
			else "Research this application (one per field)"
		b.set_pressed_no_signal(is_current and (gives_all or id == current_app))
		b.pressed.connect(func():
			command.emit({"verb": "choose_research", "field": field, "app": id, "_reply_to": "_chosen"}))
		Themes.glowing(b)
		list.add_child(b)
		branch.rows.append(b)
		branch.pointed.append(is_current and (gives_all or id == current_app))
	branch.color = get_theme_color("font_pressed_color", "Button")
	list.resized.connect(branch.queue_redraw)
	return panel


func _chosen(result: Dictionary) -> void:
	if result.get("ok", false):
		refresh()


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return digits + out
