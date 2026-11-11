extends Control
## The star system panel (WP3.3a): the legacy system box as a native panel over the galaxy. It is
## drawn from the `system` query with the box's own art (BUFFER0.LBX: the box, orbit rings, sun,
## rotating planets, fleet and gate icons) at the places the legacy box uses, in the box picture's
## pixels scaled to the panel. Hovering a planet or fleet shows the box's own text in Info; a click
## on one of the player's colonies opens it; a right click on a planet shows its colony
## information; drag the title to move the panel. Fleet clicks come with the fleet panel (WP3.3c).

signal command(dict: Dictionary)
signal closed

## The box picture's size in its own pixels.
const BOX_SIZE := Vector2(347.0, 273.0)
const PLANET_FPS := 8.0
## Where the box puts its fleet icons and gate icons (sys.cpp).
const FLEETS_AT := Vector2(0x12, 0xEC)
const FLEET_ROW_HEIGHT := 0x19
const GATES_AT := Vector2(0xE9, 0xF3)
## Places in the box picture (its pixels; sys.cpp): the title band, the wormhole line (right
## aligned at 0x149), the paragraph about an unknown star, the hover text (0x0D, 0x2F) and the
## picture's own CLOSE button.
const TITLE_RECT := Rect2(20, 10, 307, 26)
const WORMHOLE_RECT := Rect2(30, 212, 299, 14)
const DESCRIPTION_RECT := Rect2(38, 60, 271, 150)
const INFO_AT := Vector2(0x0D, 0x2F)
const CLOSE_RECT := Rect2(265, 239, 66, 18)

@onready var title: Label = $Title
@onready var wormhole: Label = $Wormhole
@onready var description: Label = $Description
@onready var info: PanelContainer = $Info
@onready var info_text: Label = $Info/Text

var art: Node # art.gd
var system := {} # the `system` query
var _time := 0.0
var _hover := "" # "planet:<id>" or "fleet:<node>"
var _potential := {} # the `potential_colony` answer shown in Info, until the pointer moves on
var _drag_from := Vector2.INF


func _ready() -> void:
	$Close.pressed.connect(close)
	info.hide()


## Shows a system (the `system` query's answer) at `screen_scale` screen pixels per box pixel.
func show_system(result: Dictionary, screen_scale: float) -> void:
	system = result
	size = BOX_SIZE * screen_scale
	_hover = ""
	_potential = {}
	info.hide()
	title.text = result.get("title", "")
	var hidden: bool = result.get("view", "full") != "full"
	description.visible = hidden and result.has("description")
	description.text = result.get("description", "")
	wormhole.visible = result.has("wormhole")
	wormhole.text = result.get("wormhole", {}).get("text", "")
	_apply_font_sizes(screen_scale)
	# Text and the clear Close button sit where the box picture has room for them (box pixels).
	for item: Array in [[title, TITLE_RECT], [wormhole, WORMHOLE_RECT], [description, DESCRIPTION_RECT], [$Close, CLOSE_RECT]]:
		var control: Control = item[0]
		var rect: Rect2 = item[1]
		control.position = rect.position * screen_scale
		control.size = rect.size * screen_scale
	info.position = INFO_AT * screen_scale
	show()
	queue_redraw()


func close() -> void:
	hide()
	system = {}
	command.emit({"verb": "close_system"}) # the saved system box record (galaxy_queries.cpp)
	closed.emit()


func _apply_font_sizes(screen_scale: float) -> void:
	title.label_settings.font_size = int(round(11.0 * screen_scale))
	for label: Label in [wormhole, description, info_text]:
		label.add_theme_font_size_override("font_size", int(round(6.5 * screen_scale)))


func _process(delta: float) -> void:
	if visible:
		_time += delta
		queue_redraw()


## Box pixels to panel pixels.
func _unit() -> float:
	return size.x / BOX_SIZE.x


func _draw() -> void:
	if system.is_empty() or art == null:
		return
	var u := _unit()
	if not art.draw(self, "BUFFER0.LBX", int(system.box_sprite), 0, Vector2.ZERO, u):
		draw_rect(Rect2(Vector2.ZERO, size), Color(0.05, 0.07, 0.12, 0.95))
	if system.get("view", "full") != "full":
		return
	var ring_at := Vector2(float(system.ring_x), float(system.ring_y))
	for planet: Dictionary in system.planets:
		art.draw(self, "BUFFER0.LBX", int(planet.ring_sprite), int(planet.ring_frame), ring_at * u, u)
	_draw_sun(u)
	for planet: Dictionary in system.planets:
		if int(planet.sprite) < 0:
			continue # asteroids: their belt is the ring
		var frames := _frame_count(int(planet.sprite))
		var frame := (int(planet.first_frame) + int(_time * PLANET_FPS)) % maxi(frames, 1)
		var at := Vector2(float(planet.box_x), float(planet.box_y)) * u
		art.draw(self, "BUFFER0.LBX", int(planet.sprite), frame, at, u)
		var colony: Dictionary = planet.get("colony", {})
		var center := at + Vector2.ONE * float(planet.dimension) * 0.5 * u
		var radius := float(planet.dimension) * 0.5 * u + 2.0 * u
		if not colony.is_empty():
			var color := _player_color(int(colony.color))
			draw_arc(center, radius, 0.0, TAU, 40, color, (1.0 if colony.outpost else 1.6) * u, true)
		if _hover == "planet:%d" % int(planet.planet):
			draw_arc(center, radius + 1.5 * u, 0.0, TAU, 40, Color(1, 1, 1, 0.8), 0.8 * u, true)
	for item in _fleet_rects(u):
		var fleet: Dictionary = item[0]
		art.draw(self, "BUFFER0.LBX", int(fleet.box_sprite), 0, item[1].position, u)
		if _hover == "fleet:%d" % int(fleet.node):
			draw_rect(item[1].grow(1.0 * u), Color(1, 1, 1, 0.8), false, 0.8 * u)
	_draw_gates(u)


