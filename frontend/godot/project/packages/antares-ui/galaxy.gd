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
##
## Fleets (WP3.3c): a click on a fleet icon opens it (repeated clicks on overlapping icons cycle
## through them, as on the legacy map); the open fleet is framed, and with its ships selected the
## hovered star shows the route preview line main.gd sets in `preview`.

signal star_clicked(star_id: int)
signal fleet_clicked(ship_id: int)
signal star_hovered(star_id: int)
signal right_clicked
signal planet_clicked(planet_id: int)
## A click on the map that hit nothing (no planet, fleet or star).
signal empty_clicked
## A right click on a planet of the open system (its potential, as the system window).
signal planet_right_clicked(planet_id: int)
## The open system's view was dragged away from it, or framed again (the RECENTER button).
signal system_panned(away: bool)
## The pointer moved onto a planet of the open system, or off them (-1): the system pane lights
## its row, a colony ship's window shows it (owner, 2026-11-13).
signal planet_hovered(planet_id: int)

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
## How far the native view zooms, in the same units: closer and further than the legacy map. Zooming
## out also stops once the whole galaxy fits the window with a small margin (owner, 2026-11-07:
## the furthest zoom showed mostly empty space).
const MIN_SCALE := 4.0
const MAX_SCALE := 70.0
const FIT_MARGIN := 1.08
const PICK_PIXELS := 10.0

@onready var camera: Camera2D = $Camera

var art: Node # art.gd, set by main.gd
var data := {} # the `galaxy` query
var stars: Array = []
var fleets: Array = [] # the `fleets` query's list
var selected := -1 # the star whose system panel is open
var influence_mode := 0 # as the legacy V key: 0 off, 1 filled, 2 borders only
var selected_fleet := -1 # the first ship of the fleet whose panel is open
var preview := {} # the route preview: {"star": id, "line": "ok" or "blocked"}
var relocating := false # F4: the next star click merges relocations there
## The open system on the map (WP3.3f): its `system` query, star, scale and selected planet;
## the Orbits layer draws it.
var system := {}
var system_star := -1
## The open system's view was dragged (owner, 2026-11-14: panning there is allowed again, with a
## button to recentre); the way back to the galaxy is then worked out from where the camera is.
var panned := false
## The open system's own fleet icons: faded out while it is open (owner, 2026-11-13: the system
## pane lists them; an icon left over the zoomed system was clickable beside its pane), back on
## leaving. Not clickable while faded.
var system_fleets_alpha := 1.0:
	set(value):
		system_fleets_alpha = value
		if has_node("Fleets"):
			$Fleets.queue_redraw()


## How visible a fleet's icon is: the open system's fade, 1 elsewhere.
func fleet_alpha(fleet: Dictionary) -> float:
	return system_fleets_alpha if system_star >= 0 and int(fleet.get("star", -1)) == system_star else 1.0
var system_scale := 0.25 # world units per system box pixel
var selected_planet := -1
## How long a camera flight takes, and the view it left (Back returns to it).
const FLIGHT_TIME := 1.0
## The system flight's length in seconds (Options: slow 1.5, medium 1, fast 0.5, instant 0).
var flight_time := FLIGHT_TIME
## The other stars' opacity while a system is open (they fade to OTHERS_IN_SYSTEM).
var others_alpha := 1.0:
	set(value):
		others_alpha = value
		redraw()
const OTHERS_IN_SYSTEM := 0.2
var _flying := false
var _flight: Tween
var _saved_view := {}
## An empire panel's view (WP3.5): the camera before the panel fitted the whole galaxy beside it,
## and the zoom it fitted at (the wheel may not zoom further out than the furthest of the two).
var _panel_view := {}
var _fit_zoom := 0.0
var _hovered_star := -1
var hovered_planet := -1

var _view_placed := false
var _target_zoom := 1.0
var _anchor_world := Vector2.ZERO
var _anchor_screen := Vector2.ZERO
var _dragging := false
var _drag_moved := false
var _press_pos := Vector2.ZERO


func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS


## Placed at the saved view with every star's picture: the frontend fades the view in then
## (owner, 2026-11-12: stand-in dots at the whole-map zoom showed first, then snapped).
func ready_to_show() -> bool:
	return _view_placed and not stars.is_empty() and _stars_drawn()


