extends Button
## A fleet / colony button (WP3.3g): its label beside a real arrowhead, not "<" or ">" (owner).
## The arrow is the Fleets screen's: a point with a notched back, in the lettering's colour.

@export var label := ""
@export var points_left := true

const GAP := 8.0


func _ready() -> void:
	text = ""
	_resize()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_resize()


func _resize() -> void:
	var font := get_theme_font("font")
	if font == null:
		return
	var size_px := get_theme_font_size("font_size")
	var w := font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px).x
	custom_minimum_size.x = maxf(custom_minimum_size.x, w + size_px + GAP + 28.0)
	queue_redraw()


func _draw() -> void:
	var font := get_theme_font("font")
	var size_px := get_theme_font_size("font_size")
	var color := get_theme_color("font_color")
	if is_hovered():
		color = get_theme_color("font_hover_color")
	if disabled:
		color = get_theme_color("font_disabled_color")
	var tw := font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px).x
	var ah := roundf(size_px * 0.85)
	var aw := roundf(ah * 0.95)
	var total := aw + GAP + tw
	var x0 := (size.x - total) * 0.5
	var mid := size.y * 0.5 + 1.0
	var arrow_x := x0 if points_left else x0 + tw + GAP
	var text_x := x0 + aw + GAP if points_left else x0
	var pts: PackedVector2Array
	if points_left:
		pts = [Vector2(arrow_x, mid), Vector2(arrow_x + aw, mid - ah * 0.5), Vector2(arrow_x + aw * 0.62, mid), Vector2(arrow_x + aw, mid + ah * 0.5)]
	else:
		pts = [Vector2(arrow_x + aw, mid), Vector2(arrow_x, mid - ah * 0.5), Vector2(arrow_x + aw * 0.38, mid), Vector2(arrow_x, mid + ah * 0.5)]
	draw_colored_polygon(pts, color)
	var baseline := mid + (font.get_ascent(size_px) - font.get_descent(size_px)) * 0.5
	draw_string(font, Vector2(text_x, baseline), label, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, color)
