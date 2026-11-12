extends Button
## A tab on the screen's right edge: a button whose label reads bottom to top, sized to it.

@export var label := "":
	set(value):
		label = value
		tooltip_text = tooltip_text if tooltip_text != "" else value
		_resize()
		queue_redraw()

const FONT_SIZE := 15
const PAD := 14.0


func _ready() -> void:
	text = ""
	_resize()


func _resize() -> void:
	var font := get_theme_font("font")
	if font == null:
		return
	var width := font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE).x
	custom_minimum_size = Vector2(34.0, width + PAD * 2.0)


func _draw() -> void:
	var font := get_theme_font("font")
	var color := get_theme_color("font_hover_color" if is_hovered() else "font_color")
	if button_pressed:
		color = get_theme_color("font_pressed_color")
	var ascent := font.get_ascent(FONT_SIZE)
	draw_set_transform(Vector2(size.x * 0.5 + (ascent - font.get_descent(FONT_SIZE)) * 0.5, size.y - PAD), -PI * 0.5)
	draw_string(font, Vector2.ZERO, label, HORIZONTAL_ALIGNMENT_LEFT, -1, FONT_SIZE, color)
	draw_set_transform(Vector2.ZERO)
