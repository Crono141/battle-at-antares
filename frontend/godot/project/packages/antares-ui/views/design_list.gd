extends PanelContainer
## Ship Design (WP3.3f): the player's design slots from the `designs` query, each with its picture,
## hull and cost, sliding out from the right edge. Edit opens the game's designer on that slot
## (`open` screen 3; the legacy screen for now). The original has a fixed set of slots and no
## "new design": a design is made by editing a slot over.

signal command(dict: Dictionary)
signal closed

const SLIDE_TIME := 0.25
const PICTURE_SIZE := Vector2(96, 64)

@onready var rows: VBoxContainer = $Box/Scroll/Rows

var art: Node
var _tween: Tween
var _shown_right := 0.0 # offset_right when open: a flyout keeps its right edge by the tabs


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()
	elif what == NOTIFICATION_RESIZED and visible and not (_tween and _tween.is_running()):
		# Rows laid out after the slide (or a refresh) widened it: keep the right edge by the tabs.
		position.x = get_parent_area_size().x + _shown_right - size.x


## Beside the tabs, from under the top bar to the bottom (WP3.3g).
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_right = -((inset - 4 if inset > 0 else 4) + 34 + gap)
	offset_left = offset_right - 472.0
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -(inset + maxi(gap, 8))
	_shown_right = offset_right


func open() -> void:
	command.emit({"verb": "query", "what": "designs", "_reply_to": "show_designs"})


func show_designs(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	for child in rows.get_children():
		child.queue_free()
	for design: Dictionary in result.designs:
		rows.add_child(_row(design))
	if not visible:
		_slide(true)


func close() -> void:
	if visible:
		_slide(false)
		closed.emit()


func _row(design: Dictionary) -> Control:
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"Row"
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	panel.add_child(row)
	var picture: TextureRect = preload("sprite_rect.gd").new()
	picture.custom_minimum_size = PICTURE_SIZE
	picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	row.add_child(picture)
	picture.bind(art)
	picture.show_sprite("SHIPS.LBX", int(design.picture))
	var names := VBoxContainer.new()
	names.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	names.alignment = BoxContainer.ALIGNMENT_CENTER
	var name_label := Label.new()
	name_label.text = design.name
	name_label.theme_type_variation = &"Name"
	names.add_child(name_label)
	var hull := Label.new()
	hull.text = design.hull_name
	hull.theme_type_variation = &"Dim"
	names.add_child(hull)
	row.add_child(names)
	var cost := TextureRect.new()
	cost.texture = preload("../themes.gd").icon("industry", 22) # the bar's pick and hammer
	cost.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
	cost.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	cost.tooltip_text = "Production cost"
	row.add_child(cost)
	var cost_label := Label.new()
	cost_label.text = str(int(design.cost))
	cost_label.add_theme_font_size_override("font_size", 18)
	cost_label.custom_minimum_size.x = 56
	cost_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	row.add_child(cost_label)
	var edit := Button.new()
	edit.text = "EDIT"
	edit.custom_minimum_size = Vector2(72, 30)
	edit.focus_mode = Control.FOCUS_NONE
	edit.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var slot := int(design.slot)
	edit.pressed.connect(func(): command.emit({"verb": "open", "screen": Screens.DESIGN, "slot": slot}))
	row.add_child(edit)
	return panel


## Slides in from (or out to) the right edge.
func _slide(opening: bool) -> void:
	if _tween:
		_tween.kill()
	var parent_width := get_parent_area_size().x
	var shown_x := parent_width + _shown_right - size.x # right-aligned, however wide its rows
	var hidden_x := parent_width
	if opening:
		show()
		position.x = hidden_x
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	if opening:
		_tween.finished.connect(func(): position.x = get_parent_area_size().x + _shown_right - size.x)
	_tween.tween_property(self, "position:x", shown_x if opening else hidden_x, SLIDE_TIME)
	if not opening:
		_tween.tween_callback(hide)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
