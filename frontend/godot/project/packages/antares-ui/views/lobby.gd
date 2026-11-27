extends Control
## The title screen (WP3.13, D30): the station backdrop, the "Battle at Antares" wordmark and the
## control panel that swings in from the right with the menu's six buttons. The same backdrop stays
## behind the new game and the Hall of Fame (their own views over it), so the three screens read
## as one place.
##
## Art, best first (each optional; the view works with the game's own art alone):
##   art/title/backdrop_wide.webp + .json  the scene widened to 21:9, no frame, no logo (derived
##                                        from the game's art: written by Moo2Mods tools/titleart.py,
##                                        git-ignored like the theme art)
##   art/title/frame/                     the metal border in pieces (same tool), drawn round any
##                                        aspect ratio
##   title/logo.png                       the wordmark (our own art, committed)
##   title/panel.glb                      the control panel in 3D (our own model; not used yet, the
##                                        swing is drawn in 2D)
## Without the wide backdrop the original title picture (MAINMENU.LBX 0, frame 0) is shown at 4:3,
## its own logo and frame included, and the new wordmark is left off.

signal command(dict: Dictionary)

var art: Node

const MENU_ITEMS := [
	["continue", "CONTINUE", KEY_C],
	["load", "LOAD GAME", KEY_L],
	["new", "NEW GAME", KEY_N],
	["multiplayer", "MULTI PLAYER", KEY_M],
	["hall_of_fame", "HALL OF FAME", KEY_H],
	["quit", "QUIT GAME", KEY_Q],
]
const SWING_TIME := 1.1
const SLIDE_TIME := 0.45
const PANEL_GREEN := Color(0.22, 1.0, 0.35)

var screen := -1
var _menu := {} # the `menu` query's answer
var _backdrop_tex: Texture2D
var _backdrop_inner := Rect2() # where the original 4:3 scene sits in the wide picture (pixels)
var _wide := false
var _frame := {} # piece name -> Texture2D, plus "info" -> Dictionary
var _logo_tex: Texture2D
var _logo: TextureRect
var _panel: PanelContainer # the control panel (its face holds the buttons)
var _arm: ColorRect
var _buttons := {} # item -> Button
var _version: Label
var _mods: PanelContainer
var _credits: Button
var _slots: PanelContainer # the shared Load page (save_slots.gd)
var _note: Label
var _swing: Tween
var _panel_in := false
var _busy := false # a choice was sent: no second one until the screen changes
## The 3D panel (title/panel.glb, our own Blender model): it swings in on its hinge at the right
## edge and slides to its place; the live buttons then lie over its face. Without the model the
## 2D panel squeezes in instead.
var _rig: SubViewportContainer
var _camera: Camera3D
var _model: Node3D # the glb's scene; its "Panel" node is the hinge
var _hinge: Node3D
var _arm3d: Node3D
var _turn := 0.0 # 0 edge-on, 1 facing the viewer
var _slide := 0.0 # 0 at the right edge, 1 home
const FACE_CENTER := Vector2(-1.0, 0.0) # the face's middle from the hinge (model units)
const FACE_SIZE := Vector2(1.70, 1.40)
const ARM_LENGTH := 2.5
const CAMERA_DISTANCE := 12.0
const CAMERA_FOV := 30.0


