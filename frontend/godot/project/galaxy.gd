extends Node2D
## The galaxy as the home view (WP3.3a). World units are the game's map units (a star's x, y), so
## the game's numbers place everything. The camera zooms continuously (wheel, towards the pointer)
## and pans by dragging or a right click (centres there, as the legacy map does); a click on a star
## asks the game what it opens (a colony or the system).
##
## Each layer is a child with its own short script, drawn in this order: Nebulae, Influence, Links,
## Stars, Fleets, Labels. They read the data kept here (the `galaxy` and `fleets` queries) and the
## level-of-detail helpers below, which reproduce the legacy map's sizes at its four zoom levels and
## carry on smoothly in between, so the default view looks like the original.

signal star_clicked(star_id: int)

## Banner colours by the game's player colour index (s_player.color).
const PLAYER_COLORS := [
	Color(0.95, 0.20, 0.20), Color(1.0, 0.90, 0.20), Color(0.25, 0.85, 0.30), Color(0.78, 0.78, 0.82),
	Color(0.30, 0.50, 1.0), Color(0.62, 0.42, 0.25), Color(0.70, 0.30, 0.90), Color(1.0, 0.55, 0.15)]
const NEUTRAL_COLOR := Color(0.72, 0.76, 0.84)
## The legacy map's zoom levels as map units per 10 pixels (map_scale.h), closest first.
const LEGACY_SCALES := [10.0, 15.0, 20.0, 30.0]
## Star picture sizes by zoom level + star size (MOX::star_fields_dim), black holes by zoom level.
const STAR_DIMS := [33.0, 29.0, 25.0, 23.0, 21.0, 17.0]
const BLACK_HOLE_DIMS := [39.0, 33.0, 33.0, 24.0]
## How far the native view zooms, in the same units: closer and further than the legacy map.
const MIN_SCALE := 4.0
const MAX_SCALE := 70.0
const PICK_PIXELS := 10.0

@onready var camera: Camera2D = $Camera

var art: Node # art.gd, set by main.gd
var data := {} # the `galaxy` query
var stars: Array = []
var fleets: Array = [] # the `fleets` query's list
var selected := -1 # the star whose system panel is open
var influence_mode := 0 # as the legacy V key: 0 off, 1 filled, 2 borders only

var _view_placed := false
var _target_zoom := 1.0
var _anchor_world := Vector2.ZERO
var _anchor_screen := Vector2.ZERO
var _dragging := false
var _drag_moved := false
var _press_pos := Vector2.ZERO


func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS


