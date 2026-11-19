extends Control
## The scene window (owner, 2026-11-14): the legacy full-screen scenes (GNN news, diplomacy) as a
## window over the galaxy, at the scene's 4:3, in the theme's pane frame. It opens like a monitor
## switching on: a thin framed line in the centre stretches to the full height, then the picture
## fades in; closing runs it backwards.
##
## What it shows is a scene state (events.cpp, dip_scrn_main.cpp):
##   backdrop: {lbx, entry, loop_from} an animation looping from that frame, or {.., frame} a still;
##   intro:    {lbx, entry, from} played once over the backdrop (the ambassador appearing);
##   actor:    {lbx, entry, loop_from | frame} drawn at its frames' origins after the intro;
##   picture:  {lbx, entry, x, y} a still at a 1x position (the GNN story);
##   ground:   {lbx, entry, frame} a still over the backdrop and under the intro (the colony
##             landing's planet under the pods);
##   frame_ms: the animations' frame time (default 110: two 55 ms ticks; the landing uses 55);
##   title, color: the heading at the top, in a player's colour;
##   text, text_box [x, centre y, width, height?]: the paragraph, where the legacy screen printed
##             it; text_align "left" for a block printed from its left edge (the planet's data);
##   list_at [x, y, width]: where the scene's lists sit (the dialog is placed there, frameless);
##   layers:   [{lbx, entry, loop_from, close}] more animations over the actor, each from frame 0
##             (the research room's light, hologram and monitor); `close` says what "closing" does
##             to it: "hide" at once, "reverse" plays its frames back to the first and hides it,
##             "after" hides it when the reversing ones are done; x, y place it (1x pixels, as
##             animate::Draw at x, y: the council's members), "frame" holds it on one frame;
##   monitor:  {title, text, box [x, y, w, h], after_frame} text on the monitor (the "reverse"
##             layer) once that has reached `after_frame`;
##   closing:  the layers close (the research picker comes next; `arrived` when done);
##   text_color: "science" for the research room's green;
##   open_style: "slide" drops the window from the top instead of the monitor opening.
## The frontend can give the window's rect (`custom_rect`, the research window beside the picker).
## A "scene" popup is a page: a click (or Space, Enter, Esc) answers it. Other requests carry the
## state as "scene_state" and keep the window up behind them. "scene_end" closes it.

signal page_done(id: int)
## The window is open and the intro (the ambassador's arrival) is over or skipped: a list over
## the scene shows from then (owner, 2026-11-14).
signal arrived

const SCENE_SIZE := Vector2(640, 480) # the legacy scene, in its own pixels
const FRAME_TIME := 0.11 # the legacy loop's Release_Time(2): two 55 ms ticks a frame
const OPEN_TIME := 0.28
const FADE_TIME := 0.22
const ART_SCALE := 2 # the animations' frames come at 2x (sprites.cpp "LBX:2")
const SLIDE_TIME := 0.32
const SCIENCE_HIGH := Color(0.55, 1.0, 0.5) # the monitor's text (science_high_color)
const SCIENCE_NORMAL := Color(0.35, 0.9, 0.35) # the room's line under the scene

var art: Node # art.gd

var _frame: Panel # the themed frame
var _stage: Control # the scene's picture and text, clipped to the frame
var _black: ColorRect # under the art: its see-through pixels were black on the legacy page
var _backdrop: TextureRect
var _ground: TextureRect
var _intro: TextureRect
var _actor: TextureRect
var _picture: TextureRect
var _title: Label
var _text: Label
var _state := {}
var _id := -1 # the page waiting for a click, or -1
var _clock := 0.0
var _backdrop_frame := 0
var _intro_frame := -1 # -1: the intro is over (or there is none)
var _intro_key := ""
var _actor_frame := 0
var _used := {} # art keys this scene loaded ("LBX:2/entry"), forgotten when it closes
var _tween: Tween
var _open := false
var _was_arriving := false
## The window's place when set (the research window); centred at 4:3 otherwise.
var custom_rect := Rect2()
var _layers_box: Control
var _layers: Array = [] # {key, rect, frame, reversing, hidden, spec}
var _closing := false
var _monitor: VBoxContainer # the monitor's text (the research room)
var _monitor_title: Label
var _monitor_text: Label
var _monitor_shown := ""


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
	_black = ColorRect.new()
	_black.color = Color.BLACK
	_black.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.add_child(_black)
	_backdrop = _texture_rect()
	_stage.add_child(_backdrop)
	_ground = _texture_rect()
	_stage.add_child(_ground)
	_intro = _texture_rect()
	_stage.add_child(_intro)
	_actor = _texture_rect()
	_stage.add_child(_actor)
	_layers_box = Control.new()
	_layers_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.add_child(_layers_box)
	_monitor = VBoxContainer.new()
	_monitor.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_monitor.clip_contents = true
	_monitor.add_theme_constant_override("separation", 2)
	_stage.add_child(_monitor)
	_monitor_title = _label()
	_monitor_title.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_monitor.add_child(_monitor_title)
	_monitor_text = _label()
	_monitor_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_monitor.add_child(_monitor_text)
	for monitor_label: Label in [_monitor_title, _monitor_text]:
		monitor_label.add_theme_color_override("font_color", SCIENCE_HIGH)
		monitor_label.add_theme_constant_override("outline_size", 3)
		monitor_label.add_theme_color_override("font_outline_color", Color(0, 0.12, 0, 0.9))
	_picture = _texture_rect()
	_stage.add_child(_picture)
	_title = _label()
	_stage.add_child(_title)
	_text = _label()
	_text.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
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


