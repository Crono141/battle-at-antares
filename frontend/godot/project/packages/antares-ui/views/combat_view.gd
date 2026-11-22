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
const MOVE_SPEEDS := [["Slow", 1.6], ["Normal", 1.0], ["Fast", 0.5], ["Instant", 0.0]]
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
var _tl_dragging := false
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
	_world = Node2D.new()
	add_child(_world)
	_backdrop = Node2D.new()
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
				dur = 1.0 # the explosion (from the state's change, _diff_effects)
			"teleport":
				dur = 0.25
			# WP3.9d-2: placeholders until each effect gets its own art (stages 3.9g-3.9i): a
			# coloured line from shooter to target, the game's explosion, or a short hold.
			"stasis", "tractor", "plasma_web", "stellar", "black_hole", "absorber", "fighters", "bombs":
				dur = _placeholder_line(event)
			"gyro":
				dur = float(event.get("steps", 8)) * TURN_STEP_TIME * 0.5
			"blast":
				var at := _effect_point(index if index >= 0 else int(event.get("ship", -1)), Vector2(float(event.get("x", 0)), float(event.get("y", 0))))
				_effects.append({"kind": "explosion", "t": 0.0, "life": 1.1, "at": at, "entry": 5})
				dur = 1.1
			"retreat":
				dur = 0.6
			"missiles_step":
				dur = 0.055 # one legacy frame: the missiles' positions are in the state
			"cloak", "lightning", "anti_missile", "missile_kill", "missile_launch", "missile_arrive", "damage":
				dur = 0.25
			_:
				dur = 0.0
	_animating = {"id": id, "t": 0.0, "dur": dur}
	if dur <= 0.0:
		_finish_animation()


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
			m.t += delta
			var k := clampf(m.t / maxf(m.dur, 0.001), 0.0, 1.0)
			e.shown = (m.p0 as Vector2).lerp(m.p1, k)
			if k >= 1.0:
				e.erase("motion")
		else:
			e.shown = e.shown.lerp(goal, minf(1.0, delta * 18.0))
		var goal_angle: float = e.get("goal_angle", e.angle)
		e.angle = lerp_angle(e.angle, goal_angle, minf(1.0, delta * 14.0))
		e.sprite.position = e.shown
		e.sprite.rotation = e.angle
	if _cam_goal != Vector2.INF:
		_cam = _cam.lerp(_cam_goal, minf(1.0, delta * 6.0))
		if _cam.distance_to(_cam_goal) < 1.0:
			_cam_goal = Vector2.INF
	if _animating_waits_capture >= 0 and not art.sprite("capture", _animating_waits_capture).is_empty():
		print("capture: volley %d arrived %d ms after its event" % [_animating_waits_capture, Time.get_ticks_msec() - _capture_asked_at])
		_animating_waits_capture = -1
	if smoke_hold:
		_place_world()
		_marks.queue_redraw()
		_beams.queue_redraw()
		return
	if not _animating.is_empty() and _animating_waits_capture < 0:
		_animating.t += delta
		if _animating.t >= _animating.dur:
			_finish_animation()
	for effect: Dictionary in _effects:
		if effect.get("waiting", false):
			if art.sprite("capture", int(effect.entry)).is_empty():
				continue
			effect.waiting = false
		effect.t += delta
		if String(effect.kind) == "capture" and effect.t >= effect.life:
			art.forget("capture", int(effect.entry))
	_effects = _effects.filter(func(effect: Dictionary) -> bool: return effect.t < effect.life)
	_place_world()
	_layout()
	_update_hover()
	if _toast_time > 0.0:
		_toast_time -= delta
		if _toast_time <= 0.0:
			_toast.hide()
	_marks.queue_redraw()
	_beams.queue_redraw()


# ---- Camera ----

func _place_world() -> void:
	_world.scale = Vector2(_zoom, _zoom)
	_world.position = size / 2.0 - _cam * _zoom
	_backdrop.queue_redraw()


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
	if tex == null:
		_backdrop.draw_rect(Rect2(top_left, bottom_right - top_left), Color(0.02, 0.03, 0.06))
	else:
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