func _ready() -> void:
	set_anchors_preset(Control.PRESET_FULL_RECT)
	mouse_filter = Control.MOUSE_FILTER_STOP
	_load_art()
	_logo = TextureRect.new()
	_logo.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_logo.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT
	_logo.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_logo.texture = _logo_tex
	add_child(_logo)

	_build_rig()

	_arm = ColorRect.new()
	_arm.color = Color(0.20, 0.21, 0.23)
	_arm.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_arm)

	_panel = PanelContainer.new()
	_panel.add_theme_stylebox_override("panel", _panel_style() if _rig == null else _face_style())
	add_child(_panel)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	_panel.add_child(box)
	for item: Array in MENU_ITEMS:
		var button := Button.new()
		button.text = item[1]
		button.focus_mode = Control.FOCUS_NONE
		button.custom_minimum_size = Vector2(300, 46)
		_style_menu_button(button)
		var shortcut := Shortcut.new()
		var key := InputEventKey.new()
		key.keycode = item[2]
		shortcut.events = [key]
		button.shortcut = shortcut
		button.tooltip_text = "%s (%s)" % [str(item[1]).capitalize(), OS.get_keycode_string(item[2])]
		var id: String = item[0]
		button.pressed.connect(func(): _choose(id))
		box.add_child(button)
		_buttons[id] = button

	_note = Label.new()
	_note.add_theme_color_override("font_color", Color(1, 0.55, 0.4))
	_note.autowrap_mode = TextServer.AUTOWRAP_WORD
	_note.custom_minimum_size = Vector2(300, 0)
	_note.hide()
	box.add_child(_note)

	_version = Label.new()
	_version.add_theme_color_override("font_color", Color(0.55, 0.85, 0.6))
	_version.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_version)

	_credits = Button.new()
	_credits.text = "CREDITS"
	_credits.flat = true
	_credits.focus_mode = Control.FOCUS_NONE
	_credits.add_theme_color_override("font_color", Color(0.55, 0.85, 0.6))
	_credits.add_theme_color_override("font_hover_color", PANEL_GREEN)
	_credits.pressed.connect(func(): _choose("credits"))
	add_child(_credits)

	_mods = PanelContainer.new()
	_mods.theme_type_variation = &"Pane"
	_mods.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_mods.hide()
	add_child(_mods)

	_slots = preload("save_slots.gd").new()
	_slots.hide()
	_slots.command.connect(func(dict: Dictionary): command.emit(dict))
	_slots.finished.connect(func(): _slots.close_slots())
	_slots.back.connect(func(): _slots.close_slots())
	_slots.refused.connect(_show_note)
	add_child(_slots)
	resized.connect(_layout)


func _load_art() -> void:
	var base: String = scene_file_path.get_base_dir().get_base_dir() # the package folder
	var wide := base + "/art/title/backdrop_wide.webp"
	if FileAccess.file_exists(wide):
		var image := Image.load_from_file(wide)
		if image != null and not image.is_empty():
			_backdrop_tex = ImageTexture.create_from_image(image)
			_wide = true
			var info_path := base + "/art/title/backdrop_wide.json"
			if FileAccess.file_exists(info_path):
				var info: Variant = JSON.parse_string(FileAccess.get_file_as_string(info_path))
				if info is Dictionary and info.has("inner_rect"):
					var r: Array = info.inner_rect
					_backdrop_inner = Rect2(r[0], r[1], r[2], r[3])
	var frame_dir := base + "/art/title/frame"
	if FileAccess.file_exists(frame_dir + "/frame.json"):
		var info: Variant = JSON.parse_string(FileAccess.get_file_as_string(frame_dir + "/frame.json"))
		if info is Dictionary:
			_frame["info"] = info
		for piece in ["corner_tl", "corner_tr", "corner_bl", "corner_br", "edge_top", "edge_bottom", "edge_left", "edge_right", "bottom_center"]:
			var path := "%s/%s.png" % [frame_dir, piece]
			if FileAccess.file_exists(path):
				var image := Image.load_from_file(path)
				if image != null and not image.is_empty():
					_frame[piece] = ImageTexture.create_from_image(image)
	var logo := base + "/title/logo.png"
	if FileAccess.file_exists(logo):
		var image := Image.load_from_file(logo)
		if image != null and not image.is_empty():
			_logo_tex = ImageTexture.create_from_image(image)


