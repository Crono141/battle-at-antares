extends PanelContainer
## Options (WP3.3f; laid out for the themes, WP3.3g): a flyout from the right edge, opened by its
## tab, with the map's controls (the influence view as a three-way switch, Alt+V cycles it; merging
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
signal native_battles_changed(on: bool) # WP3.9: battles in the native scene (preview)
## A right click's help on a row (a HELP.LBX record; the frontend shows it).
signal help_requested(topic: int, at: Control)

const Themes := preload("../themes.gd")
const SLIDE_TIME := 0.25
const WIDTH := 352.0

## Set by the frontend: the galaxy is the current screen, so the keys are live.
var keys_active := false

@onready var influence_buttons: Array[Button] = [$Fit/Box/Influence/Off, $Fit/Box/Influence/Filled, $Fit/Box/Influence/Borders]
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
	var group := ButtonGroup.new()
	for b in influence_buttons:
		b.button_group = group
		var action_name := String(b.get_meta("action"))
		b.pressed.connect(func(): action.emit(action_name))
		Themes.glowing(b)
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
	_add_native_battles_row()
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
	_glow_all()
	_fit.call_deferred()


func open() -> void:
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
	for i in influence_buttons.size():
		influence_buttons[i].set_pressed_no_signal(i == mode) # quietly, so the group doesn't
	_glow_all()


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


func _glow_all() -> void:
	for b in influence_buttons:
		Themes.glow(b)


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
	elif key.keycode == KEY_F4 and not key.alt_pressed:
		action.emit("merge_relocations")
	elif key.keycode == KEY_F9 and not key.alt_pressed:
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


## SYSTEM ZOOM (owner, 2026-11-13): how long the flight into and out of a system takes.
const FLIGHT_CHOICES := [["SLOW", 1.5], ["MEDIUM", 1.0], ["FAST", 0.5], ["INSTANT", 0.0]]
var _flight_buttons: Array[Button] = []


func _add_flight_row() -> void:
	var box: VBoxContainer = $Fit/Box
	var title := Label.new()
	title.theme_type_variation = &"Section"
	title.text = "SYSTEM ZOOM"
	box.add_child(title)
	box.move_child(title, $Fit/Box/ThemeTitle.get_index())
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 4)
	box.add_child(row)
	box.move_child(row, $Fit/Box/ThemeTitle.get_index())
	var group := ButtonGroup.new()
	for choice: Array in FLIGHT_CHOICES:
		var b := Button.new()
		b.text = String(choice[0])
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		b.add_theme_font_size_override("font_size", 12)
		var seconds: float = choice[1]
		b.pressed.connect(func(): flight_changed.emit(seconds))
		Themes.glowing(b)
		row.add_child(b)
		_flight_buttons.append(b)


## AUTO ADDRESS TO-DOS (owner, 2026-11-20): the stock game's way with the turn's to-dos, each
## opened in turn as the last is dealt with, instead of the list.
var todo_auto_button: Button
var native_battles_button: Button # WP3.9


func _add_todo_row() -> void:
	var box: VBoxContainer = $Fit/Box
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var label := Label.new()
	label.text = "AUTO ADDRESS TO-DOS"
	label.theme_type_variation = &"Section"
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.tooltip_text = "Each turn's to-do items open one after another, as the original game does,\ninstead of the TO DO list"
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(label)
	todo_auto_button = Button.new()
	todo_auto_button.toggle_mode = true
	todo_auto_button.text = "OFF"
	todo_auto_button.focus_mode = Control.FOCUS_NONE
	todo_auto_button.custom_minimum_size.x = 64
	todo_auto_button.add_theme_font_size_override("font_size", 12)
	Themes.glowing(todo_auto_button)
	todo_auto_button.toggled.connect(func(on: bool):
		todo_auto_button.text = "ON" if on else "OFF"
		todo_auto_changed.emit(on))
	row.add_child(todo_auto_button)
	box.add_child(row)
	box.move_child(row, $Fit/Box/ThemeTitle.get_index())


func show_todo_auto(on: bool) -> void:
	todo_auto_button.set_pressed_no_signal(on)
	todo_auto_button.text = "ON" if on else "OFF"
	Themes.glow(todo_auto_button)


## The flight length in use, shown armed.
func show_flight(seconds: float) -> void:
	for i in FLIGHT_CHOICES.size():
		_flight_buttons[i].set_pressed_no_signal(is_equal_approx(float(FLIGHT_CHOICES[i][1]), seconds))


## WP3.9: battles in the native scene, a preview (off by default, owner 2026-11-21).
func _add_native_battles_row() -> void:
	var box: VBoxContainer = $Fit/Box
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	var label := Label.new()
	label.text = "NATIVE BATTLES (PREVIEW)"
	label.theme_type_variation = &"Section"
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.tooltip_text = "Space combat in the new battle scene instead of the original screen.\nUnfinished: beams are native; shield hits, missiles, fighters and explosions are placeholders. Takes effect from the next battle."
	label.mouse_filter = Control.MOUSE_FILTER_PASS
	row.add_child(label)
	native_battles_button = Button.new()
	native_battles_button.toggle_mode = true
	native_battles_button.text = "OFF"
	native_battles_button.focus_mode = Control.FOCUS_NONE
	native_battles_button.custom_minimum_size.x = 64
	native_battles_button.add_theme_font_size_override("font_size", 12)
	Themes.glowing(native_battles_button)
	native_battles_button.toggled.connect(func(on: bool):
		native_battles_button.text = "ON" if on else "OFF"
		native_battles_changed.emit(on))
	row.add_child(native_battles_button)
	box.add_child(row)
	box.move_child(row, $Fit/Box/ThemeTitle.get_index())


func show_native_battles(on: bool) -> void:
	native_battles_button.set_pressed_no_signal(on)
	native_battles_button.text = "ON" if on else "OFF"
	Themes.glow(native_battles_button)
