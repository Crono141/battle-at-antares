extends PanelContainer
## Options (WP3.3f; laid out for the themes, WP3.3g): a flyout from the right edge, opened by its
## tab, with the map's controls (the influence view as a drop-down, Alt+V cycles it; merging
## relocations, F4; the distance tool, F9), the game options the legacy galaxy toggles with
## Alt+F1..F8 as ON / OFF switches (the `options` snapshot), the music and sound volumes (the game
## menu's sliders, `set_volume`) and the theme (the enabled theme packages). The keys work while the galaxy is up, flyout open or not.

signal command(dict: Dictionary)
signal action(name: String)
signal closed
signal theme_chosen(id: String)
## The interface scale (percent) changed: `final` once the slider is let go or a number entered.
signal scale_changed(percent: float, final: bool)
## The Automatic switch beside the scale (owner, 2026-11-13).
signal scale_auto_changed(on: bool)
## The system flight's length chosen (seconds; 0 instant).
signal flight_changed(seconds: float)
## AUTO ADDRESS TO-DOS switched (owner, 2026-11-20).
signal todo_auto_changed(on: bool)
## A right click's help on a row (a HELP.LBX record; the frontend shows it).
signal help_requested(topic: int, at: Control)

const Themes := preload("../themes.gd")
const SLIDE_TIME := 0.25
const WIDTH := 352.0

## Set by the frontend: the galaxy is the current screen, so the keys are live.
var keys_active := false

@onready var influence_pick: OptionButton = $Fit/Box/Influence/Pick # Off, Filled, Borders (owner 2026-11-24: a drop-down)
@onready var options_box: VBoxContainer = $Fit/Box/Options
@onready var theme_button: OptionButton = $Fit/Box/Theme
@onready var music_slider: HSlider = $Fit/Box/Music/Slider
@onready var sound_slider: HSlider = $Fit/Box/Sound/Slider
@onready var scale_slider: HSlider = $Fit/Box/Scale/Slider
@onready var scale_value: SpinBox = $Fit/Box/Scale/Value
@onready var scale_auto: Button = $Fit/Box/ScaleAuto/Line/Switch

var _tween: Tween
var _shown_right := 0.0 # offset_right when open: a flyout keeps its right edge by the tabs
var _theme_ids: Array = []


func _ready() -> void:
	$Fit/Box/Header/Close.pressed.connect(close)
	influence_pick.item_selected.connect(func(i: int):
		action.emit(["influence_off", "influence_filled", "influence_borders"][i]))
	show_influence(0)
	$Fit/Box/Map/Relocations.pressed.connect(func():
		close() # the star is picked on the map
		action.emit("merge_relocations"))
	$Fit/Box/Map/Distance.pressed.connect(func(): action.emit("distance"))
	# The game menu's volume sliders (WP3.3g): set when let go, the value shown while dragging.
	for pair in [[music_slider, "music", $Fit/Box/Music/Value], [sound_slider, "sound", $Fit/Box/Sound/Value]]:
		var slider: HSlider = pair[0]
		var key: String = pair[1]
		var value_label: Label = pair[2]
		slider.value_changed.connect(func(v: float): value_label.text = "OFF" if v <= 5.0 else str(int(v)))
		slider.drag_ended.connect(func(changed: bool):
			if changed:
				command.emit({"verb": "set_volume", key: int(slider.value)}))
	# The interface scale (owner, after a 2560x1600 playtest): live while dragging, kept when let
	# go; or typed in.
	# The slider is dragged by hand (owner, 2026-11-13): the scale moves the slider itself, so the
	# pointer is read in window pixels against where the slider was when it was grabbed. Godot's
	# own drag fed the moved slider back into the value and ran away to either end.
	scale_slider.gui_input.connect(_on_scale_slider_input)
	scale_auto.toggled.connect(func(on: bool):
		scale_auto.text = "ON" if on else "OFF"
		scale_auto_changed.emit(on))
	Themes.glowing(scale_auto)
	scale_value.value_changed.connect(func(v: float):
		_quietly(scale_slider, v)
		scale_changed.emit(v, true))
	_add_flight_row()
	_add_todo_row()
	_add_display_rows()
	# Sound first, above the map (owner 2026-11-24).
	var box: VBoxContainer = $Fit/Box
	for i in 3:
		box.move_child([$Fit/Box/SoundTitle, $Fit/Box/Music, $Fit/Box/Sound][i], $Fit/Box/Header.get_index() + 1 + i)
	theme_button.item_selected.connect(func(index: int):
		if index >= 0 and index < _theme_ids.size():
			theme_chosen.emit(_theme_ids[index]))
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()
	elif what == NOTIFICATION_RESIZED and visible and not (_tween and _tween.is_running()):
		# Rows laid out after the slide (or a refresh) widened it: keep the right edge by the tabs.
		position.x = get_parent_area_size().x + _shown_right - size.x