func _build_rig() -> void:
	var path: String = scene_file_path.get_base_dir().get_base_dir() + "/title/panel.glb"
	if not FileAccess.file_exists(path):
		return
	var doc := GLTFDocument.new()
	var state := GLTFState.new()
	if doc.append_from_file(path, state) != OK:
		push_warning("lobby: could not read %s" % path)
		return
	_model = doc.generate_scene(state) as Node3D
	if _model == null:
		return
	_hinge = _model.find_child("Panel", true, false) as Node3D
	if _hinge == null:
		_hinge = _model
	_arm3d = _model.find_child("Arm", true, false) as Node3D
	var face := _model.find_child("Face", true, false)
	if face is MeshInstance3D:
		var glass := StandardMaterial3D.new()
		glass.albedo_color = Color(0.03, 0.16, 0.05)
		glass.emission_enabled = true
		glass.emission = Color(0.05, 0.35, 0.10)
		glass.emission_energy_multiplier = 0.6
		glass.roughness = 0.25
		glass.metallic_specular = 0.8
		(face as MeshInstance3D).material_override = glass
	_rig = SubViewportContainer.new()
	_rig.stretch = true
	_rig.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_rig.set_anchors_preset(Control.PRESET_FULL_RECT)
	var viewport := SubViewport.new()
	viewport.transparent_bg = true
	viewport.own_world_3d = true
	viewport.msaa_3d = Viewport.MSAA_4X
	_rig.add_child(viewport)
	_camera = Camera3D.new()
	_camera.fov = CAMERA_FOV
	_camera.position = Vector3(0, 0, CAMERA_DISTANCE)
	viewport.add_child(_camera)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-30, -35, 0)
	sun.light_energy = 1.4
	viewport.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(10, 120, 0)
	fill.light_energy = 0.35
	fill.light_color = Color(0.7, 0.8, 1.0)
	viewport.add_child(fill)
	var env := Environment.new()
	env.background_mode = Environment.BG_CLEAR_COLOR
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.45, 0.47, 0.5)
	env.ambient_light_energy = 0.6
	var world_env := WorldEnvironment.new()
	world_env.environment = env
	viewport.add_child(world_env)
	viewport.add_child(_model)
	_model.visible = false
	add_child(_rig)


## The live buttons over the 3D face: no frame of their own.
func _face_style() -> StyleBoxEmpty:
	var style := StyleBoxEmpty.new()
	style.content_margin_left = 26
	style.content_margin_right = 26
	style.content_margin_top = 24
	style.content_margin_bottom = 24
	return style


## Pixels per model unit at the panel's depth (the hinge is at z = 0).
func _ppu() -> float:
	return size.y / (2.0 * CAMERA_DISTANCE * tan(deg_to_rad(CAMERA_FOV) * 0.5))


func _screen_to_world(at: Vector2) -> Vector2:
	var ppu := _ppu()
	return Vector2((at.x - size.x * 0.5) / ppu, -(at.y - size.y * 0.5) / ppu)


## Places the 3D panel for the current swing (_turn) and slide (_slide): its face, facing the
## viewer at home, covers the buttons' rectangle; the hinge starts at the picture's right edge.
func _update_3d() -> void:
	if _rig == null or _model == null:
		return
	_panel.reset_size()
	var home := Rect2(_panel_home(), _panel.size)
	var k := home.size.y / (FACE_SIZE.y * 0.9 * _ppu()) # the buttons inside the face's safe area
	var face_world := _screen_to_world(home.get_center())
	var hinge_home := face_world - FACE_CENTER * k
	var shown := picture_rect()
	var edge := _screen_to_world(Vector2(shown.end.x - shown.size.y * 0.02, 0)).x
	var hinge_x := lerpf(edge, hinge_home.x, _slide)
	_hinge.position = Vector3(hinge_x, hinge_home.y, 0)
	_hinge.scale = Vector3(k, k, k)
	_hinge.rotation = Vector3(0, deg_to_rad(-90.0 * (1.0 - _turn)), 0)
	if _arm3d:
		# The arm reaches past the screen's right edge, wherever the panel is.
		var right := _screen_to_world(Vector2(size.x + 40, 0)).x
		var needed := (right - hinge_x) / k
		var arm_parent_scale := 1.0
		_arm3d.scale.x = maxf(0.2, needed / ARM_LENGTH) * arm_parent_scale
		_arm3d.rotation.y = -_hinge.rotation.y # the arm stays fixed to the wall while the panel turns
	_model.visible = visible and (_turn > 0.0 or _slide > 0.0 or _panel_in)


