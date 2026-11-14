extends Control
## The scene window (owner, 2026-11-14): the legacy full-screen scenes (GNN news, then diplomacy)
## as a window over the galaxy, at the scene's 4:3, in the theme's pane frame. It opens like a
## monitor switching on: a thin framed line in the centre stretches to the full height, then the
## picture fades in; closing runs it backwards.
##
## The game sends each page as a "scene" popup (events.cpp: the backdrop animation, the story's
## picture, the text and where the legacy screen printed it); a click anywhere answers it. A
## "scene_end" popup closes the window, and is answered when the closing has played.

signal page_done(id: int)

const SCENE_SIZE := Vector2(640, 480) # the legacy scene, in its own pixels
const FRAME_TIME := 0.11 # the legacy loop's Release_Time(2): two 55 ms ticks a frame
const OPEN_TIME := 0.28
const FADE_TIME := 0.22
const ART_SCALE := 2 # the animations' frames come at 2x (sprites.cpp "LBX:2")

var art: Node # art.gd

var _frame: Panel # the themed frame
var _stage: Control # the scene's picture and text, clipped to the frame
var _backdrop: TextureRect
var _picture: TextureRect
var _text: Label
var _request := {}
var _id := -1
var _backdrop_key := ""
var _frame_index := 0
var _frame_clock := 0.0
var _tween: Tween
var _open := false


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	_frame = Panel.new()
	_frame.theme_type_variation = &"Pane"
	_frame.mouse_filter = Control.MOUSE_FILTER_STOP
	_frame.clip_contents = true
	_frame.gui_input.connect(_on_input)
	add_child(_frame)
	_stage = Control.new()
	_stage.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.clip_contents = true
	_frame.add_child(_stage)
	_backdrop = _texture_rect()
	_backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	_stage.add_child(_backdrop)
	_picture = _texture_rect()
	_stage.add_child(_picture)
	_text = Label.new()
	_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_text.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_text.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_text.add_theme_color_override("font_color", Color(0.95, 0.93, 0.85))
	_text.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_text.add_theme_constant_override("outline_size", 6)
	_stage.add_child(_text)
	hide()
	if art:
		art.loaded.connect(func(_key): _show_art())
	get_viewport().size_changed.connect(_layout)


func _texture_rect() -> TextureRect:
	var rect := TextureRect.new()
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_SCALE
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return rect


## A page of the scene: opens the window first if it isn't up.
func show_page(id: int, request: Dictionary) -> void:
	_id = id
	_request = request
	var backdrop: Dictionary = request.get("backdrop", {})
	var key := "%s:%d/%d" % [backdrop.get("lbx", ""), ART_SCALE, int(backdrop.get("entry", -1))]
	if key != _backdrop_key:
		_backdrop_key = key
		_frame_index = 0
	_text.text = String(request.get("text", ""))
	_layout()
	_show_art()
	if not _open:
		_open = true
		show()
		_play_open()


