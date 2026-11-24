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
##                 SlotButton, SideTab, Toggle, ListItem (Button), Heading, Name, Dim, Section (Label);
##                 an armed (pressed) Button's lettering is its font_pressed_color, and the
##                 Button constant "glow" / colour "glow_color" its glow
##   frame       a StyleBox drawn round the whole view, over it (or null)
##   inset       the frame's thickness: edge controls sit inside it
##   edge_gap    the gap between the buttons and slots along an edge
##   bar_height  the top bar's height (its icons are 28 px; the slots must hold them)
##   pane_gap    the gap between a pane and the screen's edge (or the frame)
##   tee         optional: {texture, anchor (the bars' meeting point in it), scale, bar_center (a
##               frame bar's centre line, in from the frame's outer edge)} drawn where a pocket's
##               sides meet the screen frame: as it is at the bottom, upside down at the top
## Views read the layout values as Theme constants of the type "Screen", with "content_top" (where
## panes start under the top bar) worked out from them.

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
	for v in ["SlotButton", "SideTab", "Toggle", "ListItem"]:
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
	# Where panes and flyouts start: under the top bar (and its pocket's frame, 9 px), with the same
	# gap as beside the tabs, at least 4 px (owner: one separation all round).
	var inset := int(built.get("inset", 0))
	var bar_top := inset - 3 if inset > 0 else 6
	var pocket := 9 if inset > 0 else 0
	t.set_constant("content_top", "Screen", bar_top + int(built.get("bar_height", 36)) + pocket + maxi(int(built.get("pane_gap", 0)), 4))


## The player's choice (a package id), or "".
static func saved_choice() -> String:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return ""
	return String(cfg.get_value("theme", "id", ""))


## The interface scale saved in the settings (percent; 0: none saved yet).
static func saved_ui_scale() -> float:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return 0.0
	return float(cfg.get_value("ui", "scale", 0.0))


static func save_ui_scale(percent: float) -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("ui", "scale", percent)
	cfg.save(SETTINGS)


## A pane the system shows through (owner, 2026-11-19: with the build list open the system behind
## should still show): its own background and every framed part inside it, now and added later, at
## `pane_alpha` and `frame_alpha`; text, pictures and buttons stay solid (self_modulate only).
static func see_through(pane: Control, pane_alpha := 0.62, frame_alpha := 0.55) -> void:
	pane.self_modulate.a = pane_alpha
	var fade := func(node: Node) -> void:
		if node is PanelContainer and node != pane:
			(node as Control).self_modulate.a = frame_alpha
	for node in pane.find_children("*", "PanelContainer", true, false):
		fade.call(node)
	pane.get_tree().node_added.connect(func(node: Node) -> void:
		if is_instance_valid(pane) and pane.is_ancestor_of(node):
			fade.call(node))


## A clear strip between a scroll area's content and its vertical bar (owner, 2026-11-20: on
## Glass the colony pane's rows ran right up to the bar). ScrollContainer has no such spacing, so
## the bar is made `gap` wider with its styles drawn only in its right part; the content gives up
## that room only while the bar shows. Taken again from the theme whenever it changes.
##
## Since 2026-11-24 (owner: the Options flyout's bar touched its rows, "sweep for it") the frontend
## gives every scroll area this gap as it enters the tree (`gap_all_scrolls`): ScrollContainers and
## RichTextLabels (their own bar). Applied once per area.
const SCROLL_GAP := 10.0


static func scroll_gap(scroll: Control, gap := SCROLL_GAP) -> void:
	if scroll.has_meta("_scroll_gap"):
		return
	scroll.set_meta("_scroll_gap", true)
	var bar: VScrollBar = scroll.call("get_v_scroll_bar")
	var apply := func() -> void:
		if bar.has_meta("_gapping"):
			return
		bar.set_meta("_gapping", true)
		for style_name in ["scroll", "scroll_focus", "grabber", "grabber_highlight", "grabber_pressed"]:
			bar.remove_theme_stylebox_override(style_name)
			var style := bar.get_theme_stylebox(style_name)
			if style == null:
				continue
			style = style.duplicate()
			style.content_margin_left = maxf(style.content_margin_left, 0.0) + gap
			if style is StyleBoxFlat or style is StyleBoxTexture:
				style.set("expand_margin_left", float(style.get("expand_margin_left")) - gap)
			bar.add_theme_stylebox_override(style_name, style)
		bar.remove_meta("_gapping")
	apply.call()
	bar.theme_changed.connect(apply)


## A question with OK and Cancel in the theme's pane style (a popup window doesn't inherit the
## theme, and the themes draw no window frame): `on_ok` runs on OK, `on_cancel` on Cancel or
## closing. Freed when answered.
static func confirm(owner: Control, text: String, ok_text: String, on_ok: Callable, on_cancel := Callable()) -> ConfirmationDialog:
	var dialog := ConfirmationDialog.new()
	dialog.borderless = true
	dialog.dialog_text = text
	dialog.ok_button_text = ok_text
	var node: Node = owner
	while node != null and not (node is Control and (node as Control).theme != null):
		node = node.get_parent()
	dialog.theme = (node as Control).theme if node != null else null
	dialog.add_theme_stylebox_override("panel", owner.get_theme_stylebox("panel", "Pane"))
	owner.add_child(dialog)
	var answered := [false]
	dialog.confirmed.connect(func():
		answered[0] = true
		on_ok.call())
	dialog.visibility_changed.connect(func():
		if not dialog.visible:
			if not answered[0] and on_cancel.is_valid():
				on_cancel.call()
			dialog.queue_free())
	dialog.popup_centered()
	return dialog


## Moves a pane sideways by its left and right offsets only. Setting `position` rewrites all four
## offsets from the pane's current rect, so a pane that was briefly taller than the screen (its
## content grew for a frame) stayed that tall for good (owner, 2026-11-19: the system pane
## stretched off the screen after some system/planet/fleet clicking).
## The two edges move one after the other, so the pane is resized in between; panes that keep
## their right edge in place on a resize call this again from there, hence the guard.
static func move_x(pane: Control, x: float) -> void:
	if pane.has_meta("_moving_x"):
		return
	pane.set_meta("_moving_x", true)
	var dx := x - pane.position.x
	pane.offset_left += dx
	pane.offset_right += dx
	pane.remove_meta("_moving_x")


## A layout setting of the player's (a split, a width), or `default` when none is saved.


static func saved_value(section: String, key: String, default: Variant) -> Variant:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return default
	return cfg.get_value(section, key, default)


static func save_value(section: String, key: String, value: Variant) -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value(section, key, value)
	cfg.save(SETTINGS)


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
	# Imported in the build (the built-in interface's art); a plain file in a package's zip that
	# replaces it.
	var path := "res://packages/antares-ui/art/%s.png" % name
	var image: Image = null
	if ResourceLoader.exists(path):
		var imported: Texture2D = load(path)
		image = imported.get_image() if imported else null
		if image != null and image.is_compressed():
			image.decompress()
	if image == null:
		image = Image.load_from_file(path)
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


## The system flight's length (seconds; Options' SYSTEM ZOOM), saved with the interface settings.
static func saved_flight_time(default: float) -> float:
	var cfg := ConfigFile.new()
	if cfg.load(SETTINGS) != OK:
		return default
	return float(cfg.get_value("ui", "flight_time", default))


static func save_flight_time(seconds: float) -> void:
	var cfg := ConfigFile.new()
	cfg.load(SETTINGS)
	cfg.set_value("ui", "flight_time", seconds)
	cfg.save(SETTINGS)