## Beside the tabs, under the top bar.
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_right = -((inset - 4 if inset > 0 else 4) + 34 + gap)
	offset_left = offset_right - WIDTH
	offset_top = get_theme_constant("content_top", "Screen")
	_shown_right = offset_right
	_fit.call_deferred()


func open() -> void:
	_show_display() # Alt+Enter may have changed the window since
	if not visible:
		_slide(true)


func close() -> void:
	if visible:
		_slide(false)
		closed.emit()


## The Alt+F1..F8 options (the `options` snapshot) as rows with an ON / OFF switch.
func show_options(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	if result.has("music"):
		music_slider.set_value_no_signal(float(result.music))
		music_slider.value_changed.emit(music_slider.value)
		sound_slider.set_value_no_signal(float(result.sound))
		sound_slider.value_changed.emit(sound_slider.value)
	for child in options_box.get_children():
		options_box.remove_child(child) # out at once: freed rows still counted towards the height
		child.queue_free()
	for option: Dictionary in result.options:
		var id := int(option.option)
		var row := PanelContainer.new()
		row.theme_type_variation = &"Row"
		var line := HBoxContainer.new()
		row.add_child(line)
		var label := Label.new()
		label.text = option.name
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		label.tooltip_text = "Alt+F%d" % (id + 1)
		label.mouse_filter = Control.MOUSE_FILTER_PASS
		line.add_child(label)
		var switch := Button.new()
		switch.theme_type_variation = &"Toggle"
		switch.toggle_mode = true
		switch.focus_mode = Control.FOCUS_NONE
		switch.custom_minimum_size = Vector2(60, 26)
		switch.size_flags_vertical = Control.SIZE_SHRINK_CENTER
		switch.add_theme_font_size_override("font_size", 12)
		switch.set_pressed_no_signal(option.on)
		switch.text = "ON" if option.on else "OFF"
		switch.toggled.connect(func(_on: bool): command.emit({"verb": "toggle_option", "option": id}))
		Themes.glowing(switch)
		line.add_child(switch)
		# The settings page's right click (EVANHELP options list): records 429 on, by option.
		row.mouse_filter = Control.MOUSE_FILTER_PASS
		row.gui_input.connect(func(event: InputEvent):
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
				help_requested.emit(429 + id, row)
				row.accept_event())
		options_box.add_child(row)
	_fit.call_deferred()


## Back to the height of what it holds (rows rebuilt by a snapshot, a theme change): a
## PanelContainer grows by itself but never shrinks (owner: it stayed past the frame).
func _fit() -> void:
	size.y = get_combined_minimum_size().y


func show_influence(mode: int) -> void:
	influence_pick.select(mode) # select() doesn't emit item_selected


## The scale slider's drag: the track in window pixels when grabbed (empty while not dragged).
var _scale_drag := {}


func _on_scale_slider_input(event: InputEvent) -> void:
	var press := event as InputEventMouseButton
	if not scale_slider.editable:
		return # Automatic: the slider only shows the scale in use
	if press and press.button_index == MOUSE_BUTTON_LEFT and press.pressed:
		var factor := get_window().content_scale_factor
		var track := scale_slider.get_global_rect()
		_scale_drag = {"left": track.position.x * factor, "width": maxf(track.size.x * factor, 1.0)}
		_drag_scale(press.global_position.x * factor, false)
		scale_slider.accept_event()
	elif event is InputEventMouseMotion or event is InputEventMouseButton:
		scale_slider.accept_event() # Godot's own drag stays off


func _input(event: InputEvent) -> void:
	if _scale_drag.is_empty():
		return
	# Event positions are in the interface's (scaled) pixels; times the scale, window pixels.
	var factor := get_window().content_scale_factor
	var motion := event as InputEventMouseMotion
	if motion:
		_drag_scale(motion.position.x * factor, false)
		get_viewport().set_input_as_handled()
	var release := event as InputEventMouseButton
	if release and release.button_index == MOUSE_BUTTON_LEFT and not release.pressed:
		_drag_scale(release.position.x * factor, true)
		_scale_drag = {} # the release goes on, so the interface lets go of the slider too


## Window pixels along the grabbed track to a scale; `final` when let go.
func _drag_scale(window_x: float, final: bool) -> void:
	var ratio := clampf((window_x - float(_scale_drag.left)) / float(_scale_drag.width), 0.0, 1.0)
	var percent := snappedf(lerpf(scale_slider.min_value, scale_slider.max_value, ratio), scale_slider.step)
	if final or percent != scale_slider.value:
		_quietly(scale_slider, percent)
		_quietly(scale_value, percent)
		scale_changed.emit(percent, final)


## The scale in use (percent) and its limits, without sending it back.
func show_scale(percent: float, largest := 300.0, smallest := 75.0, automatic := false) -> void:
	scale_auto.set_pressed_no_signal(automatic)
	scale_auto.text = "ON" if automatic else "OFF"
	scale_slider.editable = not automatic
	scale_value.editable = not automatic
	scale_slider.modulate.a = 0.5 if automatic else 1.0
	scale_value.modulate.a = 0.5 if automatic else 1.0
	# Quietly, limits too: a limit that clamps the value would otherwise send the clamped value
	# back as the player's choice (a small window saved its largest scale over theirs).
	for range: Range in [scale_slider, scale_value]:
		range.set_block_signals(true)
		range.min_value = smallest
		range.max_value = largest
		range.value = percent
		range.set_block_signals(false)
	if not _keeping_scale_in_view:
		_keeping_scale_in_view = true
		_keep_scale_in_view.call_deferred()


## Options scroll when the window is short: the scale stays in view while it changes (one at a
## time: a window drag shows a new scale many times a frame).
var _keeping_scale_in_view := false


func _keep_scale_in_view() -> void:
	await get_tree().process_frame # the scroll has refitted to the new scale
	_keeping_scale_in_view = false
	if is_inside_tree() and visible:
		($Fit as ScrollContainer).ensure_control_visible($Fit/Box/Scale)


## Sets a value without its signal. Not set_value_no_signal: that leaves a SpinBox's text as it
## was, so the box showed one scale while another was in use (owner, 2026-11-13).
static func _quietly(range: Range, value: float) -> void:
	range.set_block_signals(true)
	range.value = value
	range.set_block_signals(false)


## The enabled theme packages, the one in use selected.
func show_themes(themes: Array, current: String) -> void:
	theme_button.clear()
	_theme_ids.clear()
	for entry: Dictionary in themes:
		theme_button.add_item(String(entry.name))
		_theme_ids.append(entry.id)
	if themes.is_empty():
		theme_button.add_item("Plain (no theme package enabled)")
		theme_button.disabled = true
	else:
		theme_button.disabled = false
		theme_button.select(maxi(0, _theme_ids.find(current)))


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if not key or not key.pressed or key.echo:
		return
	if visible and key.keycode == KEY_ESCAPE:
		close()
	elif not keys_active:
		return
	elif key.alt_pressed and key.keycode == KEY_V:
		action.emit("influence")
	elif key.keycode == KEY_F4 and not key.alt_pressed and not $Fit/Box/Map/Relocations.disabled:
		action.emit("merge_relocations")
	elif key.keycode == KEY_F9 and not key.alt_pressed and not $Fit/Box/Map/Distance.disabled:
		action.emit("distance")
	elif key.alt_pressed and key.keycode >= KEY_F1 and key.keycode <= KEY_F8:
		command.emit({"verb": "toggle_option", "option": key.keycode - KEY_F1})
	else:
		return
	get_viewport().set_input_as_handled()


## Slides in from (or out to) the right edge.
func _slide(opening: bool) -> void:
	if _tween:
		_tween.kill()
	var parent_width := get_parent_area_size().x
	var shown_x := parent_width + _shown_right - size.x # right-aligned, however wide its rows
	if opening:
		show()
		position.x = parent_width
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	if opening:
		_tween.finished.connect(func(): position.x = get_parent_area_size().x + _shown_right - size.x)
	_tween.tween_property(self, "position:x", shown_x if opening else parent_width, SLIDE_TIME)
	if not opening:
		_tween.tween_callback(hide)


## "F5", "Alt+V" as a Shortcut (the map controls' keys).
static func shortcut_for(text: String) -> Shortcut:
	var key := InputEventKey.new()
	for part in text.split("+", false):
		match part:
			"Alt":
				key.alt_pressed = true
			"Ctrl":
				key.ctrl_pressed = true
			"Shift":
				key.shift_pressed = true
			_:
				key.keycode = OS.find_keycode_from_string(part)
	var shortcut := Shortcut.new()
	shortcut.events = [key]
	return shortcut


## SYSTEM ZOOM (owner, 2026-11-13): how long the flight into and out of a system takes; a
## drop-down as the theme's (owner 2026-11-24).
const FLIGHT_CHOICES := [["Slow", 1.5], ["Medium", 1.0], ["Fast", 0.5], ["Instant", 0.0]]
var _flight_pick: OptionButton


var _flight_title: Label


func _add_flight_row() -> void:
	var box: VBoxContainer = $Fit/Box
	var title := Label.new()
	title.theme_type_variation = &"Section"
	title.text = "SYSTEM ZOOM"
	_flight_title = title
	box.add_child(title)
	box.move_child(title, $Fit/Box/ThemeTitle.get_index())
	_flight_pick = OptionButton.new()
	_flight_pick.focus_mode = Control.FOCUS_NONE
	_flight_pick.custom_minimum_size = Vector2(0, 32)
	_flight_pick.tooltip_text = "How long the flight into and out of a system takes"
	for choice: Array in FLIGHT_CHOICES:
		_flight_pick.add_item(String(choice[0]))
	_flight_pick.item_selected.connect(func(i: int): flight_changed.emit(float(FLIGHT_CHOICES[i][1])))
	box.add_child(_flight_pick)
	box.move_child(_flight_pick, $Fit/Box/ThemeTitle.get_index())


## DISPLAY (owner, 2026-11-27; in game and on the title): the window mode, V-sync and a frame
## limit, applied at once and kept in the interface's settings (user://antares-ui.cfg, "display"),
## which the frontend applies at start. No resolution or refresh rate lists: the interface draws at
## the window's own size and has its own scale; the launcher sets the windowed size. A choice made
## here wins over the launcher's "full screen" from then on.
const WINDOW_MODES := [["Windowed", Window.MODE_WINDOWED], ["Full screen", Window.MODE_FULLSCREEN],
	["Exclusive full screen", Window.MODE_EXCLUSIVE_FULLSCREEN]]
const FRAME_LIMITS := [["No limit", 0], ["30 fps", 30], ["60 fps", 60], ["120 fps", 120], ["144 fps", 144], ["240 fps", 240]]
var _mode_pick: OptionButton
var _vsync_button: Button
var _limit_pick: OptionButton


func _add_display_rows() -> void:
	var box: VBoxContainer = $Fit/Box
	var before: int = $Fit/Box/ScaleTitle.get_index()
	var title := Label.new()
	title.theme_type_variation = &"Section"
	title.text = "DISPLAY"
	_mode_pick = OptionButton.new()
	_mode_pick.focus_mode = Control.FOCUS_NONE
	_mode_pick.custom_minimum_size = Vector2(0, 32)
	_mode_pick.tooltip_text = "Windowed, full screen (borderless) or exclusive full screen (Alt+Enter switches too)"
	for choice: Array in WINDOW_MODES:
		_mode_pick.add_item(String(choice[0]))
	_mode_pick.item_selected.connect(func(i: int): _set_display("mode", int(WINDOW_MODES[i][1])))
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	row.add_child(line)
	var label := Label.new()
	label.text = "V-sync"
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.tooltip_text = "Wait for the display between frames: no tearing"
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	line.add_child(label)
	_vsync_button = Button.new()
	_vsync_button.theme_type_variation = &"Toggle"
	_vsync_button.toggle_mode = true
	_vsync_button.focus_mode = Control.FOCUS_NONE
	_vsync_button.custom_minimum_size = Vector2(60, 26)
	_vsync_button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	_vsync_button.add_theme_font_size_override("font_size", 12)
	Themes.glowing(_vsync_button)
	_vsync_button.toggled.connect(func(on: bool):
		_vsync_button.text = "ON" if on else "OFF"
		_set_display("vsync", on))
	line.add_child(_vsync_button)
	_limit_pick = OptionButton.new()
	_limit_pick.focus_mode = Control.FOCUS_NONE
	_limit_pick.custom_minimum_size = Vector2(0, 32)
	_limit_pick.tooltip_text = "The most frames a second"
	for choice: Array in FRAME_LIMITS:
		_limit_pick.add_item(String(choice[0]))
	_limit_pick.item_selected.connect(func(i: int): _set_display("max_fps", int(FRAME_LIMITS[i][1])))
	var items: Array[Control] = [title, _mode_pick, row, _limit_pick]
	for i in items.size():
		box.add_child(items[i])
		box.move_child(items[i], before + i)
	_show_display()


func _show_display() -> void:
	if _mode_pick == null:
		return
	var mode := get_window().mode
	var index := 0
	for i in WINDOW_MODES.size():
		if int(WINDOW_MODES[i][1]) == mode:
			index = i
	_mode_pick.select(index)
	var vsync := DisplayServer.window_get_vsync_mode() != DisplayServer.VSYNC_DISABLED
	_vsync_button.set_pressed_no_signal(vsync)
	_vsync_button.text = "ON" if vsync else "OFF"
	Themes.glow(_vsync_button)
	var limit := 0
	for i in FRAME_LIMITS.size():
		if int(FRAME_LIMITS[i][1]) == Engine.max_fps:
			limit = i
	_limit_pick.select(limit)


func _set_display(key: String, value: Variant) -> void:
	Themes.save_value("display", key, value)
	apply_display(get_window(), key, value)


static func apply_display(window: Window, key: String, value: Variant) -> void:
	match key:
		"mode":
			window.mode = int(value) as Window.Mode
		"vsync":
			DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_ENABLED if bool(value) else DisplayServer.VSYNC_DISABLED)
		"max_fps":
			Engine.max_fps = int(value)


