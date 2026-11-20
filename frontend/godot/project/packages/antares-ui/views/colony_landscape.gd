extends PanelContainer
## VIEW COLONY natively (owner, 2026-11-20): the colony's landscape in a 4:3 frame beside the
## colony pane, composed from the `colony_landscape` query: the layers the colony screen's
## landscape view draws (Draw_Landscape_View), back to front, at their places in its 640x480
## screen, scaled to the frame; animated buildings cycle. The frontend places it (after the colony
## pane, or the build pane when that is out) and opens it with the colony pane on screens wide
## enough for it; VIEW COLONY shows or hides it; a right click on it hides it.

signal command(dict: Dictionary)
signal closed

const SCREEN := Vector2(640, 480)
const FPS := 15.0 # the colony screen's animation pace, about a frame a tick
const SLIDE_TIME := 0.25 # as the colony and build panes
const SLIDE_FROM := 60.0

var art: Node
var colony_id := -1
var landscape := {}
var _stage: Control
var _time := 0.0
var _animated := false
var _opening := false # shown, not yet slid in (the frontend places it, then slide_in)
var _tween: Tween


func _ready() -> void:
	theme_type_variation = &"Pane"
	mouse_filter = Control.MOUSE_FILTER_STOP
	_stage = Control.new()
	_stage.clip_contents = true
	_stage.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_stage.draw.connect(_draw_stage)
	add_child(_stage)
	gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_RIGHT:
			close()
			accept_event())
	if art:
		art.loaded.connect(func(_key: String): _stage.queue_redraw())


## Shows `colony`'s landscape (asked again when it is another colony's).
func open(colony: int) -> void:
	if colony != colony_id:
		landscape = {}
		_stage.queue_redraw()
	colony_id = colony
	command.emit({"verb": "query", "what": "colony_landscape", "colony": colony, "_reply_to": "show_landscape"})
	if not visible or (_tween != null and _tween.is_running() and modulate.a < 1.0 and not _opening):
		_opening = true
		if _tween:
			_tween.kill()
		modulate.a = 0.0
		show()


func show_landscape(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.get("colony", -1)) != colony_id:
		return
	landscape = result
	_animated = false
	for layer: Dictionary in result.layers:
		var s: Dictionary = art.sprite(String(layer.lbx), int(layer.entry)) # asked for now, drawn when here
		if not s.is_empty() and (s.frames as Array).size() > 1:
			_animated = true
	_stage.queue_redraw()


func close() -> void:
	if visible and not _closing():
		_opening = false
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


## Moves to `x`: slides in from behind (fading in) when just opened, else glides there.
func move_to(x: float) -> void:
	if _tween:
		_tween.kill()
	if has_meta("_closing"):
		remove_meta("_closing")
	_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	if _opening:
		_opening = false
		position.x = x - SLIDE_FROM
		_tween.tween_property(self, "modulate:a", 1.0, SLIDE_TIME)
	elif modulate.a < 1.0:
		_tween.tween_property(self, "modulate:a", 1.0, SLIDE_TIME)
	_tween.tween_property(self, "position:x", x, SLIDE_TIME)


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