func _draw_marks() -> void:
	if state.is_empty():
		return
	var acting := int(state.get("ship", -1))
	var acting_ship := _ship(acting)
	var width := 1.0 / _zoom
	# The acting ship's legal squares, while it is the player's to move.
	if _my_turn() and _alive(acting_ship):
		for code in state.get("legal", []):
			var sx := int(code) / 1000
			var sy := int(code) % 1000
			_marks.draw_circle((Vector2(sx, sy) + Vector2(0.5, 0.5)) * SQUARE, 1.6 * maxf(1.0, 1.0 / _zoom), Color(AMBER, 0.45))
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
			_draw_crescent(_ships[index].shown, _radius(index) + 9.0 * maxf(1.0, 1.2 / _zoom), ship, width)
	# Previews: a move to the hovered square, or fire at the hovered enemy.
	if _my_turn() and _alive(acting_ship) and _ships.has(acting):
		var from: Vector2 = _ships[acting].shown
		var hovered := _ship(_hover_ship)
		if _alive(hovered) and not _mine(hovered):
			var at: Vector2 = _ships[_hover_ship].shown
			_marks.draw_dashed_line(from, at, Color(FIRE, 0.8), 2.0 * width, 8.0 * width)
			_marks.draw_arc(at, _radius(_hover_ship) + 4.0, 0.0, TAU, 48, FIRE, 2.0 * width)
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
	# Missiles and fighters in flight (WP3.9c animates them): a marker in the owner's colour with the count.
	var font := get_theme_default_font()
	for missile: Dictionary in state.get("missiles", []):
		var at := Vector2(float(missile.get("x", 0)), float(missile.get("y", 0)))
		var owner_color := MONSTER_COLOR
		for ship: Dictionary in _ships_list():
			if int(ship.get("owner", -1)) == int(missile.get("owner", -2)):
				owner_color = _color(ship)
				break
		var heading := -float(int(missile.get("facing", 0))) * PI / 8.0
		var tip := at + Vector2(cos(heading), sin(heading)) * 7.0 * width * 1.5
		_marks.draw_line(at, tip, owner_color, 2.0 * width)
		_marks.draw_circle(at, 3.0 * width * 1.5, owner_color)
		var count := int(missile.get("count", 1))
		if count > 1:
			_marks.draw_set_transform(at + Vector2(6, -6) * width, 0.0, Vector2.ONE / _zoom)
			_marks.draw_string(font, Vector2.ZERO, str(count), HORIZONTAL_ALIGNMENT_LEFT, -1, 12, owner_color)
			_marks.draw_set_transform(Vector2.ZERO, 0.0, Vector2.ONE)
	_draw_effects(width)
	# The order ring.
	if _menu_ship >= 0 and _ships.has(_menu_ship):
		_draw_menu(_ships[_menu_ship].shown, width)


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


func _square_center(square: Vector2i, ship: Dictionary) -> Vector2:
	var offset := _center(ship) - Vector2(float(ship.x), float(ship.y)) * SQUARE
	return Vector2(square) * SQUARE + offset


func _square_legal(square: Vector2i) -> bool:
	if square.x < 0:
		return false
	return _legal.has(square.x * 1000 + square.y)


# ---- Input ----

func _gui_input(event: InputEvent) -> void:
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
	# Any other ship: its scan (read-only, so also while the enemy moves).
	if _hover_ship >= 0 and _hover_ship != acting and _alive(_ship(_hover_ship)):
		_scan_mode = false
		_show_scan(_hover_ship)
		return
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
			_act({"action": "auto"})
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
	if primary:
		b.add_theme_color_override("font_color", b.get_theme_color("font_pressed_color", "Button"))
	b.pressed.connect(pressed)
	return b