func _panel_style() -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.04, 0.16, 0.06, 0.96)
	style.border_color = Color(0.36, 0.38, 0.40)
	style.set_border_width_all(10)
	style.set_corner_radius_all(26)
	style.shadow_color = Color(0, 0, 0, 0.6)
	style.shadow_size = 18
	style.content_margin_left = 26
	style.content_margin_right = 26
	style.content_margin_top = 24
	style.content_margin_bottom = 24
	return style


func _style_menu_button(button: Button) -> void:
	var normal := StyleBoxFlat.new()
	normal.bg_color = Color(0.07, 0.26, 0.09)
	normal.border_color = Color(0.13, 0.42, 0.16)
	normal.set_border_width_all(2)
	normal.set_corner_radius_all(4)
	var hover := normal.duplicate() as StyleBoxFlat
	hover.bg_color = Color(0.10, 0.36, 0.13)
	hover.border_color = PANEL_GREEN
	var pressed := hover.duplicate() as StyleBoxFlat
	pressed.bg_color = Color(0.14, 0.46, 0.18)
	var disabled := normal.duplicate() as StyleBoxFlat
	disabled.bg_color = Color(0.05, 0.14, 0.06)
	disabled.border_color = Color(0.08, 0.22, 0.10)
	button.add_theme_stylebox_override("normal", normal)
	button.add_theme_stylebox_override("hover", hover)
	button.add_theme_stylebox_override("pressed", pressed)
	button.add_theme_stylebox_override("disabled", disabled)
	button.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	button.add_theme_color_override("font_color", Color(0.45, 0.95, 0.5))
	button.add_theme_color_override("font_hover_color", Color(0.75, 1.0, 0.75))
	button.add_theme_color_override("font_pressed_color", Color(0.9, 1.0, 0.9))
	button.add_theme_color_override("font_disabled_color", Color(0.18, 0.38, 0.2))
	button.add_theme_font_size_override("font_size", 24)


## Shown for the title screen, the new game and the Hall of Fame (the frontend puts their own
## views over this one).
func show_screen(new_screen: int) -> void:
	var was := screen
	screen = new_screen
	_busy = false
	show()
	_layout()
	if screen == Screens.MAIN_MENU:
		_note.hide()
		command.emit({"verb": "query", "what": "menu", "_reply_to": "show_menu"})
		_logo.show()
		_version.show()
		_credits.show()
		if not _panel_in:
			_swing_in(was == -1 or was == Screens.LEGACY)
	else:
		_slots.close_slots()
		_mods.hide()
		_credits.hide()
		_version.hide()
		_logo.hide()
		if _panel_in:
			_swing_out()
	queue_redraw()


func leave() -> void:
	screen = -1
	_panel_in = false
	_turn = 0.0
	_slide = 0.0
	if _model:
		_model.visible = false
	if _swing:
		_swing.kill()
	_slots.close_slots()
	hide()


