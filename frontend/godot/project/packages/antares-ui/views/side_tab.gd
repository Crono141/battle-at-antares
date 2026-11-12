extends Button
## A tab on the screen's right edge: a button whose label reads bottom to top, sized to it. Its
## look is the theme's "SideTab" (WP3.3g: the chrome theme lays the metal button on its side); an
## armed tab's lettering glows as the theme's armed buttons do.

@export var label := "":
	set(value):
		label = value
		tooltip_text = tooltip_text if tooltip_text != "" else value
		_resize()
		queue_redraw()

const PAD := 14.0


func _ready() -> void:
	text = ""
	theme_type_variation = &"SideTab"
	_resize()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED:
		_resize()


func _resize() -> void:
	var font := get_theme_font("font")
	if font == null:
		return
	var size_px := get_theme_font_size("font_size")
	var width := font.get_string_size(label.to_upper(), HORIZONTAL_ALIGNMENT_LEFT, -1, size_px).x
	custom_minimum_size = Vector2(34.0, width + PAD * 2.0)
	queue_redraw()


func _draw() -> void:
	var font := get_theme_font("font")
	var size_px := get_theme_font_size("font_size")
	var color := get_theme_color("font_hover_color" if is_hovered() else "font_color")
	if button_pressed:
		color = get_theme_color("font_pressed_color")
	var ascent := font.get_ascent(size_px)
	var shown := label.to_upper() # tabs letter in capitals, as the original's buttons
	var width := font.get_string_size(shown, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px).x
	draw_set_transform(Vector2(size.x * 0.5 + (ascent - font.get_descent(size_px)) * 0.5, (size.y + width) * 0.5), -PI * 0.5)
	if button_pressed:
		var glow := get_theme_constant("glow", "Button")
		if glow > 0:
			draw_string_outline(font, Vector2.ZERO, shown, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, glow, get_theme_color("glow_color", "Button"))
	draw_string(font, Vector2.ZERO, shown, HORIZONTAL_ALIGNMENT_LEFT, -1, size_px, color)
	draw_set_transform(Vector2.ZERO)