## The HUD docked to the screen's edges as the galaxy's bars are (owner, 2026-11-22): the
## initiative bar a pocket on the top edge (scrolls), Show Health and the move speed in a pocket
## under it; the acting ship and its weapons a pocket on the bottom edge with Wait / Done /
## End turn / Auto in a pocket docked on it; the round box in the bottom corner on the player's
## side (attacking: right; defending: left). _layout places them every frame.
func _build_hud() -> void:
	# The round box: a pane docked in the player's bottom corner (the galaxy's panes: pane_gap).
	_top_left = _panel()
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

	# Initiative: the top edge, scrolled by its arrows, the wheel or a drag.
	_timeline = _panel(&"Pocket")
	add_child(_timeline)
	var trow := HBoxContainer.new()
	trow.add_theme_constant_override("separation", 4)
	_timeline.add_child(trow)
	var left: Button = preload("arrow_button.gd").new()
	left.points_left = true
	left.focus_mode = Control.FOCUS_NONE
	left.tooltip_text = "Earlier in the order"
	left.pressed.connect(func(): _tl_scroll.scroll_horizontal -= 200)
	trow.add_child(left)
	_tl_scroll = ScrollContainer.new()
	_tl_scroll.vertical_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_tl_scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_SHOW_NEVER
	_tl_scroll.gui_input.connect(_on_timeline_input)
	trow.add_child(_tl_scroll)
	_timeline_row = HBoxContainer.new()
	_timeline_row.add_theme_constant_override("separation", 6)
	_tl_scroll.add_child(_timeline_row)
	var right: Button = preload("arrow_button.gd").new()
	right.points_left = false
	right.focus_mode = Control.FOCUS_NONE
	right.tooltip_text = "Later in the order"
	right.pressed.connect(func(): _tl_scroll.scroll_horizontal += 200)
	trow.add_child(right)

	# Under it: Show Health and the move speed (bare buttons hanging under the bar: a second
	# pocket's frame would reach up over it).
	_top_right = _panel()
	_top_right.add_theme_stylebox_override("panel", StyleBoxEmpty.new())
	_top_right.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_top_right)
	var tr := HBoxContainer.new()
	tr.add_theme_constant_override("separation", 6)
	_top_right.add_child(tr)
	_health_toggle = _flat_button("Show Health", "Alt", func(): pass)
	_health_toggle.theme_type_variation = &"Toggle"
	_health_toggle.toggle_mode = true
	_health_toggle.button_pressed = _always_health
	_health_toggle.tooltip_text = "Shields and hull on every ship (hold Alt to see them for a moment)"
	Themes.glowing(_health_toggle)
	_health_toggle.toggled.connect(func(on: bool):
		_always_health = on
		_save_setting("always_show_health", on)
		for index in _ships.keys():
			_apply_shield(index, _ship(index)))
	tr.add_child(_health_toggle)
	_move_speed = clampi(int(_load_setting("move_speed", 1)), 0, MOVE_SPEEDS.size() - 1)
	_speed_button = _flat_button(String(MOVE_SPEEDS[_move_speed][0]), "", func():
		_move_speed = (_move_speed + 1) % MOVE_SPEEDS.size()
		_save_setting("move_speed", _move_speed)
		_speed_button.text = String(MOVE_SPEEDS[_move_speed][0]))
	_speed_button.tooltip_text = "How fast ships move and turn (click to change; weapons keep the original's pace)"
	_speed_button.custom_minimum_size = Vector2(90, 0)
	tr.add_child(_speed_button)

	# The bottom edge: one pocket, Wait / Done / End turn / Auto framed above the acting ship and
	# its weapons (two stacked pockets' frames would overlap).
	_strip = _panel(&"BottomPocket")
	add_child(_strip)
	var stack := VBoxContainer.new()
	stack.add_theme_constant_override("separation", 6)
	_strip.add_child(stack)
	_actions = HBoxContainer.new()
	_actions.add_theme_constant_override("separation", 6)
	_actions.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	stack.add_child(_actions)
	_bottom = _actions
	_wait_button = _flat_button("Wait", "W", func(): _act({"action": "wait"}))
	_actions.add_child(_wait_button)
	_done_button = _flat_button("Done", "D", func(): _act({"action": "done"}))
	_actions.add_child(_done_button)
	_end_button = _flat_button("End turn", "T", func(): _act({"action": "end_turn"}), true)
	_actions.add_child(_end_button)
	_auto_button = _flat_button("Auto", "A", func(): _act({"action": "auto"}))
	_auto_button.tooltip_text = "The computer fights the rest of this battle for you (key: A)"
	_auto_button.theme_type_variation = &"Toggle"
	_auto_button.toggle_mode = true
	_auto_button.button_pressed = false
	_actions.add_child(_auto_button)
	for b: Button in [_wait_button, _done_button, _end_button, _auto_button]:
		b.custom_minimum_size = Vector2(96, 0)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	row.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	stack.add_child(row)
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
	_strip_weapons = HBoxContainer.new()
	_strip_weapons.add_theme_constant_override("separation", 4)
	row.add_child(_strip_weapons)
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
	for pocket: PanelContainer in [_timeline, _strip]:
		var wanted: StringName = &"Pocket" if pocket == _timeline else &"BottomPocket"
		var bare := get_theme_stylebox("panel", wanted) is StyleBoxEmpty
		var variation: StringName = &"Pane" if bare else wanted
		if pocket.theme_type_variation != variation:
			pocket.theme_type_variation = variation
	var edge := _edge()
	# Initiative: as wide as its ships, at most the screen less the corners.
	var arrows := 2.0 * 60.0
	var pane_w := 0.0
	var box := _timeline.get_theme_stylebox("panel")
	if box != null:
		pane_w = box.get_minimum_size().x
	var room := size.x - 2.0 * (edge + 40.0) - arrows - pane_w
	var want := _timeline_row.get_combined_minimum_size().x
	_tl_scroll.custom_minimum_size = Vector2(clampf(want, 60.0, maxf(room, 60.0)), _timeline_row.get_combined_minimum_size().y)
	_timeline.reset_size()
	_timeline.position = Vector2(roundf((size.x - _timeline.size.x) / 2.0), edge)
	_top_right.reset_size()
	_top_right.position = Vector2(roundf((size.x - _top_right.size.x) / 2.0), _timeline.position.y + _timeline.size.y)
	# The bottom: one pocket on the edge.
	_strip.reset_size()
	_strip.position = Vector2(roundf((size.x - _strip.size.x) / 2.0), size.y - edge - _strip.size.y)
	# The round box: a pane in the player's corner, inside the frame by the theme's pane gap.
	_top_left.reset_size()
	var inset := float(get_theme_constant("inset", "Screen"))
	var gap := inset + float(maxi(get_theme_constant("pane_gap", "Screen"), 6))
	var attacking := int(state.get("player", -1)) == int(state.get("attacker", -2))
	var x := size.x - gap - _top_left.size.x if attacking else gap
	_top_left.position = Vector2(x, size.y - gap - _top_left.size.y)
	_toast.position = Vector2(roundf((size.x - _toast.size.x) / 2.0), _top_right.position.y + _top_right.size.y + 12.0)
	_frame_overlay.queue_redraw()