func show_menu(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	_menu = result
	(_buttons["continue"] as Button).disabled = not result.get("can_continue", false)
	(_buttons["load"] as Button).disabled = not result.get("can_load", false)
	_version.text = str(result.get("version_label", ""))
	for child in _mods.get_children():
		child.queue_free()
	if result.get("show_mod_list", false):
		var list := VBoxContainer.new()
		var title := Label.new()
		title.theme_type_variation = &"Name"
		title.text = "Loaded packages"
		list.add_child(title)
		var mods: Array = result.get("mods", [])
		if mods.is_empty():
			var none := Label.new()
			none.text = "None"
			list.add_child(none)
		for mod: Dictionary in mods:
			var row := Label.new()
			row.text = "%d. %s  %s" % [int(mod.get("priority", 0)) + 1, str(mod.get("name", "")), str(mod.get("version", ""))]
			list.add_child(row)
		_mods.add_child(list)
		_mods.show()
	else:
		_mods.hide()
	_layout.call_deferred()


func _choose(item: String) -> void:
	if screen != Screens.MAIN_MENU or _busy or not _panel_in:
		return
	var button: Button = _buttons.get(item)
	if button != null and button.disabled:
		return
	if item == "load":
		if _slots.visible:
			_slots.close_slots()
		else:
			_slots.open_mode("load")
		return
	_busy = true
	command.emit({"verb": "menu_choose", "item": item, "_reply_to": "chosen"})


func chosen(result: Dictionary) -> void:
	if not result.get("ok", false):
		_busy = false
		_show_note(str(result.get("message", result.get("error", ""))))
		command.emit({"verb": "query", "what": "menu", "_reply_to": "show_menu"})


## The Load page's replies (sent through this view).
func show_saves(result: Dictionary) -> void:
	_slots.show_saves(result)
	_layout.call_deferred()


func show_result(result: Dictionary) -> void:
	if not result.get("ok", false):
		_show_note(str(result.get("message", result.get("error", ""))))
		command.emit({"verb": "query", "what": "menu", "_reply_to": "show_menu"})


func _show_note(text: String) -> void:
	_note.text = text
	_note.visible = text != ""


# ---- Layout and drawing ----

## The picture's rectangle on screen: the wide backdrop is fitted to the height and cropped at the
## sides (16:9 shows its middle); a window wider than the picture shows black beyond it. Without
## it, the original 4:3 picture fitted inside.
func picture_rect() -> Rect2:
	var view := size
	if _backdrop_tex == null:
		var tex: Texture2D = art.texture("MAINMENU.LBX", 0, 0) if art else null
		if tex == null:
			return Rect2(Vector2.ZERO, view)
		var s := minf(view.x / 640.0, view.y / 480.0)
		var w := 640.0 * s
		var h := 480.0 * s
		return Rect2((view.x - w) * 0.5, (view.y - h) * 0.5, w, h)
	var scale := view.y / float(_backdrop_tex.get_height())
	var w := _backdrop_tex.get_width() * scale
	if w < view.x:
		return Rect2((view.x - w) * 0.5, 0, w, view.y)
	return Rect2(0, 0, view.x, view.y)


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color.BLACK)
	if _backdrop_tex == null:
		if art == null:
			return
		var tex: Texture2D = art.texture("MAINMENU.LBX", 0, 0)
		if tex == null:
			art.loaded.connect(func(_k: String): queue_redraw(), CONNECT_ONE_SHOT)
			return
		draw_texture_rect(tex, picture_rect(), false)
		return
	var shown := picture_rect()
	var scale := size.y / float(_backdrop_tex.get_height())
	var full_w := _backdrop_tex.get_width() * scale
	var src_x := maxf(0.0, (full_w - size.x) * 0.5) / scale
	var src := Rect2(src_x, 0, shown.size.x / scale, _backdrop_tex.get_height())
	draw_texture_rect_region(_backdrop_tex, shown, src)
	_draw_frame(shown)


## The metal border round the picture, from its pieces: corners as they are, edges stretched
## along their length, the bottom console unstretched in the middle.
func _draw_frame(rect: Rect2) -> void:
	if not _frame.has("corner_tl"):
		return
	var info: Dictionary = _frame.get("info", {})
	# Pieces are in the 4x art's pixels (2560x1920 for 640x480); on screen they keep the scale the
	# original picture would have at this height.
	var source_height := float(info.get("source_height", 1920))
	var s := rect.size.y / source_height
	var tl: Texture2D = _frame.corner_tl
	var tr: Texture2D = _frame.corner_tr
	var bl: Texture2D = _frame.corner_bl
	var br: Texture2D = _frame.corner_br
	var top: Texture2D = _frame.get("edge_top")
	var bottom: Texture2D = _frame.get("edge_bottom")
	var left: Texture2D = _frame.get("edge_left")
	var right: Texture2D = _frame.get("edge_right")
	var tl_size := tl.get_size() * s
	var tr_size := tr.get_size() * s
	var bl_size := bl.get_size() * s
	var br_size := br.get_size() * s
	if top:
		_tile(top, Rect2(rect.position.x + tl_size.x, rect.position.y, rect.size.x - tl_size.x - tr_size.x, top.get_height() * s), s, true)
	if bottom:
		var h := bottom.get_height() * s
		_tile(bottom, Rect2(rect.position.x + bl_size.x, rect.end.y - h, rect.size.x - bl_size.x - br_size.x, h), s, true)
	if left:
		_tile(left, Rect2(rect.position.x, rect.position.y + tl_size.y, left.get_width() * s, rect.size.y - tl_size.y - bl_size.y), s, false)
	if right:
		var w := right.get_width() * s
		_tile(right, Rect2(rect.end.x - w, rect.position.y + tr_size.y, w, rect.size.y - tr_size.y - br_size.y), s, false)
	draw_texture_rect(tl, Rect2(rect.position, tl_size), false)
	draw_texture_rect(tr, Rect2(Vector2(rect.end.x - tr_size.x, rect.position.y), tr_size), false)
	draw_texture_rect(bl, Rect2(Vector2(rect.position.x, rect.end.y - bl_size.y), bl_size), false)
	draw_texture_rect(br, Rect2(rect.end - br_size, br_size), false)
	var center: Texture2D = _frame.get("bottom_center")
	if center:
		var c_size := center.get_size() * s
		draw_texture_rect(center, Rect2(Vector2(rect.get_center().x - c_size.x * 0.5, rect.end.y - c_size.y), c_size), false)


