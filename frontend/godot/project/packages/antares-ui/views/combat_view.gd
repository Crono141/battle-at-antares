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

var _world: Node2D
var _backdrop: Node2D
var _ship_layer: Node2D
var _marks: Node2D
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
var _actions: VBoxContainer
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
	_marks = Node2D.new()
	_marks.draw.connect(_draw_marks)
	_world.add_child(_marks)
	_build_hud()
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


func close() -> void:
	if not visible:
		return
	hide()
	_opened = false
	_request_id = -1
	_close_menu()
	for entry in _ships.values():
		entry.sprite.queue_free()
	_ships.clear()
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
		e.shown = e.shown.lerp(goal, minf(1.0, delta * 18.0))
		var goal_angle: float = e.get("goal_angle", e.angle)
		e.angle = lerp_angle(e.angle, goal_angle, minf(1.0, delta * 14.0))
		e.sprite.position = e.shown
		e.sprite.rotation = e.angle
	if _cam_goal != Vector2.INF:
		_cam = _cam.lerp(_cam_goal, minf(1.0, delta * 6.0))
		if _cam.distance_to(_cam_goal) < 1.0:
			_cam_goal = Vector2.INF
	_place_world()
	_update_hover()
	if _toast_time > 0.0:
		_toast_time -= delta
		if _toast_time <= 0.0:
			_toast.hide()
	_marks.queue_redraw()


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
			return "Hover a ship to see its status"
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
	if not _my_turn():
		return
	var acting := int(state.get("ship", -1))
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


# ---- HUD: mockup B4's own look (dark glass, thin steel edges, amber for "on"), not the theme's
# buttons (owner, 2026-11-15: the Chrome button art on the weapons; "not nearly as clean as B4"). ----

const GLASS_BG := Color(0.035, 0.055, 0.10, 0.88)
const GLASS_EDGE := Color(0.29, 0.35, 0.45)
const CHIP_BG := Color(0.055, 0.086, 0.15, 0.95)
const CHIP_EDGE := Color(0.24, 0.29, 0.38)
const TEXT := Color(0.85, 0.88, 0.93)
const DIM := Color(0.58, 0.64, 0.72)
const ARC_NAMES := {1: "front", 2: "front wide", 3: "front wide", 4: "rear wide", 8: "rear", 12: "rear wide", 15: "all round", 16: "all round"}


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


func _panel() -> PanelContainer:
	var p := PanelContainer.new()
	p.mouse_filter = Control.MOUSE_FILTER_STOP
	p.add_theme_stylebox_override("panel", _box(GLASS_BG, GLASS_EDGE))
	return p


func _label(text: String, font_size := 14, color := TEXT) -> Label:
	var l := Label.new()
	l.text = text
	l.add_theme_font_size_override("font_size", font_size)
	l.add_theme_color_override("font_color", color)
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	return l


## A flat button in the B4 look: its key after the name, amber when `primary` (End turn).
func _flat_button(text: String, key: String, pressed: Callable, primary := false) -> Button:
	var b := Button.new()
	b.text = text + ("    " + key if key != "" else "")
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 14)
	var bg := Color(0.95, 0.70, 0.24) if primary else Color(0.08, 0.12, 0.19, 0.92)
	var edge := AMBER if primary else Color(0.29, 0.35, 0.45)
	var normal := _box(bg, edge, 4, 12.0)
	var hover := _box(bg.lightened(0.08), AMBER, 4, 12.0)
	var disabled := _box(Color(bg, bg.a * 0.45), Color(edge, 0.4), 4, 12.0)
	b.add_theme_stylebox_override("normal", normal)
	b.add_theme_stylebox_override("hover", hover)
	b.add_theme_stylebox_override("pressed", hover)
	b.add_theme_stylebox_override("hover_pressed", hover)
	b.add_theme_stylebox_override("disabled", disabled)
	b.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	var ink := Color(0.1, 0.07, 0.02) if primary else TEXT
	for state_name in ["font_color", "font_hover_color", "font_pressed_color", "font_hover_pressed_color", "font_focus_color"]:
		b.add_theme_color_override(state_name, ink)
	b.add_theme_color_override("font_disabled_color", Color(ink, 0.4))
	b.pressed.connect(pressed)
	return b