func _label() -> Label:
	var label := Label.new()
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_color_override("font_color", Color(0.95, 0.93, 0.85))
	label.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	label.add_theme_constant_override("outline_size", 6)
	return label


## A page of the scene, waiting for a click: opens the window first if it isn't up.
func show_page(id: int, request: Dictionary) -> void:
	show_state(request.get("scene_state", request))
	_id = id


## The scene as it is now (a request's "scene_state"), behind whatever asks over it.
func show_state(state: Dictionary) -> void:
	var intro: Dictionary = state.get("intro", {})
	var new_scene: bool = _state.get("scene", "") != state.get("scene", "") or \
		_state.get("backdrop", {}).get("entry", -1) != state.get("backdrop", {}).get("entry", -1)
	_state = state
	_id = -1
	if new_scene:
		_backdrop_frame = int(state.get("backdrop", {}).get("frame", 0))
		_actor_frame = 0
		_intro_frame = int(intro.get("from", 0)) if not intro.is_empty() else -1
		_intro_key = _key(intro) if not intro.is_empty() else ""
	if new_scene:
		_clear_layers()
		_closing = false
		_monitor_shown = ""
	_sync_layers(state.get("layers", []))
	if state.get("closing", false) and not _closing:
		_start_closing()
	_text.text = String(state.get("text", ""))
	_text.visible = not _closing and not arriving() # the research picker covers the room then
	_text.add_theme_color_override("font_color", SCIENCE_NORMAL if state.get("text_color", "") == "science" else Color(0.95, 0.93, 0.85))
	var monitor: Dictionary = state.get("monitor", {})
	_monitor_title.text = String(monitor.get("title", ""))
	_monitor_text.text = String(monitor.get("text", ""))
	if _monitor_title.text + _monitor_text.text != _monitor_shown:
		# A new page: the text comes up as the legacy monitor's scramble settles.
		_monitor_shown = _monitor_title.text + _monitor_text.text
		_monitor_text.visible_ratio = 0.0
		create_tween().tween_property(_monitor_text, "visible_ratio", 1.0, 0.45)
	_title.text = String(state.get("title", ""))
	var color := int(state.get("color", -1))
	_title.add_theme_color_override("font_color", preload("../galaxy.gd").PLAYER_COLORS[color % 8] if color >= 0 else Color(0.95, 0.93, 0.85))
	_layout()
	_show_art()
	if not _open:
		_open = true
		show()
		_play_open()


## Where a list over the scene goes (its "list_at"), in this window's coordinates; INF without.
func list_position() -> Vector2:
	var at: Array = _state.get("list_at", [])
	if at.is_empty() or not _open:
		return Vector2.INF
	var k := _stage.size.x / SCENE_SIZE.x
	return _frame_rect().position + _stage.position + Vector2(float(at[0]), float(at[1])) * k


## How wide a list over the scene is (its "list_at" width), in this window's pixels; 0 without.
func list_width() -> float:
	var at: Array = _state.get("list_at", [])
	if at.size() < 3 or not _open:
		return 0.0
	return float(at[2]) * _stage.size.x / SCENE_SIZE.x