## At start: the display choices the player made here, if any.
static func apply_saved_display(window: Window) -> void:
	# Only what was saved (a null default to ConfigFile.get_value counts as none: it logged an error
	# for each key on a first start, owner's log 2026-11-27).
	var cfg := ConfigFile.new()
	if cfg.load(Themes.SETTINGS) == OK:
		for key in ["mode", "vsync", "max_fps"]:
			if cfg.has_section_key("display", key):
				apply_display(window, key, cfg.get_value("display", key))
	print("display: window mode %d, vsync %s, frame limit %d" % [window.mode,
		DisplayServer.window_get_vsync_mode() != DisplayServer.VSYNC_DISABLED, Engine.max_fps])


## AUTO ADDRESS TO-DOS (owner, 2026-11-20): the stock game's way with the turn's to-dos, each
## opened in turn as the last is dealt with, instead of the list.
var todo_auto_button: Button


func _add_todo_row() -> void:
	var box: VBoxContainer = $Fit/Box
	# A row as the game options' (owner 2026-11-24): its name in plain text, an ON / OFF switch;
	# under them, with the game's own options.
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var line := HBoxContainer.new()
	row.add_child(line)
	var label := Label.new()
	label.text = "Auto address to-dos"
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.tooltip_text = "Each turn's to-do items open one after another, as the original game does,\ninstead of the TO DO list"
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	line.add_child(label)
	todo_auto_button = Button.new()
	todo_auto_button.theme_type_variation = &"Toggle"
	todo_auto_button.toggle_mode = true
	todo_auto_button.text = "OFF"
	todo_auto_button.focus_mode = Control.FOCUS_NONE
	todo_auto_button.custom_minimum_size = Vector2(60, 26)
	todo_auto_button.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	todo_auto_button.add_theme_font_size_override("font_size", 12)
	Themes.glowing(todo_auto_button)
	todo_auto_button.toggled.connect(func(on: bool):
		todo_auto_button.text = "ON" if on else "OFF"
		todo_auto_changed.emit(on))
	line.add_child(todo_auto_button)
	box.add_child(row)
	box.move_child(row, options_box.get_index() + 1)


