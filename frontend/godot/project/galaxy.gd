extends Node2D
## The galaxy as a scene view: stars from the game's published snapshot, a camera with
## continuous zoom (wheel, towards the pointer), drag to pan, click to pick a star.

signal star_clicked(star: Dictionary)

const SPECTRAL_NAMES := ["Blue (B)", "White (F)", "Yellow (G)", "Orange (K)", "Red (M)", "Brown dwarf", "Black hole"]
const SPECTRAL_COLORS := [
	Color(0.62, 0.74, 1.0), Color(0.95, 0.96, 1.0), Color(1.0, 0.93, 0.55), Color(1.0, 0.68, 0.32),
	Color(1.0, 0.38, 0.28), Color(0.62, 0.42, 0.30), Color(0.45, 0.20, 0.70)]
# Banner colours in the game's order.
const PLAYER_COLORS := [
	Color(0.95, 0.20, 0.20), Color(1.0, 0.90, 0.20), Color(0.25, 0.85, 0.30), Color(0.78, 0.78, 0.82),
	Color(0.30, 0.50, 1.0), Color(0.62, 0.42, 0.25), Color(0.70, 0.30, 0.90), Color(1.0, 0.55, 0.15)]
const ZOOM_MIN := 0.5
const ZOOM_MAX := 16.0
const PICK_RADIUS_PX := 14.0

@onready var camera: Camera2D = $Camera
@onready var labels: Node2D = $Labels

# A soft radial glow, drawn additively under each star.
var _glow: GradientTexture2D

var stars: Array = []
var selected := -1
var _bounds := Rect2()
var _fitted := false
var _target_zoom := 2.0
var _anchor_world := Vector2.ZERO
var _anchor_screen := Vector2.ZERO
var _dragging := false
var _drag_moved := false
var _press_pos := Vector2.ZERO


func _ready() -> void:
	var gradient := Gradient.new()
	gradient.set_color(0, Color(1, 1, 1, 1))
	gradient.set_color(1, Color(1, 1, 1, 0))
	gradient.add_point(0.25, Color(1, 1, 1, 0.35))
	_glow = GradientTexture2D.new()
	_glow.gradient = gradient
	_glow.fill = GradientTexture2D.FILL_RADIAL
	_glow.fill_from = Vector2(0.5, 0.5)
	_glow.fill_to = Vector2(1.0, 0.5)
	_glow.width = 128
	_glow.height = 128
	var additive := CanvasItemMaterial.new()
	additive.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	material = additive


func set_stars(list: Array) -> void:
	stars = list
	if stars.is_empty():
		queue_redraw()
		return
	var lo := Vector2(INF, INF)
	var hi := Vector2(-INF, -INF)
	for s in stars:
		var p := Vector2(s.x, s.y)
		lo = lo.min(p)
		hi = hi.max(p)
	_bounds = Rect2(lo, hi - lo).grow(20.0)
	if not _fitted:
		_fit()
	queue_redraw()
	labels.queue_redraw()


func _fit() -> void:
	var view := get_viewport_rect().size
	var z := minf(view.x / _bounds.size.x, view.y / _bounds.size.y) * 0.92
	_target_zoom = clampf(z, ZOOM_MIN, ZOOM_MAX)
	camera.zoom = Vector2.ONE * _target_zoom
	camera.position = _bounds.get_center()
	_anchor_screen = view * 0.5
	_anchor_world = camera.position
	_fitted = true
	queue_redraw()
	labels.queue_redraw()


func _screen_to_world(screen: Vector2) -> Vector2:
	var view := get_viewport_rect().size
	return camera.position + (screen - view * 0.5) / camera.zoom.x


func _process(delta: float) -> void:
	var z := camera.zoom.x
	if absf(z - _target_zoom) < 0.0005:
		return
	# Ease towards the target zoom, keeping the point under the pointer still.
	z = lerpf(z, _target_zoom, 1.0 - exp(-delta * 14.0))
	if absf(z - _target_zoom) < 0.0005:
		z = _target_zoom
	camera.zoom = Vector2.ONE * z
	var view := get_viewport_rect().size
	camera.position = _anchor_world - (_anchor_screen - view * 0.5) / z
	queue_redraw()
	labels.queue_redraw()


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button:
		if button.button_index == MOUSE_BUTTON_WHEEL_UP or button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			if button.pressed:
				var factor := 1.2 if button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.2
				_anchor_screen = button.position
				_anchor_world = _screen_to_world(button.position)
				_target_zoom = clampf(_target_zoom * factor, ZOOM_MIN, ZOOM_MAX)
			get_viewport().set_input_as_handled()
		elif button.button_index == MOUSE_BUTTON_LEFT:
			if button.pressed:
				_dragging = true
				_drag_moved = false
				_press_pos = button.position
			else:
				_dragging = false
				if not _drag_moved:
					_pick(button.position)
			get_viewport().set_input_as_handled()
		return
	var motion := event as InputEventMouseMotion
	if motion and _dragging:
		if motion.position.distance_to(_press_pos) > 4.0:
			_drag_moved = true
		if _drag_moved:
			camera.position -= motion.relative / camera.zoom.x
			_anchor_world = camera.position
			_anchor_screen = get_viewport_rect().size * 0.5
			labels.queue_redraw()
		get_viewport().set_input_as_handled()


func _pick(screen: Vector2) -> void:
	var world := _screen_to_world(screen)
	var best := -1
	var best_d := PICK_RADIUS_PX / camera.zoom.x
	for i in stars.size():
		var d := world.distance_to(Vector2(stars[i].x, stars[i].y))
		if d < best_d:
			best_d = d
			best = i
	selected = best
	queue_redraw()
	if best >= 0:
		star_clicked.emit(stars[best])


func _draw() -> void:
	for i in stars.size():
		var s: Dictionary = stars[i]
		var p := Vector2(s.x, s.y)
		var c: Color = SPECTRAL_COLORS[clampi(s.spectral_class, 0, 6)]
		# Level of detail: when zoomed out, stars keep a minimum size on screen.
		var r := maxf(1.6 + 0.5 * float(s.size), (2.0 + 0.4 * float(s.size)) / camera.zoom.x)
		if s.spectral_class == 6:
			draw_circle(p, r * 2.2, Color(c, 0.25))
			draw_arc(p, r * 1.4, 0.0, TAU, 32, c, 1.5 / camera.zoom.x, true)
			continue
		var glow := r * 5.0
		draw_texture_rect(_glow, Rect2(p - Vector2(glow, glow), Vector2(glow, glow) * 2.0), false, Color(c, 0.55))
		draw_circle(p, r * 0.8, c.lerp(Color.WHITE, 0.5))
		# Rings keep a constant on-screen width.
		var px := 1.5 / camera.zoom.x
		if s.owner >= 0:
			draw_arc(p, r * 2.4, 0.0, TAU, 48, PLAYER_COLORS[s.owner_color % 8], px, true)
		if i == selected:
			draw_arc(p, r * 3.4, 0.0, TAU, 48, Color(1, 1, 1, 0.9), px, true)
