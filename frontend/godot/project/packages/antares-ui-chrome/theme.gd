extends RefCounted
## Chrome (WP3.3g, the owner's look A): the original screens' metal at a small size. Buttons are
## the Planets screen's metal button closed all round, lettered in Russo One, their lettering amber
## when armed; figures sit in the Fleets screen's name slot; panes are the tech-pattern panel in the
## colony screen's riveted frame, which also runs round the whole view. The art comes from the
## player's own game (art/, made by Moo2Mods tools/themeart.py; see README.md).
## The contract with antares-ui is in packages/antares-ui/themes.gd.

const NAME := "Chrome"

const ENGRAVE := Color8(22, 24, 28)
const LIGHT := Color8(226, 222, 210)
const GOLD := Color8(240, 214, 150)
const DIM := Color8(170, 172, 180)
const AMBER := Color8(255, 138, 28)

const Metal := preload("metal.gd")

var _dir := ""
var _pieces := {}


func build(dir: String) -> Dictionary:
	_dir = dir
	var pieces_file := FileAccess.open(dir + "/art/pieces.json", FileAccess.READ)
	if pieces_file == null:
		push_warning("chrome theme: no art (run Moo2Mods tools/themeart.py)")
		return {}
	_pieces = JSON.parse_string(pieces_file.get_as_text())
	var button_tex := _texture("button")
	var slot_tex := _texture("slot")
	var bezel_tex := _texture("bezel")
	var panel_tex := _texture("panel")
	var portrait_tex := _texture("portrait")
	if button_tex == null or slot_tex == null or bezel_tex == null:
		return {}

	var t := Theme.new()
	var russo := _font("RussoOne.ttf")
	var saira := _font("SairaSemiCondensed-Medium.ttf")
	var saira_bold := _font("SairaSemiCondensed-Bold.ttf")
	t.default_font = saira
	t.default_font_size = 17

	# Buttons: the metal, lettering engraved dark, amber when armed (pressed toggles).
	for type in ["Button", "MenuButton", "OptionButton"]:
		_button_styles(t, type, button_tex, false)
		t.set_font("font", type, russo)
		t.set_font_size("font_size", type, 16)
		_button_colors(t, type)
	t.set_type_variation("Toggle", "Button")
	t.set_type_variation("SideTab", "Button")
	_button_styles(t, "SideTab", button_tex, true)
	t.set_font_size("font_size", "SideTab", 15)
	t.set_constant("glow", "Button", 5)
	t.set_color("glow_color", "Button", Color(AMBER, 0.38))

	# Figures in slots, the bar's buttons too (the treasury's tax menu, command points, research).
	var slot := _nine(slot_tex, "slot", 26, 9)
	for type in ["Slot", "Row", "RowOpen"]:
		t.set_type_variation(type, "PanelContainer")
		t.set_stylebox("panel", type, slot)
	t.set_stylebox("panel", "Row", _nine(slot_tex, "slot", 26, 10))
	t.set_stylebox("panel", "RowOpen", _nine(slot_tex, "slot", 26, 10))
	t.set_type_variation("SlotButton", "Button")
	for state in ["normal", "pressed", "focus", "disabled"]:
		t.set_stylebox(state, "SlotButton", _nine(slot_tex, "slot", 26, 9))
	var hover := _nine(slot_tex, "slot", 26, 9)
	hover.tint = Color(1.15, 1.15, 1.15)
	t.set_stylebox("hover", "SlotButton", hover)
	t.set_stylebox("hover_pressed", "SlotButton", hover)
	t.set_font("font", "SlotButton", saira)
	t.set_font_size("font_size", "SlotButton", 19)
	for c in ["font_color", "font_hover_color", "font_pressed_color", "font_focus_color", "font_hover_pressed_color"]:
		t.set_color(c, "SlotButton", LIGHT)
	t.set_constant("h_separation", "SlotButton", 10)

	# Panes and flyouts: the tech-pattern panel inside the riveted frame.
	var pane := _bezel(bezel_tex, float(_pieces.bezel.scale), 22)
	pane.fill = panel_tex
	t.set_type_variation("Pane", "PanelContainer")
	t.set_stylebox("panel", "Pane", pane)
	t.set_stylebox("panel", "PopupMenu", pane)
	t.set_stylebox("panel", "PopupPanel", pane)
	t.set_font_size("font_size", "PopupMenu", 17)

	# Pockets of the screen frame round the top bar and the bottom row.
	var screen_scale := float(_pieces.bezel.screen_scale)
	var pocket := _bezel(bezel_tex, screen_scale, 0)
	pocket.fill = panel_tex
	pocket.fill_tint = Color(0.6, 0.6, 0.6)
	pocket.grow = PackedFloat32Array([9.0, 60.0, 9.0, 9.0])
	t.set_type_variation("Pocket", "PanelContainer")
	t.set_stylebox("panel", "Pocket", pocket)
	var bottom := _bezel(bezel_tex, screen_scale, 0)
	bottom.fill = panel_tex
	bottom.fill_tint = Color(0.6, 0.6, 0.6)
	bottom.grow = PackedFloat32Array([9.0, 9.0, 9.0, 60.0])
	t.set_type_variation("BottomPocket", "PanelContainer")
	t.set_stylebox("panel", "BottomPocket", bottom)

	# The governor's portrait frame.
	t.set_type_variation("Portrait", "PanelContainer")
	if portrait_tex:
		var frame := Metal.new()
		frame.texture = portrait_tex
		frame.margins = PackedInt32Array([30, 30, 30, 30])
		frame.scale = 0.25
		frame.content_margin_left = 8
		frame.content_margin_right = 8
		frame.content_margin_top = 7
		frame.content_margin_bottom = 7
		t.set_stylebox("panel", "Portrait", frame)

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

	# Text.
	t.set_color("font_color", "Label", LIGHT)
	_label(t, "Heading", russo, 30, GOLD)
	_label(t, "Name", saira_bold, 19, GOLD)
	_label(t, "Dim", saira, 15, DIM)
	_label(t, "Section", saira_bold, 14, DIM)
	t.set_color("font_color", "CheckButton", LIGHT)

	var screen_frame := _bezel(bezel_tex, screen_scale, 0)
	var tee := {}
	var tee_tex := _texture("tee")
	if tee_tex and _pieces.has("tee"):
		tee = {
			"texture": tee_tex,
			"anchor": Vector2(_pieces.tee.anchor[0], _pieces.tee.anchor[1]),
			"scale": screen_scale,
			"bar_center": float(_pieces.tee.bar_center) * screen_scale,
		}
	return {
		"tee": tee,
		"name": NAME,
		"theme": t,
		"frame": screen_frame,
		"inset": roundi(40 * screen_scale),
		"edge_gap": 0,
		"bar_height": 48,
		"pane_gap": 0,
	}


