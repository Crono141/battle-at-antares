extends Control
## The Hall of Fame (WP3.13, D30): the original picture (SCORE.LBX 15, the hall with its two
## statues) as a 4:3 window in the middle of the title backdrop, the table drawn over it at the
## legacy screen's places (score.cpp: rows at y 131 + 29 i, name 164, race 292, difficulty 364,
## score right-aligned at 500, in 640x480 units). The picture is kept whole, flanks included
## (owner, 2026-11-27: the art that flanks it was the worry); the widescreen sides show the
## dimmed title scene. The end of a game's new entry blinks. A click or Esc closes.

signal command(dict: Dictionary)
signal closed

var art: Node

var _rows: Array = []
var _flash := -1
var _blink := true
var _blink_time := 0.0
var _reset_armed := false
var _reset: Button
var _close_x: Button
var _closing := false
## Its arrival (owner, 2026-11-27): the picture slides down from above the screen, and back up
## when it closes; 1 is out of sight above, 0 in place.
var _drop := 1.0
var _slide: Tween
const SLIDE_IN := 0.45
const SLIDE_OUT := 0.3


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	_reset = Button.new()
	_reset.text = "RESET TABLE"
	_reset.focus_mode = Control.FOCUS_NONE
	_reset.tooltip_text = "Replace the table with the game's made-up one (Alt+C in the original)"
	_reset.pressed.connect(_on_reset)
	add_child(_reset)
	# A plain X in the picture's top right corner closes it (as Esc or a click outside it).
	_close_x = Button.new()
	_close_x.text = "X"
	_close_x.focus_mode = Control.FOCUS_NONE
	_close_x.tooltip_text = "Close (Esc)"
	_close_x.custom_minimum_size = Vector2(52, 52)
	_close_x.add_theme_font_size_override("font_size", 24)
	_close_x.pressed.connect(_leave)
	add_child(_close_x)
	resized.connect(_layout)


func open() -> void:
	_closing = false
	_reset_armed = false
	_reset.text = "RESET TABLE"
	show()
	_drop = 1.0
	if _slide:
		_slide.kill()
	_slide = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_slide.tween_method(_set_drop, 1.0, 0.0, SLIDE_IN)
	_layout()
	command.emit({"verb": "query", "what": "hall_of_fame", "_reply_to": "show_table"})


func show_table(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	_rows = result.get("rows", [])
	_flash = int(result.get("flash", -1))
	queue_redraw()


func close() -> void:
	hide()
	closed.emit()


## The picture's rectangle: 4:3, as tall as the screen allows less a margin (above the screen by
## its slide).
func picture_rect() -> Rect2:
	var h := minf(size.y * 0.96, size.x * 0.96 * 0.75)
	var w := h / 0.75
	var y := (size.y - h) * 0.5
	return Rect2((size.x - w) * 0.5, y - (y + h + 8.0) * _drop, w, h)


func _set_drop(value: float) -> void:
	_drop = value
	_layout()


func _layout() -> void:
	var r := picture_rect()
	_close_x.reset_size()
	var inset := r.size.y * 0.02
	_close_x.position = Vector2(r.end.x - _close_x.size.x - inset, r.position.y + inset)
	_reset.reset_size()
	_reset.position = Vector2(r.end.x + 12, r.end.y - _reset.size.y) if r.end.x + 12 + _reset.size.x < size.x \
		else Vector2(r.end.x - _reset.size.x - 8, r.end.y + 4)
	queue_redraw()


func _process(delta: float) -> void:
	if not visible or _flash < 0:
		return
	_blink_time += delta
	if _blink_time > 0.33: # the legacy entry blinks every 6 ticks
		_blink_time = 0.0
		_blink = not _blink
		queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55 * (1.0 - _drop))) # the title scene, dimmed as it arrives
	var r := picture_rect()
	var s := r.size.x / 640.0
	var tex: Texture2D = art.texture("SCORE.LBX", 0x0F, 0) if art else null
	if tex == null:
		if art:
			art.loaded.connect(func(_k: String): queue_redraw(), CONNECT_ONE_SHOT)
		draw_rect(r, Color(0.06, 0.06, 0.07))
	else:
		draw_texture_rect(tex, r, false)
	var font := get_theme_default_font()
	var font_size := int(round(14.0 * s))
	var ink := Color(0.80, 0.86, 0.95)
	var bright := Color(1.0, 0.95, 0.55)
	for i in mini(_rows.size(), 10):
		var row: Dictionary = _rows[i]
		var is_flash := int(row.get("record", -1)) == _flash
		if is_flash and not _blink:
			continue
		var color := bright if is_flash else ink
		var y := r.position.y + (131.0 + 29.0 * i + 16.0) * s
		var x := r.position.x
		draw_string(font, Vector2(x + 164.0 * s, y), str(row.get("name", "")), HORIZONTAL_ALIGNMENT_LEFT, 120.0 * s, font_size, color)
		draw_string(font, Vector2(x + 292.0 * s, y), str(row.get("race", "")), HORIZONTAL_ALIGNMENT_LEFT, 70.0 * s, font_size, color)
		draw_string(font, Vector2(x + 364.0 * s, y), str(row.get("difficulty_name", "")), HORIZONTAL_ALIGNMENT_LEFT, 86.0 * s, font_size, color)
		var score := str(int(row.get("score", 0)))
		var width := font.get_string_size(score, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		draw_string(font, Vector2(x + 500.0 * s - width, y), score, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)


func _on_reset() -> void:
	if not _reset_armed:
		_reset_armed = true
		_reset.text = "CLICK AGAIN TO RESET"
		return
	_reset_armed = false
	_reset.text = "RESET TABLE"
	command.emit({"verb": "reset_hall_of_fame", "_reply_to": "_after_reset"})


func _after_reset(_result: Dictionary) -> void:
	command.emit({"verb": "query", "what": "hall_of_fame", "_reply_to": "show_table"})


func _leave() -> void:
	if _closing:
		return
	_closing = true
	# Back up out of sight, then the game leaves the screen.
	if _slide:
		_slide.kill()
	_slide = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_slide.tween_method(_set_drop, _drop, 1.0, SLIDE_OUT)
	_slide.tween_callback(func(): command.emit({"verb": "close_hall_of_fame"}))


func _gui_input(event: InputEvent) -> void:
	var press := event as InputEventMouseButton
	if press and press.pressed and press.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
		if not _reset.get_rect().has_point(press.position) and not _close_x.get_rect().has_point(press.position):
			_leave()
			accept_event()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if not visible or key == null or not key.pressed or key.echo:
		return
	if key.keycode == KEY_ESCAPE or key.keycode == KEY_ENTER or key.keycode == KEY_SPACE:
		_leave()
		get_viewport().set_input_as_handled()
	elif key.keycode == KEY_C and key.alt_pressed:
		_on_reset() # Alt+C, as in the original (twice: it asks first here)
		get_viewport().set_input_as_handled()