func _draw_frame() -> void:
	if frame_style == null:
		return
	_frame_overlay.draw_style_box(frame_style, Rect2(Vector2.ZERO, _frame_overlay.size))
	if tee.is_empty():
		return
	var tex: Texture2D = tee.texture
	var scale: float = tee.scale
	var c: float = tee.bar_center
	var tsize := Vector2(tex.get_size()) * scale
	var anchor: Vector2 = tee.anchor * scale
	for pocket: Control in [_timeline, _strip]:
		if not pocket.visible:
			continue
		var pbox: StyleBox = pocket.get_theme_stylebox("panel")
		var grow := Vector2(9, 9)
		if pbox and "grow" in pbox:
			grow = Vector2(pbox.grow[0], pbox.grow[2])
		var r := pocket.get_rect()
		var top := pocket == _timeline
		for x in [r.position.x - grow.x + c, r.end.x + grow.y - c]:
			if top: # the joint upside down, its top flush with the screen's top (frontend.gd)
				_frame_overlay.draw_set_transform(Vector2(0, tsize.y), 0.0, Vector2(1, -1))
				_frame_overlay.draw_texture_rect(tex, Rect2(Vector2(x - anchor.x, 0), tsize), false)
				_frame_overlay.draw_set_transform(Vector2.ZERO)
			else:
				_frame_overlay.draw_texture_rect(tex, Rect2(Vector2(x - anchor.x, _frame_overlay.size.y - c - anchor.y), tsize), false)