func _build_hud() -> void:
	_top_left = _panel()
	add_child(_top_left)
	var tl := VBoxContainer.new()
	tl.add_theme_constant_override("separation", 4)
	tl.custom_minimum_size = Vector2(300, 0)
	_top_left.add_child(tl)
	_round_label = _label("", 17)
	tl.add_child(_round_label)
	_strength_label = _label("", 12, DIM)
	tl.add_child(_strength_label)
	var bar := HBoxContainer.new()
	bar.add_theme_constant_override("separation", 0)
	bar.custom_minimum_size = Vector2(0, 6)
	tl.add_child(bar)
	_strength_mine = ColorRect.new()
	_strength_mine.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(_strength_mine)
	_strength_theirs = ColorRect.new()
	_strength_theirs.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(_strength_theirs)

	var tr := HBoxContainer.new()
	tr.add_theme_constant_override("separation", 6)
	tr.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	add_child(tr)
	_health_toggle = _flat_button("Always show health", "Alt", func(): pass)
	_health_toggle.toggle_mode = true
	_health_toggle.button_pressed = _always_health
	_health_toggle.tooltip_text = "Shields and hull on every ship (hold Alt to see them for a moment)"
	_health_toggle.add_theme_stylebox_override("pressed", _box(Color(0.95, 0.70, 0.24, 0.16), AMBER, 4, 12.0))
	_health_toggle.add_theme_stylebox_override("hover_pressed", _box(Color(0.95, 0.70, 0.24, 0.22), AMBER, 4, 12.0))
	_health_toggle.add_theme_color_override("font_pressed_color", AMBER)
	_health_toggle.add_theme_color_override("font_hover_pressed_color", AMBER)
	_health_toggle.toggled.connect(func(on: bool):
		_always_health = on
		_save_setting("always_show_health", on)
		for index in _ships.keys():
			_apply_shield(index, _ship(index)))
	tr.add_child(_health_toggle)
	_auto_button = _flat_button("Auto", "A", func(): _act({"action": "auto"}))
	_auto_button.tooltip_text = "The computer fights the rest of this battle for you"
	tr.add_child(_auto_button)

	_timeline = _panel()
	_timeline.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_timeline.grow_vertical = Control.GROW_DIRECTION_BEGIN
	add_child(_timeline)
	_timeline_row = HBoxContainer.new()
	_timeline_row.add_theme_constant_override("separation", 8)
	_timeline.add_child(_timeline_row)

	_strip = _panel()
	_strip.grow_vertical = Control.GROW_DIRECTION_BEGIN
	add_child(_strip)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	_strip.add_child(row)
	var who := VBoxContainer.new()
	who.add_theme_constant_override("separation", 3)
	who.custom_minimum_size = Vector2(170, 0)
	row.add_child(who)
	_strip_name = _label("", 17, AMBER)
	who.add_child(_strip_name)
	_strip_info = _label("", 12, DIM)
	who.add_child(_strip_info)
	_strip_bars = _bars_control(Vector2(170, 10))
	who.add_child(_strip_bars)
	var sep := ColorRect.new()
	sep.color = Color(GLASS_EDGE, 0.6)
	sep.custom_minimum_size = Vector2(1, 0)
	row.add_child(sep)
	_strip_weapons = HBoxContainer.new()
	_strip_weapons.add_theme_constant_override("separation", 6)
	row.add_child(_strip_weapons)

	_actions = VBoxContainer.new()
	_actions.add_theme_constant_override("separation", 6)
	_actions.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	_actions.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_actions.custom_minimum_size = Vector2(170, 0)
	add_child(_actions)
	_wait_button = _flat_button("Wait", "W", func(): _act({"action": "wait"}))
	_actions.add_child(_wait_button)
	_done_button = _flat_button("Done", "D", func(): _act({"action": "done"}))
	_actions.add_child(_done_button)
	_end_button = _flat_button("End turn", "T", func(): _act({"action": "end_turn"}), true)
	_actions.add_child(_end_button)

	_card = _panel()
	_card.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_card.hide()
	add_child(_card)
	var cv := VBoxContainer.new()
	cv.add_theme_constant_override("separation", 5)
	cv.custom_minimum_size = Vector2(270, 0)
	_card.add_child(cv)
	_card_title = _label("", 16)
	cv.add_child(_card_title)
	_card_bars = _bars_control(Vector2(270, 12))
	cv.add_child(_card_bars)
	_card_body = _label("", 13)
	cv.add_child(_card_body)
	_card_fire = _label("", 13, FIRE)
	cv.add_child(_card_fire)

	_toast = _label("", 15, Color(1.0, 0.82, 0.55))
	_toast.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	_toast.add_theme_constant_override("outline_size", 6)
	_toast.grow_horizontal = Control.GROW_DIRECTION_BOTH
	_toast.hide()
	add_child(_toast)
	# Anchors last, once each container knows its size (code-built controls: the offsets must be set too).
	_top_left.set_anchors_and_offsets_preset(Control.PRESET_TOP_LEFT, Control.PRESET_MODE_MINSIZE, 16)
	tr.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT, Control.PRESET_MODE_MINSIZE, 16)
	_timeline.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM, Control.PRESET_MODE_MINSIZE, 16)
	_strip.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_LEFT, Control.PRESET_MODE_MINSIZE, 16)
	_strip.offset_top -= 104.0 # above the timeline row
	_strip.offset_bottom -= 104.0
	_actions.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT, Control.PRESET_MODE_MINSIZE, 16)
	_toast.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP, Control.PRESET_MODE_MINSIZE, 70)


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
	_round_label.text = ("Battle at " + star if star != "" else "Space combat") + "      Round %d" % (int(state.get("round", 0)) + 1)
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
	_auto_button.text = "Auto: on" if int(state.get("auto", 0)) != 0 else "Auto    A"
	_refresh_strip()
	_refresh_timeline()


