extends Control
## WP3.9b: tactical space combat as a native scene (mockup B4, canvas
## https://claude.ai/artifact/Da8ib6hDQUepddK6yxe4sm). It replaces the whole view while a battle
## runs, as the legacy combat screen replaces the galaxy.
##
## The battle itself is the game's legacy code (antares/combat_native.h): the "combat" snapshot
## brings the state after every step, and each decision for one of the player's ships is a
## "combat" request, answered with an action as JSON text ({"action": "move", "x": 12, "y": 30}).
##
## The field is the legacy grid (squares of 20 combat pixels; world units are combat pixels) under a
## free camera: wheel to zoom at the pointer, drag to pan. Ships turn smoothly to their facings
## (16 steps, counter-clockwise from east). Hovering a ship shows its shield glow and hull crescent
## (or every ship's, with "Always show health") and its card. Left click moves to a legal square,
## fires at an enemy, or picks your ship (initiative off); right click on the acting ship opens the
## order ring (Wait, Done, Retreat, Board, Scan, Self-destruct), elsewhere it turns to face.

signal opened
signal closed
## A right click on a part in the scan: its help record (frontend.gd show_help, as the design panel's).
signal help_requested(topic: int, at: Control)

const SQUARE := 20.0
const MIN_ZOOM := 0.3
const MAX_ZOOM := 3.0
const SHIELD_SHADER := preload("combat_shield.gdshader")
const BeamDraw := preload("beam_draw.gd")
const Themes := preload("../themes.gd")
const BeamVolley := preload("beam_volley.gd")
const PLAYER_COLORS := [
	Color(0.95, 0.20, 0.20), Color(1.0, 0.90, 0.20), Color(0.25, 0.85, 0.30), Color(0.78, 0.78, 0.82),
	Color(0.30, 0.50, 1.0), Color(0.62, 0.42, 0.25), Color(0.70, 0.30, 0.90), Color(1.0, 0.55, 0.15)]
const MONSTER_COLOR := Color(0.72, 0.76, 0.84)
const AMBER := Color(0.95, 0.70, 0.24)
const FIRE := Color(1.0, 0.48, 0.29)
const ARMOR_COLOR := Color(0.79, 0.84, 0.92)
const STRUCTURE_COLOR := Color(0.94, 0.42, 0.42)
const SYSTEMS_COLOR := Color(0.95, 0.83, 0.42)
const TRACK_COLOR := Color(0.24, 0.29, 0.38, 0.75)
const SETTINGS := "user://antares-ui.cfg"
const ANIM_TIME := 0.12 # a ship's engine frames (1 to 3 of its facing)

var art: Node # art.gd
var game: AntaresGame
var dialog_open: Callable # the frontend: a dialog is up over the battle (it has the keys)

var state := {}
var _ships := {} # combat index -> {sprite: Sprite2D, shown: Vector2, angle: float}
var _request_id := -1
var _zoom := 1.4
var _cam := Vector2(810, 680)
var _cam_goal := Vector2.INF
var _zoom_goal := -1.0 # the zoom the camera eases to (-1: none)
# The camera frames an enemy's beam (owner 2026-11-22): the effect waits until it is in place.
const FRAMED_KINDS := ["beam", "stasis", "tractor", "plasma_web", "stellar", "black_hole", "absorber", "gyro"]
const FRAME_MARGIN := 110.0 # screen pixels between the framed ships and the clear area's edges (a constant)
const FRAME_WAIT_MAX := 2.5 # seconds the effect waits for the camera at most (a safety: it waits until the camera settles)
var _art_hold := 0.0 # > 0: a volley waits for its flashes' art (seconds waited + a tiny bit)
var _art_wait: Array = [] # [name, entry] the waiting volley needs
const ART_WAIT_MAX := 1.0
# A framing glides over the galaxy's flight time (Options' SYSTEM ZOOM; owner 2026-11-22: the
# camera's speed preference), eased in and out; 0 cuts.
var _frame := {} # {from_cam, to_cam, from_zoom, to_zoom, t, dur} while the camera frames
var _cam_hold := 0.0 # > 0: the effect waits for the camera (seconds waited so far + a tiny bit)
var _hover_ship := -1
var _hover_square := Vector2i(-1, -1)
var _always_health := false
var _alt_held := false
var _drag_from := Vector2.INF
var _dragged := false
var _anim_clock := 0.0
var _anim_frame := 0
var _last_acting := -1
var _menu_ship := -1
var _menu_hover := -1
var _menu_confirm := ""
var _toast_time := 0.0
var _opened := false
var _legal := {} # x * 1000 + y (int) -> true: JSON numbers arrive as floats, which Array.has() won't match
var _order := {} # ships still to act this round (int)
## WP3.9c: effects playing (beams, sparks, explosions, damage numbers, shield flashes), each
## {kind, t (seconds since start), life, ...}; drawn over the ships, dropped when done.
var _effects: Array = []
# WP3.9d: the effect the engine is waiting for ("animate" request): {id, t, dur}; empty when none.
var _animating := {}
var _animating_waits_capture := -1 # the capture the current animation waits for (not loaded yet)
var _capture_asked_at := 0
var animated := {} # effects played, by kind (the smoke reports them)
var framed := 0 # enemy fire the camera framed (the smoke reports it)
var art_late := {} # volley art ("name/entry") not there when a tick first wanted it (the smoke reports it)
var art_drawn := {} # volley art drawn at least once
var _native_volleys := false # volleys play natively: shield hits are theirs, not the 3.9c arcs
const SHIELD_BASE_ROT := [0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 3, 4, 3, 2, 1] # BEAMS::Rot_To_Base_Rot
const SHIELD_ROT_METHOD := [0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3] # BEAMS::Rot_To_Rot_Method
const SHIELD_ART := "beams.lbx!shield" # glass kept as the shield's tint (sprites.cpp)
var _warned_fragments := false # a volley's non-recipe colours were missing (warned once)
var smoke_hold := false # the smoke freezes effects and the awaited animation to shoot chosen ticks
# Spike 3.9: volleys arrive captured from the legacy drawing (sprite "capture", entry); they carry
# legacy's own damage numbers and shield hits, so the state-diff ones are left out.
var _captures_seen := false
const CAPTURE_FRAME_TIME := 0.06 # legacy's measured pace (ms per frame of the beam loop: ~60)
const MOVE_STEP_TIME := 0.012 # seconds per legacy move step (2 px); tuned with the ships (3.9f)
const TURN_STEP_TIME := 0.05 # seconds per 22.5 degree facing step
# Ship movement speed (owner 2026-11-21: "no options for ship movement speed yet"): the battle's
# Moves button, saved as [combat] move_speed; scales moves and turns (not weapons: their pace is
# the original's).
# Owner 2026-11-22: "the speeds are slow overall": Slow is the old Normal, Normal the old Fast,
# Fast twice Normal.
const MOVE_SPEEDS := [["Slow", 1.0], ["Normal", 0.5], ["Fast", 0.25], ["Instant", 0.0]]
var _move_speed := 1 # index in MOVE_SPEEDS
var _speed_button: Button
# Scan (owner 2026-11-21): the original's read-only view of any ship (Display_Combat_View_Ship).
var _scan_mode := false # armed: the next click on a ship scans it
var _scan_ship := -1 # the ship the scan panel shows, or -1
var _scan_panel: PanelContainer
var _scan_title: Label
var _scan_sub: Label
var _scan_bars: Control
var _scan_body: VBoxContainer

var _world: Node2D
var _backdrop: Node2D
var _ship_layer: Node2D
var _marks: Node2D
# WP3.9 stage 1: native beam strands, on their own layer (the strand edge shader), under the
# muzzle flashes and hit flares drawn on _marks.
var _beams: Node2D
var _menu: Control
var _top_left: PanelContainer
var _round_label: Label
var _strength_mine: ColorRect
var _strength_theirs: ColorRect
var _strength_label: Label
var _health_toggle: Button
var _auto_button: Button
var _timeline: PanelContainer
var _timeline_row: HBoxContainer
var _strip: PanelContainer
var _strip_name: Label
var _strip_bars: Control
var _strip_info: Label
var _weapon_chips := {} # slot -> chip
var _strip_weapons: HBoxContainer
var _actions: HBoxContainer
var _top_right: Control # the pocket under the initiative bar: Show Health and the move speed
var _bottom: Control # the actions row (Wait, Done, End turn, Auto), framed with the weapons in one bottom pocket
var _strip_row: Control # the acting ship and its weapons (hidden when the acting ship isn't the player's)
var _round_number: Label
var _tl_scroll: ScrollContainer
var _tl_dragged := false
var _scroll_goal := {} # ScrollContainer -> the horizontal scroll it eases to (smooth scrolling)
var _wp_scroll: ScrollContainer # the acting ship's weapons (scrolls when more than fit)
var _wp_press := Vector2.INF # where a press on the weapons began (a drag scrolls, a click toggles)
var _wp_dragged := false
var _chip_hover := -1 # the ship whose initiative chip is under the pointer (hovers it as on the field)
var _options_tab: PanelContainer # the Options button, framed under the initiative bar
var _options_panel: PanelContainer # its flyout
var _show_grid := false
var _always_moves := false
var _camera_follow := true # Options: the camera frames the enemy's moves and shots
var _starfield: Control
var _scan_scroll: ScrollContainer
var _tl_acting := -2 # the acting ship the bar was last scrolled for
## The theme's screen frame and its joints (frontend.gd sets them, as for the galaxy).
var frame_style: StyleBox
var tee := {}
var _frame_overlay: Control
var _wait_button: Button
var _done_button: Button
var _end_button: Button
var _card: PanelContainer
var _card_title: Label
var _card_body: Label
var _card_bars: Control
var _card_fire: Label
var _toast: Label

const MENU_ITEMS := ["wait", "done", "retreat", "board", "scan", "self_destruct"]
const MENU_LABELS := {"wait": "WAIT", "done": "DONE", "retreat": "RETREAT", "board": "BOARD", "scan": "SCAN",
	"self_destruct": "SELF-\nDESTRUCT"}


func _ready() -> void:
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	focus_mode = Control.FOCUS_ALL
	clip_contents = true
	_always_health = _load_setting("always_show_health", false)
	# Opaque: the backdrop art's black is see-through, and the hidden legacy combat page is below.
	var base := ColorRect.new()
	base.color = Color(0.0, 0.0, 0.0)
	base.mouse_filter = Control.MOUSE_FILTER_IGNORE
	base.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	add_child(base)
	# The galaxy's parallax starfield behind the battle's cloud art (owner 2026-11-22).
	_starfield = Control.new()
	_starfield.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_starfield.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	_starfield.draw.connect(_draw_starfield)
	add_child(_starfield)
	_show_grid = bool(_load_setting("show_grid", false))
	_always_moves = bool(_load_setting("always_show_moves", false))
	_camera_follow = bool(_load_setting("camera_follow", true))
	_world = Node2D.new()
	add_child(_world)
	_backdrop = Node2D.new()
	# The cloud art adds onto the starfield, so the stars show through its dark parts.
	var add := CanvasItemMaterial.new()
	add.blend_mode = CanvasItemMaterial.BLEND_MODE_ADD
	_backdrop.material = add
	_backdrop.draw.connect(_draw_backdrop)
	_world.add_child(_backdrop)
	_ship_layer = Node2D.new()
	_world.add_child(_ship_layer)
	_beams = Node2D.new()
	_beams.material = BeamDraw.edge_material()
	_beams.texture_filter = BeamDraw.TEXTURE_FILTER
	_beams.draw.connect(_draw_beams)
	_world.add_child(_beams)
	_marks = Node2D.new()
	_marks.draw.connect(_draw_marks)
	_world.add_child(_marks)
	_build_hud()
	resized.connect(func():
		if not state.is_empty():
			_refresh_timeline())
	hide()
	if art != null:
		art.loaded.connect(func(_key: String):
			if visible:
				_refresh_timeline()
				_refresh_strip()
				for index in _ships.keys():
					_apply_texture(index))


# ---- State ----

func is_open() -> bool:
	return visible


## The "combat" snapshot (and each request's scene_state): the battle after a step.
func show_state(st: Dictionary) -> void:
	if not bool(st.get("active", false)):
		close()
		return
	if not state.is_empty():
		_diff_effects(state, st)
	state = st
	_legal.clear()
	for code in st.get("legal", []):
		_legal[int(code)] = true
	_order.clear()
	for index in st.get("order", []):
		_order[int(index)] = true
	if not visible:
		_open()
	_sync_ships()
	_refresh_hud()
	_marks.queue_redraw()


## A decision for one of the player's ships.
func on_request(id: int, request: Dictionary) -> void:
	_request_id = id
	var st: Dictionary = request.get("scene_state", {})
	if not st.is_empty():
		show_state(st)
	if request.has("error"):
		_show_toast(String(request.error))
	var acting := int(state.get("ship", -1))
	if acting != _last_acting:
		_last_acting = acting
		var ship := _ship(acting)
		if not ship.is_empty() and not _on_screen(_center(ship)):
			_cam_goal = _center(ship)
	_refresh_hud()
	_marks.queue_redraw()
	grab_focus()


## WP3.9d: an effect the rules wait for (combat_native.cpp, "animate" requests): plays it, then
## answers. One at a time: the engine posts the next only after this answer.
func on_animate(id: int, request: Dictionary) -> void:
	if not _animating.is_empty():
		_finish_animation() # cannot happen (the engine waits), but never leave one unanswered
	var event: Dictionary = request.get("event", {})
	var st: Dictionary = request.get("scene_state", {})
	var dur := 0.0
	var event_kind := String(event.get("kind", ""))
	animated[event_kind] = int(animated.get(event_kind, 0)) + 1
	if event.has("capture"):
		_captures_seen = true # before the state: its damage and shield hits are in the capture
	if not st.is_empty():
		show_state(st)
	if visible:
		_frame_enemy_fire(event)
		var index := int(event.get("ship", -1))
		match String(event.get("kind", "")):
			"move":
				var ship := _ship(index)
				if _ships.has(index) and not ship.is_empty():
					var off := _center(ship) - Vector2(float(ship.get("x", 0)), float(ship.get("y", 0))) * 20.0
					dur = float(event.get("steps", 1)) * MOVE_STEP_TIME * _move_factor()
					var e: Dictionary = _ships[index]
					var p0 := Vector2(float(event.get("x0", 0)), float(event.get("y0", 0))) + off
					# Continue from where the ship is shown when the segment follows the last one.
					if e.shown.distance_to(p0) < 30.0:
						p0 = e.shown
					e.motion = {"p0": p0, "p1": Vector2(float(event.get("x1", 0)), float(event.get("y1", 0))) + off, "t": 0.0, "dur": dur}
			"turn":
				dur = float(event.get("steps", 1)) * TURN_STEP_TIME * _move_factor()
			"beam":
				if event.has("capture"):
					_captures_seen = true
					var cap: Dictionary = event.capture
					var entry := int(cap.get("entry", 0))
					art.sprite("capture", entry) # asked now; played once it has arrived
					_effects.append({"kind": "capture", "t": 0.0, "life": float(cap.get("frames", 1)) * CAPTURE_FRAME_TIME,
						"entry": entry, "at": Vector2(float(cap.get("x", 0)), float(cap.get("y", 0))), "frames": int(cap.get("frames", 1)),
						"waiting": true})
					dur = float(cap.get("frames", 1)) * CAPTURE_FRAME_TIME
					_animating_waits_capture = entry
					_capture_asked_at = Time.get_ticks_msec()
				else:
					dur = play_event(event)
			"destroyed":
				dur = _play_death(event) # the rules wait while it plays, as Destroy_Ship_FX
			# The specials as the original draws them (combat graphics, owner 2026-11-22).
			"stasis", "tractor", "plasma_web", "stellar", "black_hole", "fighters", "bombs", "gyro", "blast", "retreat", "lightning", "anti_missile", "missile_kill", "teleport", "cloak":
				dur = _play_special(event) # combat graphics (owner 2026-11-22), combat-effects-legacy.md
			"absorber":
				dur = 0.0 # drawn as its beam volley (weapon 46, the beam event before it)
			"missiles_step":
				dur = 0.055 # one legacy frame: the missiles' positions are in the state
			"missile_launch", "missile_arrive", "damage":
				dur = 0.25
			_:
				dur = 0.0
	_animating = {"id": id, "t": 0.0, "dur": dur}
	if dur <= 0.0:
		_finish_animation()


## The battlefield area the HUD leaves clear (screen pixels): under the Options tab, above the
## buttons' tab.
func _clear_area() -> Rect2:
	var top := 0.0
	var bottom := size.y
	if _options_tab != null:
		top = _options_tab.get_rect().end.y + 12.0
	if _bottom != null:
		bottom = _bottom.get_rect().position.y - 12.0
	return Rect2(Vector2(24.0, top), Vector2(size.x - 48.0, maxf(bottom - top, 100.0)))


## The camera frames the action (owner 2026-11-22) on the enemy's turn and on the player's while
## Auto plays: a move (the ship's from and to), then each beam or beam-like special (shooter and
## target). Each gets its own zoom, in or out, and centre: the ships sit FRAME_MARGIN screen pixels
## inside the area the HUD leaves clear. The effect (and a move's motion) waits until the camera is
## there. Missiles, torpedoes and fighters don't need it.
func _frame_enemy_fire(event: Dictionary) -> void:
	if not _camera_follow:
		return # the player's choice (owner 2026-11-22: some hate the camera taken over)
	var kind := String(event.get("kind", ""))
	var points: Array = []
	var radius := 0.0
	var actor := -1
	if kind == "move":
		actor = int(event.get("ship", -1))
		var ship := _ship(actor)
		if not _alive(ship) or not _ships.has(actor):
			return
		var off := _center(ship) - Vector2(float(ship.get("x", 0)), float(ship.get("y", 0))) * 20.0
		points = [Vector2(float(event.get("x0", 0)), float(event.get("y0", 0))) + off, Vector2(float(event.get("x1", 0)), float(event.get("y1", 0))) + off]
		radius = _radius(actor)
	elif FRAMED_KINDS.has(kind):
		actor = int(event.get("from", -1))
		if not _ships.has(actor):
			return
		var to := int(event.get("to", -1))
		points = [_ships[actor].shown]
		radius = _radius(actor)
		if bool(event.get("missile", false)):
			points.append(Vector2(float(event.get("tx", 0)), float(event.get("ty", 0))))
		elif _ships.has(to):
			points.append(_ships[to].shown)
			radius = maxf(radius, _radius(to))
		else:
			return
	else:
		return
	var who := _ship(actor)
	# The player's own ships only while Auto plays them.
	if not _alive(who) or (_mine(who) and int(state.get("auto", 0)) == 0):
		return
	var box := Rect2(points[0], Vector2.ZERO)
	for point: Vector2 in points:
		box = box.expand(point)
	box = box.grow(radius)
	var area := _clear_area()
	# Owner 2026-11-22: when the shooter and the target (each whole ship) are already in view, in
	# the area the HUD leaves clear, the camera stays where it is.
	if _frame.is_empty() and _cam_goal == Vector2.INF and _zoom_goal < 0.0:
		var seen := Rect2(_to_screen(box.position), Vector2.ZERO).expand(_to_screen(box.end))
		if area.encloses(seen):
			return
	var room := area.size - Vector2(2.0 * FRAME_MARGIN, 2.0 * FRAME_MARGIN)
	var fit := minf(room.x / maxf(box.size.x, 1.0), room.y / maxf(box.size.y, 1.0))
	var zoom := clampf(fit, MIN_ZOOM, MAX_ZOOM)
	var cam := box.get_center() - (area.get_center() - size / 2.0) / zoom
	if _cam.distance_to(cam) * zoom < 2.0 and absf(zoom - _zoom) < 0.01:
		return # already framed so
	framed += 1
	_cam_goal = Vector2.INF
	_zoom_goal = -1.0
	var dur := Themes.saved_flight_time(1.0)
	if dur <= 0.0:
		_cam = cam
		_zoom = zoom
		return
	_frame = {"from_cam": _cam, "to_cam": cam, "from_zoom": _zoom, "to_zoom": zoom, "t": 0.0, "dur": dur}
	_cam_hold = 0.0001


const PLACEHOLDER_COLORS := {"stasis": Color(0.6, 0.85, 1.0), "tractor": Color(0.5, 0.7, 1.0), "plasma_web": Color(0.5, 1.0, 0.5),
	"stellar": Color(1.0, 1.0, 1.0), "black_hole": Color(0.6, 0.3, 0.9), "absorber": Color(1.0, 0.6, 0.9), "fighters": Color(1.0, 0.9, 0.5),
	"bombs": Color(1.0, 0.55, 0.3)}


## A special's stand-in: one line from the shooter (or a fighter stack's point) to the target.
func _placeholder_line(event: Dictionary) -> float:
	var kind := String(event.get("kind", ""))
	var from := int(event.get("from", -1))
	var to := int(event.get("to", -1))
	var point := Vector2(float(event.get("x", 0)), float(event.get("y", 0)))
	if kind == "fighters":
		from = -1
	_effects.append({"kind": "shot", "t": 0.0, "life": 0.5, "from": from, "to": to, "missile": false, "point": point,
		"jitter": Vector2.ZERO, "color": PLACEHOLDER_COLORS.get(kind, AMBER), "projectile": false, "from_point": point})
	return 0.5


