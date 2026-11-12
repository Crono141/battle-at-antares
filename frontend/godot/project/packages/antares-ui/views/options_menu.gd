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

const Themes := preload("../themes.gd")
const SLIDE_TIME := 0.25
const WIDTH := 352.0

## Set by the frontend: the galaxy is the current screen, so the keys are live.
var keys_active := false

@onready var influence_buttons: Array[Button] = [$Box/Influence/Off, $Box/Influence/Filled, $Box/Influence/Borders]
@onready var options_box: VBoxContainer = $Box/Options
@onready var theme_button: OptionButton = $Box/Theme
@onready var music_slider: HSlider = $Box/Music/Slider
@onready var sound_slider: HSlider = $Box/Sound/Slider

var _tween: Tween
var _shown_right := 0.0 # offset_right when open: a flyout keeps its right edge by the tabs
var _theme_ids: Array = []


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)
	var group := ButtonGroup.new()
	for b in influence_buttons:
		b.button_group = group
		var action_name := String(b.get_meta("action"))
		b.pressed.connect(func(): action.emit(action_name))
		Themes.glowing(b)
	show_influence(0)
	$Box/Map/Relocations.pressed.connect(func():
		close() # the star is picked on the map
		action.emit("merge_relocations"))
	$Box/Map/Distance.pressed.connect(func(): action.emit("distance"))
	# The game menu's volume sliders (WP3.3g): set when let go, the value shown while dragging.
	for pair in [[music_slider, "music", $Box/Music/Value], [sound_slider, "sound", $Box/Sound/Value]]:
		var slider: HSlider = pair[0]
		var key: String = pair[1]
		var value_label: Label = pair[2]
		slider.value_changed.connect(func(v: float): value_label.text = "OFF" if v <= 5.0 else str(int(v)))
		slider.drag_ended.connect(func(changed: bool):
			if changed:
				command.emit({"verb": "set_volume", key: int(slider.value)}))
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
		options_box.add_child(row)


func show_influence(mode: int) -> void:
	for i in influence_buttons.size():
		influence_buttons[i].set_pressed_no_signal(i == mode) # quietly, so the group doesn't
	_glow_all()


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