func _arc_name(arc: int) -> String:
	return ARC_NAMES.get(arc, "arc %d" % arc)


## The acting ship's weapons as chips: lit while they can still fire this turn, dim once fired or
## switched off; with an enemy under the pointer, the ones that can hit it light up in fire colour
## (the legacy weapon list's lit lines). A click cycles on, fire once, off.
func _refresh_strip() -> void:
	var acting := _ship(int(state.get("ship", -1)))
	if not _alive(acting) or not _mine(acting):
		_strip.hide()
		return
	_strip.show()
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
		var chip := PanelContainer.new()
		chip.mouse_filter = Control.MOUSE_FILTER_STOP
		chip.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
		chip.tooltip_text = "Click: on, fire once, off"
		var targeted := _hover_ship >= 0 and not _mine(_ship(_hover_ship))
		var lit := targeted and in_arc.has(slot)
		chip.add_theme_stylebox_override("panel", _box(Color(FIRE, 0.14) if lit else CHIP_BG, FIRE if lit else CHIP_EDGE, 4, 8.0))
		var v := VBoxContainer.new()
		v.add_theme_constant_override("separation", 1)
		chip.add_child(v)
		v.add_child(_label("%d × %s" % [count, String(weapon.get("name", ""))], 13, TEXT))
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
		v.add_child(_label(line, 11, FIRE if lit else (SYSTEMS_COLOR if status == "destroyed" else DIM)))
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
		child.queue_free()
	var round_box := VBoxContainer.new()
	round_box.add_theme_constant_override("separation", 0)
	round_box.add_child(_label("ROUND", 11, DIM))
	var round_number := _label(str(int(state.get("round", 0)) + 1), 26)
	round_number.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	round_box.add_child(round_number)
	_timeline_row.add_child(round_box)
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
		frame.add_theme_stylebox_override("panel", _box(Color(AMBER, 0.08), AMBER, 6, 4.0, 2))
		bracket = HBoxContainer.new()
		bracket.add_theme_constant_override("separation", 6)
		frame.add_child(bracket)
		_timeline_row.add_child(frame)
	const MAX_CHIPS := 10
	for n in mini(order.size(), MAX_CHIPS):
		var chip := _chip(order[n], order[n] == acting)
		if bracket != null and n < run:
			bracket.add_child(chip)
		else:
			_timeline_row.add_child(chip)
	if order.size() > MAX_CHIPS:
		_timeline_row.add_child(_label("+%d" % (order.size() - MAX_CHIPS), 14, DIM))


func _chip(index: int, acting: bool) -> Control:
	var ship := _ship(index)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 3)
	var pic := PanelContainer.new()
	pic.add_theme_stylebox_override("panel", _box(CHIP_BG, AMBER if acting else Color(_color(ship), 0.8), 4, 2.0, 2 if acting else 1))
	pic.custom_minimum_size = Vector2(56, 46)
	pic.tooltip_text = String(ship.get("name", ""))
	var rect := TextureRect.new()
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.custom_minimum_size = Vector2(50, 40)
	var sprite_info: Dictionary = ship.get("sprite", {})
	rect.texture = art.texture(String(sprite_info.get("lbx", "")), int(sprite_info.get("entry", -1)), 0)
	pic.add_child(rect)
	box.add_child(pic)
	var bars := _bars_control(Vector2(56, 8))
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
		lines.append("Shields   front %d · right %d · rear %d · left %d   of %d" % [int(arcs[0]), int(arcs[1]), int(arcs[2]), int(arcs[3]), int(shield.get("max", 0))])
	else:
		lines.append("Shields   down")
	lines.append("Armour %d / %d     Structure %d / %d" % [int(armor.get("now", 0)), int(armor.get("max", 0)), int(structure.get("now", 0)), int(structure.get("max", 0))])
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
	var at := _to_screen(_ships[_hover_ship].shown) + Vector2(_radius(_hover_ship) * _zoom + 24.0, -_card.size.y / 2.0)
	if at.x + _card.size.x > size.x - 8.0:
		at.x = _to_screen(_ships[_hover_ship].shown).x - _radius(_hover_ship) * _zoom - 24.0 - _card.size.x
	at.y = clampf(at.y, 80.0, size.y - _card.size.y - 150.0)
	_card.position = at


func _show_toast(text: String, seconds := 3.0) -> void:
	_toast.text = text
	_toast.show()
	_toast_time = seconds


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
