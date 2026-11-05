extends Node2D
## Star names: a layer that fades in past a zoom threshold and keeps a constant on-screen size.

const SHOW_ZOOM := 2.2
const FULL_ZOOM := 3.0
const FONT_SIZE := 15

@onready var galaxy: Node2D = get_parent()


func _draw() -> void:
	var z: float = galaxy.camera.zoom.x
	if z < SHOW_ZOOM:
		return
	var alpha := clampf((z - SHOW_ZOOM) / (FULL_ZOOM - SHOW_ZOOM), 0.0, 1.0)
	var font := ThemeDB.fallback_font
	var inv := 1.0 / z
	for s in galaxy.stars:
		if s.spectral_class == 6 or String(s.name).is_empty():
			continue
		var color := Color(0.85, 0.88, 0.95, alpha)
		if s.owner >= 0:
			color = Color(galaxy.PLAYER_COLORS[s.owner_color % 8], alpha)
		# Below the owner ring (galaxy.gd: radius r * 2.4), then a fixed gap on screen.
		var r := maxf(1.6 + 0.5 * float(s.size), (2.0 + 0.4 * float(s.size)) * inv)
		var p := Vector2(s.x, s.y) + Vector2(0, r * 2.4 + 15.0 * inv)
		draw_set_transform(p, 0.0, Vector2(inv, inv))
		var width := font.get_string_size(s.name, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE).x
		draw_string(font, Vector2(-width * 0.5, 0), s.name, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, color)
	draw_set_transform(Vector2.ZERO)
