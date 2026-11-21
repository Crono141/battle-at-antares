extends PanelContainer
## VIEW COLONY natively (owner, 2026-11-20): the colony's landscape in a 4:3 frame beside the
## colony pane, composed from the `colony_landscape` query: the layers the colony screen's
## landscape view draws (Draw_Landscape_View), back to front, at their places in its 640x480
## screen, scaled to the frame; animated buildings cycle. The frontend places it (after the colony
## pane, or the build pane when that is out) and opens it with the colony pane on screens wide
## enough for it; VIEW COLONY shows or hides it; a right click on it hides it.
## It is shown whole (owner, 2026-11-20: it filled in a picture at a time for seconds): a colony's
## layers load out of sight and it slides in, or swaps to the next colony, once all have come;
## prefetch() starts that when the colony pane opens, so VIEW COLONY usually finds it ready.

signal command(dict: Dictionary)
signal closed

const SCREEN := Vector2(640, 480)
const FPS := 15.0 # the colony screen's animation pace, about a frame a tick
const SLIDE_TIME := 0.25 # as the colony and build panes
const SLIDE_FROM := 60.0
const WAIT_LIMIT := 8.0 # seconds: shown with what has come by then

var art: Node
var colony_id := -1
var landscape := {}
var _stage: Control
var _time := 0.0
var _animated := false
var _opening := false # shown, not yet slid in (the frontend places it, then slide_in)
var _tween: Tween
var _next := {} # the asked-for colony's layers while their art loads
var _next_since := 0.0
var _target_x := 0.0
var _reveal_waiting := false # placed, waiting for the art to slide in
var _close_button: Button # shown when it pops up over the panes (no room beside them)


func _ready() -> void:
	theme_type_variation = &"Pane"
	mouse_filter = Control.MOUSE_FILTER_STOP
	_stage = Control.new()
	_stage.clip_contents = true
	_stage.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.draw.connect(_draw_stage)
	add_child(_stage)
	_close_button = Button.new()
	_close_button.text = "X"
	_close_button.focus_mode = Control.FOCUS_NONE
	_close_button.tooltip_text = "Close (VIEW COLONY shows it again)"
	_close_button.custom_minimum_size = Vector2(34, 30)
	# Clear of the frame's bevel in every theme (owner, 2026-11-20: on Chrome it sat against it).
	_close_button.anchor_left = 1.0
	_close_button.anchor_right = 1.0
	_close_button.offset_right = -16.0
	_close_button.offset_left = -16.0 - 34.0
	_close_button.offset_top = 14.0
	_close_button.offset_bottom = 14.0 + 30.0
	_close_button.pressed.connect(close)
	_close_button.hide()
	_stage.add_child(_close_button)
	mouse_filter = Control.MOUSE_FILTER_IGNORE # until it is in
	gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_RIGHT:
			close()
			accept_event())
	if art:
		art.loaded.connect(func(_key: String):
			if not _next.is_empty():
				_check_next())


## Loads `colony`'s landscape art out of sight (the colony pane opened on it).
func prefetch(colony: int) -> void:
	command.emit({"verb": "query", "what": "colony_landscape", "colony": colony, "_reply_to": "_take_prefetch"})