## The scene is over: the window closes, then `done` runs (the game's answer).
func close(done: Callable) -> void:
	if not _open:
		done.call()
		return
	_open = false
	if _tween:
		_tween.kill()
	var full := _frame_rect()
	_tween = create_tween()
	_tween.tween_property(_stage, "modulate:a", 0.0, FADE_TIME)
	_tween.tween_property(_frame, "position:y", full.get_center().y - _line_height() * 0.5, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.parallel().tween_property(_frame, "size:y", _line_height(), OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_callback(func():
		hide()
		_backdrop_key = ""
		done.call())


func is_open() -> bool:
	return _open


func _play_open() -> void:
	if _tween:
		_tween.kill()
	var full := _frame_rect()
	_frame.position = Vector2(full.position.x, full.get_center().y - _line_height() * 0.5)
	_frame.size = Vector2(full.size.x, _line_height())
	_stage.modulate.a = 0.0
	_tween = create_tween()
	_tween.tween_property(_frame, "position:y", full.position.y, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.parallel().tween_property(_frame, "size:y", full.size.y, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(_stage, "modulate:a", 1.0, FADE_TIME)


## The frame's thickness: the opening starts from the frame alone, as a line.
func _line_height() -> float:
	var box := _frame.get_theme_stylebox("panel")
	return maxf(8.0, box.get_margin(SIDE_TOP) + box.get_margin(SIDE_BOTTOM) + 4.0) if box else 12.0


## The frame's place when open: centred, the stage at 4:3 as large as fits in most of the screen.
func _frame_rect() -> Rect2:
	var area := get_viewport_rect().size
	var box := _frame.get_theme_stylebox("panel")
	var pad := Vector2(box.get_margin(SIDE_LEFT) + box.get_margin(SIDE_RIGHT), box.get_margin(SIDE_TOP) + box.get_margin(SIDE_BOTTOM)) if box else Vector2.ZERO
	var room := area * 0.86 - pad
	var k := minf(room.x / SCENE_SIZE.x, room.y / SCENE_SIZE.y)
	var stage := (SCENE_SIZE * k).floor()
	var size_all := stage + pad
	return Rect2(((area - size_all) * 0.5).floor(), size_all)


func _layout() -> void:
	if _frame == null:
		return
	var full := _frame_rect()
	var box := _frame.get_theme_stylebox("panel")
	var inset := Vector2(box.get_margin(SIDE_LEFT), box.get_margin(SIDE_TOP)) if box else Vector2.ZERO
	var pad := Vector2(box.get_margin(SIDE_LEFT) + box.get_margin(SIDE_RIGHT), box.get_margin(SIDE_TOP) + box.get_margin(SIDE_BOTTOM)) if box else Vector2.ZERO
	if not (_tween and _tween.is_running()):
		_frame.position = full.position
		_frame.size = full.size
	_frame.position.x = full.position.x
	_frame.size.x = full.size.x
	# The stage keeps its full size and stays centred in the frame while the frame opens (the
	# frame clips it), so the picture is revealed from the middle.
	_stage.size = full.size - pad
	_stage.position = Vector2(inset.x, inset.y)
	var k := _stage.size.x / SCENE_SIZE.x
	var box_info: Array = _request.get("text_box", [55, 445, 550])
	var text_width := float(box_info[2]) * k
	_text.add_theme_font_size_override("font_size", maxi(12, roundi(13.0 * k)))
	_text.size = Vector2(text_width, 90.0 * k)
	_text.position = Vector2(float(box_info[0]) * k, float(box_info[1]) * k - _text.size.y * 0.5)


func _process(delta: float) -> void:
	if not visible:
		return
	# While opening, the stage stays centred on the frame's middle.
	var box := _frame.get_theme_stylebox("panel")
	var top := box.get_margin(SIDE_TOP) if box else 0.0
	var full := _frame_rect()
	_stage.position.y = top - (_frame.position.y - full.position.y)
	_frame_clock += delta
	if _frame_clock < FRAME_TIME:
		return
	_frame_clock = 0.0
	var backdrop: Dictionary = _request.get("backdrop", {})
	var s: Dictionary = art.sprite("%s:%d" % [backdrop.get("lbx", ""), ART_SCALE], int(backdrop.get("entry", -1))) if art else {}
	if s.is_empty():
		return
	_frame_index += 1
	if _frame_index >= s.frames.size():
		_frame_index = mini(int(backdrop.get("loop_from", 0)), s.frames.size() - 1)
	_backdrop.texture = s.frames[_frame_index]


func _show_art() -> void:
	if art == null or _request.is_empty():
		return
	var k := _stage.size.x / SCENE_SIZE.x
	var backdrop: Dictionary = _request.get("backdrop", {})
	var s: Dictionary = art.sprite("%s:%d" % [backdrop.get("lbx", ""), ART_SCALE], int(backdrop.get("entry", -1)))
	if not s.is_empty():
		_backdrop.texture = s.frames[clampi(_frame_index, 0, s.frames.size() - 1)]
	var picture: Dictionary = _request.get("picture", {})
	_picture.visible = not picture.is_empty()
	if picture.is_empty():
		return
	var p: Dictionary = art.sprite(String(picture.lbx), int(picture.entry))
	if p.is_empty():
		_picture.texture = null
		return
	_picture.texture = p.frames[0]
	var origin: Vector2 = p.origins[0]
	_picture.position = (Vector2(float(picture.get("x", 0)), float(picture.get("y", 0))) + origin) * k
	_picture.size = Vector2(p.frames[0].get_size()) / float(p.scale) * k


func _on_input(event: InputEvent) -> void:
	var click := event as InputEventMouseButton
	if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT and _open and _id >= 0:
		if _tween and _tween.is_running():
			return # the window is still opening
		var id := _id
		_id = -1
		_frame.accept_event()
		page_done.emit(id)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and _open and _id >= 0 and key and key.pressed and key.keycode in [KEY_SPACE, KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE]:
		var id := _id
		_id = -1
		get_viewport().set_input_as_handled()
		page_done.emit(id)
