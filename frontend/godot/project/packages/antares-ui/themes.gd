extends RefCounted
## Themes (WP3.3g): each look is its own package (antares-ui-chrome, antares-ui-glass, or anyone's)
## holding res://packages/<id>/theme.gd; the Options flyout switches between the enabled ones while
## playing (owner: "ship each as its own package, but also have a theme selector").
##
## The contract: theme.gd is a RefCounted with `build(dir: String) -> Dictionary`, `dir` being its
## package folder (res://packages/<id>), returning {} when it can't (missing art), or:
##   name        what the selector shows
##   theme       a Theme for every native view, with these type variations:
##                 Pane, Row, RowOpen, Slot, Pocket, BottomPocket, Portrait (PanelContainer),
##                 SlotButton, SideTab, Toggle (Button), Heading, Name, Dim, Section (Label);
##                 an armed (pressed) Button's lettering is its font_pressed_color, and the
##                 Button constant "glow" / colour "glow_color" its glow
##   frame       a StyleBox drawn round the whole view, over it (or null)
##   inset       the frame's thickness: edge controls sit inside it
##   edge_gap    the gap between the buttons and slots along an edge
##   bar_height  the top bar's height (its icons are 28 px; the slots must hold them)
##   pane_gap    the gap between a pane and the screen's edge (or the frame)
## Views read the layout values as Theme constants of the type "Screen".

const SETTINGS := "user://antares-ui.cfg"
const ICON_HEIGHT := 28

static var _icons := {}


## The themes of the enabled packages, highest priority first: [{id, name, built}].
static func discover(package_ids: PackedStringArray) -> Array:
	var found := []
	for id in package_ids:
		var path := "res://packages/%s/theme.gd" % id
		if not FileAccess.file_exists(path) and not ResourceLoader.exists(path):
			continue
		var script: GDScript = load(path)
		if script == null:
			continue
		var built: Dictionary = script.new().build("res://packages/%s" % id)
		if built.is_empty() or not built.has("theme"):
			push_warning("themes: %s did not build" % id)
			continue
		_add_screen_constants(built)
		found.append({"id": id, "name": String(built.get("name", id)), "built": built})
	return found


## Godot's own look with the layout values: used when no theme package is enabled.
static func plain() -> Dictionary:
	var built := {"name": "Plain", "theme": Theme.new(), "frame": null, "inset": 0, "edge_gap": 4, "bar_height": 36, "pane_gap": 8}
	var t: Theme = built.theme
	t.set_color("font_pressed_color", "Button", Color8(255, 138, 28))
	t.set_color("font_hover_pressed_color", "Button", Color8(255, 138, 28))
	for v in ["Pane", "Row", "RowOpen", "Slot", "Pocket", "BottomPocket", "Portrait"]:
		t.set_type_variation(v, "PanelContainer")
	for v in ["SlotButton", "SideTab", "Toggle"]:
		t.set_type_variation(v, "Button")
	for v in ["Heading", "Name", "Dim", "Section"]:
		t.set_type_variation(v, "Label")
	t.set_font_size("font_size", "Heading", 28)
	t.set_color("font_color", "Heading", Color8(240, 214, 150))
	t.set_color("font_color", "Name", Color8(240, 214, 150))
	t.set_color("font_color", "Dim", Color8(170, 172, 180))
	t.set_font_size("font_size", "Dim", 14)
	_add_screen_constants(built)
	return built


static func _add_screen_constants(built: Dictionary) -> void:
	var t: Theme = built.theme
	for key in ["inset", "edge_gap", "bar_height", "pane_gap"]:
		t.set_constant(key, "Screen", int(built.get(key, 0)))
	t.set_constant("icon_height", "Screen", ICON_HEIGHT)


## The player's choice (a package id), or "".
static func saved_choice() -> String:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return ""
	return String(cfg.get_value("theme", "id", ""))


static func save_choice(id: String) -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("theme", "id", id)
	cfg.save(SETTINGS)


## One of the bar's icons (art/, from the player's game: Moo2Mods tools/themeart.py) at `height`
## pixels, no wider than 1.45 times that; null when the art isn't there.
static func icon(name: String, height := ICON_HEIGHT) -> Texture2D:
	var key := "%s@%d" % [name, height]
	if _icons.has(key):
		return _icons[key]
	var image := Image.load_from_file("res://packages/antares-ui/art/%s.png" % name)
	var tex: Texture2D = null
	if image != null and not image.is_empty():
		var w := roundi(image.get_width() * float(height) / image.get_height())
		var h := height
		var max_w := roundi(height * 1.45)
		if w > max_w:
			h = roundi(h * float(max_w) / w)
			w = max_w
		image.resize(w, h, Image.INTERPOLATE_LANCZOS)
		tex = ImageTexture.create_from_image(image)
	_icons[key] = tex
	return tex


## An armed (pressed) toggle's lettering glows in the theme's glow colour; call after setting
## `button_pressed` without a signal, or connect it to `toggled`.
static func glow(button: Button) -> void:
	if button.button_pressed:
		button.add_theme_color_override("font_outline_color", button.get_theme_color("glow_color", "Button"))
		button.add_theme_constant_override("outline_size", button.get_theme_constant("glow", "Button"))
	else:
		button.remove_theme_color_override("font_outline_color")
		button.remove_theme_constant_override("outline_size")


## Wires glow() to a toggle button.
static func glowing(button: Button) -> void:
	if not button.toggled.is_connected(_on_toggled.bind(button)):
		button.toggled.connect(_on_toggled.bind(button))
	glow(button)


static func _on_toggled(_on: bool, button: Button) -> void:
	glow(button)
