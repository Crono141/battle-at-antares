extends Node2D
## Lines between stars: wormholes (from their known ends) and, with the game's "relocation lines"
## option on, where each of the player's systems sends its new ships (a moving dashed line towards
## the destination, as the legacy map's multi-coloured line moves).

const WORMHOLE_COLOR := Color(0.62, 0.42, 0.95, 0.75)
const RELOCATION_COLOR := Color(0.35, 0.85, 0.75, 0.9)
const DASH_PIXELS := 6.0
const DASH_SPEED := 18.0 # screen pixels per second

@onready var galaxy: Node2D = get_parent()

var _time := 0.0


func _process(delta: float) -> void:
	_time += delta
	if galaxy.data.get("relocation_lines", false):
		queue_redraw()


func _draw() -> void:
	var px: float = galaxy.pixel()
	var drawn := {}
	for star: Dictionary in galaxy.stars:
		var other := int(star.wormhole)
		if other < 0:
			continue
		var key := Vector2i(mini(int(star.id), other), maxi(int(star.id), other))
		if drawn.has(key):
			continue
		drawn[key] = true
		draw_line(galaxy.star_position(int(star.id)), galaxy.star_position(other), WORMHOLE_COLOR, 1.5 * px, true)
	if not galaxy.data.get("relocation_lines", false):
		return
	for star: Dictionary in galaxy.stars:
		if int(star.relocation) >= 0:
			dashed_line(self, galaxy.star_position(int(star.id)), galaxy.star_position(int(star.relocation)),
				RELOCATION_COLOR, px, _time)


## A dashed line whose dashes move from `from` towards `to` (shared with the fleets layer).
static func dashed_line(item: CanvasItem, from: Vector2, to: Vector2, color: Color, px: float, time: float) -> void:
	var length := from.distance_to(to)
	if length <= 0.0:
		return
	var dir := (to - from) / length
	var dash := DASH_PIXELS * px
	var offset := fmod(time * DASH_SPEED * px, dash * 2.0)
	var t := offset - dash * 2.0
	while t < length:
		var a := maxf(t, 0.0)
		var b := minf(t + dash, length)
		if b > a:
			item.draw_line(from + dir * a, from + dir * b, color, 1.5 * px, true)
		t += dash * 2.0
