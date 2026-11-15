extends Control
## A star system drawn small (WP3.7, the Leaders panel's top: the owner's "system view" of the
## system the leaders' cycler is at): the `system` query's orbit rings and planets in the legacy
## system box's coordinates, as the galaxy's orbit layer draws them (galaxy/orbits.gd), and the
## star in the middle. Clicks do nothing; hovering a planet shows a small card in the corner: its
## name, size and climate, population and minerals.

const BOX_CENTER := Vector2(173.0, 136.0)
const BOX_SIZE := Vector2(346.0, 272.0)
const PLANET_FPS := 8.0

var art: Node
var system := {}
var star := {} # the galaxy's star entry (its sprite)
var _time := 0.0
var _hovered := -1
var _card: Label


func _ready() -> void:
	clip_contents = true
	mouse_filter = Control.MOUSE_FILTER_PASS
	_card = Label.new()
	_card.theme_type_variation = &"Dim"
	_card.add_theme_font_size_override("font_size", 12)
	_card.position = Vector2(6, 4)
	_card.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.9))
	_card.add_theme_constant_override("outline_size", 4)
	add_child(_card)


func show_system(result: Dictionary, star_entry: Dictionary) -> void:
	system = result
	star = star_entry
	_hovered = -1
	_card.text = ""
	queue_redraw()


func _process(delta: float) -> void:
	if visible and not system.is_empty():
		_time += delta
		queue_redraw()


func _scale() -> float:
	return minf(size.x / BOX_SIZE.x, size.y / BOX_SIZE.y)


func _origin() -> Vector2:
	return size * 0.5 - BOX_CENTER * _scale()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color(0, 0, 0, 0.55))
	if system.is_empty() or art == null:
		return
	var k := _scale()
	var origin := _origin()
	var ring_at := Vector2(float(system.get("ring_x", 0)), float(system.get("ring_y", 0)))
	for planet: Dictionary in system.get("planets", []):
		art.draw(self, "BUFFER0.LBX", int(planet.ring_sprite), int(planet.ring_frame), origin + ring_at * k, k)
	if not star.is_empty():
		var s: Dictionary = art.sprite("BUFFER0.LBX", int(star.sprite))
		if not s.is_empty():
			var star_k := k * 2.4 # the galaxy's star art is small beside the system box's planets
			art.draw(self, "BUFFER0.LBX", int(star.sprite), 0, size * 0.5 - Vector2(s.size) * 0.5 * star_k, star_k)
	for planet: Dictionary in system.get("planets", []):
		if int(planet.sprite) < 0:
			continue
		var at := origin + Vector2(float(planet.box_x), float(planet.box_y)) * k
		var sprite: Dictionary = art.sprite("BUFFER0.LBX", int(planet.sprite))
		var frames: int = (sprite.get("frames", []) as Array).size() if not sprite.is_empty() else 1
		art.draw(self, "BUFFER0.LBX", int(planet.sprite), (int(planet.first_frame) + int(_time * PLANET_FPS)) % maxi(frames, 1), at, k)
		var center := at + Vector2.ONE * float(planet.dimension) * 0.5 * k
		var radius := float(planet.dimension) * 0.5 * k + 2.0 * k
		var colony: Dictionary = planet.get("colony", {})
		if not colony.is_empty():
			var colors: Array = preload("../galaxy.gd").PLAYER_COLORS
			draw_arc(center, radius, 0.0, TAU, 32, colors[int(colony.color) % 8], 1.4, true)
		if int(planet.planet) == _hovered:
			draw_arc(center, radius + 2.0, 0.0, TAU, 32, Color(1, 1, 1, 0.7), 1.0, true)


func _gui_input(event: InputEvent) -> void:
	var motion := event as InputEventMouseMotion
	if motion == null or system.is_empty():
		return
	var k := _scale()
	var origin := _origin()
	var found := -1
	var found_planet := {}
	for planet: Dictionary in system.get("planets", []):
		if int(planet.sprite) < 0:
			continue
		var center := origin + (Vector2(float(planet.box_x), float(planet.box_y)) + Vector2.ONE * float(planet.dimension) * 0.5) * k
		if motion.position.distance_to(center) <= float(planet.dimension) * 0.5 * k + 4.0:
			found = int(planet.planet)
			found_planet = planet
	if found != _hovered:
		_hovered = found
		_card.text = _card_text(found_planet)
		queue_redraw()


## The hover card: name, size and climate, population of the most, minerals.
static func _card_text(planet: Dictionary) -> String:
	if planet.is_empty():
		return ""
	var lines: Array[String] = [String(planet.get("name", ""))]
	var kind := "%s %s" % [String(planet.get("size_name", "")), String(planet.get("climate_name", ""))]
	if kind.strip_edges() != "":
		lines.append(kind.strip_edges())
	var colony: Dictionary = planet.get("colony", {})
	if not colony.is_empty():
		lines.append("Pop %d / %d" % [int(colony.get("population", 0)), int(planet.get("max_population", 0))])
	elif int(planet.get("max_population", 0)) > 0:
		lines.append("Max pop %d" % int(planet.max_population))
	if String(planet.get("minerals_name", "")) != "":
		lines.append(String(planet.minerals_name))
	return "\n".join(lines)
