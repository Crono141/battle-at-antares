extends Control
## The marker between the system pane and the colony pane (WP3.4; the owner's sketch "larger Chrome
## Chevron"): a ">" from the open colony's planet row into the colony pane, its back half on the
## planet's row and its tip inside the colony pane's border. With the theme's `chevron_bar` (Chrome)
## it is two bars of the frame's metal; without (Glass) the fleet and colony buttons' notched
## arrowhead in the theme's "on" colour. The frontend places it each frame (`span`), or hides it.

## The theme's metal strip (shading across the bar, the length stretches), or null.
var bar: Texture2D

const THICKNESS := 13.0


## From `back` (a point inside the planet's row) to `tip_x` (inside the colony pane), `height` tall,
## centred on the row.
func span(back: Vector2, tip_x: float, height: float) -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	position = Vector2(back.x, back.y - height * 0.5)
	size = Vector2(maxf(tip_x - back.x, 12.0), height)
	show()
	queue_redraw()


func _draw() -> void:
	var w := size.x
	var h := size.y
	if bar != null:
		_bar(Vector2(0, 0), Vector2(w, h * 0.5))
		_bar(Vector2(w, h * 0.5), Vector2(0, h))
		return
	var color := get_theme_color("font_pressed_color", "Button")
	var pts := PackedVector2Array([Vector2(w, h * 0.5), Vector2(0, 0), Vector2(w * 0.38, h * 0.5), Vector2(0, h)])
	var glow := get_theme_constant("glow", "Button")
	if glow > 0:
		draw_polyline(pts + PackedVector2Array([pts[0]]), Color(get_theme_color("glow_color", "Button"), 0.6), float(glow) + 2.0, true)
	draw_colored_polygon(pts, color)


## One metal bar from `a` to `b`: a quad THICKNESS wide, the strip's shading across it.
func _bar(a: Vector2, b: Vector2) -> void:
	var along := (b - a).normalized()
	var across := Vector2(-along.y, along.x) * THICKNESS * 0.5
	# Overlap at the tip, so the two bars meet without a notch.
	var b2 := b + along * THICKNESS * 0.5
	var pts := PackedVector2Array([a - across, a + across, b2 + across, b2 - across])
	var uvs := PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)])
	draw_colored_polygon(pts, Color.WHITE, uvs, bar)
	# A dark edge, as the frame's bars have.
	draw_polyline(pts + PackedVector2Array([pts[0]]), Color(0, 0, 0, 0.55), 1.0, true)