## An edge piece repeated along `area` at scale `s` (the pieces are seamless along their length),
## the last one cut to fit.
func _tile(tex: Texture2D, area: Rect2, s: float, horizontal: bool) -> void:
	if area.size.x <= 0.0 or area.size.y <= 0.0:
		return
	var step := (tex.get_width() if horizontal else tex.get_height()) * s
	var length := area.size.x if horizontal else area.size.y
	var at := 0.0
	while at < length - 0.5:
		var part := minf(step, length - at)
		var src := Rect2(0, 0, part / s, tex.get_height()) if horizontal else Rect2(0, 0, tex.get_width(), part / s)
		var dst := Rect2(area.position.x + at, area.position.y, part + 0.5, area.size.y) if horizontal \
			else Rect2(area.position.x, area.position.y + at, area.size.x, part + 0.5)
		draw_texture_rect_region(tex, dst, src)
		at += step


## The panel's place when it has swung in: right of centre, vertically centred, as in the original
## (its right edge about 87% across the 4:3 picture; on wider pictures, the same distance from the
## right edge of the screen as the original's).
func _panel_home() -> Vector2:
	var shown := picture_rect()
	var panel_size := _panel.get_combined_minimum_size()
	var margin := shown.size.y * 0.09
	var width := panel_size.x
	if _rig != null:
		# The 3D panel round the buttons: its face (the buttons' safe area) and the bezel.
		width = panel_size.y / 0.9 * FACE_SIZE.x / FACE_SIZE.y * 1.22
	var center_x := shown.end.x - margin - width * 0.5
	var y := shown.position.y + shown.size.y * 0.47 - panel_size.y * 0.5
	return Vector2(center_x - panel_size.x * 0.5, y)


func _layout() -> void:
	if _panel == null:
		return
	var shown := picture_rect()
	_panel.reset_size()
	if _panel_in and (_swing == null or not _swing.is_running()):
		_panel.position = _panel_home()
	_place_arm()
	_logo.visible = _logo_tex != null and _wide and screen == Screens.MAIN_MENU
	if _logo_tex:
		var logo_w := shown.size.y * 0.62
		var logo_h := logo_w * _logo_tex.get_height() / maxf(1.0, _logo_tex.get_width())
		_logo.size = Vector2(logo_w, logo_h)
		_logo.position = Vector2(shown.position.x + shown.size.y * 0.09, shown.position.y + shown.size.y * 0.07)
	_version.reset_size()
	var below := _panel.size.y * (0.2 if _rig != null else 0.0) + 10.0 # under the bezel
	_version.position = Vector2(_panel_home().x + (_panel.size.x - _version.size.x) * 0.5, _panel_home().y + _panel.size.y + below)
	_credits.reset_size()
	_credits.position = Vector2(shown.position.x + shown.size.y * 0.09, shown.end.y - shown.size.y * 0.12 - _credits.size.y)
	_mods.reset_size()
	_mods.position = Vector2(shown.position.x + shown.size.y * 0.09, _credits.position.y - _mods.size.y - 8)
	if _slots.visible:
		_slots.reset_size()
		_slots.position = Vector2(_panel_home().x - _slots.size.x - 16, clampf(_panel_home().y, 8, size.y - _slots.size.y - 8))
	queue_redraw()