## Every star's picture has arrived.
func _stars_drawn() -> bool:
	for star: Dictionary in stars:
		if art == null or art.sprite("BUFFER0.LBX", int(star.sprite)).is_empty():
			return false
	return true


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
	var scale := float(view.scale)
	# The legacy map window is 506 x 400 pixels from (x, y), at `scale` map units per 10 pixels.
	camera.position = Vector2(float(view.x), float(view.y)) + Vector2(253.0, 200.0) * scale / 10.0
	_target_zoom = clampf(zoom_for_scale(scale), furthest_zoom(), zoom_for_scale(MIN_SCALE))
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
	if _flying:
		redraw()
		return
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
	if _fit_zoom > 0.0: # beside a panel the galaxy's centre sits off the screen's
		margin = maxf(margin, get_viewport_rect().size.x / camera.zoom.x)
	camera.position = camera.position.clamp(Vector2(-margin, -margin),
		Vector2(float(data.width) + margin, float(data.height) + margin))


func zoom_by(factor: float, at_screen: Vector2) -> void:
	_anchor_screen = at_screen
	_anchor_world = _screen_to_world(at_screen)
	_target_zoom = clampf(_target_zoom * factor, furthest_zoom(), zoom_for_scale(MIN_SCALE))


## The furthest zoom out: MAX_SCALE, or closer once the whole galaxy fits the window.
func furthest_zoom() -> float:
	var furthest := zoom_for_scale(MAX_SCALE)
	if data.is_empty():
		return furthest
	var window := get_viewport_rect().size
	var fit := minf(window.x / (float(data.width) * FIT_MARGIN), window.y / (float(data.height) * FIT_MARGIN))
	fit = maxf(furthest, fit)
	return minf(fit, _fit_zoom) if _fit_zoom > 0.0 else fit


func _unhandled_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button:
		if button.button_index == MOUSE_BUTTON_WHEEL_UP or button.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			# Over a panel the wheel is the panel's: a list scrolled to its end passes it on, and
			# it must not zoom the map (owner, 2026-11-11).
			if button.pressed and get_viewport().gui_get_hovered_control() == null:
				# In a system the wheel zooms about its star, so it can't wander off (owner).
				var at := button.position
				if not system.is_empty() and not panned:
					at = get_global_transform_with_canvas() * star_position(system_star)
				zoom_by(1.2 if button.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.2, at)
			get_viewport().set_input_as_handled()
		elif button.button_index == MOUSE_BUTTON_RIGHT:
			# A right click cancels F4's mode, or in a system shows a planet's potential (no panning).
			if button.pressed:
				if relocating:
					right_clicked.emit()
				elif not system.is_empty(): # no right-click panning (owner, 2026-11-13: dropped)
					var planet: int = $Orbits.pick(_screen_to_world(button.position))
					if planet >= 0:
						planet_right_clicked.emit(planet)
			get_viewport().set_input_as_handled()
		elif button.button_index == MOUSE_BUTTON_LEFT:
			if button.pressed:
				_dragging = true
				_drag_moved = false
				_press_pos = button.position
			else:
				# Only a release whose press was on the map clicks it: a panel rebuilt under the
				# pointer (a pane row expanding) let its release through, and that clicked whatever
				# was under the pane (owner, 2026-11-11: an enemy colony's row closed the pane).
				var pressed_here := _dragging
				_dragging = false
				if pressed_here and not _drag_moved:
					var fleet := _pick_fleet(button.position)
					var star := pick(button.position)
					# A planet of the open system (owner, 2026-11-13: a colony's planet opens its
					# colony pane, as its row in the system pane does).
					var planet: int = -1 if relocating else $Orbits.pick(_screen_to_world(button.position))
					if planet >= 0:
						planet_clicked.emit(planet)
					elif fleet >= 0 and not relocating:
						fleet_clicked.emit(fleet)
					elif star >= 0:
						star_clicked.emit(star)
					elif not relocating:
						empty_clicked.emit()
			get_viewport().set_input_as_handled()
		return
	var motion := event as InputEventMouseMotion
	if motion and not _dragging:
		var star := pick(motion.position)
		if star != _hovered_star:
			_hovered_star = star
			star_hovered.emit(star)
		var planet: int = $Orbits.pick(_screen_to_world(motion.position)) if not system.is_empty() else -1
		if planet != hovered_planet:
			hovered_planet = planet
			$Orbits.queue_redraw()
			planet_hovered.emit(planet)
	if motion and _dragging:
		if motion.position.distance_to(_press_pos) > 4.0:
			_drag_moved = true
		if _drag_moved:
			if not system.is_empty() and not panned:
				panned = true
				system_panned.emit(true)
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