func _finish_animation() -> void:
	var id := int(_animating.get("id", -1))
	_animating = {}
	for e: Dictionary in _ships.values():
		if e.has("motion"):
			e.shown = e.motion.p1
			e.erase("motion")
	if id >= 0:
		game.answer_request(id, 1)


func close() -> void:
	if not visible:
		return
	if not _animating.is_empty():
		_finish_animation()
	hide()
	_opened = false
	_request_id = -1
	_close_menu()
	for entry in _ships.values():
		entry.sprite.queue_free()
	_ships.clear()
	_effects.clear()
	state = {}
	closed.emit()


func _open() -> void:
	_tl_round = -1 # a new battle: its own initiative cycle
	_tl_base = []
	_tl_shown = []
	show()
	move_to_front()
	_opened = true
	_last_acting = -1
	_fit_fleets()
	grab_focus()
	opened.emit()


func _my_turn() -> bool:
	return _request_id >= 0


func _ships_list() -> Array:
	return state.get("ships", [])


func _ship(index: int) -> Dictionary:
	var ships := _ships_list()
	if index < 0 or index >= ships.size():
		return {}
	return ships[index]


func _alive(ship: Dictionary) -> bool:
	return not ship.is_empty() and int(ship.get("status", 1)) == 0


func _mine(ship: Dictionary) -> bool:
	return int(ship.get("owner", -1)) == int(state.get("player", -2))


func _center(ship: Dictionary) -> Vector2:
	return Vector2(float(ship.get("cx", 0)), float(ship.get("cy", 0)))


func _color(ship: Dictionary) -> Color:
	var c := int(ship.get("color", -1))
	return PLAYER_COLORS[c] if c >= 0 and c < PLAYER_COLORS.size() else MONSTER_COLOR


func _acted(index: int) -> bool:
	return index != int(state.get("ship", -1)) and not _order.has(index)


# ---- Ships ----

func _sync_ships() -> void:
	var seen := {}
	for ship: Dictionary in _ships_list():
		var index := int(ship.i)
		seen[index] = true
		if not _alive(ship):
			if _ships.has(index):
				_ships[index].sprite.queue_free()
				_ships.erase(index)
			continue
		var sprite_info: Dictionary = ship.get("sprite", {})
		var lbx := String(sprite_info.get("lbx", ""))
		var entry := int(sprite_info.get("entry", -1))
		if not _ships.has(index):
			var sprite := Sprite2D.new()
			sprite.centered = false # placed by each frame's origin in the sprite's box (_apply_texture)
			sprite.region_enabled = true # padded past the texture: room for the shield glow
			sprite.texture_filter = CanvasItem.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS
			var material := ShaderMaterial.new()
			material.shader = SHIELD_SHADER
			sprite.material = material
			_ship_layer.add_child(sprite)
			_ships[index] = {"sprite": sprite, "shown": _center(ship), "angle": _facing_angle(ship), "key": ""}
		var entry_data: Dictionary = _ships[index]
		entry_data["goal"] = _center(ship)
		entry_data["goal_angle"] = _facing_angle(ship)
		entry_data["lbx"] = lbx
		entry_data["entry"] = entry
		entry_data["planet"] = bool(ship.get("planet", false))
		_apply_texture(index)
		_apply_shield(index, ship)
	for index in _ships.keys():
		if not seen.has(index):
			_ships[index].sprite.queue_free()
			_ships.erase(index)


func _facing_angle(ship: Dictionary) -> float:
	if bool(ship.get("planet", false)):
		return 0.0
	return -float(int(ship.get("facing", 0))) * PI / 8.0


func _apply_texture(index: int) -> void:
	var e: Dictionary = _ships[index]
	var sprite: Sprite2D = e.sprite
	var s: Dictionary = art.sprite(e.lbx, e.entry)
	if s.is_empty():
		return
	var frames: Array = s.frames
	# Combat ships: frames 0-3 face east; 0 is the hull alone, 1-3 add the engines' flames, so only
	# 1-3 cycle (owner, 2026-11-15: the flameless frame made every ship blink once a cycle).
	var frame := 0
	if not e.planet and frames.size() >= 4:
		frame = 1 + _anim_frame % 3
	frame = clampi(frame, 0, frames.size() - 1)
	var tex: Texture2D = frames[frame]
	if sprite.texture != tex:
		sprite.texture = tex
	sprite.scale = Vector2.ONE / float(s.scale)
	# Each frame sits at its own origin in the sprite's box (frames 1-3 are wider: the flames), and
	# the box's centre is the ship's: the hull no longer jumps between frames.
	var box: Vector2 = s.size
	var origin: Vector2 = s.origins[frame]
	# Room for the full shield round the frame: the most a legacy shield grows (10 layers), so the
	# padding never changes with damage.
	var pad := 10.0 * 1.2 * float(s.scale) + 2.0 * float(s.scale)
	if e.planet:
		pad = 0.0
	sprite.region_rect = Rect2(-pad, -pad, tex.get_width() + 2.0 * pad, tex.get_height() + 2.0 * pad)
	sprite.offset = (origin - box / 2.0) * float(s.scale) - Vector2(pad, pad)
	e["pad"] = pad
	e["size"] = box
	e["art_scale"] = float(s.scale)
	var material := sprite.material as ShaderMaterial
	if material != null and not e.planet and frames.size() >= 4:
		var hull: Texture2D = frames[0]
		material.set_shader_parameter("hull_tex", hull)
		material.set_shader_parameter("hull_offset", (s.origins[0] - origin) * float(s.scale))
		material.set_shader_parameter("hull_size", Vector2(hull.get_width(), hull.get_height()))
		material.set_shader_parameter("use_hull", 1.0)
	_apply_shield(index, _ship(index))


func _apply_shield(index: int, ship: Dictionary) -> void:
	var e: Dictionary = _ships[index]
	var material: ShaderMaterial = e.sprite.material
	var shield: Dictionary = ship.get("shield", {})
	var layers := float(shield.get("layers", 0))
	var art_scale := float(e.get("art_scale", 1.0))
	# One legacy outline layer was one 1x pixel on a 640-pixel screen; at HD sizes it reads as more.
	var thickness := layers * 1.2 * art_scale
	var max_arc := maxf(float(shield.get("max", 0)), 1.0)
	var arcs: Array = shield.get("arcs", [0, 0, 0, 0])
	var strengths := Vector4(
		clampf(float(arcs[0]) / max_arc, 0.0, 1.0), clampf(float(arcs[1]) / max_arc, 0.0, 1.0),
		clampf(float(arcs[2]) / max_arc, 0.0, 1.0), clampf(float(arcs[3]) / max_arc, 0.0, 1.0))
	material.set_shader_parameter("arcs", strengths)
	material.set_shader_parameter("thickness_px", 0.0 if e.planet else thickness)
	var show_health := _always_health or _alt_held or index == _hover_ship
	material.set_shader_parameter("shield_on", 1.0 if show_health else 0.0)
	e.sprite.modulate = Color(1, 1, 1, 0.62) if _acted(index) and not e.planet else Color.WHITE


func _process(delta: float) -> void:
	if not visible:
		return
	_anim_clock += delta
	if _anim_clock >= ANIM_TIME:
		_anim_clock = 0.0
		_anim_frame += 1
		for index in _ships.keys():
			_apply_texture(index)
	# Ships ease to their positions and facings: snapshots come every few legacy frames.
	for index in _ships.keys():
		var e: Dictionary = _ships[index]
		var goal: Vector2 = e.get("goal", e.shown)
		if e.has("motion"): # WP3.9d: a move segment at the legacy pace
			var m: Dictionary = e.motion
			if _cam_hold <= 0.0 and _art_hold <= 0.0:
				m.t += delta # a move starts once the camera frames it
			var k := clampf(m.t / maxf(m.dur, 0.001), 0.0, 1.0)
			e.shown = (m.p0 as Vector2).lerp(m.p1, k)
			# Owner 2026-11-22: a ship moves in a straight line facing the way it travels, and turns
			# to its final facing only once it has arrived (the next segment follows within moments).
			var travel: Vector2 = (m.p1 as Vector2) - (m.p0 as Vector2)
			if travel.length() > 0.5:
				e["travel_angle"] = travel.angle()
			e["travel_hold"] = 0.15
			if k >= 1.0:
				e.erase("motion")
		else:
			e.shown = e.shown.lerp(goal, minf(1.0, delta * 18.0))
			e["travel_hold"] = float(e.get("travel_hold", 0.0)) - delta
		var goal_angle: float = e.get("goal_angle", e.angle)
		if e.has("travel_angle") and float(e.get("travel_hold", 0.0)) > 0.0:
			goal_angle = e.travel_angle
		elif e.has("travel_angle"):
			e.erase("travel_angle")
		e.angle = lerp_angle(e.angle, goal_angle, minf(1.0, delta * 14.0))
		e.sprite.position = e.shown + _ship_shake(index)
		e.sprite.rotation = e.angle
		var look := _ship_effect_look(index)
		var ship_now := _ship(index)
		var status := int(ship_now.get("special_status", 0))
		# Plasma_Darken: a cloak turning on or off (its effect), else a cloaked ship's lasting
		# darkness 6 on frame (ship_frame + ship x 2) % 16.
		var dark: Array = _cloak_darkness(index)
		if dark.is_empty() and (status == 1 or status == 5):
			dark = [6.0, (_legacy_frame() + index * 2) % 16]
		if status == 4: # phased out: own ships faint, others unseen
			look.a *= 0.25 if _mine(ship_now) else 0.0
		e.sprite.self_modulate = look
		var mat := e.sprite.material as ShaderMaterial
		if mat != null:
			mat.set_shader_parameter("darkness", 0.0 if dark.is_empty() else float(dark[0]))
			if not dark.is_empty():
				mat.set_shader_parameter("plasma_tex", _plasma_texture())
				mat.set_shader_parameter("plasma_frame", int(dark[1]))
				mat.set_shader_parameter("art_scale", float(e.get("art_scale", 1.0)))
			mat.set_shader_parameter("brighten", _shield_glow.get(index, 0.0))
	for index in _shield_glow.keys():
		_shield_glow[index] = float(_shield_glow[index]) - delta * 8.0
		if float(_shield_glow[index]) <= 0.0:
			_shield_glow.erase(index)
	if _cam_goal != Vector2.INF:
		_cam = _cam.lerp(_cam_goal, minf(1.0, delta * (9.0 if _cam_hold > 0.0 else 6.0)))
		if _cam.distance_to(_cam_goal) * _zoom < 1.5:
			_cam = _cam_goal
			_cam_goal = Vector2.INF
	if _animating_waits_capture >= 0 and not art.sprite("capture", _animating_waits_capture).is_empty():
		print("capture: volley %d arrived %d ms after its event" % [_animating_waits_capture, Time.get_ticks_msec() - _capture_asked_at])
		_animating_waits_capture = -1
	if smoke_hold:
		_place_world()
		_marks.queue_redraw()
		_beams.queue_redraw()
		return
	if _zoom_goal > 0.0:
		_zoom = lerpf(_zoom, _zoom_goal, minf(1.0, delta * (9.0 if _cam_hold > 0.0 else 6.0)))
		if absf(_zoom - _zoom_goal) < 0.005:
			_zoom = _zoom_goal
			_zoom_goal = -1.0
	if _art_hold > 0.0:
		_art_hold += delta
		_art_wait = _art_wait.filter(func(key: Array) -> bool:
			return art.sprite(String(key[0]), int(key[1])).is_empty() and not art.settled(String(key[0]), int(key[1])))
		if _art_wait.is_empty() or _art_hold > ART_WAIT_MAX:
			_art_hold = 0.0
		else:
			_advance_effects(delta, true)
			_place_world()
			_layout()
			_marks.queue_redraw()
			_beams.queue_redraw()
			return # the volley starts once its art is here
	if not _frame.is_empty():
		_frame.t = float(_frame.t) + delta
		var fk := smoothstep(0.0, 1.0, clampf(float(_frame.t) / float(_frame.dur), 0.0, 1.0))
		_cam = (_frame.from_cam as Vector2).lerp(_frame.to_cam, fk)
		_zoom = lerpf(float(_frame.from_zoom), float(_frame.to_zoom), fk)
		if fk >= 1.0:
			_frame = {}
	if _cam_hold > 0.0:
		_cam_hold += delta
		var arrived := _frame.is_empty() and _cam_goal == Vector2.INF and _zoom_goal < 0.0
		if arrived or _cam_hold > FRAME_WAIT_MAX + Themes.saved_flight_time(1.0):
			_cam_hold = 0.0
		else:
			_advance_effects(delta, true)
			_place_world()
			_layout()
			_marks.queue_redraw()
			_beams.queue_redraw()
			return # the effect and the rules' wait start once the camera is in place
	if not _animating.is_empty() and _animating_waits_capture < 0:
		_animating.t += delta
		if _animating.t >= _animating.dur:
			_finish_animation()
	_advance_effects(delta, false)
	_place_world()
	_fill_waiting_chips()
	_ease_scrolls(delta)
	_layout()
	_update_hover()
	if _toast_time > 0.0:
		_toast_time -= delta
		if _toast_time <= 0.0:
			_toast.hide()
	_marks.queue_redraw()
	_beams.queue_redraw()


## Effects' clocks. While the camera or a volley's art holds the next effect, the ones already
## playing go on (owner 2026-11-22: an explosion froze while the camera framed the next shot, and
## looked half as fast as the original's); a new one (t <= 0) waits.
func _advance_effects(delta: float, started_only: bool) -> void:
	for effect: Dictionary in _effects:
		if started_only and float(effect.t) <= 0.0:
			continue
		if effect.get("waiting", false):
			if art.sprite("capture", int(effect.entry)).is_empty():
				continue
			effect.waiting = false
		effect.t += delta
		if String(effect.kind) == "sfx":
			_effect_sounds(effect)
		if String(effect.kind) == "capture" and effect.t >= effect.life:
			art.forget("capture", int(effect.entry))
	_effects = _effects.filter(func(effect: Dictionary) -> bool: return effect.t < effect.life)


## A special's sounds play when its animation reaches them (owner 2026-11-22: the stellar
## converter's sounded while the camera was still on its way).
func _effect_sounds(effect: Dictionary) -> void:
	for part: Dictionary in effect.parts:
		if String(part.type) == "sound" and not part.get("played", false) and float(effect.t) >= float(part.get("start", 0.0)):
			part["played"] = true
			_sound(int(part.id))


# ---- Camera ----

func _place_world() -> void:
	_world.scale = Vector2(_zoom, _zoom)
	_world.position = size / 2.0 - _cam * _zoom
	_backdrop.queue_redraw()
	if _starfield != null:
		_starfield.queue_redraw()


## STARBG.LBX 0-2 (far to near) tiled across the window, sliding slower than the field as the
## camera moves (galaxy/backdrop.gd's speeds, a little faster: the battle's camera moves less).
const STAR_SPEEDS := [0.06, 0.12, 0.24]


func _draw_starfield() -> void:
	if art == null:
		return
	var legacy := maxf(size.y / 480.0, 1.0)
	var camera_screen := _cam * _zoom
	for layer in 3:
		var tex: Texture2D = art.texture("STARBG.LBX", layer)
		if tex == null:
			continue
		var tile := Vector2(640.0, 480.0) * legacy
		var offset: Vector2 = -camera_screen * float(STAR_SPEEDS[layer])
		offset = Vector2(fposmod(offset.x, tile.x), fposmod(offset.y, tile.y)) - tile
		var y: float = offset.y
		while y < size.y:
			var x: float = offset.x
			while x < size.x:
				_starfield.draw_texture_rect(tex, Rect2(Vector2(x, y), tile), false)
				x += tile.x
			y += tile.y


func _to_world(screen: Vector2) -> Vector2:
	return (screen - _world.position) / _zoom


func _to_screen(world: Vector2) -> Vector2:
	return world * _zoom + _world.position


func _on_screen(world: Vector2) -> bool:
	var at := _to_screen(world)
	return Rect2(Vector2(80, 80), (size - Vector2(160, 260)).max(Vector2.ONE)).has_point(at)


func _field_size() -> Vector2:
	var grid: Dictionary = state.get("grid", {})
	return Vector2(float(grid.get("width", 81)), float(grid.get("height", 68))) * SQUARE


## Opens on both fleets: everything alive fits, with room for the bars.
func _fit_fleets() -> void:
	var box := Rect2()
	var first := true
	for ship: Dictionary in _ships_list():
		if not _alive(ship):
			continue
		var c := _center(ship)
		if first:
			box = Rect2(c, Vector2.ZERO)
			first = false
		else:
			box = box.expand(c)
	if first:
		box = Rect2(Vector2.ZERO, _field_size())
	box = box.grow(140.0)
	var view := size - Vector2(0, 220)
	# Close enough to read the ships (the legacy screen showed 32 x 18 squares at about 2x): the
	# whole battle when it fits at that size, else the middle of it; the wheel goes wider.
	_zoom = clampf(minf(view.x / box.size.x, view.y / box.size.y), 1.2, 2.0)
	_cam = box.get_center() + Vector2(0, 50) / _zoom
	_cam_goal = Vector2.INF


func _zoom_at(screen: Vector2, factor: float) -> void:
	var before := _to_world(screen)
	_zoom = clampf(_zoom * factor, MIN_ZOOM, MAX_ZOOM)
	_place_world()
	var after := _to_world(screen)
	_cam += before - after
	_cam_goal = Vector2.INF


# ---- Drawing ----

func _draw_backdrop() -> void:
	var tex: Texture2D = art.texture("combat.lbx", 46, 0)
	var top_left := _to_world(Vector2.ZERO)
	var bottom_right := _to_world(size)
	if tex != null:
		var tile := Vector2(640, 480)
		var start := Vector2(floorf(top_left.x / tile.x) * tile.x, floorf(top_left.y / tile.y) * tile.y)
		var y := start.y
		while y < bottom_right.y:
			var x := start.x
			while x < bottom_right.x:
				_backdrop.draw_texture_rect(tex, Rect2(Vector2(x, y), tile), false)
				x += tile.x
			y += tile.y
	# The field's edge: legacy ships can't leave it.
	_backdrop.draw_rect(Rect2(Vector2.ZERO, _field_size()), Color(0.55, 0.65, 0.8, 0.18), false, 2.0 / _zoom)
	if _show_grid: # Options: the field's squares, faint
		var field := _field_size()
		var line := Color(0.55, 0.65, 0.8, 0.10)
		var x := SQUARE
		while x < field.x:
			_backdrop.draw_line(Vector2(x, 0), Vector2(x, field.y), line, 1.0 / _zoom)
			x += SQUARE
		var y := SQUARE
		while y < field.y:
			_backdrop.draw_line(Vector2(0, y), Vector2(field.x, y), line, 1.0 / _zoom)
			y += SQUARE


func _draw_marks() -> void:
	if state.is_empty():
		return
	var acting := int(state.get("ship", -1))
	var acting_ship := _ship(acting)
	var width := 1.0 / _zoom
	# The acting ship's legal squares lit as the original lights its grid (owner 2026-11-22), each
	# where the ship would stand (its centre), not the square's own centre: the dots drew a big
	# ship's squares half a ship off. Shown always (Options) or while the pointer is on the field.
	if _my_turn() and _alive(acting_ship) and (_always_moves or _pointer_on_field()):
		var half := Vector2(SQUARE, SQUARE) / 2.0
		for code in state.get("legal", []):
			var square := Vector2i(int(code) / 1000, int(code) % 1000)
			var at := _square_center(square, acting_ship)
			var lit := square == _hover_square and _hover_ship < 0
			_marks.draw_rect(Rect2(at - half + Vector2(1, 1), half * 2.0 - Vector2(2, 2)), Color(AMBER, 0.30 if lit else 0.10))
			_marks.draw_rect(Rect2(at - half + Vector2(1, 1), half * 2.0 - Vector2(2, 2)), Color(AMBER, 0.55 if lit else 0.28), false, width)
	# The acting ship's ring.
	if _alive(acting_ship) and _ships.has(acting):
		var e: Dictionary = _ships[acting]
		var radius := _radius(acting)
		_dashed_circle(e.shown, radius + 6.0, Color(AMBER, 0.85 if _my_turn() else 0.45), 2.0 * width)
	# Hull crescents: the hovered ship, or every ship with "Always show health" (or Alt held).
	for index in _ships.keys():
		var ship := _ship(index)
		if not _alive(ship):
			continue
		if _always_health or _alt_held or index == _hover_ship:
			_draw_crescent(_ships[index].shown, _radius(index) + 9.0 * maxf(1.0, 1.2 / _zoom), _view_ship(ship), width)
	# Previews: a move to the hovered square, or fire at the hovered enemy.
	if _my_turn() and _alive(acting_ship) and _ships.has(acting):
		var from: Vector2 = _ships[acting].shown
		var hovered := _ship(_hover_ship)
		if _alive(hovered) and not _mine(hovered):
			var at: Vector2 = _ships[_hover_ship].shown
			_marks.draw_dashed_line(from, at, Color(FIRE, 0.8), 2.0 * width, 8.0 * width)
		elif _hover_ship < 0 and _square_legal(_hover_square):
			var to := _square_center(_hover_square, acting_ship)
			_marks.draw_dashed_line(from, to, Color(AMBER, 0.9), 2.5 * width, 6.0 * width)
			_marks.draw_circle(to, 4.0 * width * 1.5, AMBER)
			var e: Dictionary = _ships[acting]
			var tex: Texture2D = e.sprite.texture
			if tex != null:
				# The ship ends its move facing the way it travelled (legacy Move_Ship turns it to the
				# destination, Get_Facing: 16 steps, counter-clockwise from east).
				var travel := to - from
				var facing := posmod(int(round(-travel.angle() / (PI / 8.0))), 16)
				var end_angle: float = -float(facing) * PI / 8.0 if travel.length() > 1.0 else e.angle
				_marks.draw_set_transform(to, end_angle, e.sprite.scale)
				_marks.draw_texture(tex, e.sprite.offset + Vector2(e.get("pad", 0.0), e.get("pad", 0.0)), Color(1, 1, 1, 0.35))
				_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	# Missiles, torpedoes and fighters in flight: the original's pictures (CMBTMISL / CMBTFGTR by
	# type and facing, laid out as CMBTMIS::Draw_Missile does; fighters in their side's colours),
	# a stack without its art yet as a marker in the owner's colour.
	var font := get_theme_default_font()
	for missile: Dictionary in state.get("missiles", []):
		var at := Vector2(float(missile.get("x", 0)), float(missile.get("y", 0)))
		if _draw_flyer(missile, at):
			continue
		var owner_color := MONSTER_COLOR
		for ship: Dictionary in _ships_list():
			if int(ship.get("owner", -1)) == int(missile.get("owner", -2)):
				owner_color = _color(ship)
				break
		var heading := -float(int(missile.get("facing", 0))) * PI / 8.0
		var tip := at + Vector2(cos(heading), sin(heading)) * 7.0 * width * 1.5
		_marks.draw_line(at, tip, owner_color, 2.0 * width)
		_marks.draw_circle(at, 3.0 * width * 1.5, owner_color)
	_draw_lasting()
	_draw_effects(width)
	# The order ring.
	if _menu_ship >= 0 and _ships.has(_menu_ship):
		_draw_menu(_ships[_menu_ship].shown, width)