## The initiative bar scrolls with the wheel and a drag (and its arrows).
func _on_timeline_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button != null:
		if button.pressed and button.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_LEFT]:
			_tl_scroll.scroll_horizontal -= 80
			_tl_scroll.accept_event()
		elif button.pressed and button.button_index in [MOUSE_BUTTON_WHEEL_DOWN, MOUSE_BUTTON_WHEEL_RIGHT]:
			_tl_scroll.scroll_horizontal += 80
			_tl_scroll.accept_event()
		elif button.button_index == MOUSE_BUTTON_LEFT:
			_tl_dragging = button.pressed
			_tl_scroll.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion != null and _tl_dragging:
		_tl_scroll.scroll_horizontal -= int(motion.relative.x)
		_tl_scroll.accept_event()


## Straight bars (the timeline, the strip, the card): armour on top, shrinking to the centre;
## below it structure on the left and systems on the right, each empty at the centre.
func _bars_control(min_size: Vector2) -> Control:
	var c := Control.new()
	c.custom_minimum_size = min_size
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	c.set_meta("ship", {})
	c.draw.connect(func():
		var ship: Dictionary = c.get_meta("ship")
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
	_auto_button.disabled = not my_turn
	_auto_button.set_pressed_no_signal(int(state.get("auto", 0)) != 0)
	Themes.glow(_auto_button)
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
		child.queue_free()
	_weapon_chips.clear()
	var in_arc := _slots_against(_hover_ship)
	for weapon: Dictionary in acting.get("weapons", []):
		var slot := int(weapon.get("slot", 0))
		var count := int(weapon.get("count", 0))
		var active := int(weapon.get("active", 1))
		var ready := bool(weapon.get("ready", false))
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
		chip.gui_input.connect(func(event: InputEvent):
			var click := event as InputEventMouseButton
			if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT and count > 0:
				_act({"action": "toggle_weapon", "slot": slot})
				accept_event())
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


func _refresh_timeline() -> void:
	for child in _timeline_row.get_children():
		_timeline_row.remove_child(child)
		child.queue_free()
	var order: Array = []
	var acting := int(state.get("ship", -1))
	if acting >= 0:
		order.append(acting)
	for index in state.get("order", []):
		if int(index) != acting:
			order.append(int(index))
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
		var chip := _chip(order[n], order[n] == acting)
		if bracket != null and n < run:
			bracket.add_child(chip)
		else:
			_timeline_row.add_child(chip)
	if acting != _tl_acting:
		_tl_acting = acting
		_tl_scroll.scroll_horizontal = 0 # the acting ship in view


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
	return box


func _place_card() -> void:
	var ship := _ship(_hover_ship)
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
	var gap := inset + float(maxi(get_theme_constant("pane_gap", "Screen"), 6))
	var attacking := int(state.get("player", -1)) == int(state.get("attacker", -2))
	var x := gap if attacking else size.x - gap - _card.size.x
	_card.position = Vector2(x, size.y - gap - _card.size.y)


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


func _build_scan() -> void:
	_scan_panel = _panel()
	_scan_panel.hide()
	add_child(_scan_panel)
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	v.custom_minimum_size = Vector2(330, 0)
	_scan_panel.add_child(v)
	var head := HBoxContainer.new()
	v.add_child(head)
	_scan_title = _label("", 0, AMBER)
	_scan_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	head.add_child(_scan_title)
	head.add_child(_flat_button("Close", "Esc", _hide_scan))
	_scan_sub = _label("", 0, DIM)
	v.add_child(_scan_sub)
	_scan_bars = _bars_control(Vector2(330, 12))
	v.add_child(_scan_bars)
	_scan_body = VBoxContainer.new()
	_scan_body.add_theme_constant_override("separation", 3)
	v.add_child(_scan_body)
	_scan_panel.set_anchors_and_offsets_preset(Control.PRESET_CENTER_RIGHT, Control.PRESET_MODE_MINSIZE, 16)


func _hide_scan() -> void:
	_scan_ship = -1
	if _scan_panel != null:
		_scan_panel.hide()


## The original's scan of `index`: parts, combat values, crew and officer, weapons, specials, and
## the pools as the card shows them. Kept current while open (_refresh_hud).
func _show_scan(index: int) -> void:
	var ship := _ship(index)
	if not _alive(ship):
		_hide_scan()
		return
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
	if scan.has("hull"):
		sub.append(String(scan.hull))
	if scan.has("crew"):
		sub.append(String(scan.crew))
	_scan_sub.text = "   ·   ".join(sub)
	_set_bars(_scan_bars, ship)
	for child in _scan_body.get_children():
		child.queue_free()
	var add := func(text: String, color := TEXT):
		var l := _label(text, 0, color)
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.custom_minimum_size = Vector2(330, 0)
		_scan_body.add_child(l)
	var section := func(text: String):
		var l := _label(text)
		l.theme_type_variation = &"Section"
		_scan_body.add_child(l)
	if scan.has("officer"):
		add.call("Officer: " + String(scan.officer))
	var shield: Dictionary = ship.get("shield", {})
	var armor: Dictionary = ship.get("armor", {})
	var structure: Dictionary = ship.get("structure", {})
	section.call("SYSTEMS")
	var arcs: Array = shield.get("arcs", [0, 0, 0, 0])
	if int(shield.get("max", 0)) > 0:
		add.call("Shields: %s   front %d · right %d · rear %d · left %d   of %d" % [String(scan.get("shield", "")), int(arcs[0]), int(arcs[1]),
			int(arcs[2]), int(arcs[3]), int(shield.get("max", 0))])
	else:
		add.call("Shields: none")
	add.call("Armour: %s   %d / %d" % [String(scan.get("armor", "")), int(armor.get("now", 0)), int(armor.get("max", 0))])
	add.call("Structure %d / %d" % [int(structure.get("now", 0)), int(structure.get("max", 0))])
	add.call("Computer: " + String(scan.get("computer", "")))
	if scan.has("drive"):
		add.call("Drive: %s   combat speed %d" % [String(scan.drive), int(scan.get("speed", 0))])
	add.call("%s %d    %s %d    Missile evasion %d" % [String(scan.get("attack_label", "Beam attack")), int(scan.get("attack", 0)),
		String(scan.get("defense_label", "Beam defence")), int(scan.get("defense", 0)), int(scan.get("evade", 0))])
	if int(ship.get("marines", 0)) > 0:
		add.call("Marines %d" % int(ship.get("marines", 0)))
	var weapons: Array = ship.get("weapons", [])
	if not weapons.is_empty():
		section.call("WEAPONS")
		for weapon: Dictionary in weapons:
			var count := int(weapon.get("count", 0))
			var line := "%d × %s   %s" % [count, String(weapon.get("name", "")), _arc_name(int(weapon.get("arc", 0)))]
			var ammo := int(weapon.get("ammo", 0))
			if ammo > 0 and ammo < 99:
				line += "   %d left" % ammo
			add.call(line + ("   destroyed" if count == 0 else ""), SYSTEMS_COLOR if count == 0 else TEXT)
	var specials: Array = scan.get("specials", [])
	if not specials.is_empty():
		section.call("SPECIALS")
		for special: Dictionary in specials:
			var damaged := bool(special.get("damaged", false))
			add.call(String(special.get("name", "")) + ("   damaged" if damaged else ""), SYSTEMS_COLOR if damaged else TEXT)
	_scan_panel.show()
	_scan_panel.reset_size()
	_scan_panel.set_anchors_and_offsets_preset(Control.PRESET_CENTER_RIGHT, Control.PRESET_MODE_MINSIZE, 16)


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
			var size := int(old.get("size", 0))
			var entry := 3 if size <= 1 else (4 if size <= 3 else 5)
			_effects.append({"kind": "explosion", "t": 0.0, "life": 1.1, "at": at, "entry": entry})
			continue
		var hp_old := float((old.get("armor", {}) as Dictionary).get("now", 0)) + float((old.get("structure", {}) as Dictionary).get("now", 0))
		var hp_new := float((ship.get("armor", {}) as Dictionary).get("now", 0)) + float((ship.get("structure", {}) as Dictionary).get("now", 0))
		if hp_new < hp_old and not _captures_seen:
			_effects.append({"kind": "damage", "t": 0.0, "life": 1.3, "ship": index, "at": at, "text": "-%d" % int(hp_old - hp_new)})
		var arcs_old: Array = (old.get("shield", {}) as Dictionary).get("arcs", [])
		var arcs_new: Array = (ship.get("shield", {}) as Dictionary).get("arcs", [])
		for a in mini(arcs_old.size(), arcs_new.size()):
			if float(arcs_new[a]) < float(arcs_old[a]) and not _captures_seen:
				_effects.append({"kind": "shield", "t": 0.0, "life": 0.45, "ship": index, "arc": a})


func _effect_point(ship_index: int, fallback: Vector2) -> Vector2:
	if _ships.has(ship_index):
		return _ships[ship_index].shown
	return fallback


func _draw_effects(width: float) -> void:
	var font := get_theme_default_font()
	for effect: Dictionary in _effects:
		var t: float = effect.t
		if t < 0.0:
			continue
		var life: float = effect.life
		var k := clampf(t / life, 0.0, 1.0)
		match String(effect.kind):
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
		"tick": -1, "strands": [], "at": Vector2(float(target_center[0]), float(target_center[1]))}
	_effects.append(effect)
	_request_volley_art(effect)
	return float(ticks.size()) * BeamVolley.TICK