## The fleet icon under a screen point (its first ship), or -1. Repeated clicks on icons that
## overlap there cycle through them (SHIPS::Overlapped_Ship_Icon_Button).
func _pick_fleet(screen: Vector2) -> int:
	var world := _screen_to_world(screen)
	var hits := []
	for fleet: Dictionary in fleets:
		if fleet_alpha(fleet) < 0.5:
			continue # faded in the open system: its pane has them
		var rect: Rect2 = $Fleets.icon_rect(fleet)
		if rect.grow(2.0 * pixel()).has_point(world):
			hits.append(int(fleet.ship))
	if hits.is_empty():
		return -1
	var at := hits.find(selected_fleet)
	return hits[(at + 1) % hits.size()] if at >= 0 else hits[0]


## Centres the camera on a star (F5/F6, the system panel).
func focus(star_id: int) -> void:
	focus_point(star_position(star_id))


func focus_point(world: Vector2) -> void:
	camera.position = world
	_clamp_camera()
	_anchor_world = camera.position
	_anchor_screen = get_viewport_rect().size * 0.5
	redraw()


# ---- The open system (WP3.3f) ----

## Shows a system's planets around its star, fading in (the `system` query's answer).
func show_system(star_id: int, result: Dictionary) -> void:
	var fresh := star_id != system_star
	system = result
	system_star = star_id
	if fresh:
		selected_planet = -1
		create_tween().tween_property(self, "others_alpha", OTHERS_IN_SYSTEM, maxf(flight_time, 0.3))
		# The system's own fleets stay by its star, clickable as on the map (owner, 2026-11-19,
		# reversing 2026-11-13's fade: more intuitive than the pane's list, which keeps them too).
		system_fleets_alpha = 1.0
		$Orbits.modulate.a = 0.0
		create_tween().tween_property($Orbits, "modulate:a", 1.0, maxf(flight_time, 0.3)).set_delay(flight_time * 0.5)
	$Orbits.queue_redraw()


func hide_system() -> void:
	if system.is_empty():
		return
	var fade := create_tween()
	create_tween().tween_property(self, "others_alpha", 1.0, maxf(flight_time, 0.3))
	create_tween().tween_property(self, "system_fleets_alpha", 1.0, maxf(flight_time, 0.3))
	fade.tween_property($Orbits, "modulate:a", 0.0, 0.3)
	fade.tween_callback(func():
		system = {}
		system_star = -1
		system_fleets_alpha = 1.0
		selected_planet = -1
		$Orbits.queue_redraw())


## Flies the camera to `world` at `zoom`, so that `world` ends at `screen` (a point in the area the
## panes leave free). The first flight remembers the view to come back to.
func fly_to(world: Vector2, zoom: float, screen: Vector2) -> void:
	if _saved_view.is_empty():
		_saved_view = {"position": camera.position, "zoom": camera.zoom.x, "anchor": world}
	elif _saved_view.get("anchor", Vector2.INF) != world:
		_saved_view.moved = true # another system: the flight in no longer reverses
	if panned:
		panned = false
		system_panned.emit(false)
	var center := world - (screen - get_viewport_rect().size * 0.5) / zoom
	_fly(center, zoom, world)


## Moves the camera smoothly to a point at the current zoom (the fleet buttons).
func glide_to(world: Vector2) -> void:
	_fly(world, camera.zoom.x)


## Leaves a system for its fleet (WP3.3g, owner): the camera glides out a little round `world`
## (the system's star), keeping the view it will come back to.
func step_back(world: Vector2, factor := 0.45) -> void:
	_fly(world, maxf(camera.zoom.x * factor, furthest_zoom()), world)