func _draw_sun(u: float) -> void:
	var sun: Dictionary = art.sprite("BUFFER0.LBX", int(system.sun_sprite))
	if sun.is_empty():
		return
	# Width and height crossed, as the original places it (SYS::Draw_Sun_Seg).
	var at := Vector2((BOX_SIZE.x - sun.size.y) * 0.5, (BOX_SIZE.y - sun.size.x) * 0.5)
	art.draw(self, "BUFFER0.LBX", int(system.sun_sprite), 0, (at).floor() * u, u)


func _draw_gates(u: float) -> void:
	var gates: Dictionary = system.get("gates", {})
	var at := GATES_AT
	if gates.get("artemis", false):
		at.x -= 12.0
		art.draw(self, "BUFFER0.LBX", int(gates.artemis_sprite), 0, at * u, u)
	if gates.get("portal", false):
		at += Vector2(-12.0, 1.0)
		art.draw(self, "BUFFER0.LBX", int(gates.portal_sprite), 0, at * u, u)


## Each fleet icon and its rectangle in panel pixels, in a row as the box lays them out.
func _fleet_rects(u: float) -> Array:
	var out := []
	var x: float = FLEETS_AT.x
	for fleet: Dictionary in system.get("fleets", []):
		var sprite: Dictionary = art.sprite("BUFFER0.LBX", int(fleet.box_sprite))
		var icon_size: Vector2 = Vector2(16, 12) if sprite.is_empty() else sprite.size
		var y: float = FLEETS_AT.y + floorf((FLEET_ROW_HEIGHT - icon_size.y) / 2.0)
		out.append([fleet, Rect2(Vector2(x, y) * u, icon_size * u)])
		x += icon_size.x + 3.0
	return out


func _frame_count(entry: int) -> int:
	var sprite: Dictionary = art.sprite("BUFFER0.LBX", entry)
	return 1 if sprite.is_empty() else sprite.frames.size()


func _player_color(color_index: int) -> Color:
	var galaxy_script := preload("res://galaxy.gd")
	return galaxy_script.PLAYER_COLORS[color_index % 8] if color_index >= 0 else galaxy_script.NEUTRAL_COLOR


# ---- Pointer ----

## What is under a panel point: "planet:<id>", "fleet:<node>" or "".
func _hit(at: Vector2) -> String:
	if system.is_empty() or system.get("view", "full") != "full":
		return ""
	var u := _unit()
	for item in _fleet_rects(u):
		if item[1].grow(1.0 * u).has_point(at):
			return "fleet:%d" % int(item[0].node)
	for planet: Dictionary in system.planets:
		if int(planet.sprite) < 0:
			continue
		var center := (Vector2(float(planet.box_x), float(planet.box_y)) + Vector2.ONE * float(planet.dimension) * 0.5) * u
		if at.distance_to(center) <= float(planet.dimension) * 0.5 * u + 2.0 * u:
			return "planet:%d" % int(planet.planet)
	return ""


func _gui_input(event: InputEvent) -> void:
	var motion := event as InputEventMouseMotion
	if motion:
		if _drag_from != Vector2.INF:
			position += motion.position - _drag_from
		else:
			_set_hover(_hit(motion.position))
		accept_event()
		return
	var button := event as InputEventMouseButton
	if button == null:
		return
	accept_event()
	if button.button_index == MOUSE_BUTTON_LEFT:
		if not button.pressed:
			_drag_from = Vector2.INF
			return
		var hit := _hit(button.position)
		if hit.begins_with("planet:"):
			var planet := _planet(int(hit.get_slice(":", 1)))
			if planet.get("colony", {}).get("mine", false):
				command.emit({"verb": "open_colony", "planet": int(planet.planet)})
		elif hit == "" and button.position.y < 22.0 * _unit():
			_drag_from = button.position # the title bar
	elif button.button_index == MOUSE_BUTTON_RIGHT and button.pressed:
		var hit := _hit(button.position)
		if hit.begins_with("planet:"):
			var planet := _planet(int(hit.get_slice(":", 1)))
			if planet.type != "gas_giant":
				command.emit({"verb": "query", "what": "potential_colony", "planet": int(planet.planet),
					"_reply_to": "show_potential"})


func _planet(planet_id: int) -> Dictionary:
	for planet: Dictionary in system.get("planets", []):
		if int(planet.planet) == planet_id:
			return planet
	return {}


func _set_hover(hit: String) -> void:
	if hit == _hover:
		return
	_hover = hit
	_potential = {}
	var lines: Array = []
	if hit.begins_with("planet:"):
		lines = _planet(int(hit.get_slice(":", 1))).get("lines", [])
	elif hit.begins_with("fleet:"):
		for fleet: Dictionary in system.get("fleets", []):
			if "fleet:%d" % int(fleet.node) == hit:
				lines = fleet.get("lines", [])
	_show_info(lines.map(func(line: Dictionary) -> String: return line.text))


## The right-click answer (main.gd routes it here).
func show_potential(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	_potential = result
	var lines: Array = [result.title]
	for line: Dictionary in result.lines:
		lines.append("%s  %s" % [line.label, line.value])
	_show_info(lines)


func _show_info(lines: Array) -> void:
	info.visible = not lines.is_empty()
	info_text.text = "\n".join(lines)
	info.reset_size()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