## A flying stack's pictures (false while its art is on its way).
func _draw_flyer(missile: Dictionary, at: Vector2) -> bool:
	if not missile.has("lbx"):
		return false
	var name := String(missile.lbx).to_lower()
	if bool(missile.get("fighter", false)) and not bool(missile.get("monster", false)):
		var colors: Array = (state.get("side_colors", {}) as Dictionary).get(String(missile.get("side", "attacker")), [])
		if colors.size() == 16:
			var hex := PackedStringArray()
			for c in colors:
				hex.append("%06x" % int(c))
			name += "~80=" + ".".join(hex) # COMBINIT: the fighters' 0x50-0x5F take the side's colours
	var entry := int(missile.get("entry", 0))
	var s: Dictionary = art.sprite(name, entry)
	if s.is_empty():
		return false
	var frames: int = s.frames.size()
	var pictures: Array = missile.get("pictures", [])
	var fighter := bool(missile.get("fighter", false))
	for n in pictures.size():
		var p: Array = pictures[n]
		var frame := (_anim_frame + n + int(missile.get("i", 0))) % maxi(frames, 1)
		var jitter := Vector2.ZERO
		if fighter:
			# CMBTMIS::Draw_Fighter: each picture -1..+1 px off, a new offset every third frame.
			var h := hash([int(missile.get("i", 0)), n, _legacy_frame() / 3])
			jitter = Vector2(float(h % 3) - 1.0, float((h / 3) % 3) - 1.0)
		art.draw(_marks, name, entry, frame, at + Vector2(float(p[0]), float(p[1])) + jitter, 1.0)
	return true


func _radius(index: int) -> float:
	var e: Dictionary = _ships.get(index, {})
	var s: Vector2 = e.get("size", Vector2(40, 40))
	return maxf(s.x, s.y) * 0.5


func _dashed_circle(at: Vector2, radius: float, color: Color, line: float) -> void:
	var steps := 40
	for i in steps:
		if i % 2 == 1:
			continue
		var a0 := TAU * float(i) / steps
		_marks.draw_arc(at, radius, a0, a0 + TAU / steps, 4, color, line)


## The hull crescent (B4): armour on the outer arc, shrinking from both tips to the centre; inside,
## structure on the left and systems on the right, each empty at the centre.
func _draw_crescent(at: Vector2, radius: float, ship: Dictionary, width: float) -> void:
	var half := PI / 3.0
	var bottom := PI / 2.0
	var line := 3.5 * width * maxf(1.0, _zoom * 0.8)
	var gap := line * 1.9
	var armor := _fraction(ship.get("armor", {}))
	var structure := _fraction(ship.get("structure", {}))
	var systems := _fraction(ship.get("systems", {}))
	var inner := radius - gap
	var notch := 0.03
	_marks.draw_arc(at, radius, bottom - half, bottom + half, 40, TRACK_COLOR, line, true)
	_marks.draw_arc(at, inner, bottom + notch, bottom + half, 24, TRACK_COLOR, line, true)
	_marks.draw_arc(at, inner, bottom - half, bottom - notch, 24, TRACK_COLOR, line, true)
	if armor > 0.0:
		_marks.draw_arc(at, radius, bottom - half * armor, bottom + half * armor, 40, ARMOR_COLOR, line, true)
	if structure > 0.0:
		_marks.draw_arc(at, inner, bottom + notch, bottom + notch + (half - notch) * structure, 24, STRUCTURE_COLOR, line, true)
	if systems > 0.0:
		_marks.draw_arc(at, inner, bottom - notch - (half - notch) * systems, bottom - notch, 24, SYSTEMS_COLOR, line, true)


## Health as the bars show it: during a volley, the target's as each weapon lands (owner
## 2026-11-22), from the engine's per-weapon record (the beam event's "before" and shots' "after");
## else the state's.
const BARS_GROUP := &"combat_bars"
var _hp_view := {} # ship index -> {"effect": the volley, "pools": {armor, structure, systems}}


func _view_ship(ship: Dictionary) -> Dictionary:
	if ship.is_empty() or _hp_view.is_empty():
		return ship
	var index := int(ship.get("i", -1))
	var view: Dictionary = _hp_view.get(index, {})
	if view.is_empty():
		return ship
	var effect: Dictionary = view.effect
	if float(effect.t) >= float(effect.life) or not _effects.has(effect):
		_hp_view.erase(index)
		return ship
	var out := ship.duplicate()
	for key in ["armor", "structure", "systems"]:
		var pool: Dictionary = (ship.get(key, {}) as Dictionary).duplicate()
		pool["now"] = int((view.pools as Dictionary).get(key, pool.get("now", 0)))
		out[key] = pool
	return out


func _set_hp_view(index: int, effect: Dictionary, pools: Dictionary) -> void:
	if pools.is_empty():
		return
	_hp_view[index] = {"effect": effect, "pools": pools}
	get_tree().call_group(BARS_GROUP, "queue_redraw")


func _fraction(pool: Dictionary) -> float:
	var top := float(pool.get("max", 0))
	if top <= 0.0:
		return 0.0
	return clampf(float(pool.get("now", 0)) / top, 0.0, 1.0)


# ---- The order ring ----

func _menu_reason(item: String) -> String:
	var ship := _ship(_menu_ship)
	match item:
		"wait":
			return "" if bool(state.get("can_wait", true)) else "Not in this wait stage"
		"retreat":
			if bool(ship.get("retreating", false)):
				return "Already retreating"
			if bool(ship.get("planet", false)):
				return "A planet can't retreat"
			if int(ship.get("speed", 0)) < 1:
				return "No speed to retreat"
			return ""
		"board":
			return "Boarding comes in a later build"
		"scan":
			return ""
		"self_destruct":
			return "A planet can't self-destruct" if bool(ship.get("planet", false)) else ""
	return ""


func _draw_menu(at: Vector2, width: float) -> void:
	var inner := (_radius(_menu_ship) + 18.0 / _zoom)
	var outer := inner + 62.0 / _zoom
	var font := get_theme_default_font()
	for i in MENU_ITEMS.size():
		var item: String = MENU_ITEMS[i]
		var a0 := -PI / 2.0 - PI / 6.0 + i * PI / 3.0 + 0.04
		var a1 := a0 + PI / 3.0 - 0.08
		var refused := _menu_reason(item) != ""
		var fill := Color(0.06, 0.09, 0.15, 0.92)
		if item == "self_destruct":
			fill = Color(0.24, 0.08, 0.08, 0.92)
		if i == _menu_hover and not refused:
			fill = Color(AMBER, 0.32)
		var points := PackedVector2Array()
		var steps := 12
		for s in steps + 1:
			var a := lerpf(a0, a1, float(s) / steps)
			points.append(at + Vector2(cos(a), sin(a)) * outer)
		for s in steps + 1:
			var a := lerpf(a1, a0, float(s) / steps)
			points.append(at + Vector2(cos(a), sin(a)) * inner)
		_marks.draw_colored_polygon(points, Color(fill, fill.a * (0.55 if refused else 1.0)))
		points.append(points[0])
		_marks.draw_polyline(points, Color(0.29, 0.35, 0.45), width)
		var mid := (a0 + a1) / 2.0
		var label: String = MENU_LABELS[item]
		if item == "self_destruct" and _menu_confirm == "self_destruct":
			label = "SURE?"
		var color := Color(0.85, 0.88, 0.93)
		if refused:
			color = Color(0.42, 0.48, 0.56)
		elif item == "self_destruct":
			color = Color(1.0, 0.55, 0.48)
		var lines := label.split("\n")
		var font_size := 13
		var center := at + Vector2(cos(mid), sin(mid)) * (inner + outer) / 2.0
		_marks.draw_set_transform(center, 0.0, Vector2.ONE / _zoom)
		for l in lines.size():
			var text_size := font.get_string_size(lines[l], HORIZONTAL_ALIGNMENT_CENTER, -1, font_size)
			var y := (float(l) - (lines.size() - 1) / 2.0) * (font_size + 2) + font_size * 0.35
			_marks.draw_string(font, Vector2(-text_size.x / 2.0, y), lines[l], HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)
		_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	if _menu_hover >= 0:
		var reason := _menu_reason(MENU_ITEMS[_menu_hover])
		if reason == "" and MENU_ITEMS[_menu_hover] == "retreat":
			reason = "Leaves at the start of your next turn"
		if reason != "":
			_show_toast(reason, 0.2)


func _menu_index_at(screen: Vector2) -> int:
	if _menu_ship < 0 or not _ships.has(_menu_ship):
		return -1
	var at: Vector2 = _ships[_menu_ship].shown
	var w := _to_world(screen)
	var d := w.distance_to(at)
	var inner := _radius(_menu_ship) + 18.0 / _zoom
	var outer := inner + 62.0 / _zoom
	if d < inner or d > outer:
		return -1
	var a := fposmod((w - at).angle() + PI / 2.0 + PI / 6.0, TAU)
	return clampi(int(a / (PI / 3.0)), 0, MENU_ITEMS.size() - 1)


func _open_menu(index: int) -> void:
	_menu_ship = index
	_menu_hover = -1
	_menu_confirm = ""


func _close_menu() -> void:
	_menu_ship = -1
	_menu_hover = -1
	_menu_confirm = ""


func _menu_pick(i: int) -> void:
	var item: String = MENU_ITEMS[i]
	var reason := _menu_reason(item)
	if reason != "":
		_show_toast(reason)
		return
	if item == "scan":
		_close_menu()
		_arm_scan()
		return
	if item == "self_destruct" and _menu_confirm != "self_destruct":
		_menu_confirm = "self_destruct" # a second click confirms
		return
	_close_menu()
	_act({"action": item})


# ---- Hover and squares ----

func _update_hover() -> void:
	var mouse := get_local_mouse_position()
	var world := _to_world(mouse)
	var best := -1
	var best_d := INF
	for index in _ships.keys():
		var e: Dictionary = _ships[index]
		var d: float = world.distance_to(e.shown)
		if d < _radius(index) * 0.85 and d < best_d:
			best = index
			best_d = d
	if _chip_hover >= 0 and _ships.has(_chip_hover):
		best = _chip_hover # its initiative chip is under the pointer
	if _menu_ship >= 0:
		_menu_hover = _menu_index_at(mouse)
	if best != _hover_ship:
		_hover_ship = best
		for index in _ships.keys():
			_apply_shield(index, _ship(index))
	_hover_square = _square_at(world)
	_place_card()


## The square the acting ship would move to so that its centre lands nearest `world`.
func _square_at(world: Vector2) -> Vector2i:
	var acting := _ship(int(state.get("ship", -1)))
	if not _alive(acting):
		return Vector2i(-1, -1)
	var offset := _center(acting) - Vector2(float(acting.x), float(acting.y)) * SQUARE
	var sq := ((world - offset) / SQUARE).round()
	return Vector2i(int(sq.x), int(sq.y))


## The pointer is over the battlefield, not over a panel of the HUD.
func _pointer_on_field() -> bool:
	return get_viewport().gui_get_hovered_control() == self


func _square_center(square: Vector2i, ship: Dictionary) -> Vector2:
	var offset := _center(ship) - Vector2(float(ship.x), float(ship.y)) * SQUARE
	return Vector2(square) * SQUARE + offset


func _square_legal(square: Vector2i) -> bool:
	if square.x < 0:
		return false
	return _legal.has(square.x * 1000 + square.y)


# ---- Input ----

func _gui_input(event: InputEvent) -> void:
	# Events that bubbled up from the HUD (a scan list scrolled to its end, a label in a panel) are
	# the HUD's: they never zoom or act on the field (owner 2026-11-22).
	if (event is InputEventMouseButton) and not _pointer_on_field():
		accept_event()
		return
	var wheel := event as InputEventMouseButton
	if wheel and wheel.pressed and (wheel.button_index == MOUSE_BUTTON_WHEEL_UP or wheel.button_index == MOUSE_BUTTON_WHEEL_DOWN):
		_zoom_at(wheel.position, 1.12 if wheel.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0 / 1.12)
		accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion:
		if _drag_from != Vector2.INF and (motion.button_mask & (MOUSE_BUTTON_MASK_LEFT | MOUSE_BUTTON_MASK_RIGHT | MOUSE_BUTTON_MASK_MIDDLE)) != 0:
			if _dragged or motion.position.distance_to(_drag_from) > 6.0:
				_dragged = true
				_cam -= motion.relative / _zoom
				_cam_goal = Vector2.INF
		accept_event()
		return
	var button := event as InputEventMouseButton
	if button == null:
		return
	accept_event()
	if button.pressed:
		_drag_from = button.position
		_dragged = false
		return
	var was_drag := _dragged
	_drag_from = Vector2.INF
	_dragged = false
	if was_drag:
		return
	if button.button_index == MOUSE_BUTTON_LEFT:
		_left_click(button.position)
	elif button.button_index == MOUSE_BUTTON_RIGHT:
		_right_click(button.position)


func _left_click(at: Vector2) -> void:
	if _menu_ship >= 0:
		var i := _menu_index_at(at)
		if i >= 0:
			_menu_pick(i)
		else:
			_close_menu()
		return
	if _scan_mode:
		_scan_mode = false
		if _alive(_ship(_hover_ship)):
			_show_scan(_hover_ship)
		else:
			_show_toast("Scan cancelled")
		return
	if not _my_turn():
		return
	var hovered := _ship(_hover_ship)
	if _alive(hovered):
		if not _mine(hovered):
			_act({"action": "fire", "target": _hover_ship})
		elif int(state.get("initiative", 1)) == 0 and _hover_ship != int(state.get("ship", -1)):
			_act({"action": "select", "target": _hover_ship})
		return
	if _square_legal(_hover_square):
		_act({"action": "move", "x": _hover_square.x, "y": _hover_square.y})


func _right_click(at: Vector2) -> void:
	if _menu_ship >= 0:
		_close_menu()
		return
	var acting := int(state.get("ship", -1))
	if not _my_turn():
		return
	if _hover_ship == acting and acting >= 0:
		_open_menu(acting)
		return
	if _hover_ship < 0:
		var w := _to_world(at)
		_act({"action": "turn", "x": int(floorf(w.x / SQUARE)), "y": int(floorf(w.y / SQUARE))})


func _input(event: InputEvent) -> void:
	if not visible:
		return
	# A click outside the Options flyout or the scan closes it; on the field it does nothing else.
	var press := event as InputEventMouseButton
	if press != null and press.pressed and press.button_index in [MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT]:
		var closed := false
		if _options_panel != null and _options_panel.visible and not _options_panel.get_global_rect().has_point(press.global_position) \
				and not _options_tab.get_global_rect().has_point(press.global_position):
			_close_options()
			closed = true
		if _scan_panel != null and _scan_panel.visible and not _scan_panel.get_global_rect().has_point(press.global_position):
			_hide_scan()
			closed = true
		if closed and _pointer_on_field():
			get_viewport().set_input_as_handled()
		return
	var key := event as InputEventKey
	if key == null:
		return
	if key.keycode == KEY_ALT:
		if key.pressed != _alt_held:
			_alt_held = key.pressed
			for index in _ships.keys():
				_apply_shield(index, _ship(index))
		return
	if not key.pressed or key.echo:
		return
	if dialog_open.is_valid() and dialog_open.call():
		return # a dialog over the battle has the keys
	var handled := true
	match key.keycode:
		KEY_ESCAPE:
			_close_menu()
			if _options_panel != null and _options_panel.visible:
				_close_options()
			_scan_mode = false
			_hide_scan()
		KEY_S:
			if _scan_mode:
				_scan_mode = false
				_show_toast("Scan cancelled")
			else:
				_arm_scan()
		KEY_W:
			_act({"action": "wait"})
		KEY_H:
			_act({"action": "side_wait"})
		KEY_D:
			_act({"action": "done"})
		KEY_T:
			_act({"action": "end_turn"})
		KEY_A:
			_toggle_auto()
		KEY_R:
			_act({"action": "retreat"})
		KEY_SPACE, KEY_C:
			var ship := _ship(int(state.get("ship", -1)))
			if _alive(ship):
				_cam_goal = _center(ship)
		_:
			handled = false
	# Every key stays with the battle: none may reach the hidden legacy screen.
	get_viewport().set_input_as_handled()


## Auto: on the player's turn an action; while the AI plays their ships, a request the AI takes at
## its next look at the Auto button (combat::Request_Auto_Off; owner 2026-11-22: Auto couldn't
## be turned off).
var _auto_pending_on := false # Auto asked for while the enemy moved: sent on the player's next turn
var _auto_override := -1 # the state the button shows until the game's flag agrees (owner 2026-11-22: show it at once)
var _x2_buttons: Array = []
var _x2_override := -1 # the speed asked for, until a state shows it


func _set_speed_x2(on: bool) -> void:
	_x2_override = 1 if on else 0
	if game != null and game.has_method("request_speed_x2"):
		game.request_speed_x2(on)


## The game's x2 speed (the original's button; legacy effect loops step two frames).
func _speedx2() -> bool:
	if _x2_override >= 0:
		return _x2_override == 1
	return bool(state.get("speedx2", false))


## The player's own Auto flag (the state's "auto" is the side moving now's: 0 on the enemy's turn).
func _auto_mine() -> bool:
	return int(state.get("auto_mine", state.get("auto", 0))) != 0


func _auto_shown() -> bool:
	if _auto_override >= 0:
		return _auto_override == 1
	return _auto_mine() or _auto_pending_on


func _toggle_auto() -> void:
	var on := _auto_mine()
	var want := not _auto_shown()
	if _my_turn():
		_auto_pending_on = false
		if want != on:
			_act({"action": "auto"})
	elif not want and on and game != null:
		_auto_pending_on = false
		game.request_auto_off()
		_show_toast("Auto off: you take over from your next ship")
	else:
		_auto_pending_on = want and not on
		_show_toast("Auto on from your next ship" if _auto_pending_on else "Auto cancelled")
	# The button shows the state asked for at once (owner 2026-11-22), until the game's flag agrees.
	_auto_override = 1 if want else 0
	_auto_button.set_pressed_no_signal(want)
	Themes.glow(_auto_button)


func _act(action: Dictionary) -> void:
	if not _my_turn():
		return
	var id := _request_id
	_request_id = -1
	_close_menu()
	game.answer_request_text(id, 1, JSON.stringify(action))
	_refresh_hud()


# ---- HUD (owner 2026-11-21: the active theme, not B4's fixed glass; initiative centred at the
# top, the acting ship and its weapons centred at the bottom with Wait / Done / End turn in a row
# above them). Panels are the theme's Pane, chips its Row (RowOpen when lit), buttons its own,
# labels Name / Dim / Section. Colours that mean something (fire, damage, sides) stay. ----

const ARC_NAMES := {1: "front", 2: "front wide", 3: "front wide", 4: "rear wide", 8: "rear", 12: "rear wide", 15: "all round", 16: "all round"}
const DIM := Color(0.58, 0.64, 0.72) # a label role: drawn as the theme's Dim
const TEXT := Color(0.85, 0.88, 0.93) # a label role: the theme's plain label


## A small flat box (the run bracket in the timeline): drawn over the theme's panels.
func _box(bg: Color, edge: Color, radius := 6, pad := 10.0, width := 1) -> StyleBoxFlat:
	var box := StyleBoxFlat.new()
	box.bg_color = bg
	box.border_color = edge
	box.set_border_width_all(width)
	box.set_corner_radius_all(radius)
	box.content_margin_left = pad
	box.content_margin_right = pad
	box.content_margin_top = pad * 0.7
	box.content_margin_bottom = pad * 0.7
	return box