func set_galaxy(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	data = result
	stars = result.stars
	redraw()


func set_fleets(result: Dictionary) -> void:
	if result.get("ok", false):
		fleets = result.fleets
		redraw()


## Starts where the legacy map was (the `view` query; the position is saved with the game).
func place_view(view: Dictionary) -> void:
	if _view_placed or not view.get("ok", false):
		return
	_view_placed = true
	var scale := clampf(float(view.scale), MIN_SCALE, MAX_SCALE)
	# The legacy map window is 506 x 400 pixels from (x, y), at `scale` map units per 10 pixels.
	camera.position = Vector2(float(view.x), float(view.y)) + Vector2(253.0, 200.0) * scale / 10.0
	_target_zoom = zoom_for_scale(scale)
	camera.zoom = Vector2.ONE * _target_zoom
	_anchor_world = camera.position
	_anchor_screen = get_viewport_rect().size * 0.5
	redraw()


func redraw() -> void:
	for layer in get_children():
		if layer is CanvasItem:
			layer.queue_redraw()


# ---- Level of detail ----

## Screen pixels per legacy pixel: the legacy frame fills the window's height.
func legacy_pixel() -> float:
	return get_viewport_rect().size.y / 480.0


## The current zoom as the legacy map's scale (map units per 10 legacy pixels).
func legacy_scale() -> float:
	return 10.0 * legacy_pixel() / camera.zoom.x


func zoom_for_scale(scale: float) -> float:
	return 10.0 * legacy_pixel() / scale


## The legacy zoom level (0 closest .. 3) for the current scale, continuous in between.
func zoom_level() -> float:
	var s := legacy_scale()
	if s <= LEGACY_SCALES[0]:
		return 0.0
	for i in range(1, LEGACY_SCALES.size()):
		if s <= LEGACY_SCALES[i]:
			return float(i - 1) + (s - LEGACY_SCALES[i - 1]) / (LEGACY_SCALES[i] - LEGACY_SCALES[i - 1])
	return 3.0


## A legacy-pixel size `dims` (one per zoom level) at the current zoom, in world units. Past the
## furthest legacy level, pictures keep their world size as the huge-galaxy view does
## (Star_Scale_Percent_), so they shrink on screen; closer than the closest, they keep their world
## size too, so they grow with the zoom.
func lod_size(dims: Array) -> float:
	var level := zoom_level()
	var i := mini(int(level), dims.size() - 2)
	var legacy := lerpf(dims[i], dims[i + 1], level - float(i))
	return legacy * clampf(legacy_scale(), LEGACY_SCALES[0], LEGACY_SCALES[3]) / 10.0


## A star's picture size in world units.
func star_size(star: Dictionary) -> float:
	if star.black_hole:
		return lod_size(BLACK_HOLE_DIMS)
	var size := int(star.size)
	return lod_size([STAR_DIMS[size], STAR_DIMS[size + 1], STAR_DIMS[size + 2], STAR_DIMS[size + 3]])


## World units per screen pixel: lines and text keep their on-screen size with it.
func pixel() -> float:
	return 1.0 / camera.zoom.x


func star_position(star_id: int) -> Vector2:
	if star_id < 0 or star_id >= stars.size():
		return Vector2.ZERO
	return Vector2(float(stars[star_id].x), float(stars[star_id].y))


func player_color(color_index: int) -> Color:
	return PLAYER_COLORS[color_index % 8] if color_index >= 0 else NEUTRAL_COLOR


# ---- Camera and input ----

func _screen_to_world(screen: Vector2) -> Vector2:
	return camera.position + (screen - get_viewport_rect().size * 0.5) / camera.zoom.x


func _process(delta: float) -> void:
	var z := camera.zoom.x
	if absf(z - _target_zoom) < 0.0001:
		return
	# Ease towards the target zoom, keeping the point under the pointer still.
	z = lerpf(z, _target_zoom, 1.0 - exp(-delta * 14.0))
	if absf(z - _target_zoom) < 0.0001:
		z = _target_zoom
	camera.zoom = Vector2.ONE * z
	camera.position = _anchor_world - (_anchor_screen - get_viewport_rect().size * 0.5) / z
	_clamp_camera()
	redraw()


func _clamp_camera() -> void:
	if data.is_empty():
		return
	var margin := 200.0
	camera.position = camera.position.clamp(Vector2(-margin, -margin),
		Vector2(float(data.width) + margin, float(data.height) + margin))


func zoom_by(factor: float, at_screen: Vector2) -> void:
	_anchor_screen = at_screen
	_anchor_world = _screen_to_world(at_screen)
	_target_zoom = clampf(_target_zoom * factor, zoom_for_scale(MAX_SCALE), zoom_for_scale(MIN_SCALE))


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button:
		if button.button_index == MOUSE_BUTTON_WHEEL_UP or button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			if button.pressed:
				zoom_by(1.2 if button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.2, button.position)
			get_viewport().set_input_as_handled()
		elif button.button_index == MOUSE_BUTTON_RIGHT:
			# As the legacy map: a right click centres the map there.
			if button.pressed:
				focus_point(_screen_to_world(button.position))
			get_viewport().set_input_as_handled()
		elif button.button_index == MOUSE_BUTTON_LEFT:
			if button.pressed:
				_dragging = true
				_drag_moved = false
				_press_pos = button.position
			else:
				_dragging = false
				if not _drag_moved:
					var star := pick(button.position)
					if star >= 0:
						star_clicked.emit(star)
			get_viewport().set_input_as_handled()
		return
	var motion := event as InputEventMouseMotion
	if motion and _dragging:
		if motion.position.distance_to(_press_pos) > 4.0:
			_drag_moved = true
		if _drag_moved:
			camera.position -= motion.relative / camera.zoom.x
			_clamp_camera()
			_anchor_world = camera.position
			_anchor_screen = get_viewport_rect().size * 0.5
			redraw()
		get_viewport().set_input_as_handled()


## The star under a screen point (its picture, or a few pixels around it), or -1.
func pick(screen: Vector2) -> int:
	var world := _screen_to_world(screen)
	var best := -1
	var best_d := INF
	for star: Dictionary in stars:
		var reach := maxf(star_size(star) * 0.5, PICK_PIXELS * pixel())
		var d := world.distance_to(Vector2(float(star.x), float(star.y)))
		if d < reach and d < best_d:
			best_d = d
			best = int(star.id)
	return best


## Centres the camera on a star (F5/F6, the system panel).
func focus(star_id: int) -> void:
	focus_point(star_position(star_id))


func focus_point(world: Vector2) -> void:
	camera.position = world
	_clamp_camera()
	_anchor_world = camera.position
	_anchor_screen = get_viewport_rect().size * 0.5
	redraw()