## Asks for the volley's flashes and flares in their colours now, so they are here (or on their
## way) by the tick that draws them: each is a new export the first time a weapon fires.
func _request_volley_art(effect: Dictionary) -> void:
	for ops: Array in effect.ticks:
		for op: Dictionary in ops:
			var slot := int(op.get("slot", 0))
			match String(op.op):
				"burst":
					art.sprite(_slot_sprite(_first_ramp(effect, slot), slot), 1 + int(op.variant) * 32 + slot * 16 + int(op.dir))
				"explosion":
					art.sprite(_slot_sprite(_first_ramp(effect, slot), slot), 65 + slot)
				"ball":
					art.sprite(_slot_sprite(_first_ramp(effect, 0), 0), 88)


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
	for effect: Dictionary in _effects:
		if String(effect.kind) != "volley" or effect.t < 0.0:
			continue
		var ticks: Array = effect.ticks
		var tick := mini(int(effect.t / BeamVolley.TICK), ticks.size() - 1)
		if tick != int(effect.tick):
			# Each tick passed plays its sounds once (a slow frame may pass several).
			for k in range(int(effect.tick) + 1, tick + 1):
				_volley_sounds(ticks[k])
			effect.tick = tick
			effect.strands = _volley_strands(effect, ticks[tick])
		for st: Dictionary in effect.strands:
			(st.beam as BeamDraw).draw(_beams, st.ramp, int(st.base))


## A volley's muzzle flashes, hit flares and the Mauler's ball on this tick (over the strands).
func _draw_volley_sprites(effect: Dictionary) -> void:
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
				if not s.is_empty():
					var count: int = s.frames.size()
					var frame := int(op.frame) % maxi(count - 1, 1)
					art.draw(_marks, name, 65 + slot, frame, Vector2(op.at) - Vector2(8, 8), 1.0)
			"ball":
				if not ramps.has(0):
					ramps[0] = _first_ramp(effect, 0)
				_draw_centered(_slot_sprite(ramps[0], 0), 88, 0, op.at)


## The colours of the first segment in `slot` anywhere in the volley (a burst on frame 0 comes
## before its weapon's first segment).
func _first_ramp(effect: Dictionary, slot: int) -> Array:
	for ops: Array in effect.ticks:
		for op: Dictionary in ops:
			if String(op.op) == "seg" and int(op.slot) == slot:
				return _volley_ramp(op, effect.fragments)
	return _volley_ramp({"params": {"frag": 0}}, effect.fragments)


## ERIC::Draw_Centered: the frame's 1x box centred on `at`.
func _draw_centered(name: String, entry: int, frame: int, at: Vector2i) -> void:
	var s: Dictionary = art.sprite(name, entry)
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