func _panel(variation := &"Pane") -> PanelContainer:
	var p := PanelContainer.new()
	p.mouse_filter = Control.MOUSE_FILTER_STOP
	p.theme_type_variation = variation
	return p


## A label in a theme role: DIM -> Dim, AMBER -> Name, TEXT -> the plain label; any other colour is
## kept (it means something: fire, damage). `font_size` 0 keeps the theme's.
func _label(text: String, font_size := 0, color := TEXT) -> Label:
	var l := Label.new()
	l.text = text
	if color == DIM:
		l.theme_type_variation = &"Dim"
	elif color == AMBER:
		l.theme_type_variation = &"Name"
	elif color != TEXT:
		l.add_theme_color_override("font_color", color)
	if font_size > 0:
		l.add_theme_font_size_override("font_size", font_size)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


## The theme's button, its key after the name; `primary` (End turn) is armed in the theme's way.
func _flat_button(text: String, key: String, pressed: Callable, primary := false) -> Button:
	var b := Button.new()
	b.text = text # the key goes in the tooltip (owner, 2026-11-22: "Just Wait is fine")
	if key != "":
		b.tooltip_text = "Key: " + key
	b.focus_mode = Control.FOCUS_NONE
	b.pressed.connect(pressed)
	return b


## The HUD docked to the screen's edges as the galaxy's bars are (owner, 2026-11-22): the
## initiative bar a pocket on the top edge (scrolls), Show Health and the move speed in a pocket
## under it; the acting ship and its weapons a pocket on the bottom edge with Wait / Done /
## End turn / Auto in a pocket docked on it; the round box in the bottom corner on the player's
## side (attacking: right; defending: left). _layout places them every frame.
func _build_hud() -> void:
	# The round box: a pocket on the bottom edge in the player's corner (owner 2026-11-22: framed and
	# attached to the outer frame as the other bars are).
	_top_left = _panel(&"BottomPocket")
	add_child(_top_left)
	var tl := VBoxContainer.new()
	tl.add_theme_constant_override("separation", 2)
	tl.custom_minimum_size = Vector2(230, 0)
	_top_left.add_child(tl)
	_round_label = _label("", 0, AMBER)
	tl.add_child(_round_label)
	_round_number = _label("", 0)
	tl.add_child(_round_number)
	_strength_label = _label("", 12, DIM)
	tl.add_child(_strength_label)
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 0)
	bar.custom_minimum_size = Vector2(0, 5)
	tl.add_child(bar)
	_strength_mine = ColorRect.new()
	_strength_mine.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(_strength_mine)
	_strength_theirs = ColorRect.new()
	_strength_theirs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(_strength_theirs)

	# Under the initiative bar: the Options button in its own framed tab (added first, so the bar's
	# pocket draws over the tab's top and the tab reads as hanging from it).
	_options_tab = _panel(&"Pocket")
	add_child(_options_tab)
	_top_right = _options_tab
	var options_button := _flat_button("Options", "", func():
		if _options_panel.visible:
			_close_options()
		else:
			_open_options())
	options_button.tooltip_text = "Battle options: health, grid, move squares, ship speed, layout"
	_options_tab.add_child(options_button)

	# Initiative: the top edge, half the screen wide, centred; its arrows and the wheel scroll it
	# smoothly (owner 2026-11-22: no drag).
	_timeline = _panel(&"Pocket")
	add_child(_timeline)
	var trow := HBoxContainer.new()
	trow.add_theme_constant_override("separation", 4)
	_timeline.add_child(trow)
	trow.add_child(_arrow(true, "Earlier in the order", func(): _scroll_by(_tl_scroll, -240)))
	_tl_scroll = ScrollContainer.new()
	_tl_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_tl_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_NEVER
	_tl_scroll.gui_input.connect(func(event: InputEvent): _timeline_input(event, -1))
	trow.add_child(_tl_scroll)
	# A margin inside the scroller: the run's gold bracket was cut at its top.
	var tl_margin := MarginContainer.new()
	for side in ["margin_top", "margin_bottom", "margin_left", "margin_right"]:
		tl_margin.add_theme_constant_override(side, 3)
	_tl_scroll.add_child(tl_margin)
	_timeline_row = HBoxContainer.new()
	_timeline_row.add_theme_constant_override("separation", 6)
	tl_margin.add_child(_timeline_row)
	trow.add_child(_arrow(false, "Later in the order", func(): _scroll_by(_tl_scroll, 240)))

	_build_options()
	move_child(_options_panel, _options_tab.get_index()) # slides out from under the tab and the bar

	# The bottom: Wait / Done / End turn / Auto in their own framed tab, stepping up from the
	# weapons pocket (added first: the pocket draws over the tab's lower edge).
	_bottom = _panel(&"BottomPocket")
	add_child(_bottom)
	var actions_pad := MarginContainer.new() # a little taller: the buttons' lower edge showed cut
	actions_pad.add_theme_constant_override("margin_top", 2)
	actions_pad.add_theme_constant_override("margin_bottom", 6)
	_bottom.add_child(actions_pad)
	_actions = HBoxContainer.new()
	_actions.add_theme_constant_override("separation", 6)
	actions_pad.add_child(_actions)
	_wait_button = _flat_button("Wait", "W", func(): _act({"action": "wait"}))
	_actions.add_child(_wait_button)
	_done_button = _flat_button("Done", "D", func(): _act({"action": "done"}))
	_actions.add_child(_done_button)
	_end_button = _flat_button("End turn", "T", func(): _act({"action": "end_turn"}), true)
	_actions.add_child(_end_button)
	_auto_button = _flat_button("Auto", "A", _toggle_auto)
	_auto_button.tooltip_text = "The computer fights the rest of this battle for you (key: A)"
	_auto_button.theme_type_variation = &"Toggle"
	_auto_button.toggle_mode = true
	_auto_button.button_pressed = false
	_actions.add_child(_auto_button)
	for b: Button in [_wait_button, _done_button, _end_button, _auto_button]:
		b.custom_minimum_size = Vector2(96, 0)

	# The weapons pocket on the bottom edge, about half the screen wide; its weapons scroll.
	_strip = _panel(&"BottomPocket")
	add_child(_strip)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	_strip.add_child(row)
	_strip_row = row
	var pad_left := Control.new()
	pad_left.custom_minimum_size = Vector2(6, 0)
	row.add_child(pad_left)
	var who := VBoxContainer.new()
	who.add_theme_constant_override("separation", 2)
	who.custom_minimum_size = Vector2(130, 0)
	row.add_child(who)
	_strip_name = _label("", 14, AMBER)
	who.add_child(_strip_name)
	_strip_info = _label("", 11, DIM)
	who.add_child(_strip_info)
	_strip_bars = _bars_control(Vector2(130, 8))
	who.add_child(_strip_bars)
	row.add_child(VSeparator.new())
	row.add_child(_arrow(true, "More weapons", func(): _scroll_by(_wp_scroll, -200)))
	_wp_scroll = ScrollContainer.new()
	_wp_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_wp_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_NEVER
	_wp_scroll.gui_input.connect(func(event: InputEvent): _weapons_input(event, -1))
	row.add_child(_wp_scroll)
	_strip_weapons = HBoxContainer.new()
	_strip_weapons.add_theme_constant_override("separation", 4)
	_wp_scroll.add_child(_strip_weapons)
	row.add_child(_arrow(false, "More weapons", func(): _scroll_by(_wp_scroll, 200)))
	var pad_right := Control.new()
	pad_right.custom_minimum_size = Vector2(6, 0)
	row.add_child(pad_right)

	_card = _panel()
	_card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_card.hide()
	add_child(_card)
	var cv := VBoxContainer.new()
	cv.add_theme_constant_override("separation", 5)
	cv.custom_minimum_size = Vector2(230, 0)
	_card.add_child(cv)
	_card_title = _label("", 14, AMBER)
	cv.add_child(_card_title)
	_card_bars = _bars_control(Vector2(230, 8))
	cv.add_child(_card_bars)
	_card_body = _label("", 12)
	_card_body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_card_body.custom_minimum_size = Vector2(230, 0)
	cv.add_child(_card_body)
	_card_fire = _label("", 12, FIRE)
	_card_fire.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_card_fire.custom_minimum_size = Vector2(230, 0)
	cv.add_child(_card_fire)

	_build_scan()

	_toast = _label("", 15, Color(1.0, 0.82, 0.55))
	_toast.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_toast.add_theme_constant_override("outline_size", 6)
	_toast.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_toast.hide()
	add_child(_toast)

	# The screen frame over everything, as the galaxy's (its joints where the edge pockets meet it).
	_frame_overlay = Control.new()
	_frame_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_frame_overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
	_frame_overlay.draw.connect(_draw_frame)
	add_child(_frame_overlay)


## Where the edge pockets sit (the galaxy's offsets: map_controls.gd, galaxy_bar.gd).
func _edge() -> float:
	var inset := get_theme_constant("inset", "Screen")
	return float(inset - 3) if inset > 0 else 6.0


func _layout() -> void:
	if _timeline == null:
		return
	# A theme without edge pockets (Glass: empty, no screen frame) gives the content pockets its
	# Pane, so the ship, its weapons and the round don't float on the battlefield.
	for pocket: PanelContainer in [_timeline, _strip, _top_left]:
		var wanted: StringName = &"Pocket" if pocket == _timeline else &"BottomPocket"
		var bare := get_theme_stylebox("panel", wanted) is StyleBoxEmpty
		var variation: StringName = &"Pane" if bare else wanted
		if pocket.theme_type_variation != variation:
			pocket.theme_type_variation = variation
	var edge := _edge()
	var half_screen := size.x * 0.5
	# Initiative: at most half the screen, centred (owner 2026-11-22).
	var tl_chrome := _timeline.get_combined_minimum_size().x - _tl_scroll.get_combined_minimum_size().x
	var tl_room := maxf(half_screen - tl_chrome, 120.0)
	var tl_content: Vector2 = (_tl_scroll.get_child(0) as Control).get_combined_minimum_size()
	_tl_scroll.custom_minimum_size = Vector2(minf(tl_content.x, tl_room), tl_content.y)
	_timeline.reset_size()
	_timeline.position = Vector2(roundf((size.x - _timeline.size.x) / 2.0), edge)
	# The Options tab hangs from it (drawn under it); the flyout below the tab.
	_options_tab.reset_size()
	_options_tab.position = Vector2(roundf((size.x - _options_tab.size.x) / 2.0), _timeline.position.y + _timeline.size.y + _grow(_timeline, 3) - 4.0)
	_options_panel.reset_size()
	# Its top level with the initiative bar's bottom edge; the Options tab juts over it (owner
	# 2026-11-22).
	var out_y := _timeline.position.y + _timeline.size.y + _grow(_timeline, 3)
	var ko := clampf(_options_anim, 0.0, 1.0)
	_options_panel.position = Vector2(roundf((size.x - _options_panel.size.x) / 2.0), lerpf(out_y - _options_panel.size.y * 0.6, out_y, ko))
	_options_panel.modulate.a = ko
	# The weapons pocket: at most half the screen, centred, on the bottom edge.
	var wp_chrome := _strip.get_combined_minimum_size().x - _wp_scroll.get_combined_minimum_size().x
	var wp_room := maxf(half_screen - wp_chrome, 120.0)
	var wp_content := _strip_weapons.get_combined_minimum_size()
	_wp_scroll.custom_minimum_size = Vector2(minf(wp_content.x, wp_room), wp_content.y)
	_strip.reset_size()
	_strip.position = Vector2(roundf((size.x - _strip.size.x) / 2.0), size.y - edge - _strip.size.y)
	# The buttons' tab steps up from it (drawn under it).
	_bottom.reset_size()
	_bottom.position = Vector2(roundf((size.x - _bottom.size.x) / 2.0), _strip.position.y - _grow(_strip, 1) - _bottom.size.y + 4.0)
	# The round box: in the player's bottom corner (attacking: right; defending: left), sharing the
	# bottom and that side's edge with the screen frame (owner 2026-11-22).
	_top_left.reset_size()
	var x := size.x - edge - _top_left.size.x if _round_on_right() else edge
	_top_left.position = Vector2(x, size.y - edge - _top_left.size.y)
	_toast.position = Vector2(roundf((size.x - _toast.size.x) / 2.0), _options_tab.position.y + _options_tab.size.y + 12.0)
	# The scan panel: centred between the button frames (under the Options tab, above the
	# buttons' tab), its list capped to that room; it grows out of the scanned ship.
	if _scan_panel.visible:
		var room := _clear_area()
		var head := _scan_panel.get_combined_minimum_size().y - _scan_scroll.get_combined_minimum_size().y
		var body := _scan_body.get_combined_minimum_size()
		_scan_scroll.custom_minimum_size = Vector2(0, clampf(body.y, 40.0, maxf(room.size.y - head, 40.0)))
		_scan_panel.reset_size()
		var final := Vector2(roundf((size.x - _scan_panel.size.x) / 2.0), roundf(room.position.y + (room.size.y - _scan_panel.size.y) / 2.0))
		final.y = maxf(final.y, room.position.y)
		var k := clampf(_scan_anim, 0.0, 1.0)
		var grow := lerpf(0.08, 1.0, k)
		_scan_panel.pivot_offset = Vector2.ZERO
		_scan_panel.scale = Vector2(grow, grow)
		_scan_panel.position = (_scan_from - _scan_panel.size * grow / 2.0).lerp(final, k)
		_scan_panel.modulate.a = lerpf(0.2, 1.0, k)
	_frame_overlay.queue_redraw()


## How far a pocket's art reaches past its box on one side (Chrome's metal.gd `grow`: left, top,
## right, bottom), so a tab docked on it shows.
func _round_on_right() -> bool:
	return int(state.get("player", -1)) == int(state.get("attacker", -2))


func _grow(pocket: Control, side: int) -> float:
	var box := pocket.get_theme_stylebox("panel")
	if box != null and "grow" in box:
		return float(box.grow[side])
	return 0.0


func _draw_frame() -> void:
	if frame_style == null:
		return
	_frame_overlay.draw_style_box(frame_style, Rect2(Vector2.ZERO, _frame_overlay.size))
	if tee.is_empty():
		return
	var c: float = tee.bar_center
	# The edge pockets meet the screen frame (as the galaxy's bars do).
	for pocket: Control in [_timeline, _strip, _top_left]:
		if not pocket.visible:
			continue
		var r := pocket.get_rect()
		var top := pocket == _timeline
		if pocket == _top_left:
			# In the corner: one joint on the bottom bar (its inner side), one on the side bar (its top).
			var right := _round_on_right()
			var inner_x: float = r.position.x - _grow(pocket, 0) + c if right else r.end.x + _grow(pocket, 2) - c
			_draw_tee(Vector2(inner_x, _frame_overlay.size.y - c), false)
			var top_y := r.position.y - _grow(pocket, 1) + c
			if right:
				_draw_tee(Vector2(_frame_overlay.size.x - c, top_y), false, -PI / 2.0)
			else:
				_draw_tee(Vector2(c, top_y), false, PI / 2.0)
			continue
		for x in [r.position.x - _grow(pocket, 0) + c, r.end.x + _grow(pocket, 2) - c]:
			if top:
				_draw_tee(Vector2(x, c), true)
			else:
				_draw_tee(Vector2(x, _frame_overlay.size.y - c), false)
	# The tabs meet their pockets (owner 2026-11-22, ClearFieldBattleMarkup.png): the Options tab
	# hangs from the initiative bar's lower edge, the buttons' tab stands on the weapons pocket.
	if _options_tab.visible:
		var r := _options_tab.get_rect()
		var bar_y := _timeline.get_rect().end.y + _grow(_timeline, 3) - c
		for x in [r.position.x - _grow(_options_tab, 0) + c, r.end.x + _grow(_options_tab, 2) - c]:
			_draw_tee(Vector2(x, bar_y), true)
	if _bottom.visible:
		var r := _bottom.get_rect()
		var bar_y := _strip.get_rect().position.y - _grow(_strip, 1) + c
		for x in [r.position.x - _grow(_bottom, 0) + c, r.end.x + _grow(_bottom, 2) - c]:
			_draw_tee(Vector2(x, bar_y), false)


## The theme's joint (its art is a bar arriving from above onto a horizontal bar), its meeting
## point at `at`: upside down (a bar leaving downward), or turned (a bar arriving from the side).
func _draw_tee(at: Vector2, upside_down: bool, turn := 0.0) -> void:
	var tex: Texture2D = tee.texture
	var scale: float = tee.scale
	var tsize := Vector2(tex.get_size()) * scale
	var anchor: Vector2 = tee.anchor * scale
	_frame_overlay.draw_set_transform(at, turn, Vector2(1.0, -1.0 if upside_down else 1.0))
	_frame_overlay.draw_texture_rect(tex, Rect2(-anchor, tsize), false)
	_frame_overlay.draw_set_transform(Vector2.ZERO)


# ---- Scrolling bars and the Options flyout (owner 2026-11-22) ----

## A galaxy arrow (arrow_button.gd) that scrolls a bar.
func _arrow(points_left: bool, tip: String, pressed: Callable) -> Button:
	var b: Button = preload("arrow_button.gd").new()
	b.points_left = points_left
	b.focus_mode = Control.FOCUS_NONE
	b.tooltip_text = tip
	b.pressed.connect(pressed)
	return b


func _scroll_max(sc: ScrollContainer) -> float:
	return maxf(0.0, sc.get_h_scroll_bar().max_value - sc.size.x)


## Smooth scrolling: the bar eases toward a goal (_process), so a long list shows which way it goes.
func _scroll_by(sc: ScrollContainer, pixels: float) -> void:
	var goal: float = _scroll_goal.get(sc, float(sc.scroll_horizontal))
	_scroll_goal[sc] = clampf(goal + pixels, 0.0, _scroll_max(sc))


func _ease_scrolls(delta: float) -> void:
	for sc: ScrollContainer in _scroll_goal.keys():
		var goal: float = clampf(_scroll_goal[sc], 0.0, _scroll_max(sc))
		var now := float(sc.scroll_horizontal)
		var next := lerpf(now, goal, minf(1.0, delta * 10.0))
		if absf(goal - next) < 1.0:
			next = goal
		sc.scroll_horizontal = int(roundf(next))


func _wheel_scroll(sc: ScrollContainer, event: InputEvent) -> bool:
	var button := event as InputEventMouseButton
	if button == null or not button.pressed:
		return false
	if button.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_LEFT]:
		_scroll_by(sc, -120)
	elif button.button_index in [MOUSE_BUTTON_WHEEL_DOWN, MOUSE_BUTTON_WHEEL_RIGHT]:
		_scroll_by(sc, 120)
	else:
		return false
	sc.accept_event()
	return true


var _tl_press := Vector2.INF


## The initiative bar: the wheel and its arrows scroll it, a drag pans it (owner 2026-11-22: "click
## to pan"), a click (no drag) on a chip centres the camera on that ship.
func _timeline_input(event: InputEvent, index: int) -> void:
	if _wheel_scroll(_tl_scroll, event):
		return
	var button := event as InputEventMouseButton
	if button != null and button.button_index == MOUSE_BUTTON_LEFT:
		if button.pressed:
			_tl_press = button.global_position
			_tl_dragged = false
		else:
			if not _tl_dragged and index >= 0 and _ships.has(index):
				_cam_goal = _ships[index].shown
			_tl_press = Vector2.INF
		accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null and _tl_press != Vector2.INF:
		if _tl_dragged or motion.global_position.distance_to(_tl_press) > 6.0:
			_tl_dragged = true
			_scroll_goal.erase(_tl_scroll)
			_tl_scroll.scroll_horizontal -= int(motion.relative.x)
		accept_event()


## The weapons: the wheel scrolls, a drag scrolls, a click (no drag) on a chip cycles that weapon.
func _weapons_input(event: InputEvent, slot: int) -> void:
	if _wheel_scroll(_wp_scroll, event):
		return
	var button := event as InputEventMouseButton
	if button != null and button.button_index == MOUSE_BUTTON_LEFT:
		if button.pressed:
			_wp_press = button.global_position
			_wp_dragged = false
		else:
			if not _wp_dragged and slot >= 0 and _wp_press != Vector2.INF:
				_act({"action": "toggle_weapon", "slot": slot})
			_wp_press = Vector2.INF
		accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null and _wp_press != Vector2.INF:
		if _wp_dragged or motion.global_position.distance_to(_wp_press) > 6.0:
			_wp_dragged = true
			_scroll_goal.erase(_wp_scroll)
			_wp_scroll.scroll_horizontal -= int(motion.relative.x)
		accept_event()


## The flyout under the Options tab: every battle option (owner: "all the options we have/haven't
## discovered"), and the layout, as themes are for the galaxy.
var _options_anim := 0.0 # 0: tucked under the Options tab, 1: out


## The flyout slides down out from under the Options tab (as the galaxy's panes fly out).
func _open_options() -> void:
	_options_panel.show()
	var tween := create_tween()
	tween.tween_property(self, "_options_anim", 1.0, 0.18).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)


func _close_options() -> void:
	var tween := create_tween()
	tween.tween_property(self, "_options_anim", 0.0, 0.14).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	tween.tween_callback(func():
		if _options_anim <= 0.0:
			_options_panel.hide())