func show_todo_auto(on: bool) -> void:
	todo_auto_button.set_pressed_no_signal(on)
	todo_auto_button.text = "ON" if on else "OFF"
	Themes.glow(todo_auto_button)


## The flight length in use, shown armed.
func show_flight(seconds: float) -> void:
	var best := 0
	for i in FLIGHT_CHOICES.size():
		if absf(float(FLIGHT_CHOICES[i][1]) - seconds) < absf(float(FLIGHT_CHOICES[best][1]) - seconds):
			best = i
	_flight_pick.select(best)


## Opened from the title screen (WP3.13): no game, so the map's rows, the game options (they
## change a game: `toggle_option` runs on the galaxy only), the system zoom and the to-dos switch
## are left out; sound, display, interface scale and theme stay.
func set_title_mode(on: bool) -> void:
	var rows: Array[Control] = [$Fit/Box/MapTitle, $Fit/Box/Influence, $Fit/Box/Map, $Fit/Box/GameTitle, options_box,
		_flight_title, _flight_pick, todo_auto_button.get_parent().get_parent() as Control]
	for row in rows:
		row.visible = not on
	_fit.call_deferred()


## While a battle's question is up (owner 2026-11-24): MERGE RELOC. and DISTANCE greyed out.
func set_map_tools_enabled(on: bool) -> void:
	for b: Button in [$Fit/Box/Map/Relocations, $Fit/Box/Map/Distance]:
		b.disabled = not on
