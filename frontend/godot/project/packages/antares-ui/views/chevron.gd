extends Control
## The marker between the system pane and the colony pane (WP3.4, owner): a notched arrow in the
## theme's "on" colour, the fleet and colony buttons' arrowhead, pointing from the open colony's
## planet row at the colony pane. The frontend places it each frame (`point_at`), or hides it.

const SIZE := Vector2(14, 24)


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	size = SIZE


## Its tip at `tip` (a screen point on the colony pane's edge), centred on the row's height.
func point_at(tip: Vector2) -> void:
	position = tip - Vector2(SIZE.x, SIZE.y * 0.5)
	show()
	queue_redraw()


func _draw() -> void:
	var color := get_theme_color("font_pressed_color", "Button")
	var w := SIZE.x
	var h := SIZE.y
	var pts := PackedVector2Array([Vector2(w, h * 0.5), Vector2(0, 0), Vector2(w * 0.38, h * 0.5), Vector2(0, h)])
	var glow := get_theme_constant("glow", "Button")
	if glow > 0:
		draw_polyline(pts + PackedVector2Array([pts[0]]), Color(get_theme_color("glow_color", "Button"), 0.6), float(glow), true)
	draw_colored_polygon(pts, color)