func _build_options() -> void:
	_options_panel = _panel()
	_options_panel.hide()
	add_child(_options_panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	v.custom_minimum_size = Vector2(260, 0)
	_options_panel.add_child(v)
	var section := func(text: String):
		var l := _label(text)
		l.theme_type_variation = &"Section"
		v.add_child(l)
	var toggle := func(text: String, tip: String, on: bool, changed: Callable) -> Button:
		var b := _flat_button(text, "", func(): pass)
		b.theme_type_variation = &"Toggle"
		b.toggle_mode = true
		b.button_pressed = on
		b.tooltip_text = tip
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		Themes.glowing(b)
		b.toggled.connect(changed)
		v.add_child(b)
		return b
	section.call("SHOW")
	_health_toggle = toggle.call("Health on every ship", "Shields and hull on every ship (hold Alt to see them for a moment)", _always_health,
		func(on: bool):
			_always_health = on
			_save_setting("always_show_health", on)
			for index in _ships.keys():
				_apply_shield(index, _ship(index)))
	toggle.call("Grid", "The battlefield's squares", _show_grid, func(on: bool):
		_show_grid = on
		_save_setting("show_grid", on)
		_backdrop.queue_redraw())
	toggle.call("Camera follows", "On the enemy's turn (and yours under Auto) the camera frames each move and shot. Off: the camera stays where you put it.", _camera_follow,
		func(on: bool):
			_camera_follow = on
			_save_setting("camera_follow", on))
	toggle.call("Moves always", "The acting ship's squares lit all the time, not only while the pointer is on the field", _always_moves,
		func(on: bool):
			_always_moves = on
			_save_setting("always_show_moves", on))
	section.call("SHIP SPEED")
	var speeds := HBoxContainer.new()
	speeds.add_theme_constant_override("separation", 4)
	v.add_child(speeds)
	_move_speed = clampi(int(_load_setting("move_speed", 1)), 0, MOVE_SPEEDS.size() - 1)
	var group := ButtonGroup.new()
	for i in MOVE_SPEEDS.size():
		var b := _flat_button(String(MOVE_SPEEDS[i][0]), "", func():
			_move_speed = i
			_save_setting("move_speed", i))
		b.theme_type_variation = &"Toggle"
		b.toggle_mode = true
		b.button_group = group
		b.button_pressed = i == _move_speed
		b.tooltip_text = "How fast ships move and turn (weapons keep the original's pace)"
		Themes.glowing(b)
		speeds.add_child(b)
		if i == _move_speed:
			_speed_button = b
	# The original's x2 speed button (owner 2026-11-22: not exposed): weapons and effects play every
	# other frame, as the game's own setting (saved with the game's settings).
	section.call("COMBAT SPEED")
	var x2_row := HBoxContainer.new()
	x2_row.add_theme_constant_override("separation", 4)
	v.add_child(x2_row)
	var x2_group := ButtonGroup.new()
	_x2_buttons = []
	for i in 2:
		var b := _flat_button("Normal" if i == 0 else "Double", "", func():
			_set_speed_x2(i == 1))
		b.theme_type_variation = &"Toggle"
		b.toggle_mode = true
		b.button_group = x2_group
		b.button_pressed = i == 0
		b.tooltip_text = "The original's combat speed: Double plays weapons and effects at twice the pace"
		Themes.glowing(b)
		x2_row.add_child(b)
		_x2_buttons.append(b)
	section.call("LAYOUT")
	var layout := _flat_button("Clear Field", "", func(): pass)
	layout.theme_type_variation = &"Toggle"
	layout.toggle_mode = true
	layout.button_pressed = true
	layout.tooltip_text = "The battle's layout. Bridge and Command Console come next."
	Themes.glowing(layout)
	v.add_child(layout)


## Straight bars (the timeline, the strip, the card): armour on top, shrinking to the centre;
## below it structure on the left and systems on the right, each empty at the centre.
func _bars_control(min_size: Vector2) -> Control:
	var c := Control.new()
	c.custom_minimum_size = min_size
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	c.set_meta("ship", {})
	c.add_to_group(BARS_GROUP)
	c.draw.connect(func():
		var ship: Dictionary = _view_ship(c.get_meta("ship"))
		if ship.is_empty():
			return
		var w := c.size.x
		var row := maxf(2.0, (c.size.y - 2.0) / 2.0)
		var a := _fraction(ship.get("armor", {}))
		var s := _fraction(ship.get("structure", {}))
		var y := _fraction(ship.get("systems", {}))
		c.draw_rect(Rect2(0, 0, w, row), TRACK_COLOR)
		c.draw_rect(Rect2(w * (1.0 - a) / 2.0, 0, w * a, row), ARMOR_COLOR)
		var half := w / 2.0 - 1.0
		c.draw_rect(Rect2(0, row + 2.0, half, row), TRACK_COLOR)
		c.draw_rect(Rect2(w / 2.0 + 1.0, row + 2.0, half, row), TRACK_COLOR)
		c.draw_rect(Rect2(half - half * s, row + 2.0, half * s, row), STRUCTURE_COLOR)
		c.draw_rect(Rect2(w / 2.0 + 1.0, row + 2.0, half * y, row), SYSTEMS_COLOR))
	return c


func _set_bars(c: Control, ship: Dictionary) -> void:
	c.set_meta("ship", ship)
	c.queue_redraw()


func _side_name(owner: int) -> String:
	if owner == int(state.get("attacker", -1)):
		return String(state.get("attacker_race", ""))
	return String(state.get("defender_race", ""))


func _refresh_hud() -> void:
	if state.is_empty():
		return
	var star := String(state.get("star", ""))
	_round_label.text = "Battle at " + star if star != "" else "Space combat"
	_round_number.text = "Round %d" % (int(state.get("round", 0)) + 1)
	var mine := 0.0
	var theirs := 0.0
	var my_count := 0
	var their_count := 0
	var their_owner := -1
	var planet := false
	for ship: Dictionary in _ships_list():
		if not _alive(ship):
			continue
		var hp := float((ship.get("armor", {}) as Dictionary).get("now", 0)) + float((ship.get("structure", {}) as Dictionary).get("now", 0))
		if _mine(ship):
			mine += hp
			my_count += 1
		else:
			theirs += hp
			if bool(ship.get("planet", false)):
				planet = true
			else:
				their_count += 1
				their_owner = int(ship.get("owner", -1))
	var total := maxf(mine + theirs, 1.0)
	_strength_mine.size_flags_stretch_ratio = maxf(mine / total, 0.01)
	_strength_theirs.size_flags_stretch_ratio = maxf(theirs / total, 0.01)
	var my_color := MONSTER_COLOR
	var their_color := MONSTER_COLOR
	for ship: Dictionary in _ships_list():
		if _mine(ship):
			my_color = _color(ship)
		else:
			their_color = _color(ship)
	_strength_mine.color = my_color
	_strength_theirs.color = their_color
	var me := int(state.get("player", -1))
	if their_owner < 0:
		their_owner = int(state.get("defender", -1)) if me == int(state.get("attacker", -1)) else int(state.get("attacker", -1))
	_strength_label.text = "%s  %d ships          %s  %d%s" % [_side_name(me), my_count, _side_name(their_owner), their_count,
		" + colony" if planet else ""]
	var my_turn := _my_turn()
	_wait_button.disabled = not my_turn or not bool(state.get("can_wait", true))
	_done_button.disabled = not my_turn
	_end_button.disabled = not my_turn
	_auto_button.disabled = false # always reachable (owner 2026-11-22), whoever's turn it is
	if _auto_pending_on and _my_turn() and not _auto_mine():
		_auto_pending_on = false
		_act.call_deferred({"action": "auto"}) # asked for while the enemy moved
	if _auto_override >= 0 and (_auto_override == 1) == _auto_mine():
		_auto_override = -1 # the game's flag agrees
	_auto_button.set_pressed_no_signal(_auto_shown())
	Themes.glow(_auto_button)
	if _x2_override >= 0 and state.has("speedx2") and bool(state.speedx2) == (_x2_override == 1):
		_x2_override = -1
	for i in _x2_buttons.size():
		(_x2_buttons[i] as Button).set_pressed_no_signal((i == 1) == _speedx2())
	_refresh_strip()
	_refresh_timeline()
	if _scan_ship >= 0 and _scan_panel.visible:
		_show_scan(_scan_ship)


func _arc_name(arc: int) -> String:
	return ARC_NAMES.get(arc, "arc %d" % arc)


## The acting ship's weapons as chips: lit while they can still fire this turn, dim once fired or
## switched off; with an enemy under the pointer, the ones that can hit it light up in fire colour
## (the legacy weapon list's lit lines). A click cycles on, fire once, off.
func _refresh_strip() -> void:
	var acting := _ship(int(state.get("ship", -1)))
	if not _alive(acting) or not _mine(acting):
		_strip_row.hide()
		return
	_strip_row.show()
	_strip_name.text = String(acting.get("name", ""))
	_strip_info.text = "%d moves   ·   marines %d" % [int(acting.get("moves", 0)), int(acting.get("marines", 0))]
	_set_bars(_strip_bars, acting)
	for child in _strip_weapons.get_children():
		_strip_weapons.remove_child(child) # out now: a freed chip lingering a frame doubled the width (flicker)
		child.queue_free()
	_weapon_chips.clear()
	var in_arc := _slots_against(_hover_ship)
	for weapon: Dictionary in acting.get("weapons", []):
		var slot := int(weapon.get("slot", 0))
		var count := int(weapon.get("count", 0))
		var active := int(weapon.get("active", 1))
		var ready := bool(weapon.get("ready", false)) and not bool(weapon.get("spent", false))
		var ammo := int(weapon.get("ammo", 0))
		var shots := int(weapon.get("shots_left", 0))
		var targeted := _hover_ship >= 0 and not _mine(_ship(_hover_ship))
		var lit := targeted and in_arc.has(slot)
		var chip := _panel(&"RowOpen" if lit else &"Row")
		# Up to 8 weapon slots must fit: the theme's chip with tighter padding.
		var chip_box: StyleBox = chip.get_theme_stylebox("panel", &"RowOpen" if lit else &"Row")
		if chip_box != null:
			chip_box = chip_box.duplicate()
			chip_box.content_margin_left = 7
			chip_box.content_margin_right = 7
			chip_box.content_margin_top = 4
			chip_box.content_margin_bottom = 4
			chip.add_theme_stylebox_override("panel", chip_box)
		chip.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		chip.tooltip_text = "Click: on, fire once, off"
		var v := VBoxContainer.new()
		v.add_theme_constant_override("separation", 0)
		chip.add_child(v)
		v.add_child(_label("%d × %s" % [count, String(weapon.get("name", ""))], 11))
		var status := ""
		if count == 0:
			status = "destroyed"
		elif active == -1:
			status = "off"
		elif not ready:
			status = "fired"
		elif targeted:
			status = "in arc" if lit else "out of arc or range"
		else:
			status = "ready" + (" · once" if active == 0 else "") + (" · %d shots" % shots if shots > 1 and shots != count else "")
		var line := _arc_name(int(weapon.get("arc", 0))) + " · " + status
		if ammo > 0 and ammo < 99:
			line += " · %d left" % ammo
		var status_label := _label(line, 10, FIRE if lit else (SYSTEMS_COLOR if status == "destroyed" else DIM))
		v.add_child(status_label)
		if count == 0 or active == -1 or not ready or (targeted and not lit):
			chip.modulate = Color(1, 1, 1, 0.5)
		chip.gui_input.connect(func(event: InputEvent): _weapons_input(event, slot if count > 0 else -1))
		_strip_weapons.add_child(chip)
		_weapon_chips[slot] = chip


## The acting ship's weapon slots that can hit `target` now (the state's "targets").
func _slots_against(target: int) -> Dictionary:
	var slots := {}
	for entry: Dictionary in state.get("targets", []):
		if int(entry.get("ship", -1)) == target:
			for slot in entry.get("slots", []):
				slots[int(slot)] = true
	return slots


var _tl_round := -1
var _tl_base: Array = [] # the round's whole order, as it stood at the round's start
var _tl_shown: Array = [] # the sequence the bar shows now
var _tl_bars := {} # ship -> its chip's bars (updated in place)
var _tl_split := -1 # where the next round begins in the bar


## The initiative bar (owner 2026-11-22): a fixed cycle of every live ship, the acting one first,
## then those still to act this round, then the ones that already acted in the round's order (they
## come round again); a destroyed ship leaves at once. Rebuilt only when that sequence changes.
func _refresh_timeline() -> void:
	var acting := int(state.get("ship", -1))
	var order: Array = []
	if acting >= 0 and _alive(_ship(acting)):
		order.append(acting)
	for index in state.get("order", []):
		var i := int(index)
		if i != acting and _alive(_ship(i)) and not order.has(i):
			order.append(i)
	var this_round := order.size() # the ships still to act this round; the rest come next round
	var round := int(state.get("round", 0))
	if round != _tl_round or _tl_base.is_empty():
		_tl_round = round
		_tl_base = order.duplicate() # the round's start: everyone is still to act
	for i in _tl_base:
		if _alive(_ship(int(i))) and not order.has(int(i)):
			order.append(int(i))
	for ship: Dictionary in _ships_list(): # anyone the round's start missed (never expected)
		var i := int(ship.i)
		if _alive(ship) and not order.has(i):
			order.append(i)
	if order == _tl_shown and this_round == _tl_split:
		for i in _tl_bars:
			_set_bars(_tl_bars[i], _ship(int(i)))
		return
	_tl_shown = order.duplicate()
	_tl_split = this_round
	_tl_bars.clear()
	for child in _timeline_row.get_children():
		_timeline_row.remove_child(child)
		child.queue_free()
	# Your run: the player's ships before the next enemy one (the bracket; WP3.9d lets you pick in it).
	var run := 0
	for index in order:
		if _mine(_ship(index)):
			run += 1
		else:
			break
	var bracket: HBoxContainer = null
	if run > 0 and _mine(_ship(order[0])):
		var frame := PanelContainer.new()
		frame.add_theme_stylebox_override("panel", _box(Color(AMBER, 0.08), AMBER, 6, 3.0, 2))
		bracket = HBoxContainer.new()
		bracket.add_theme_constant_override("separation", 4)
		frame.add_child(bracket)
		_timeline_row.add_child(frame)
	# Every ship still to act (owner 2026-11-22); the bar scrolls.
	for n in order.size():
		if n == this_round and n > 0:
			_timeline_row.add_child(_round_marker(round + 2)) # owner 2026-11-22: where the next round begins
		var chip := _chip(order[n], order[n] == acting)
		if bracket != null and n < run:
			bracket.add_child(chip)
		else:
			_timeline_row.add_child(chip)
	if acting != _tl_acting:
		_tl_acting = acting
		_scroll_goal.erase(_tl_scroll)
		_tl_scroll.scroll_horizontal = 0 # the acting ship in view


## The handoff to the next round in the initiative bar: a bright divider with the round's number.
func _round_marker(number: int) -> Control:
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 0)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	var line := ColorRect.new()
	line.color = AMBER
	line.custom_minimum_size = Vector2(3, 28)
	line.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(line)
	var label := _label("ROUND\n%d" % number, 10, AMBER)
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(label)
	box.custom_minimum_size = Vector2(40, 0)
	return box


var _chip_waiting: Array = [] # [TextureRect, lbx, entry] whose picture hasn't arrived


func _fill_waiting_chips() -> void:
	if _chip_waiting.is_empty():
		return
	_chip_waiting = _chip_waiting.filter(func(w: Array) -> bool:
		if not is_instance_valid(w[0]):
			return false # a chip from an older bar
		var tex: Texture2D = art.texture(String(w[1]), int(w[2]), 0)
		if tex == null:
			return true
		(w[0] as TextureRect).texture = tex
		return false)


func _chip(index: int, acting: bool) -> Control:
	var ship := _ship(index)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 3)
	var pic := _panel(&"RowOpen" if acting else &"Row")
	pic.mouse_filter = Control.MOUSE_FILTER_PASS
	# The side's colour as a thin line under the picture (the theme draws the frame).
	pic.custom_minimum_size = Vector2(46, 36)
	pic.tooltip_text = String(ship.get("name", ""))
	var rect := TextureRect.new()
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.custom_minimum_size = Vector2(40, 30)
	var sprite_info: Dictionary = ship.get("sprite", {})
	rect.texture = art.texture(String(sprite_info.get("lbx", "")), int(sprite_info.get("entry", -1)), 0)
	if rect.texture == null:
		# Its art still on the way: filled in when it arrives (the bar is rebuilt only when the
		# order changes; owner 2026-11-22: chips came up empty).
		_chip_waiting.append([rect, String(sprite_info.get("lbx", "")), int(sprite_info.get("entry", -1))])
	pic.add_child(rect)
	box.add_child(pic)
	var side := ColorRect.new()
	side.color = _color(ship)
	side.custom_minimum_size = Vector2(46, 2)
	side.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(side)
	var bars := _bars_control(Vector2(46, 6))
	_set_bars(bars, ship)
	box.add_child(bars)
	_tl_bars[index] = bars
	# The chip hovers its ship as the field does (owner 2026-11-22); a click centres the camera on it.
	box.mouse_filter = Control.MOUSE_FILTER_PASS
	box.mouse_entered.connect(func(): _chip_hover = index)
	box.mouse_exited.connect(func():
		if _chip_hover == index:
			_chip_hover = -1)
	box.gui_input.connect(func(event: InputEvent): _timeline_input(event, index))
	return box


func _place_card() -> void:
	var ship := _view_ship(_ship(_hover_ship))
	if not _alive(ship) or _menu_ship >= 0:
		if _card.visible:
			_card.hide()
			_refresh_strip()
		return
	var was_hidden: bool = not _card.visible or _card.get_meta("ship", -1) != _hover_ship
	_card.set_meta("ship", _hover_ship)
	_card.show()
	_card_title.text = String(ship.get("name", "")) + ("" if _mine(ship) else "   ·   " + _side_name(int(ship.get("owner", -1))))
	_card_title.add_theme_color_override("font_color", AMBER if _mine(ship) else _color(ship).lightened(0.25))
	_set_bars(_card_bars, ship)
	var shield: Dictionary = ship.get("shield", {})
	var arcs: Array = shield.get("arcs", [0, 0, 0, 0])
	var armor: Dictionary = ship.get("armor", {})
	var structure: Dictionary = ship.get("structure", {})
	var systems: Dictionary = ship.get("systems", {})
	var lines := []
	if int(shield.get("layers", 0)) > 0:
		lines.append("Shields %d / %d / %d / %d  of %d  (front, right, rear, left)" % [int(arcs[0]), int(arcs[1]), int(arcs[2]), int(arcs[3]), int(shield.get("max", 0))])
	else:
		lines.append("Shields   down")
	lines.append("Armour %d / %d   Structure %d / %d" % [int(armor.get("now", 0)), int(armor.get("max", 0)), int(structure.get("now", 0)), int(structure.get("max", 0))])
	var sys_max := maxf(float(systems.get("max", 0)), 1.0)
	lines.append("Systems %d%%" % int(round(100.0 * float(systems.get("now", 0)) / sys_max)))
	if _mine(ship):
		lines.append("%d moves" % int(ship.get("moves", 0)))
	_card_body.text = "\n".join(lines)
	_card_fire.text = ""
	if _my_turn() and not _mine(ship):
		var acting := _ship(int(state.get("ship", -1)))
		var slots := _slots_against(_hover_ship)
		var can := []
		for weapon: Dictionary in acting.get("weapons", []):
			if slots.has(int(weapon.get("slot", -1))):
				can.append("%d %s" % [int(weapon.get("count", 0)), String(weapon.get("name", ""))])
		_card_fire.text = ("Click to fire: " + ", ".join(can)) if not can.is_empty() else "No weapon can reach it from here"
	_card_fire.visible = _card_fire.text != ""
	if was_hidden:
		_refresh_strip() # the chips light for this target
	_card.reset_size()
	# Docked in the bottom corner opposite the round box (owner 2026-11-22: beside the ship it
	# covered a large part of the battlefield).
	var inset := float(get_theme_constant("inset", "Screen"))
	var corner := inset if inset > 0.0 else 6.0
	var attacking := int(state.get("player", -1)) == int(state.get("attacker", -2))
	var x := corner if attacking else size.x - corner - _card.size.x
	_card.position = Vector2(x, size.y - corner - _card.size.y)


func _show_toast(text: String, seconds := 3.0) -> void:
	_toast.text = text
	_toast.show()
	_toast_time = seconds



# ---- Scan ----

func _move_factor() -> float:
	return float(MOVE_SPEEDS[_move_speed][1])


func _arm_scan() -> void:
	_scan_mode = true
	_show_toast("Scan: click any ship  (Esc cancels)", 4.0)


## The scan panel, modelled on the Ship Design panel (owner 2026-11-22): the name and side with a
## Close button; the hull, crew and officer; one frame with the systems (a glyph each: shield,
## armour, structure, computer, drive) beside a frame with the ship's picture and its pools; the
## combat values; the weapons as rows with their arc glyph; the specials. It grows out of the
## scanned ship to the middle of the screen, between the button frames.
const SCAN_WIDTH := 660.0
var _scan_anim := 1.0 # 0: at the ship, 1: in place (tweened when it opens)
var _scan_from := Vector2.ZERO # the scanned ship on screen when it opened
var _scan_picture: TextureRect
var _scan_systems: VBoxContainer
var _scan_values: Label