## The scene is over: the window closes, then `done` runs (the game's answer).
func close(done: Callable) -> void:
	if not _open:
		done.call()
		return
	_open = false
	_id = -1
	if _tween:
		_tween.kill()
	var full := _frame_rect()
	_tween = create_tween()
	if _slides():
		_tween.tween_property(_frame, "position:y", -full.size.y - 8.0, SLIDE_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	else:
		_tween.tween_property(_stage, "modulate:a", 0.0, FADE_TIME)
		_tween.tween_property(_frame, "position:y", full.get_center().y - _line_height() * 0.5, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
		_tween.parallel().tween_property(_frame, "size:y", _line_height(), OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_callback(func():
		hide()
		_state = {}
		custom_rect = Rect2()
		_clear_layers()
		_closing = false
		_monitor_shown = ""
		for texture_rect in [_backdrop, _ground, _intro, _actor, _picture]:
			texture_rect.texture = null
		# The scene's frames are large (tens of MB at 2x): let them go.
		for key: String in _used:
			var at := key.rfind("/")
			art.forget(key.left(at), int(key.substr(at + 1)))
		_used.clear()
		done.call())


func is_open() -> bool:
	return _open


## Still opening, or the intro still playing.
func arriving() -> bool:
	return _open and ((_tween != null and _tween.is_running()) or (_intro_frame >= 0 and not _state.get("intro", {}).is_empty()) or _layers_closing())


func _play_open() -> void:
	if _tween:
		_tween.kill()
	var full := _frame_rect()
	if _slides():
		# Down from the top, under the bar (owner, 2026-11-14: the research window).
		_frame.position = Vector2(full.position.x, -full.size.y - 8.0)
		_frame.size = full.size
		_stage.modulate.a = 1.0
		_tween = create_tween()
		_tween.tween_property(_frame, "position:y", full.position.y, SLIDE_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
		return
	_frame.position = Vector2(full.position.x, full.get_center().y - _line_height() * 0.5)
	_frame.size = Vector2(full.size.x, _line_height())
	_stage.modulate.a = 0.0
	_tween = create_tween()
	_tween.tween_property(_frame, "position:y", full.position.y, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.parallel().tween_property(_frame, "size:y", full.size.y, OPEN_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(_stage, "modulate:a", 1.0, FADE_TIME)


func _margins() -> Rect2: # position: left/top, size: left+right / top+bottom
	var box := _frame.get_theme_stylebox("panel") if _frame else null
	if box == null:
		return Rect2()
	return Rect2(box.get_margin(SIDE_LEFT), box.get_margin(SIDE_TOP),
		box.get_margin(SIDE_LEFT) + box.get_margin(SIDE_RIGHT), box.get_margin(SIDE_TOP) + box.get_margin(SIDE_BOTTOM))


## The frame's thickness: the opening starts from the frame alone, as a line.
func _line_height() -> float:
	return maxf(8.0, _margins().size.y + 4.0)


## The frame's place when open: centred, the stage at 4:3 as large as fits in most of the screen.
func _frame_rect() -> Rect2:
	if custom_rect.has_area():
		return custom_rect
	var area := get_viewport_rect().size
	var pad := _margins().size
	var room := area * 0.86 - pad
	var k := minf(room.x / SCENE_SIZE.x, room.y / SCENE_SIZE.y)
	var size_all := (SCENE_SIZE * k).floor() + pad
	return Rect2(((area - size_all) * 0.5).floor(), size_all)


func _layout() -> void:
	if _frame == null:
		return
	var full := _frame_rect()
	var m := _margins()
	if not (_tween and _tween.is_running()):
		_frame.position = full.position
		_frame.size = full.size
	_frame.position.x = full.position.x
	_frame.size.x = full.size.x
	# The stage keeps its full size and stays centred in the frame while the frame opens (the
	# frame clips it), so the picture is revealed from the middle.
	_stage.size = full.size - m.size
	_stage.position = m.position
	_backdrop.size = _stage.size
	_black.size = _stage.size
	_layers_box.size = _stage.size
	_intro.size = _stage.size
	var k := _stage.size.x / SCENE_SIZE.x
	var font := maxi(12, roundi(13.0 * k))
	var box_info: Array = _state.get("text_box", [55, 445, 550])
	_text.add_theme_font_size_override("font_size", font)
	_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_LEFT if String(_state.get("text_align", "")) == "left" else HORIZONTAL_ALIGNMENT_CENTER
	_text.size = Vector2(float(box_info[2]) * k, (float(box_info[3]) if box_info.size() > 3 else 90.0) * k)
	_text.position = Vector2(float(box_info[0]) * k, float(box_info[1]) * k - _text.size.y * 0.5)
	var monitor_box: Array = _state.get("monitor", {}).get("box", [221, 44, 175, 195])
	_monitor_title.add_theme_font_size_override("font_size", roundi(font * 1.25))
	_monitor_text.add_theme_font_size_override("font_size", maxi(11, roundi(font * 0.95)))
	_monitor.position = Vector2(float(monitor_box[0]), float(monitor_box[1])) * k
	_monitor.size = Vector2(float(monitor_box[2]), float(monitor_box[3])) * k
	_title.add_theme_font_size_override("font_size", roundi(font * 1.3))
	_title.size = Vector2(_stage.size.x, 30.0 * k)
	_title.position = Vector2(0, 0)
	_show_art()


func _key(layer: Dictionary) -> String:
	return "%s:%d/%d" % [layer.get("lbx", ""), ART_SCALE, int(layer.get("entry", -1))]


## The layer's sprite (at 2x), or {} while it is on its way.
func _sprite(layer: Dictionary) -> Dictionary:
	if art == null or layer.is_empty():
		return {}
	var key := _key(layer)
	_used[key] = true
	return art.sprite("%s:%d" % [layer.get("lbx", ""), ART_SCALE], int(layer.get("entry", -1)))


func _process(delta: float) -> void:
	if not visible:
		return
	var now_arriving := arriving()
	if _was_arriving and not now_arriving and _open:
		arrived.emit()
	# The paragraph comes once the speaker is there (owner, 2026-11-19: the ambassador's words
	# showed before the first frame of the room), as the lists over the scene do.
	_text.visible = not _closing and not now_arriving
	_was_arriving = now_arriving
	# While opening, the stage stays centred on the frame's middle (sliding, it moves with it).
	_stage.position.y = _margins().position.y if _slides() else _margins().position.y - (_frame.position.y - _frame_rect().position.y)
	_clock += delta
	if _clock < float(_state.get("frame_ms", FRAME_TIME * 1000.0)) / 1000.0:
		return
	_clock = 0.0
	var backdrop: Dictionary = _state.get("backdrop", {})
	var b := _sprite(backdrop)
	if not b.is_empty() and backdrop.has("loop_from"):
		_backdrop_frame += 1
		if _backdrop_frame >= b.frames.size():
			_backdrop_frame = mini(int(backdrop.loop_from), b.frames.size() - 1)
	if _intro_frame >= 0:
		var i := _sprite(_state.get("intro", {}))
		if not i.is_empty() and _stage.modulate.a >= 1.0:
			_intro_frame += 1
			if _intro_frame >= i.frames.size():
				# Over: the actor takes its place, once its art is here (owner, 2026-11-14: the
				# ambassador vanished for a moment as the list came up); the last frame holds.
				var waiting: Dictionary = _state.get("actor", {})
				_intro_frame = i.frames.size() - 1 if not waiting.is_empty() and _sprite(waiting).is_empty() else -1
	else:
		var actor: Dictionary = _state.get("actor", {})
		var a := _sprite(actor)
		if not a.is_empty() and actor.has("loop_from"):
			_actor_frame += 1
			if _actor_frame >= a.frames.size():
				_actor_frame = mini(int(actor.loop_from), a.frames.size() - 1)
		elif actor.has("frame"):
			_actor_frame = int(actor.frame)
	_step_layers()
	_show_art()


func _show_art() -> void:
	if art == null or _state.is_empty() or _stage == null:
		return
	var k := _stage.size.x / SCENE_SIZE.x
	var b := _sprite(_state.get("backdrop", {}))
	_backdrop.texture = b.frames[clampi(_backdrop_frame, 0, b.frames.size() - 1)] if not b.is_empty() else null
	var ground: Dictionary = _state.get("ground", {})
	_ground.visible = not ground.is_empty()
	if _ground.visible:
		_place(_ground, _sprite(ground), int(ground.get("frame", 0)), Vector2.ZERO, k)
	var intro: Dictionary = _state.get("intro", {})
	_intro.visible = _intro_frame >= 0 and not intro.is_empty()
	if _intro.visible:
		var i := _sprite(intro)
		_intro.texture = i.frames[clampi(_intro_frame, 0, i.frames.size() - 1)] if not i.is_empty() else null
	var actor: Dictionary = _state.get("actor", {})
	var actor_sprite := _sprite(actor) # asked for during the intro, so it is here when it ends
	_actor.visible = _intro_frame < 0 and not actor.is_empty()
	if _actor.visible:
		_place(_actor, actor_sprite, _actor_frame, Vector2.ZERO, k)
	for layer: Dictionary in _layers:
		var rect: TextureRect = layer.rect
		rect.visible = not layer.hidden
		if rect.visible:
			_place(rect, _sprite(layer.spec), int(layer.frame), Vector2(float(layer.spec.get("x", 0)), float(layer.spec.get("y", 0))), k)
	var monitor_on := false
	for layer: Dictionary in _layers:
		if String(layer.spec.get("close", "")) == "reverse":
			monitor_on = not layer.hidden and not layer.reversing and not _closing and \
				int(layer.frame) >= int(_state.get("monitor", {}).get("after_frame", 0))
	_monitor.visible = monitor_on
	var picture: Dictionary = _state.get("picture", {})
	_picture.visible = not picture.is_empty()
	if _picture.visible:
		var p: Dictionary = art.sprite(String(picture.lbx), int(picture.entry))
		_place(_picture, p, 0, Vector2(float(picture.get("x", 0)), float(picture.get("y", 0))), k)


## A frame of `s` at `at` (1x pixels) plus the frame's own origin.
func _place(rect: TextureRect, s: Dictionary, frame: int, at: Vector2, k: float) -> void:
	if s.is_empty():
		rect.texture = null
		return
	var f := clampi(frame, 0, s.frames.size() - 1)
	var texture: Texture2D = s.frames[f]
	rect.texture = texture
	rect.position = (at + Vector2(s.origins[f])) * k
	rect.size = Vector2(texture.get_size()) / float(s.scale) * k


func _on_input(event: InputEvent) -> void:
	var click := event as InputEventMouseButton
	if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT and _open:
		if _tween and _tween.is_running():
			return # the window is still opening
		if _intro_frame >= 0 and not _state.get("intro", {}).is_empty():
			_intro_frame = -1 # a click skips the ambassador's arrival, as on the legacy screen
			_frame.accept_event()
			return
		if _id < 0:
			return
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


func _slides() -> bool:
	return String(_state.get("open_style", "")) == "slide"


func _layer_key(spec: Dictionary) -> String:
	return "%s/%d" % [String(spec.get("lbx", "")), int(spec.get("entry", -1))]


## The state's layers: kept (with their frame) where the same art is still there, new ones from
## their first frame, gone ones removed.
func _sync_layers(specs: Array) -> void:
	var kept := []
	for spec: Dictionary in specs:
		var found := {}
		for layer: Dictionary in _layers:
			if layer.key == _layer_key(spec):
				found = layer
				break
		if found.is_empty():
			var rect := _texture_rect()
			_layers_box.add_child(rect)
			found = {"key": _layer_key(spec), "rect": rect, "frame": 0, "reversing": false, "hidden": false}
		found.spec = spec
		kept.append(found)
	for layer: Dictionary in _layers:
		if not kept.has(layer):
			(layer.rect as Node).queue_free()
	_layers = kept
	for i in _layers.size():
		_layers_box.move_child(_layers[i].rect, i)


func _clear_layers() -> void:
	for layer: Dictionary in _layers:
		(layer.rect as Node).queue_free()
	_layers = []


func _start_closing() -> void:
	_closing = true
	for layer: Dictionary in _layers:
		match String(layer.spec.get("close", "")):
			"hide":
				layer.hidden = true
			"reverse":
				# From the last opening frame back to the first (Science_Room's close).
				layer.reversing = true
				layer.frame = mini(int(layer.frame), maxi(int(layer.spec.get("loop_from", 1)) - 1, 0))


func _layers_closing() -> bool:
	if not _closing:
		return false
	for layer: Dictionary in _layers:
		if layer.reversing or (String(layer.spec.get("close", "")) == "after" and not layer.hidden):
			return true
	return false


func _step_layers() -> void:
	var reversing := false
	for layer: Dictionary in _layers:
		if layer.hidden:
			continue
		var s := _sprite(layer.spec)
		if s.is_empty():
			continue
		if layer.reversing:
			layer.frame = int(layer.frame) - 1
			if int(layer.frame) < 0:
				layer.reversing = false
				layer.hidden = true
			else:
				reversing = true
			continue
		if layer.spec.has("frame"):
			layer.frame = int(layer.spec.frame)
			continue
		layer.frame = int(layer.frame) + 1
		if int(layer.frame) >= s.frames.size():
			layer.frame = mini(int(layer.spec.get("loop_from", 0)), s.frames.size() - 1)
	if _closing and not reversing:
		for layer: Dictionary in _layers:
			if String(layer.spec.get("close", "")) == "after":
				layer.hidden = true