## The arm: from the panel's right edge to the edge of the picture, at the panel's middle.
func _place_arm() -> void:
	var shown := picture_rect()
	var thickness := _panel.size.y * 0.16
	var x0 := _panel.position.x + _panel.size.x * _panel.scale.x - 6
	_arm.visible = _rig == null and _panel.visible and _panel.scale.x > 0.6
	_arm.position = Vector2(x0, _panel.position.y + _panel.size.y * 0.5 - thickness * 0.5)
	_arm.size = Vector2(maxf(0.0, shown.end.x - x0), thickness)


## The original's entrance: the panel turns in edge-on from the right edge (a hinge on its right
## side), then slides left to its place while the arm follows it.
func _swing_in(first: bool) -> void:
	_panel_in = true
	if _swing:
		_swing.kill()
	if _rig != null:
		_panel.hide()
		_panel.scale = Vector2.ONE
		_panel.modulate = Color.WHITE
		_turn = 0.0
		_slide = 0.0
		_update_3d()
		_model.visible = true
		var swing_time := SWING_TIME if first else SWING_TIME * 0.6
		_swing = create_tween()
		_swing.tween_interval(0.25 if first else 0.05)
		_swing.tween_property(self, "_turn", 1.0, swing_time).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
		_swing.tween_property(self, "_slide", 1.0, SLIDE_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN_OUT)
		_swing.tween_callback(func():
			_layout()
			_panel.position = _panel_home()
			_panel.show())
		set_process(true)
		return
	_panel.reset_size()
	var home := _panel_home()
	var shown := picture_rect()
	var hinge_x := shown.end.x - shown.size.y * 0.02
	_panel.show()
	_panel.pivot_offset = Vector2(_panel.size.x, _panel.size.y * 0.5)
	_panel.position = Vector2(hinge_x - _panel.size.x, home.y)
	_panel.scale = Vector2(0.0, 1.0)
	_panel.modulate = Color(0.55, 0.6, 0.55)
	var swing_time := SWING_TIME if first else SWING_TIME * 0.6
	_swing = create_tween()
	_swing.tween_interval(0.25 if first else 0.05)
	_swing.tween_property(_panel, "scale:x", 1.0, swing_time).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_OUT)
	_swing.parallel().tween_property(_panel, "modulate", Color.WHITE, swing_time)
	_swing.tween_property(_panel, "position:x", home.x, SLIDE_TIME).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN_OUT)
	_swing.tween_callback(_layout)
	_swing.set_process_mode(Tween.TWEEN_PROCESS_IDLE)
	_swing.step_finished.connect(func(_i: int): _place_arm())
	set_process(true)


func _swing_out() -> void:
	_panel_in = false
	if _swing:
		_swing.kill()
	if _rig != null:
		_panel.hide()
		_swing = create_tween()
		_swing.tween_property(self, "_slide", 0.0, SLIDE_TIME * 0.6).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
		_swing.tween_property(self, "_turn", 0.0, SWING_TIME * 0.4).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
		_swing.tween_callback(func(): _model.visible = false)
		return
	var shown := picture_rect()
	var hinge_x := shown.end.x - shown.size.y * 0.02
	_swing = create_tween()
	_swing.tween_property(_panel, "position:x", hinge_x - _panel.size.x, SLIDE_TIME * 0.6).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_swing.tween_property(_panel, "scale:x", 0.0, SWING_TIME * 0.4).set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN)
	_swing.tween_callback(func(): _panel.hide())


func _process(_delta: float) -> void:
	if not visible:
		return
	if _rig != null:
		_update_3d()
	elif _panel.visible:
		_place_arm()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if not visible or key == null or not key.pressed or key.echo or screen != Screens.MAIN_MENU:
		return
	if key.keycode == KEY_ESCAPE and _slots.visible:
		_slots.close_slots()
		get_viewport().set_input_as_handled()