func _build_scan() -> void:
	_scan_panel = _panel()
	_scan_panel.hide()
	add_child(_scan_panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 8)
	v.custom_minimum_size = Vector2(SCAN_WIDTH, 0)
	_scan_panel.add_child(v)
	var head := HBoxContainer.new()
	head.add_theme_constant_override("separation", 10)
	v.add_child(head)
	_scan_title = _label("")
	_scan_title.theme_type_variation = &"Heading"
	_scan_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scan_title.clip_text = true
	head.add_child(_scan_title)
	var close := _flat_button("X", "Esc", _hide_scan)
	close.custom_minimum_size = Vector2(34, 30)
	head.add_child(close)
	_scan_sub = _label("", 0, DIM)
	v.add_child(_scan_sub)
	# The top: the systems' frame beside the picture's frame (the design panel's).
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 10)
	v.add_child(top)
	var systems_frame := _panel(&"Row")
	systems_frame.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(systems_frame)
	_scan_systems = VBoxContainer.new()
	_scan_systems.add_theme_constant_override("separation", 0)
	systems_frame.add_child(_scan_systems)
	var picture_frame := _panel(&"Row")
	picture_frame.custom_minimum_size = Vector2(190, 0)
	top.add_child(picture_frame)
	var pv := VBoxContainer.new()
	pv.add_theme_constant_override("separation", 4)
	picture_frame.add_child(pv)
	_scan_picture = TextureRect.new()
	_scan_picture.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_scan_picture.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_scan_picture.custom_minimum_size = Vector2(170, 110)
	pv.add_child(_scan_picture)
	# The shield's four arcs written round the picture (owner 2026-11-22): the nose points right, so
	# front at the right, rear at the left, the right arc below and the left above.
	var arcs_overlay := Control.new()
	arcs_overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE
	arcs_overlay.set_anchors_preset(Control.PRESET_FULL_RECT)
	arcs_overlay.draw.connect(func(): _draw_scan_arcs(arcs_overlay))
	_scan_picture.add_child(arcs_overlay)
	_scan_bars = _bars_control(Vector2(170, 10))
	pv.add_child(_scan_bars)
	_scan_values = _label("", 12, DIM)
	v.add_child(_scan_values)
	# Weapons and specials scroll; the panel never outgrows the room between the frames.
	_scan_scroll = ScrollContainer.new()
	_scan_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	v.add_child(_scan_scroll)
	_scan_body = VBoxContainer.new()
	_scan_body.add_theme_constant_override("separation", 4)
	_scan_body.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_scan_scroll.add_child(_scan_body)


func _hide_scan() -> void:
	_scan_ship = -1
	if _scan_panel != null:
		_scan_panel.hide()


func _draw_scan_arcs(item: Control) -> void:
	var ship := _ship(_scan_ship)
	var shield: Dictionary = ship.get("shield", {})
	if int(shield.get("max", 0)) <= 0:
		return
	var arcs: Array = shield.get("arcs", [0, 0, 0, 0])
	var font := get_theme_default_font()
	var size_px := 13
	var c := item.size / 2.0
	var places := [Vector2(item.size.x - 4.0, c.y), Vector2(c.x, item.size.y - 2.0), Vector2(4.0, c.y), Vector2(c.x, 14.0)]
	for a in 4:
		var text := str(int(arcs[a]))
		var w := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px).x
		var at: Vector2 = places[a]
		var pos := Vector2(at.x - w / 2.0, at.y)
		if a == 0:
			pos.x = at.x - w
		elif a == 2:
			pos.x = at.x
		item.draw_string_outline(font, pos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, 4, Color(0, 0, 0, 0.9))
		item.draw_string(font, pos, text, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, Color(0.6, 1.0, 0.8))


## A system's glyph (drawn in the label colour): shield, armor, structure, computer, drive.
func _glyph(kind: String) -> Control:
	var g := Control.new()
	g.custom_minimum_size = Vector2(20, 20)
	g.mouse_filter = Control.MOUSE_FILTER_IGNORE
	g.draw.connect(_draw_glyph.bind(g, kind))
	return g


func _draw_glyph(g: Control, kind: String) -> void:
	var c := g.size / 2.0
	var r := minf(g.size.x, g.size.y) * 0.42
	var ink := g.get_theme_color("font_color", "Label")
	match kind:
		"shield": # a shield's outline
			var pts := PackedVector2Array([c + Vector2(-r, -r * 0.8), c + Vector2(0, -r), c + Vector2(r, -r * 0.8), c + Vector2(r, 0),
				c + Vector2(0, r), c + Vector2(-r, 0), c + Vector2(-r, -r * 0.8)])
			g.draw_polyline(pts, ink, 1.5, true)
		"armor": # stacked plates
			for k in 3:
				var y := c.y - r + k * r * 0.75
				g.draw_rect(Rect2(Vector2(c.x - r, y), Vector2(r * 2.0, r * 0.55)), ink, false, 1.5)
		"structure": # a girder
			g.draw_rect(Rect2(c - Vector2(r, r * 0.6), Vector2(r * 2.0, r * 1.2)), ink, false, 1.5)
			g.draw_line(c + Vector2(-r, -r * 0.6), c + Vector2(0, r * 0.6), ink, 1.5, true)
			g.draw_line(c + Vector2(0, r * 0.6), c + Vector2(r, -r * 0.6), ink, 1.5, true)
		"computer": # a chip with pins
			g.draw_rect(Rect2(c - Vector2(r * 0.6, r * 0.6), Vector2(r * 1.2, r * 1.2)), ink, false, 1.5)
			for k in [-0.3, 0.3]:
				for side in [-1.0, 1.0]:
					g.draw_line(c + Vector2(r * k, side * r * 0.6), c + Vector2(r * k, side * r), ink, 1.5)
					g.draw_line(c + Vector2(side * r * 0.6, r * k), c + Vector2(side * r, r * k), ink, 1.5)
		"drive": # an exhaust flame behind a nozzle
			g.draw_colored_polygon(PackedVector2Array([c + Vector2(-r, -r * 0.5), c + Vector2(0, -r * 0.5), c + Vector2(0, r * 0.5),
				c + Vector2(-r, r * 0.5)]), Color(ink, 0.25))
			g.draw_polyline(PackedVector2Array([c + Vector2(0, -r * 0.5), c + Vector2(r, 0), c + Vector2(0, r * 0.5)]), ink, 1.5, true)


## A right click on `control` shows help record `topic` (as the design panel's rows do).
func _with_help(control: Control, topic: int) -> Control:
	if topic < 0:
		return control
	control.mouse_filter = Control.MOUSE_FILTER_STOP
	control.tooltip_text = "Right click: what it does"
	control.gui_input.connect(func(event: InputEvent):
		var b := event as InputEventMouseButton
		if b != null and b.pressed and b.button_index == MOUSE_BUTTON_RIGHT:
			help_requested.emit(topic, control)
			control.accept_event())
	return control


## One system line, as the design panel's: glyph, small dim title, the part, the detail at the right.
func _scan_line(kind: String, title: String, value: String, detail: String) -> Control:
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", 6)
	line.custom_minimum_size.y = 24
	line.add_child(_glyph(kind))
	var t := _label(title, 11, DIM)
	t.custom_minimum_size.x = 76
	line.add_child(t)
	var v := _label(value)
	v.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	v.clip_text = true
	v.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	line.add_child(v)
	var d := _label(detail, 12, DIM)
	d.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	line.add_child(d)
	return line


## The weapon slot's arc (combat data) as the arc glyph's (0 forward, 1 forward wide, 2 back wide,
## 3 back, 4 all round).
func _arc_glyph_index(arc: int) -> int:
	match arc:
		1:
			return 0
		2, 3:
			return 1
		4, 12:
			return 2
		8:
			return 3
	return 4


## The original's scan of `index`. Kept current while open (_refresh_hud).
func _show_scan(index: int) -> void:
	var ship := _ship(index)
	if not _alive(ship):
		_hide_scan()
		return
	var opening := not _scan_panel.visible or _scan_ship != index
	_scan_ship = index
	var scan: Dictionary = ship.get("scan", {})
	var mine := _mine(ship)
	_scan_title.text = String(ship.get("name", ""))
	if not mine:
		_scan_title.add_theme_color_override("font_color", _color(ship).lightened(0.25))
	else:
		_scan_title.remove_theme_color_override("font_color")
	var sub := []
	if not mine:
		sub.append(_side_name(int(ship.get("owner", -1))))
	for key in ["hull", "crew", "officer"]:
		if scan.has(key):
			sub.append(String(scan[key]))
	_scan_sub.text = "   ·   ".join(sub)
	var sprite_info: Dictionary = ship.get("sprite", {})
	_scan_picture.texture = art.texture(String(sprite_info.get("lbx", "")), int(sprite_info.get("entry", -1)), 0)
	_set_bars(_scan_bars, ship)
	for child in _scan_picture.get_children():
		(child as CanvasItem).queue_redraw()
	for child in _scan_systems.get_children():
		_scan_systems.remove_child(child)
		child.queue_free()
	var shield: Dictionary = ship.get("shield", {})
	var arcs: Array = shield.get("arcs", [0, 0, 0, 0])
	var armor: Dictionary = ship.get("armor", {})
	var structure: Dictionary = ship.get("structure", {})
	var shield_detail := "none" if int(shield.get("max", 0)) <= 0 else "max %d" % int(shield.get("max", 0)) # each arc's on the picture
	var helps: Dictionary = scan.get("help", {})
	_scan_systems.add_child(_with_help(_scan_line("shield", "SHIELD", String(scan.get("shield", "")), shield_detail), int(helps.get("shield", -1))))
	_scan_systems.add_child(_with_help(_scan_line("armor", "ARMOUR", String(scan.get("armor", "")), "%d / %d" % [int(armor.get("now", 0)), int(armor.get("max", 0))]), int(helps.get("armor", -1))))
	_scan_systems.add_child(_scan_line("structure", "STRUCTURE", "", "%d / %d" % [int(structure.get("now", 0)), int(structure.get("max", 0))]))
	_scan_systems.add_child(_with_help(_scan_line("computer", "COMPUTER", String(scan.get("computer", "")), "%s %d" % [String(scan.get("attack_label", "Beam OCV")), int(scan.get("attack", 0))]), int(helps.get("computer", -1))))
	if scan.has("drive"):
		_scan_systems.add_child(_with_help(_scan_line("drive", "DRIVE", String(scan.drive), "speed %d" % int(scan.get("speed", 0))), int(helps.get("drive", -1))))
	var values := ["%s %d" % [String(scan.get("defense_label", "Beam DCV")), int(scan.get("defense", 0))], "Missile evasion %d" % int(scan.get("evade", 0))]
	if int(ship.get("marines", 0)) > 0:
		values.append("Marines %d" % int(ship.get("marines", 0)))
	_scan_values.text = "      ".join(values)
	for child in _scan_body.get_children():
		_scan_body.remove_child(child)
		child.queue_free()
	for weapon: Dictionary in ship.get("weapons", []):
		var row := _panel(&"Row")
		var line := HBoxContainer.new()
		line.add_theme_constant_override("separation", 8)
		row.add_child(line)
		var glyph := preload("arc_glyph.gd").new()
		glyph.arc = _arc_glyph_index(int(weapon.get("arc", 0)))
		glyph.custom_minimum_size = Vector2(22, 22)
		glyph.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		line.add_child(glyph)
		var count := int(weapon.get("count", 0))
		var n := _label("%d×" % count, 0, AMBER)
		n.custom_minimum_size.x = 34
		line.add_child(n)
		var name := _label(String(weapon.get("name", "")), 0, SYSTEMS_COLOR if count == 0 else TEXT)
		name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(name)
		var ammo := int(weapon.get("ammo", 0))
		var detail := "" # the arc is the glyph's (owner 2026-11-22)
		if ammo > 0 and ammo < 99:
			detail = "%d left" % ammo
		if count == 0:
			detail = "destroyed"
		line.add_child(_label(detail, 12, DIM))
		_scan_body.add_child(_with_help(row, int(weapon.get("help", -1))))
	for special: Dictionary in scan.get("specials", []):
		var row := _panel(&"Row")
		var damaged := bool(special.get("damaged", false))
		var line := HBoxContainer.new()
		row.add_child(line)
		var name := _label(String(special.get("name", "")), 0, SYSTEMS_COLOR if damaged else TEXT)
		name.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		line.add_child(name)
		if damaged:
			line.add_child(_label("damaged", 12, SYSTEMS_COLOR))
		_scan_body.add_child(_with_help(row, int(special.get("help", -1))))
	_scan_panel.show()
	_toast.hide()
	if opening:
		# Out of the scanned ship to its place.
		_scan_from = _to_screen(_ships[index].shown) if _ships.has(index) else size / 2.0
		_scan_anim = 0.0
		var tween := create_tween()
		tween.tween_property(self, "_scan_anim", 1.0, 0.22).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)


# ---- Combat graphics: the specials (owner 2026-11-22; notes/antares/combat-effects-legacy.md) ----

const TICK_S := 0.055 # Release_Time(1)
const SYNC_S := 0.033 # loops paced only by page flips: about two frames at 60 Hz
const SFX := "cmbtsfx.lbx"


## A ship's centre on the field (as shown).
func _centre(index: int) -> Vector2:
	if _ships.has(index):
		return _ships[index].shown
	return _center(_ship(index))


func _sound(id: int) -> void:
	if game != null and id >= 0:
		game.play_game_sound(id, -1, 0)


## The stasis ball's entry and flip for a ship (CMBTSPEC::Draw_Stasis_Ball): by size and the folded
## facing.
func _stasis_ball(index: int) -> Array:
	var ship := _ship(index)
	var size := int(ship.get("size", 0))
	var f := int(ship.get("facing", 0)) % 16
	var d := f
	var flip := Vector2.ONE
	if f >= 5 and f <= 8:
		d = 8 - f
		flip = Vector2(-1, 1)
	elif f >= 9 and f <= 12:
		d = f - 8
	elif f >= 13:
		d = 16 - f
		flip = Vector2(1, -1)
	var base := 53 if size == 0 else (58 if size <= 2 else (63 if size == 3 else 9))
	return [base + d, flip]


## The plasma web's entry and flip (CMBTSPEC::Draw_Plasma_Web).
func _web_sprite(index: int) -> Array:
	var ship := _ship(index)
	var size := int(ship.get("size", 0))
	var facing := int(ship.get("facing", 0)) % 16
	var sub := 4 if facing == 4 or facing == 12 else facing % 4
	var even := ((facing / 4) & 1) == 0
	var entry: int
	if size == 0:
		entry = 31 + sub if even else 35 - sub
	elif size <= 2:
		entry = 26 + sub if even else 30 - sub
	elif size == 3:
		entry = 21 + sub if even else 25 - sub
	else:
		entry = 21 + sub if even else 20 - sub
	var flip := Vector2(-1, 1) if facing / 4 == 1 else (Vector2(1, -1) if facing / 4 == 3 else Vector2.ONE)
	return [entry, flip]


## Plays a special as the original draws it; returns how long the rules wait.
func _play_special(event: Dictionary) -> float:
	var kind := String(event.get("kind", ""))
	var from := int(event.get("from", -1))
	var to := int(event.get("to", -1))
	var parts: Array = []
	var life := 0.0
	match kind:
		"stellar": # beam CMBTSFX 40 18 frames x 110 ms; impact 41 (glass) from beam frame 9
			parts.append({"type": "strip", "name": SFX, "entry": 40, "from": _centre(from), "to": _centre(to), "start": 0.0, "frame_time": 2 * TICK_S, "frames": 18})
			parts.append({"type": "anim", "name": SFX + "!glass=stellar", "entry": 41, "at": _centre(to), "start": 9 * 2 * TICK_S, "frame_time": 2 * TICK_S, "frames": 9})
			life = 18 * 2 * TICK_S
			parts.append({"type": "sound", "id": 20, "start": 0.0})
		"stasis": # beam CMBTSFX 1, 7 frames x 110 ms; then the ball grows over 10 x 55 ms
			parts.append({"type": "strip", "name": SFX, "entry": 1, "from": _centre(from), "to": _centre(to), "start": 0.0, "frame_time": 2 * TICK_S, "frames": 7})
			var ball: Array = _stasis_ball(to)
			parts.append({"type": "anim", "name": SFX + "!shield", "entry": ball[0], "flip": ball[1], "at": _centre(to), "start": 7 * 2 * TICK_S,
				"frame_time": 10 * TICK_S, "frames": 1, "grow": 10 * TICK_S})
			life = 7 * 2 * TICK_S + 10 * TICK_S
			parts.append({"type": "sound", "id": 10, "start": 0.0})
		"tractor": # CMBTSFX 2 frames 0-6 x 55 ms (then held, frame 6, as a lasting look)
			parts.append({"type": "strip", "name": SFX, "entry": 2, "from": _centre(from), "to": _centre(to), "start": 0.0, "frame_time": TICK_S, "frames": 7})
			life = 8 * TICK_S
		"plasma_web": # a ball (CMBTSFX 14) flies 6 px a frame, then the impact by size (glass)
			var a := _centre(from)
			var b := _centre(to)
			var fly := a.distance_to(b) / 6.0 * SYNC_S
			parts.append({"type": "proj", "name": SFX, "entry": 14, "from": a, "to": b, "start": 0.0, "dur": fly, "frame_time": SYNC_S, "frames": 12})
			var size := int(_ship(to).get("size", 0))
			var impact := 36 if size == 0 else (37 if size <= 2 else (38 if size == 3 else 39))
			parts.append({"type": "anim", "name": SFX + "!glass=web", "entry": impact, "at": b, "start": fly, "frame_time": SYNC_S, "frames": 20})
			life = fly + 20 * SYNC_S
			parts.append({"type": "sound", "id": 0x1B, "start": 0.0})
		"black_hole": # CMBTSFX 47 frame i%3, 12 x 55 ms, the target end raised 8 px
			parts.append({"type": "strip", "name": SFX, "entry": 47, "from": _centre(from), "to": _centre(to) - Vector2(0, 8), "start": 0.0,
				"frame_time": TICK_S, "frames": 3, "loop": true})
			life = 12 * TICK_S
			parts.append({"type": "sound", "id": 10, "start": 0.0})
		"gyro": # CMBTSFX 8: frames 0-7 x 55 ms, then 8 x 110 ms cycling 5-7 while the target spins
			parts.append({"type": "strip", "name": SFX, "entry": 8, "from": _centre(from), "to": _centre(to), "start": 0.0, "frame_time": TICK_S, "frames": 8})
			parts.append({"type": "strip", "name": SFX, "entry": 8, "from": _centre(from), "to": _centre(to), "start": 8 * TICK_S, "frame_time": 2 * TICK_S,
				"frames": 3, "first": 5, "loop": true, "until": 8 * TICK_S + 16 * TICK_S})
			life = 8 * TICK_S + 16 * TICK_S
			parts.append({"type": "sound", "id": 11, "start": 0.0})
		"blast": # Do_Spherical_SFX: 0 flux, 1 pulsar (CMBTSFX 52 glass), 2 compressor (6), 3 drive (SPHERSFX 0), 4 quantum (1)
			var type := int(event.get("type", 1))
			var at := _centre(int(event.get("ship", -1)))
			var name := SFX + "!glass=pulsar"
			var entry := 52
			var frames := 11
			match type:
				2:
					name = SFX
					entry = 6
					frames = 12
				3:
					name = "sphersfx.lbx!glass=warp"
					entry = 0
					frames = 12
				4:
					name = "sphersfx.lbx!glass=warp"
					entry = 1
					frames = 23
			var source := int(event.get("ship", -1))
			if type >= 3 and _ships.has(source):
				# Draw_Ship_Collapsing: the dying ship under the blast, shrinking (100 % less 10 a
				# frame; the quantum detonator's 132 % less 8, capped at 99) and turning a facing a
				# frame, gone below 2 %.
				var e: Dictionary = _ships[source]
				parts.append({"type": "collapse", "ship": source, "start": 0.0, "frame_time": TICK_S, "scale0": 100 if type == 3 else 132,
					"less": 10 if type == 3 else 8, "at": at, "angle": e.angle, "tex": e.sprite.texture, "offset": e.sprite.offset + Vector2(e.get("pad", 0.0), e.get("pad", 0.0)),
					"scale": e.sprite.scale, "dur": (frames + 1) * TICK_S})
			parts.append({"type": "anim", "name": name, "entry": entry, "at": at, "start": 0.0, "frame_time": TICK_S, "frames": frames + 1})
			# Nearby ships shake (+-2 px) as the blast's front passes them.
			parts.append({"type": "shake", "blast": type, "source": source, "at": at, "start": 0.0, "frame_time": TICK_S, "dur": (frames + 1) * TICK_S})
			life = (frames + 1) * TICK_S
			parts.append({"type": "sound", "id": 0x14 if type <= 2 else (0x2F if type == 3 else 0x19), "start": 0.0})
			if type >= 3:
				parts.append({"type": "sound", "id": 8, "start": 0.0})
		"lightning": # CMBTSFX 0 on the defending ship, 15 x 55 ms
			parts.append({"type": "anim", "name": SFX, "entry": 0, "at": _centre(int(event.get("ship", -1))), "start": 0.0, "frame_time": TICK_S,
				"frames": 7, "loop": true, "until": 15 * TICK_S})
			life = 15 * TICK_S
			parts.append({"type": "sound", "id": 4, "start": 0.0})
		"anti_missile": # a rocket from the ship to the stack, 20 px a frame x 55 ms
			var a := _centre(int(event.get("ship", -1)))
			var b := _stack_point(int(event.get("stack", -1)), a)
			var fly := maxf(a.distance_to(b) / 20.0, 1.0) * TICK_S
			parts.append({"type": "rocket", "from": a, "to": b, "start": 0.0, "dur": fly})
			life = fly
		"missile_kill": # CMBTSFX 7 at the stack, 7 x 55 ms
			var at := _stack_point(int(event.get("stack", -1)), Vector2.ZERO)
			parts.append({"type": "anim", "name": SFX, "entry": 7, "at": at, "start": 0.0, "frame_time": TICK_S, "frames": 7})
			life = 7 * TICK_S
			parts.append({"type": "sound", "id": 0x1c, "start": 0.0})
		"bombs": # CMBTMISL 144+facing flies 6 px a frame, then CMBTSFX 7 on the planet
			var a := _centre(from)
			var b := _centre(to)
			var travel := b - a
			var facing := posmod(int(round(-travel.angle() / (PI / 8.0))), 16)
			var fly := maxf(a.distance_to(b) / 6.0, 1.0) * TICK_S * 0.5
			parts.append({"type": "proj", "name": "cmbtmisl.lbx", "entry": 144 + facing, "from": a, "to": b, "start": 0.0, "dur": fly, "frame_time": TICK_S, "frames": 1})
			parts.append({"type": "anim", "name": SFX, "entry": 7, "at": b, "start": fly, "frame_time": TICK_S, "frames": 7})
			life = fly + 7 * TICK_S
			parts.append({"type": "sound", "id": 24, "start": 0.0})
		"retreat": # the portal (CMBTSFX 42, 20 x 55 ms) ahead of the ship; the ship slides in and goes
			var index := int(event.get("ship", -1))
			var at := _centre(index)
			var attacker := int(_ship(index).get("owner", -1)) == int(state.get("attacker", -2))
			var ahead := Vector2(-1.0 if attacker else 1.0, 0.0) * (_radius(index) + 40.0)
			parts.append({"type": "anim", "name": SFX, "entry": 42, "at": at + ahead, "start": 0.0, "frame_time": TICK_S, "frames": 20})
			parts.append({"type": "fade", "ship": index, "start": 0.0, "dur": 20 * TICK_S, "slide": ahead})
			life = 20 * TICK_S
			parts.append({"type": "sound", "id": 52, "start": 0.0})
		"teleport": # the ship dissolves at its old place and forms at the new (10 steps each way)
			var index := int(event.get("ship", -1))
			var off := _center(_ship(index)) - Vector2(float(_ship(index).get("x", 0)), float(_ship(index).get("y", 0))) * SQUARE
			parts.append({"type": "ghost", "ship": index, "at": Vector2(float(event.get("x0", 0)), float(event.get("y0", 0))) * SQUARE + off,
				"start": 0.0, "dur": 10 * SYNC_S, "out": true})
			parts.append({"type": "fade", "ship": index, "start": 0.0, "dur": 10 * SYNC_S, "fade_in_after": true})
			life = 20 * SYNC_S
			parts.append({"type": "sound", "id": 0x37, "start": 0.0})
		"cloak": # Draw_Cloak: the ship darkens (on, phase out) or brightens (off, phase in)
			var index := int(event.get("ship", -1))
			var k := int(event.get("cloak_type", 1)) # "kind" is the event's own
			var steps := 24 if k == 1 or k == 2 else (60 if k == 4 else 36)
			parts.append({"type": "cloak", "ship": index, "start": 0.0, "dur": steps * TICK_S, "step": TICK_S, "steps": steps, "mode": k})
			life = steps * TICK_S
			parts.append({"type": "sound", "id": 10, "start": 0.0})
		"fighters": # each picture fires a short beam at the target (the owner's point-defence weapon)
			var stack := int(event.get("stack", -1))
			var target := _centre(to)
			var origin := _stack_point(stack, Vector2(float(event.get("x", 0)), float(event.get("y", 0))))
			for k in 3:
				var jitter := Vector2(randf_range(-10, 10), randf_range(-10, 10))
				parts.append({"type": "bolt", "from": origin + jitter, "to": target, "start": k * TICK_S, "dur": 5 * TICK_S})
			life = 8 * TICK_S
	return _add_sfx(parts, life)


