extends RefCounted
## Glass (WP3.3g, the owner's look B): dark translucent panels with thin steel edges, Saira
## lettering, amber for "on" (the armed buttons' text and edge, as the chrome theme's glow).
## No art of its own: the game's icons come from antares-ui. The contract is in
## packages/antares-ui/themes.gd.

const NAME := "Glass"

const LIGHT := Color8(226, 222, 210)
const WHITE := Color8(255, 255, 255)
const GOLD := Color8(240, 214, 150)
const DIM := Color8(170, 172, 180)
const AMBER := Color8(255, 138, 28)
const STEEL := Color8(118, 124, 140)

var _dir := ""


func build(dir: String) -> Dictionary:
	_dir = dir
	var t := Theme.new()
	var saira := _font("SairaSemiCondensed-Medium.ttf")
	var saira_bold := _font("SairaSemiCondensed-Bold.ttf")
	t.default_font = saira
	t.default_font_size = 17

	for type in ["Button", "MenuButton", "OptionButton"]:
		_button(t, type, 14, 4)
		t.set_font("font", type, saira_bold)
		t.set_font_size("font_size", type, 16)
	t.set_type_variation("Toggle", "Button")
	t.set_type_variation("SideTab", "Button")
	_button(t, "SideTab", 4, 14)
	t.set_font_size("font_size", "SideTab", 15)
	t.set_constant("glow", "Button", 4)
	t.set_color("glow_color", "Button", Color(AMBER, 0.3))

	t.set_type_variation("Slot", "PanelContainer")
	t.set_stylebox("panel", "Slot", _glass(5, 0.87, STEEL, 12, 4))
	t.set_type_variation("SlotButton", "Button")
	for state in ["normal", "pressed", "focus", "disabled"]:
		t.set_stylebox(state, "SlotButton", _glass(5, 0.87, STEEL, 12, 4))
	t.set_stylebox("hover", "SlotButton", _glass(5, 0.92, Color8(170, 176, 192), 12, 4))
	t.set_stylebox("hover_pressed", "SlotButton", _glass(5, 0.92, Color8(170, 176, 192), 12, 4))
	t.set_font("font", "SlotButton", saira)
	t.set_font_size("font_size", "SlotButton", 19)
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_focus_color", "font_hover_pressed_color"]:
		t.set_color(c, "SlotButton", LIGHT)
	t.set_constant("h_separation", "SlotButton", 10)

	t.set_type_variation("Pane", "PanelContainer")
	t.set_stylebox("panel", "Pane", _glass(8, 0.89, STEEL, 20, 18))
	t.set_stylebox("panel", "PopupMenu", _glass(6, 0.94, STEEL, 8, 8))
	t.set_stylebox("panel", "PopupPanel", _glass(6, 0.94, STEEL, 8, 8))
	t.set_type_variation("Row", "PanelContainer")
	t.set_stylebox("panel", "Row", _glass(5, 0.67, Color8(90, 96, 110), 12, 8))
	t.set_type_variation("RowOpen", "PanelContainer")
	t.set_stylebox("panel", "RowOpen", _glass(5, 0.67, AMBER, 12, 8))
	for type in ["Pocket", "BottomPocket"]:
		t.set_type_variation(type, "PanelContainer")
		t.set_stylebox("panel", type, StyleBoxEmpty.new())
	t.set_type_variation("Portrait", "PanelContainer")
	t.set_stylebox("panel", "Portrait", _glass(4, 1.0, STEEL, 2, 2))

	# Sliders (the volumes): a visible track, the filled part amber.
	var track := StyleBoxFlat.new()
	track.bg_color = Color8(52, 56, 66)
	track.border_color = Color8(110, 116, 130)
	track.set_border_width_all(1)
	track.set_corner_radius_all(3)
	track.content_margin_top = 3
	track.content_margin_bottom = 3
	var filled := track.duplicate()
	filled.bg_color = Color(AMBER, 0.85)
	filled.border_color = AMBER
	t.set_stylebox("slider", "HSlider", track)
	t.set_stylebox("grabber_area", "HSlider", filled)
	t.set_stylebox("grabber_area_highlight", "HSlider", filled)

	t.set_color("font_color", "Label", LIGHT)
	_label(t, "Heading", saira_bold, 30, GOLD)
	_label(t, "Name", saira_bold, 19, GOLD)
	_label(t, "Dim", saira, 15, DIM)
	_label(t, "Section", saira_bold, 14, DIM)
	t.set_color("font_color", "CheckButton", LIGHT)

	return {
		"name": NAME,
		"theme": t,
		"frame": null,
		"inset": 0,
		"edge_gap": 6,
		"bar_height": 36,
		"pane_gap": 8,
	}


func _font(file: String) -> FontFile:
	var f := FontFile.new()
	if f.load_dynamic_font("%s/fonts/%s" % [_dir, file]) != OK:
		return null
	return f


func _glass(radius: int, alpha: float, edge: Color, pad_x: int, pad_y: int) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = Color(10.0 / 255.0, 13.0 / 255.0, 22.0 / 255.0, alpha)
	s.border_color = Color(edge, 0.85)
	s.set_border_width_all(1)
	s.set_corner_radius_all(radius)
	s.content_margin_left = pad_x
	s.content_margin_right = pad_x
	s.content_margin_top = pad_y
	s.content_margin_bottom = pad_y
	return s


func _button(t: Theme, type: String, pad_x: int, pad_y: int) -> void:
	var looks := {
		"normal": [Color8(28, 32, 44, 235), STEEL, 1],
		"hover": [Color8(40, 46, 62, 240), Color8(170, 176, 192), 1],
		"pressed": [Color8(40, 26, 10, 240), AMBER, 1],
		"hover_pressed": [Color8(52, 34, 12, 240), AMBER, 1],
		"disabled": [Color8(18, 20, 26, 200), Color8(60, 62, 70), 1],
	}
	for state: String in looks:
		var s := StyleBoxFlat.new()
		s.bg_color = looks[state][0]
		s.border_color = looks[state][1]
		s.set_border_width_all(looks[state][2])
		if state.contains("pressed"):
			s.border_width_bottom = 2 # the amber underline
		s.set_corner_radius_all(5)
		s.content_margin_left = pad_x
		s.content_margin_right = pad_x
		s.content_margin_top = pad_y
		s.content_margin_bottom = pad_y
		t.set_stylebox(state, type, s)
	t.set_stylebox("focus", type, StyleBoxEmpty.new())
	t.set_color("font_color", type, LIGHT)
	t.set_color("font_hover_color", type, WHITE)
	t.set_color("font_focus_color", type, LIGHT)
	t.set_color("font_pressed_color", type, AMBER)
	t.set_color("font_hover_pressed_color", type, AMBER)
	t.set_color("font_disabled_color", type, Color8(90, 92, 100))


func _label(t: Theme, type: String, font: Font, size: int, color: Color) -> void:
	t.set_type_variation(type, "Label")
	t.set_font("font", type, font)
	t.set_font_size("font_size", type, size)
	t.set_color("font_color", type, color)