func _texture(piece: String) -> Texture2D:
	var image := Image.load_from_file("%s/art/%s.png" % [_dir, piece])
	if image == null or image.is_empty():
		return null
	return ImageTexture.create_from_image(image)


func _font(file: String) -> FontFile:
	var f := FontFile.new()
	if f.load_dynamic_font("%s/fonts/%s" % [_dir, file]) != OK:
		return null
	return f


func _button_styles(t: Theme, type: String, tex: Texture2D, vertical: bool) -> void:
	var tints := {"normal": 1.0, "hover": 1.12, "pressed": 1.0, "hover_pressed": 1.08, "disabled": 0.45, "focus": -1.0}
	for state: String in tints:
		var box: StyleBox
		if tints[state] < 0.0:
			box = StyleBoxEmpty.new()
		else:
			var m := Metal.new()
			m.texture = tex
			m.caps = int(_pieces.button.caps)
			m.vertical = vertical
			m.tint = Color(tints[state], tints[state], tints[state])
			box = m
		if vertical:
			box.content_margin_top = 14
			box.content_margin_bottom = 14
			box.content_margin_left = 4
			box.content_margin_right = 4
		else:
			box.content_margin_left = 14
			box.content_margin_right = 14
			box.content_margin_top = 4
			box.content_margin_bottom = 3 # the lettering sits a little low on the face
		t.set_stylebox(state, type, box)


func _button_colors(t: Theme, type: String) -> void:
	t.set_color("font_color", type, ENGRAVE)
	t.set_color("font_hover_color", type, ENGRAVE)
	t.set_color("font_focus_color", type, ENGRAVE)
	t.set_color("font_pressed_color", type, AMBER)
	t.set_color("font_hover_pressed_color", type, AMBER)
	t.set_color("font_disabled_color", type, Color8(14, 14, 16))
	t.set_color("icon_normal_color", type, Color.WHITE)


func _nine(tex: Texture2D, piece: String, pad_x: int, pad_y: int) -> StyleBox:
	var m := Metal.new()
	m.texture = tex
	m.margins = PackedInt32Array(_pieces[piece].margins)
	m.scale = float(_pieces[piece].scale)
	m.content_margin_left = pad_x
	m.content_margin_right = pad_x
	m.content_margin_top = pad_y
	m.content_margin_bottom = pad_y
	return m


func _bezel(tex: Texture2D, scale: float, pad: int) -> StyleBox:
	var m := Metal.new()
	m.texture = tex
	m.margins = PackedInt32Array(_pieces.bezel.margins)
	m.scale = scale
	m.center = false
	m.content_margin_left = pad
	m.content_margin_right = pad
	m.content_margin_top = pad
	m.content_margin_bottom = pad
	return m


func _label(t: Theme, type: String, font: Font, size: int, color: Color) -> void:
	t.set_type_variation(type, "Label")
	t.set_font("font", type, font)
	t.set_font_size("font_size", type, size)
	t.set_color("font_color", type, color)