## Adds a special's parts as one effect (at the x2 speed every time is halved: the legacy loops
## step two frames); returns its length.
func _add_sfx(parts: Array, life: float) -> float:
	if parts.is_empty():
		return 0.0
	if _speedx2():
		for part: Dictionary in parts:
			for key in ["start", "frame_time", "dur", "until", "grow"]:
				if part.has(key):
					part[key] = float(part[key]) * 0.5
		life *= 0.5
	for part: Dictionary in parts:
		if part.has("name"):
			art.sprite(String(part.name), int(part.entry))
	_effects.append({"kind": "sfx", "t": 0.0, "life": life, "parts": parts})
	return life


var _death_played := {} # ship index -> its death is drawn (the destroyed event's)


## A ship's death (CMBTSPEC::Destroy_Ship_FX): a monster's own animation (CMBTSFX 0x45-0x4E, its
## facing's 16 frames: shrinking to a point, then a twinkle), a drive or quantum-detonator death
## (the blast event that follows draws it), else the explosion by size.
func _play_death(event: Dictionary) -> float:
	var index := int(event.get("ship", -1))
	if index < 0 or _death_played.has(index):
		return 0.0
	_death_played[index] = true
	var at := _centre(index)
	if event.has("monster_entry"):
		var entry := int(event.monster_entry)
		if entry < 0:
			return 0.0
		var facing := int(event.get("facing", 0)) % 16
		var quadrant := (facing - 1) / 4 # C's division: facing 0 is in the first quadrant
		var dir := facing
		var flip := Vector2.ONE
		match quadrant:
			1:
				dir = 8 - facing
				flip = Vector2(-1, 1)
			2:
				dir = facing - 8
				flip = Vector2(-1, -1)
			3:
				dir = 16 - facing
				flip = Vector2(1, -1)
		var parts: Array = [{"type": "anim", "name": SFX, "entry": entry, "at": at, "start": 0.0, "frame_time": TICK_S, "frames": 16,
			"first": dir * 16, "flip": flip}, {"type": "hide", "ship": index, "start": 0.0, "dur": 16 * TICK_S}]
		if int(event.get("monster_sound", -1)) >= 0:
			parts.append({"type": "sound", "id": int(event.monster_sound), "start": 0.0})
		return _add_sfx(parts, 16 * TICK_S)
	if int(event.get("death", 1)) != 1:
		return 0.0 # Do_Spherical_SFX: the blast event that follows
	return _play_explosion(index, at, int(event.get("size", 0)))


## Destroy_Ship_FX's explosion: CMBTSFX 3 (size 0, 18 frames), 4 (sizes 1-2), 5 (3-5; 12 frames),
## 55 ms a frame, the ship gone from the middle frame; Play_Ship_Explosion_SFX's sound.
func _play_explosion(index: int, at: Vector2, size: int) -> float:
	var entry := 3 if size == 0 else (4 if size <= 2 else 5)
	var frames := 18 if entry == 3 else 12
	var parts: Array = [{"type": "anim", "name": SFX, "entry": entry, "at": at, "start": 0.0, "frame_time": TICK_S, "frames": frames},
		{"type": "hide", "ship": index, "start": float(frames / 2) * TICK_S, "dur": float(frames) * TICK_S}]
	var pick := randi_range(1, 3) + size - 1
	if pick == 0:
		parts.append({"type": "sound", "id": 0x1d, "start": 0.0})
	elif pick == 5:
		parts.append({"type": "sound", "id": 0x1e, "start": 0.0})
		parts.append({"type": "sound", "id": 0x2b, "start": 0.0})
	else:
		parts.append({"type": "sound", "id": 0x18, "start": 0.0})
	return _add_sfx(parts, float(frames) * TICK_S)


## A missile stack's point (the state's), else `fallback`.
func _stack_point(stack: int, fallback: Vector2) -> Vector2:
	for missile: Dictionary in state.get("missiles", []):
		if int(missile.get("i", -1)) == stack:
			return Vector2(float(missile.get("x", 0)), float(missile.get("y", 0)))
	return fallback


func _draw_sfx(effect: Dictionary) -> void:
	var t: float = effect.t
	for part: Dictionary in effect.parts:
		var local := t - float(part.get("start", 0.0))
		if local < 0.0 or (part.has("until") and t > float(part.until)):
			continue
		match String(part.type):
			"anim", "strip", "proj":
				var name := String(part.name)
				var entry := int(part.entry)
				var s: Dictionary = art.sprite(name, entry)
				if s.is_empty():
					continue
				var count: int = s.frames.size()
				var step := int(local / float(part.get("frame_time", TICK_S)))
				var frames := int(part.get("frames", count))
				var frame: int
				if bool(part.get("loop", false)):
					frame = int(part.get("first", 0)) + step % maxi(mini(frames, count - int(part.get("first", 0))), 1)
				else:
					if step >= frames and String(part.type) == "anim" and not part.has("grow"):
						continue # played out
					frame = int(part.get("first", 0)) + mini(step, frames - 1)
				frame = clampi(frame, 0, count - 1)
				var box: Vector2 = s.size
				if String(part.type) == "anim":
					var scale := 1.0
					if part.has("grow"):
						scale = clampf(local / float(part.grow), 0.0, 1.0) * 0.9 + 0.1
					var flip: Vector2 = part.get("flip", Vector2.ONE)
					_marks.draw_set_transform(part.at, 0.0, flip * scale)
					art.draw(_marks, name, entry, frame, -box / 2.0, 1.0)
					_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
				elif String(part.type) == "strip":
					# Draw_Bitmap_Line: the strip stretched along the line, centred on its middle.
					var a: Vector2 = part.from
					var b: Vector2 = part.to
					var length := a.distance_to(b)
					if length < 1.0:
						continue
					_marks.draw_set_transform((a + b) / 2.0, (b - a).angle(), Vector2(length / box.x, 1.0))
					art.draw(_marks, name, entry, frame, -box / 2.0, 1.0)
					_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
				else: # a projectile along its path
					var k := clampf(local / maxf(float(part.dur), 0.001), 0.0, 1.0)
					if k >= 1.0:
						continue
					var at := (part.from as Vector2).lerp(part.to, k)
					art.draw(_marks, name, entry, frame, at - box / 2.0, 1.0)
			"rocket":
				var k := clampf(local / maxf(float(part.dur), 0.001), 0.0, 1.0)
				if k < 1.0:
					var at := (part.from as Vector2).lerp(part.to, k)
					_marks.draw_rect(Rect2(at - Vector2(1.5, 1.5), Vector2(3, 3)), Color(1.0, 0.85, 0.5))
			"bolt":
				var k := clampf(local / maxf(float(part.dur), 0.001), 0.0, 1.0)
				if k < 1.0:
					var fade := 1.0 - k
					_marks.draw_line(part.from, part.to, Color(1.0, 0.45, 0.3, 0.35 * fade), 3.0 / _zoom)
					_marks.draw_line(part.from, part.to, Color(1.0, 0.85, 0.7, 0.9 * fade), 1.2 / _zoom)
			"collapse": # Draw_Ship_Collapsing: shrinking and turning a facing a frame under the blast
				var frame := int(local / maxf(float(part.frame_time), 0.001))
				var pct := mini(int(part.scale0) - int(part.less) * frame, 99)
				var tex: Texture2D = part.get("tex")
				if pct >= 2 and tex != null:
					_marks.draw_set_transform(part.at, float(part.angle) - float(frame) * PI / 8.0, (part.scale as Vector2) * (float(pct) / 100.0))
					_marks.draw_texture(tex, part.offset)
					_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
			"ghost": # the ship's picture dissolving at its old place
				var index := int(part.ship)
				if _ships.has(index) and local < float(part.dur):
					var e: Dictionary = _ships[index]
					var tex: Texture2D = e.sprite.texture
					if tex != null:
						_marks.draw_set_transform(part.at, e.angle, e.sprite.scale)
						_marks.draw_texture(tex, e.sprite.offset + Vector2(e.get("pad", 0.0), e.get("pad", 0.0)), Color(1, 1, 1, 1.0 - local / float(part.dur)))
						_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


## A ship's look during a fade, retreat, death or collapse (the ship sprite's modulate).
func _ship_effect_look(index: int) -> Color:
	var look := Color.WHITE
	for effect: Dictionary in _effects:
		if String(effect.kind) != "sfx" or float(effect.t) <= 0.0:
			continue
		for part: Dictionary in effect.parts:
			if int(part.get("ship", -999)) != index:
				continue
			var local := float(effect.t) - float(part.get("start", 0.0))
			var k := clampf(local / maxf(float(part.get("dur", 1.0)), 0.001), 0.0, 1.0)
			match String(part.type):
				"fade":
					if bool(part.get("fade_in_after", false)):
						look.a = minf(look.a, k)
					else:
						look.a = minf(look.a, 1.0 - k)
				"hide":
					if local >= 0.0:
						look.a = 0.0
				"collapse": # drawn by the effect itself
					look.a = 0.0
	return look


## Draw_Cloak's darkness and noise frame for a ship's cloak effect: [darkness, frame], or [].
## Kind 1 on: step / 4 up to 6 (24 steps); 2 off: 6 - step / 4; 4 phase out: step / 3 up to 20
## (60 steps); 5 phase in: 12 - step / 3 (36 steps). 55 ms a step.
func _cloak_darkness(index: int) -> Array:
	for effect: Dictionary in _effects:
		if String(effect.kind) != "sfx" or float(effect.t) <= 0.0:
			continue
		for part: Dictionary in effect.parts:
			if String(part.type) != "cloak" or int(part.get("ship", -999)) != index:
				continue
			var step := int((float(effect.t) - float(part.get("start", 0.0))) / maxf(float(part.get("step", TICK_S)), 0.001))
			var steps := int(part.get("steps", 24))
			step = clampi(step, 0, steps - 1)
			var d := 0.0
			match int(part.get("mode", 1)):
				1:
					d = minf(step / 4, 6)
				2:
					d = maxf(6 - step / 4, 0)
				4:
					d = minf(step / 3, 20)
				5:
					d = maxf(12 - step / 3, 0)
			return [d, step % 16]
	return []


## The legacy frame counter (ship_frame: one a 55 ms tick).
func _legacy_frame() -> int:
	return int(Time.get_ticks_msec() / 55)


var _plasma_tex: ImageTexture = null
var _shield_glow := {} # ship index -> brightening under a shield hit (fades between frames)


## The cloak's noise map: 16 frames of 60x60, values 0-13, a slowly changing plasma (the original's
## COMBAT.LBX 0x31 is of this kind; generated, not copied).
func _plasma_texture() -> ImageTexture:
	if _plasma_tex != null:
		return _plasma_tex
	var noise := FastNoiseLite.new()
	noise.noise_type = FastNoiseLite.TYPE_SIMPLEX_SMOOTH
	noise.frequency = 0.09
	noise.fractal_octaves = 2
	noise.seed = 141
	var img := Image.create(60, 60 * 16, false, Image.FORMAT_L8)
	for f in 16:
		# A loop in the third dimension: frame 15 runs back into frame 0.
		var a := TAU * float(f) / 16.0
		for y in 60:
			for x in 60:
				var n := noise.get_noise_3d(float(x) + 9.0 * cos(a), float(y) + 9.0 * sin(a), 0.0)
				var v := clampi(int(round((n * 0.5 + 0.5) * 13.0)), 0, 13)
				img.set_pixel(x, y + 60 * f, Color8(v, v, v))
	_plasma_tex = ImageTexture.create_from_image(img)
	return _plasma_tex


## Do_Spherical_SFX's shake: a ship in the blast's reach drawn -2..+2 px off, a new offset each
## frame, while the blast's front passes it (pulsar and flux: rings by distance in squares; the
## others: within the front's radius by frame).
const BLAST_FRONT := [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0xd, 0xd, 0x16, 0x2e, 0x3d, 0x3d, 0x56, 0x5f, 0x6b, 0x72, 0x76, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xd,
	0x10, 0x19, 0x1e, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0, 0, 0xb, 0x15, 0x2d, 0x40, 0x41, 0x55, 0x61, 0x6c, 0x73, 0x73, 0, 0, 0, 0]


func _ship_shake(index: int) -> Vector2:
	for effect: Dictionary in _effects:
		if String(effect.kind) != "sfx" or float(effect.t) <= 0.0:
			continue
		for part: Dictionary in effect.parts:
			if String(part.type) != "shake" or int(part.get("source", -1)) == index or not _ships.has(index):
				continue
			var frame := int((float(effect.t) - float(part.start)) / maxf(float(part.frame_time), 0.001))
			if float(effect.t) - float(part.start) >= float(part.dur):
				continue
			var type := int(part.blast)
			var d: Vector2 = (_ships[index].shown as Vector2) - (part.at as Vector2)
			var shaking := false
			if type <= 2:
				var ratio := int(maxf(absf(d.x), absf(d.y)) / 20.0)
				shaking = (frame < 5 and ratio == 1) or (frame > 0 and frame < 7 and ratio == 2) or (frame > 4 and frame < 13 and ratio == 3) or (frame > 8 and ratio == 4)
			if not shaking and type != 0 and type != 1:
				var front := int(BLAST_FRONT[mini(type * 25 + frame, BLAST_FRONT.size() - 1)])
				var dist_sq := d.length_squared()
				var reach := float(front * front) + float(int(_ship(index).get("size", 0)) * 25)
				shaking = dist_sq <= reach and (front < 85 or dist_sq <= float(front * front - 625))
			if shaking:
				var h := hash([index, frame, int(part.source)])
				return Vector2(float(h % 5) - 2.0, float((h / 5) % 5) - 2.0)
	return Vector2.ZERO


## The lasting looks (CMBTDRW1::Draw_Ship): stasis ball, plasma web, black-hole vortex, absorber
## glow; a held tractor (Full_Draw_Combat_Screen).
func _draw_lasting() -> void:
	for ship: Dictionary in _ships_list():
		if not _alive(ship):
			continue
		var index := int(ship.i)
		if not _ships.has(index):
			continue
		var at: Vector2 = _ships[index].shown
		var tractor := int(ship.get("tractor", -1))
		if tractor >= 0 and _alive(_ship(tractor)) and _ships.has(tractor):
			_draw_lasting_strip(SFX, 2, 6, at, _ships[tractor].shown)
		if bool(ship.get("stasis", false)):
			var ball: Array = _stasis_ball(index)
			_draw_lasting_anim(SFX + "!shield", ball[0], 0, at, ball[1])
		elif bool(ship.get("webbed", false)):
			var web: Array = _web_sprite(index)
			_draw_lasting_anim(SFX + "!glass=web", web[0], _anim_frame, at, web[1])
		elif bool(ship.get("black_hole", false)):
			var size := int(ship.get("size", 0))
			var vortex := 46 if size == 0 else (45 if size <= 2 else (44 if size <= 4 else 43))
			var frame := (_anim_frame / 2) % 8 + (8 if int(ship.get("black_hole_flag", 1)) != 1 else 0)
			_draw_lasting_anim(SFX + "!glass=bhg", vortex, frame, at, Vector2.ONE)
		elif bool(ship.get("absorbing", false)):
			var size := int(ship.get("size", 0))
			var glow := 51 if size == 0 else (50 if size <= 2 else (49 if size == 3 else 48))
			_draw_lasting_anim(SFX, glow, (_anim_frame / 2 + index) % 4, at, Vector2.ONE, 0.6)


func _draw_lasting_anim(name: String, entry: int, frame: int, at: Vector2, flip: Vector2, alpha := 1.0) -> void:
	var s: Dictionary = art.sprite(name, entry)
	if s.is_empty():
		return
	var box: Vector2 = s.size
	_marks.draw_set_transform(at, 0.0, flip)
	art.draw(_marks, name, entry, frame % s.frames.size(), -box / 2.0, 1.0, Color(1, 1, 1, alpha))
	_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


func _draw_lasting_strip(name: String, entry: int, frame: int, a: Vector2, b: Vector2) -> void:
	var s: Dictionary = art.sprite(name, entry)
	var length := a.distance_to(b)
	if s.is_empty() or length < 1.0:
		return
	var box: Vector2 = s.size
	_marks.draw_set_transform((a + b) / 2.0, (b - a).angle(), Vector2(length / box.x, 1.0))
	art.draw(_marks, name, entry, mini(frame, s.frames.size() - 1), -box / 2.0, 1.0)
	_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


# ---- Effects (WP3.9c) ----

## Beam colours by weapon family (legacy builds them per weapon in the palette's beam slots).
const BEAM_COLORS := [["laser", Color(1.0, 0.25, 0.2)], ["fusion", Color(1.0, 0.55, 0.15)], ["phasor", Color(0.6, 0.8, 1.0)],
	["graviton", Color(0.75, 0.45, 1.0)], ["disrupt", Color(0.3, 1.0, 0.85)], ["death", Color(1.0, 0.3, 0.85)],
	["plasma", Color(0.4, 1.0, 0.4)], ["neutron", Color(0.4, 0.9, 1.0)], ["ion", Color(0.55, 0.75, 1.0)],
	["particle", Color(1.0, 0.95, 0.6)], ["tachyon", Color(0.9, 0.9, 1.0)], ["mauler", Color(1.0, 0.75, 0.2)],
	["stellar", Color(1.0, 1.0, 1.0)], ["pulsar", Color(0.9, 0.6, 1.0)], ["spatial", Color(0.7, 0.5, 1.0)],
	["mass driver", Color(0.75, 0.78, 0.82)], ["gauss", Color(0.75, 0.85, 0.95)]]
