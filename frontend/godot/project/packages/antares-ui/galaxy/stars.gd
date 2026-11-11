extends Node2D
## Stars and black holes, with the game's own pictures (BUFFER0.LBX, the closest zoom level's,
## so HD art stays sharp up close). Black holes turn as on the legacy map; the system whose panel is
## open is ringed.

const BLACK_HOLE_FPS := 8.0

@onready var galaxy: Node2D = get_parent()

var _time := 0.0


func _process(delta: float) -> void:
	_time += delta
	# Only black holes animate.
	queue_redraw()


func _draw() -> void:
	var px: float = galaxy.pixel()
	for star: Dictionary in galaxy.stars:
		var center := Vector2(float(star.x), float(star.y))
		var size: float = galaxy.star_size(star)
		var frame := int(_time * BLACK_HOLE_FPS) if star.black_hole else 0
		var art_size := _art_size(star)
		var drawn := false
		if art_size > 0.0:
			var scale := size / art_size
			drawn = galaxy.art.draw(self, "BUFFER0.LBX", int(star.sprite), frame,
				center - Vector2(art_size, art_size) * 0.5 * scale, scale)
		if not drawn:
			# A stand-in until the picture arrives.
			draw_circle(center, size * 0.18, Color(1, 1, 1, 0.8))
		if int(star.id) == galaxy.selected:
			draw_arc(center, size * 0.62 + 3.0 * px, 0.0, TAU, 48, Color(1, 1, 1, 0.85), 1.5 * px, true)


## The picture's 1x size (its closest zoom level), which `galaxy.star_size` scales.
func _art_size(star: Dictionary) -> float:
	var sprite: Dictionary = galaxy.art.sprite("BUFFER0.LBX", int(star.sprite))
	return 0.0 if sprite.is_empty() else float(sprite.size.x)