func _take_prefetch(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	for layer: Dictionary in result.layers:
		art.sprite(String(layer.lbx), int(layer.entry))


## Shows `colony`'s landscape (asked again when it is another colony's).
func open(colony: int) -> void:
	colony_id = colony
	command.emit({"verb": "query", "what": "colony_landscape", "colony": colony, "_reply_to": "show_landscape"})
	if not visible or (_tween != null and _tween.is_running() and modulate.a < 1.0 and not _opening):
		_opening = true
		if _tween:
			_tween.kill() # a close's slide too: its end would have cleared _closing
		if has_meta("_closing"):
			remove_meta("_closing") # else shown() stays false: never placed, never revealed again
		modulate.a = 0.0
		mouse_filter = Control.MOUSE_FILTER_IGNORE
		show()


func show_landscape(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.get("colony", -1)) != colony_id:
		return
	_next = result
	_next_since = Time.get_ticks_msec() / 1000.0
	_check_next()


## The asked-for layers all here (or the wait over): they replace the shown ones at once.
func _check_next() -> void:
	if _next.is_empty():
		return
	var waited := Time.get_ticks_msec() / 1000.0 - _next_since
	if waited < WAIT_LIMIT:
		for layer: Dictionary in _next.layers:
			art.sprite(String(layer.lbx), int(layer.entry)) # asked for (again, after a failure)
			if not art.settled(String(layer.lbx), int(layer.entry)):
				return
	landscape = _next
	_next = {}
	_animated = false
	for layer: Dictionary in landscape.layers:
		var s: Dictionary = art.sprite(String(layer.lbx), int(layer.entry))
		if not s.is_empty() and (s.frames as Array).size() > 1:
			_animated = true
	_stage.queue_redraw()
	if _reveal_waiting:
		_reveal()


## Over the panes, centred (no room beside them): with its own close button.
func set_popped_up(on: bool) -> void:
	_close_button.visible = on


## Ready to be seen: this colony's layers are in.
func is_complete() -> bool:
	return _next.is_empty() and not landscape.is_empty() and int(landscape.get("colony", -1)) == colony_id


func close() -> void:
	if visible and not _closing():
		_opening = false
		_reveal_waiting = false
		mouse_filter = Control.MOUSE_FILTER_IGNORE
		if _tween:
			_tween.kill()
		_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
		_tween.tween_property(self, "position:x", position.x - SLIDE_FROM, SLIDE_TIME)
		_tween.tween_property(self, "modulate:a", 0.0, SLIDE_TIME)
		_tween.chain().tween_callback(hide)
		set_meta("_closing", true)
		_tween.finished.connect(func(): remove_meta("_closing"))
		closed.emit()


func _closing() -> bool:
	return has_meta("_closing")


## Up, and not on its way out.
func shown() -> bool:
	return visible and not _closing()


## Moves to `x`: slides in from behind (fading in) when just opened and its art is in, else
## glides there.
func move_to(x: float) -> void:
	_target_x = x
	if _tween:
		_tween.kill()
	if has_meta("_closing"):
		remove_meta("_closing")
	if _opening:
		_opening = false
		position.x = x - SLIDE_FROM
		_reveal_waiting = true
		if is_complete():
			_reveal()
		return
	if _reveal_waiting:
		position.x = x - SLIDE_FROM
		return
	_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	if modulate.a < 1.0:
		_tween.tween_property(self, "modulate:a", 1.0, SLIDE_TIME)
	_tween.tween_property(self, "position:x", x, SLIDE_TIME)


func _reveal() -> void:
	_reveal_waiting = false
	mouse_filter = Control.MOUSE_FILTER_STOP
	if _tween:
		_tween.kill()
	_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "modulate:a", 1.0, SLIDE_TIME)
	_tween.tween_property(self, "position:x", _target_x, SLIDE_TIME)


## The frame's size for a picture `height` tall inside it (4:3, the theme's margins round it).
func frame_size_for(height: float) -> Vector2:
	var margins := _margins()
	return Vector2((height - margins.y) * 4.0 / 3.0 + margins.x, height)


## The frame's size for a frame `width` wide (4:3 picture inside the margins).
func frame_size_for_width(width: float) -> Vector2:
	var margins := _margins()
	return Vector2(width, (width - margins.x) * 3.0 / 4.0 + margins.y)


func _margins() -> Vector2:
	var style := get_theme_stylebox("panel", "Pane")
	if style == null:
		return Vector2.ZERO
	return Vector2(style.get_margin(SIDE_LEFT) + style.get_margin(SIDE_RIGHT), style.get_margin(SIDE_TOP) + style.get_margin(SIDE_BOTTOM))


func _process(delta: float) -> void:
	if not _next.is_empty() and Time.get_ticks_msec() / 1000.0 - _next_since >= WAIT_LIMIT:
		_check_next() # the wait is over: what has come
	if not visible or landscape.is_empty():
		return
	_time += delta
	if _animated or _time < 2.0: # art still arriving: redraw a while; then only animation
		_stage.queue_redraw()


func _draw_stage() -> void:
	if landscape.is_empty() or art == null:
		return
	var k := minf(_stage.size.x / SCREEN.x, _stage.size.y / SCREEN.y)
	var origin := ((_stage.size - SCREEN * k) * 0.5).floor()
	_stage.draw_rect(Rect2(origin, SCREEN * k), Color.BLACK)
	var frame := int(_time * FPS)
	for layer: Dictionary in landscape.layers:
		var lbx := String(layer.lbx)
		var entry := int(layer.entry)
		var s: Dictionary = art.sprite(lbx, entry)
		if s.is_empty():
			continue
		var count: int = (s.frames as Array).size()
		if count > 1:
			_animated = true
		var at := Vector2(float(layer.x), float(layer.y))
		if String(layer.get("anchor", "")) == "bottom": # ERIC::Draw_Bottom_Centered
			var frame_size: Vector2 = s.size
			at -= Vector2(floorf(frame_size.x / 2.0), frame_size.y)
		art.draw(_stage, lbx, entry, frame % maxi(count, 1), origin + at * k, k)