const PROJECTILE_WORDS := ["mass driver", "gauss", "mauler", "plasma torpedo"]
const SHOT_GAP := 0.06 # seconds between the shots of one weapon
const SHOT_LIFE := 0.28


func _beam_color(name: String) -> Color:
	var lower := name.to_lower()
	for pair in BEAM_COLORS:
		if lower.contains(pair[0]):
			return pair[1]
	return AMBER


## A volley from the engine (combat_native.cpp, Beam). WP3.9 stage 1: the original's own volley,
## tick by tick (beam_volley.gd), drawn natively (beam_draw.gd strands, the game's muzzle flashes
## and hit flares in the weapon's colours). Events without the volley's details (an older engine)
## get the 3.9c drawing: each weapon's shots one after another, sparks where they land.
func play_event(event: Dictionary) -> float:
	if not visible or String(event.get("kind", "")) != "beam":
		return 0.0
	if event.has("attacker"):
		var length := _play_volley(event)
		if length > 0.0:
			return length
		# Not a volley the port draws (a first weapon that isn't a beam): the 3.9c drawing.
	var from := int(event.get("from", -1))
	var to := int(event.get("to", -1))
	var missile := bool(event.get("missile", false))
	var point := Vector2(float(event.get("tx", 0)), float(event.get("ty", 0)))
	var delay := 0.0
	for shot: Dictionary in event.get("shots", []):
		var count := clampi(int(shot.get("count", 1)), 1, 12)
		var name := String(shot.get("name", ""))
		var projectile := false
		for word in PROJECTILE_WORDS:
			if name.to_lower().contains(word):
				projectile = true
		for n in count:
			var jitter := Vector2(randf_range(-6, 6), randf_range(-6, 6))
			_effects.append({"kind": "shot", "t": -delay, "life": SHOT_LIFE + (0.18 if projectile else 0.0), "from": from, "to": to,
				"missile": missile, "point": point, "jitter": jitter, "color": _beam_color(name), "projectile": projectile})
			delay += SHOT_GAP
		delay += 0.05
	return delay + SHOT_LIFE + 0.18 # the last shot has landed


## What changed between two states: ships destroyed (the game's explosion), hull damage (a number
## rising from the ship), shield arcs struck (that arc flashes).
func _diff_effects(before: Dictionary, after: Dictionary) -> void:
	var old_ships: Array = before.get("ships", [])
	for ship: Dictionary in after.get("ships", []):
		var index := int(ship.i)
		if index >= old_ships.size():
			continue
		var old: Dictionary = old_ships[index]
		if int(old.get("status", 1)) != 0:
			continue
		var at: Vector2 = _ships[index].shown if _ships.has(index) else _center(old)
		if int(ship.get("status", 0)) != 0:
			# A death without its event (an older engine): the ordinary explosion.
			if int(ship.get("status", 0)) == 5 and not _death_played.has(index):
				_death_played[index] = true
				_play_explosion(index, at, int(old.get("size", 0)))
			continue
		var hp_old := float((old.get("armor", {}) as Dictionary).get("now", 0)) + float((old.get("structure", {}) as Dictionary).get("now", 0))
		var hp_new := float((ship.get("armor", {}) as Dictionary).get("now", 0)) + float((ship.get("structure", {}) as Dictionary).get("now", 0))
		if hp_new < hp_old and not _captures_seen:
			_effects.append({"kind": "damage", "t": 0.0, "life": 1.3, "ship": index, "at": at, "text": "-%d" % int(hp_old - hp_new)})
		var arcs_old: Array = (old.get("shield", {}) as Dictionary).get("arcs", [])
		var arcs_new: Array = (ship.get("shield", {}) as Dictionary).get("arcs", [])
		for a in mini(arcs_old.size(), arcs_new.size()):
			if float(arcs_new[a]) < float(arcs_old[a]) and not _captures_seen and not _native_volleys:
				_effects.append({"kind": "shield", "t": 0.0, "life": 0.45, "ship": index, "arc": a})


func _effect_point(ship_index: int, fallback: Vector2) -> Vector2:
	if _ships.has(ship_index):
		return _ships[ship_index].shown
	return fallback


func _draw_effects(width: float) -> void:
	var font := get_theme_default_font()
	var holding := _cam_hold > 0.0 or _art_hold > 0.0
	for effect: Dictionary in _effects:
		var t: float = effect.t
		if t < 0.0 or (holding and t <= 0.0):
			continue # an effect waiting for the camera (or its art) isn't shown yet
		var life: float = effect.life
		var k := clampf(t / life, 0.0, 1.0)
		match String(effect.kind):
			"sfx":
				_draw_sfx(effect)
			"volley":
				_draw_volley_sprites(effect)
			"shot":
				var from := _effect_point(int(effect.from), effect.get("from_point", Vector2.ZERO))
				var to: Vector2 = effect.point if effect.missile else _effect_point(int(effect.to), Vector2.ZERO)
				to += effect.jitter
				var color: Color = effect.color
				if effect.projectile:
					# A bolt that flies, then sparks.
					var fly := clampf(t / (life * 0.6), 0.0, 1.0)
					var at := from.lerp(to, fly)
					if fly < 1.0:
						_marks.draw_circle(at, 4.0 * width * 1.5, Color(color, 0.35))
						_marks.draw_circle(at, 2.0 * width * 1.5, color.lightened(0.5))
					else:
						_spark(to, (t - life * 0.6) / (life * 0.4), width)
				else:
					var fade := 1.0 - smoothstep(0.55, 1.0, k)
					_marks.draw_line(from, to, Color(color, 0.28 * fade), 7.0 * width)
					_marks.draw_line(from, to, Color(color.lightened(0.35), 0.95 * fade), 2.2 * width)
					_spark(to, k, width)
			"capture":
				if effect.get("waiting", false):
					continue
				var frame := mini(int(t / CAPTURE_FRAME_TIME), int(effect.frames) - 1)
				art.draw(_marks, "capture", int(effect.entry), frame, effect.at, 1.0)
			"explosion":
				var s: Dictionary = art.sprite("cmbtsfx.lbx", int(effect.entry))
				if not s.is_empty():
					var frame := int(k * float(s.frames.size()))
					var box: Vector2 = s.size
					art.draw(_marks, "cmbtsfx.lbx", int(effect.entry), mini(frame, s.frames.size() - 1), effect.at - box / 2.0, 1.0)
				else:
					_marks.draw_circle(effect.at, 30.0 * k, Color(1.0, 0.6, 0.2, 1.0 - k))
			"damage":
				var at: Vector2 = _effect_point(int(effect.ship), effect.at) + Vector2(0, -_radius(int(effect.ship)) - 10.0 - 26.0 * k)
				_marks.draw_set_transform(at, 0.0, Vector2.ONE / _zoom)
				var text_size := font.get_string_size(String(effect.text), HORIZONTAL_ALIGNMENT_LEFT, -1, 16)
				_marks.draw_string_outline(font, Vector2(-text_size.x / 2.0, 0), String(effect.text), HORIZONTAL_ALIGNMENT_LEFT, -1, 16, 4, Color(0, 0, 0, 1.0 - k))
				_marks.draw_string(font, Vector2(-text_size.x / 2.0, 0), String(effect.text), HORIZONTAL_ALIGNMENT_LEFT, -1, 16, Color(1.0, 0.45, 0.4, 1.0 - k * k))
				_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
			"shield":
				var index := int(effect.ship)
				if not _ships.has(index):
					continue
				var e: Dictionary = _ships[index]
				# Arc a is clockwise from the nose (0 front, 1 right, 2 rear, 3 left); the nose points
				# along the ship's angle.
				var mid: float = e.angle + float(int(effect.arc)) * PI / 2.0
				var radius := _radius(index) + 5.0
				_marks.draw_arc(e.shown, radius, mid - PI / 4.0 + 0.08, mid + PI / 4.0 - 0.08, 18, Color(0.6, 1.0, 0.8, 0.9 * (1.0 - k)), 4.0 * width)
				_marks.draw_arc(e.shown, radius + 3.0 * width, mid - PI / 4.0 + 0.08, mid + PI / 4.0 - 0.08, 18, Color(0.35, 0.95, 0.6, 0.35 * (1.0 - k)), 9.0 * width)


## The game's hull-hit sparks (BEAMS 65) where a shot lands, or a flash until they load.
func _spark(at: Vector2, k: float, width: float) -> void:
	if k < 0.0 or k > 1.0:
		return
	var s: Dictionary = art.sprite("beams.lbx", 65)
	if not s.is_empty():
		var frame := mini(int(k * float(s.frames.size())), s.frames.size() - 1)
		var box: Vector2 = s.size
		art.draw(_marks, "beams.lbx", 65, frame, at - box / 2.0, 1.0, Color(1, 1, 1, 1.0 - k * 0.5))
	else:
		_marks.draw_circle(at, (3.0 + 6.0 * k) * width * 1.5, Color(1.0, 0.85, 0.6, 1.0 - k))

# ---- Native beams (WP3.9 stage 1) ----

## Plans the volley and adds it as an effect; returns its length in seconds.
func _play_volley(event: Dictionary) -> float:
	var entries: Array = []
	for shot: Dictionary in event.get("shots", []):
		entries.append({"weapon": int(shot.get("weapon", 0)), "count": int(shot.get("count", 1)), "result": int(shot.get("flags", 0)),
			"special": int(shot.get("special", 0)), "hits": int(shot.get("hits", 0)), "specials": int(shot.get("specials", 0)),
			"type": int(shot.get("type", 0)), "damage_entry": bool(shot.get("damage_entry", false)),
			"fire_points": shot.get("fire_points", [[0, 0]])})
	var volley := {"entries": entries, "attacker": event.attacker, "target": event.target, "missile": bool(event.get("missile", false)),
		"speedx2": bool(event.get("speedx2", false)), "shield_frames": int(event.get("shield_frames", 10))}
	var planner := BeamVolley.new()
	var ticks: Array = planner.plan(volley)
	if ticks.is_empty():
		return 0.0
	var fragments: Array = []
	for f: Array in event.get("fragments", []):
		fragments.append(BeamVolley.to_colors(f))
	var target_center: Array = event.target.get("center", [0, 0])
	var effect := {"kind": "volley", "t": 0.0, "life": float(ticks.size()) * BeamVolley.TICK, "ticks": ticks, "fragments": fragments,
		"tick": -1, "strands": [], "at": Vector2(float(target_center[0]), float(target_center[1])), "target_size": int(event.target.get("size", 0))}
	_native_volleys = true # the original's shield hits now: no 3.9c arc flashes
	var afters: Array = []
	for shot: Dictionary in event.get("shots", []):
		afters.append(shot.get("after", {}))
	effect["afters"] = afters
	effect["target_ship"] = int(event.target.get("ship", -1))
	_effects.append(effect)
	if event.has("before") and int(effect.target_ship) >= 0:
		_set_hp_view(int(effect.target_ship), effect, event.before)
	_request_volley_art(effect)
	return float(ticks.size()) * BeamVolley.TICK


## Asks for the volley's flashes and flares in their colours now; the volley waits (at most
## ART_WAIT_MAX) until they are here: each is a new export the first time a weapon fires in a
## direction, and a flash missing on its tick is lost (measured: 6 of 19 late locally).
func _request_volley_art(effect: Dictionary) -> void:
	var wanted := {}
	for ops: Array in effect.ticks:
		for op: Dictionary in ops:
			var slot := int(op.get("slot", 0))
			match String(op.op):
				"burst":
					wanted[[_slot_sprite(_first_ramp(effect, slot), slot), 1 + int(op.variant) * 32 + slot * 16 + int(op.dir)]] = true
				"explosion":
					wanted[[_slot_sprite(_first_ramp(effect, slot), slot), 65 + slot]] = true
				"ball":
					wanted[[_slot_sprite(_first_ramp(effect, 0), 0), 88]] = true
				"shield":
					wanted[[SHIELD_ART, _shield_entry(op, int(op.get("size", effect.get("target_size", 0))))]] = true
	_art_wait = []
	for key: Array in wanted.keys():
		if art.sprite(String(key[0]), int(key[1])).is_empty():
			_art_wait.append(key)
	_art_hold = 0.0001 if not _art_wait.is_empty() else 0.0


## The 16 colours a segment op draws with (Color, 8-bit) and the palette slot's base index.
func _volley_ramp(op: Dictionary, fragments: Array) -> Array:
	if op.has("recipe"):
		var fx: Dictionary = op.recipe
		if not fx.has("_colors"):
			fx["_colors"] = BeamVolley.to_colors(BeamVolley.recipe_ramp(fx))
		return fx._colors
	var frag := int((op.params as Dictionary).get("frag", 0))
	if frag < fragments.size():
		return fragments[frag]
	if not _warned_fragments:
		_warned_fragments = true
		push_warning("combat: a non-recipe beam (fragment %d) without the game's palette fragments; drawn grey" % frag)
	var grey := []
	for i in 16:
		grey.append(Color(i / 15.0, i / 15.0, i / 15.0))
	return grey


## The strands of one tick's segment ops, generated once per tick: [{beam: BeamDraw, ramp, base}].
func _volley_strands(effect: Dictionary, ops: Array) -> Array:
	var out: Array = []
	for op: Dictionary in ops:
		if String(op.op) != "seg":
			continue
		var base := 144 + 16 * int(op.slot)
		var beam: BeamDraw = BeamVolley.strands(op)
		out.append({"beam": beam, "ramp": _volley_ramp(op, effect.fragments), "base": base})
	return out


## A weapon's hit landing: the target's bars drop to what the engine recorded after it.
func _volley_damage(effect: Dictionary, ops: Array) -> void:
	var afters: Array = effect.get("afters", [])
	for op: Dictionary in ops:
		if String(op.op) == "damage":
			var entry := int(op.entry)
			if entry < afters.size() and not (afters[entry] as Dictionary).is_empty() and int(effect.get("target_ship", -1)) >= 0:
				_set_hp_view(int(effect.target_ship), effect, afters[entry])


## A tick's sounds, played by the game (its sound effects and volume; native::Play_Game_Sound).
func _volley_sounds(ops: Array) -> void:
	if art == null or art.game == null:
		return
	for op: Dictionary in ops:
		match String(op.op):
			"sound":
				art.game.play_game_sound(int(op.id), int(op.get("tag", -1)), int(op.get("pitch", 0)))
			"fade":
				art.game.fade_game_sound(int(op.tag), 10)


## The art name of a BEAMS.LBX sprite in the colours of the slot's weapon (sprites.cpp "~").
func _slot_sprite(op_ramp: Array, slot: int) -> String:
	var hex := PackedStringArray()
	for c: Color in op_ramp:
		hex.append(c.to_html(false))
	return "beams.lbx~%d=%s" % [144 + 16 * slot, ".".join(hex)]


func _draw_beams() -> void:
	var holding := _art_hold > 0.0 or _cam_hold > 0.0
	for effect: Dictionary in _effects:
		if String(effect.kind) != "volley" or effect.t < 0.0 or (holding and effect.t <= 0.0):
			continue # a new volley waits for its art or the camera
		var ticks: Array = effect.ticks
		var tick := mini(int(effect.t / BeamVolley.TICK), ticks.size() - 1)
		if tick != int(effect.tick):
			# Each tick passed plays its sounds once (a slow frame may pass several).
			for k in range(int(effect.tick) + 1, tick + 1):
				_volley_sounds(ticks[k])
				_volley_damage(effect, ticks[k])
			effect.tick = tick
			effect.strands = _volley_strands(effect, ticks[tick])
		for st: Dictionary in effect.strands:
			(st.beam as BeamDraw).draw(_beams, st.ramp, int(st.base))


## A volley's muzzle flashes, hit flares and the Mauler's ball on this tick (over the strands).
func _draw_volley_sprites(effect: Dictionary) -> void:
	if (_art_hold > 0.0 or _cam_hold > 0.0) and float(effect.t) <= 0.0:
		return # a new volley waits for its art or the camera
	var ticks: Array = effect.ticks
	var tick := mini(int(effect.t / BeamVolley.TICK), ticks.size() - 1)
	var ops: Array = ticks[tick]
	# Each slot's colours: those of the segment drawn in it this tick, else the last known.
	var ramps: Dictionary = effect.get("slot_ramps", {})
	for op: Dictionary in ops:
		if String(op.op) == "seg":
			ramps[int(op.slot)] = _volley_ramp(op, effect.fragments)
	effect["slot_ramps"] = ramps
	for op: Dictionary in ops:
		var slot := int(op.get("slot", 0))
		match String(op.op):
			"burst":
				if not ramps.has(slot):
					ramps[slot] = _first_ramp(effect, slot)
				var entry := 1 + int(op.variant) * 32 + slot * 16 + int(op.dir)
				_draw_centered(_slot_sprite(ramps[slot], slot), entry, int(op.frame) % 3, op.at)
			"explosion":
				if not ramps.has(slot):
					ramps[slot] = _first_ramp(effect, slot)
				var name := _slot_sprite(ramps[slot], slot)
				var s: Dictionary = art.sprite(name, 65 + slot)
				_note_art(name, 65 + slot, s)
				if not s.is_empty():
					var count: int = s.frames.size()
					var frame := int(op.frame) % maxi(count - 1, 1)
					art.draw(_marks, name, 65 + slot, frame, Vector2(op.at) - Vector2(8, 8), 1.0)
			"ball":
				if not ramps.has(0):
					ramps[0] = _first_ramp(effect, 0)
				_draw_centered(_slot_sprite(ramps[0], 0), 88, 0, op.at)
			"shield":
				_draw_shield_hit(op, effect)
				if int(effect.get("target_ship", -1)) >= 0 and not op.get("reflected", false):
					_shield_glow[int(effect.target_ship)] = 1.0 # Mask_Brighten_Bitmap while it shows


## The colours of the first segment in `slot` anywhere in the volley (a burst on frame 0 comes
## before its weapon's first segment).
func _first_ramp(effect: Dictionary, slot: int) -> Array:
	for ops: Array in effect.ticks:
		for op: Dictionary in ops:
			if String(op.op) == "seg" and int(op.slot) == slot:
				return _volley_ramp(op, effect.fragments)
	return _volley_ramp({"params": {"frag": 0}}, effect.fragments)


## BEAMS::Shield_Anims: the shield hit's entry for its size, direction, special and heavy.
func _shield_entry(op: Dictionary, target_size: int) -> int:
	var size_idx := BeamVolley.shield_size(target_size)
	var base_rot: int = SHIELD_BASE_ROT[int(op.rotation) & 15]
	if size_idx >= 4:
		return 113 + size_idx * 5 + base_rot
	if int(op.get("heavy", 0)) != 0:
		return 129 + size_idx
	if int(op.get("special", 0)) != 0:
		return 109 + size_idx * 5 + base_rot
	return 67 + size_idx * 5 + base_rot


## BEAMS::Draw_Shield_Hit: the frame in a box the shield's diameter wide round the target's
## centre, mirrored for the other directions, drawn as glass (the export's tint and strength,
## blended over the ship). Not yet: the ship brightened under it (Mask_Brighten_Bitmap).
func _draw_shield_hit(op: Dictionary, effect: Dictionary) -> void:
	var target_size := int(op.get("size", effect.get("target_size", 0))) # a reflected beam's: the attacker's
	var entry := _shield_entry(op, target_size)
	var s: Dictionary = art.sprite(SHIELD_ART, entry)
	_note_art(SHIELD_ART, entry, s)
	if s.is_empty():
		return
	var diam: float = BeamVolley.SHIELD_DIAMETERS[BeamVolley.shield_size(target_size)]
	var method: int = SHIELD_ROT_METHOD[int(op.rotation) & 15]
	var flip := Vector2(-1.0 if method == 1 or method == 2 else 1.0, -1.0 if method == 2 or method == 3 else 1.0)
	var frame := int(op.frame) % maxi(s.frames.size(), 1)
	_marks.draw_set_transform(op.get("at", effect.at), 0.0, flip)
	art.draw(_marks, SHIELD_ART, entry, frame, Vector2(-diam / 2.0, -diam / 2.0), 1.0)
	_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)


## Whether a volley's art was there the first time a tick drew it (measured, WP3.9: the owner plays
## from a network share).
func _note_art(name: String, entry: int, s: Dictionary) -> void:
	var key := "%s/%d" % [name.get_slice("~", 0), entry]
	if art_drawn.has(key):
		return
	if s.is_empty():
		art_late[key] = true
	else:
		art_drawn[key] = true


## ERIC::Draw_Centered: the frame's 1x box centred on `at`.
func _draw_centered(name: String, entry: int, frame: int, at: Vector2i) -> void:
	var s: Dictionary = art.sprite(name, entry)
	_note_art(name, entry, s)
	if s.is_empty():
		return
	var box: Vector2 = s.size
	art.draw(_marks, name, entry, mini(frame, s.frames.size() - 1), Vector2(at) - Vector2(floorf(box.x / 2.0), floorf(box.y / 2.0)), 1.0)


# ---- Settings ----

func _load_setting(key: String, fallback: Variant) -> Variant:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return fallback
	return cfg.get_value("combat", key, fallback)


func _save_setting(key: String, value: Variant) -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("combat", key, value)
	cfg.save(SETTINGS)