## An empire panel opened (WP3.5, owner 2026-11-14): the whole galaxy fits `area` (the screen left
## of the panel). The view to come back to is the one before any system flight, else this one.
func fit_in(area: Rect2) -> void:
	if data.is_empty() or area.size.x < 50.0:
		return
	if _panel_view.is_empty():
		_panel_view = _saved_view.duplicate() if not _saved_view.is_empty() else {"position": camera.position, "zoom": camera.zoom.x}
	_saved_view = {}
	var size := Vector2(float(data.width), float(data.height)) * FIT_MARGIN
	var zoom := minf(area.size.x / size.x, area.size.y / size.y)
	_fit_zoom = zoom
	var half := get_viewport_rect().size * 0.5
	var center := Vector2(float(data.width), float(data.height)) * 0.5 - (area.get_center() - half) / zoom
	if panned:
		panned = false
		system_panned.emit(false)
	_fly(center, zoom)


## The panel closed: back where the camera was before it opened.
func unfit() -> void:
	_fit_zoom = 0.0
	if _panel_view.is_empty():
		return
	_saved_view = {}
	_fly(_panel_view.position, _panel_view.zoom)
	_panel_view = {}


## The panel closed for a system it opened (the build pane from its list): the camera stays for
## the flight in, and the panel's view is kept for its return.
func keep_fit_for_return() -> void:
	_fit_zoom = 0.0


## The panel won't come back: its view is dropped (the camera stays where it is).
func forget_fit() -> void:
	_fit_zoom = 0.0
	_panel_view = {}


## The flight into a system won't be flown back (the panel's fit replaces it).
func forget_flight() -> void:
	_saved_view = {}


## Back to the view before the first flight.
func fly_back() -> void:
	if _saved_view.is_empty():
		return
	var to_position: Vector2 = _saved_view.position
	var to_zoom: float = _saved_view.zoom
	if panned or _saved_view.get("moved", false):
		# Worked out from where the camera is now (owner, 2026-11-14: after panning, or other
		# systems, replaying the flight in backwards went the wrong way): a zoom about the one point
		# that keeps its place on the screen from this view to the old one.
		var from_zoom := camera.zoom.x
		if absf(from_zoom - to_zoom) > from_zoom * 0.001:
			_fly(to_position, to_zoom, (camera.position * from_zoom - to_position * to_zoom) / (from_zoom - to_zoom))
		else:
			_fly(to_position, to_zoom)
	else:
		_fly(to_position, to_zoom, _saved_view.get("anchor", Vector2.INF))
	_saved_view = {}
	if panned:
		panned = false
		system_panned.emit(false)


func _fly(center: Vector2, zoom: float, anchor := Vector2.INF) -> void:
	if _flight:
		_flight.kill()
	_flying = true
	_target_zoom = zoom
	# One eased clock, two curves (owner, 2026-11-13): flying in, the star travels to its place on
	# the screen early and the fast part is the zoom, about the star (`anchor`), so it never heads
	# for the screen's centre first; flying out is the same path reversed. The zoom moves evenly in
	# log space. Without an anchor the camera's centre glides.
	var from_position := camera.position
	var from_zoom := camera.zoom.x
	var zooming_in := zoom > from_zoom
	var same_zoom := absf(zoom - from_zoom) <= from_zoom * 0.001 # a pan (a pane opened or closed)
	var half := get_viewport_rect().size * 0.5
	var anchored := anchor.is_finite()
	var from_screen := half + (anchor - from_position) * from_zoom if anchored else Vector2.ZERO
	var to_screen := half + (anchor - center) * zoom if anchored else Vector2.ZERO
	var step := func(s: float):
		var pan := s if same_zoom else (1.0 - pow(1.0 - s, 3.0) if zooming_in else pow(s, 3.0))
		var grow := s * s if zooming_in else 1.0 - (1.0 - s) * (1.0 - s)
		var z := exp(lerpf(log(from_zoom), log(zoom), grow))
		camera.zoom = Vector2.ONE * z
		if anchored:
			camera.position = anchor - (from_screen.lerp(to_screen, pan) - half) / z
		else:
			camera.position = from_position.lerp(center, pan)
		redraw()
	_flight = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN_OUT)
	if flight_time <= 0.0:
		step.call(1.0) # instant
		camera.position = center
	else:
		_flight.tween_method(step, 0.0, 1.0, flight_time)
	_flight.tween_callback(func():
		_flying = false
		_anchor_world = camera.position
		_anchor_screen = get_viewport_rect().size * 0.5
		redraw())
