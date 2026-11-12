extends Node2D
## The open system's planets on the map (WP3.3f): the legacy system box's orbit rings and rotating
## planets (BUFFER0.LBX), laid out as the box lays them out, centred on the star at the galaxy's
## `system_scale` world units per box pixel. The galaxy fades this layer in when a system opens.
## The player's colonies get their owner's ring, the selected planet a white one.

const PLANET_FPS := 8.0
## The box's sun centre in its picture's pixels (the rings are centred on it; sys.cpp).
const BOX_CENTER := Vector2(173.0, 136.0)

var _time := 0.0

@onready var galaxy: Node2D = get_parent()


func _process(delta: float) -> void:
	if not galaxy.system.is_empty() and modulate.a > 0.0:
		_time += delta
		queue_redraw()


func _draw() -> void:
	var system: Dictionary = galaxy.system
	if system.is_empty() or galaxy.art == null or system.get("view", "full") != "full":
		return
	var k: float = galaxy.system_scale
	var origin: Vector2 = galaxy.star_position(galaxy.system_star) - BOX_CENTER * k
	var ring_at := Vector2(float(system.ring_x), float(system.ring_y))
	for planet: Dictionary in system.planets:
		galaxy.art.draw(self, "BUFFER0.LBX", int(planet.ring_sprite), int(planet.ring_frame), origin + ring_at * k, k)
	for planet: Dictionary in system.planets:
		if int(planet.sprite) < 0:
			continue # asteroids: their belt is the ring
		var sprite: Dictionary = galaxy.art.sprite("BUFFER0.LBX", int(planet.sprite))
		var frames: int = 1 if sprite.is_empty() else sprite.frames.size()
		var frame := (int(planet.first_frame) + int(_time * PLANET_FPS)) % maxi(frames, 1)
		var at := origin + Vector2(float(planet.box_x), float(planet.box_y)) * k
		galaxy.art.draw(self, "BUFFER0.LBX", int(planet.sprite), frame, at, k)
		var center := at + Vector2.ONE * float(planet.dimension) * 0.5 * k
		var radius := float(planet.dimension) * 0.5 * k + 2.0 * k
		var colony: Dictionary = planet.get("colony", {})
		if not colony.is_empty():
			draw_arc(center, radius, 0.0, TAU, 48, galaxy.player_color(int(colony.color)), (1.0 if colony.outpost else 1.6) * k, true)
		if int(planet.planet) == galaxy.selected_planet:
			draw_arc(center, radius + 2.0 * k, 0.0, TAU, 48, Color(1, 0.4, 0.4, 0.95), 1.4 * k, true)


## The planet under a world point, or -1.
func pick(world: Vector2) -> int:
	var system: Dictionary = galaxy.system
	if system.is_empty() or modulate.a < 0.5 or system.get("view", "full") != "full":
		return -1
	var k: float = galaxy.system_scale
	var origin: Vector2 = galaxy.star_position(galaxy.system_star) - BOX_CENTER * k
	for planet: Dictionary in system.planets:
		if int(planet.sprite) < 0:
			continue
		var center := origin + (Vector2(float(planet.box_x), float(planet.box_y)) + Vector2.ONE * float(planet.dimension) * 0.5) * k
		if world.distance_to(center) <= float(planet.dimension) * 0.5 * k + 3.0 * k:
			return int(planet.planet)
	return -1
