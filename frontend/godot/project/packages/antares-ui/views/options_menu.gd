extends PanelContainer
## Options (WP3.3f, owner's layout): a flyout from the right edge, opened by its tab, with the map's
## influence view (Alt+V), merging relocations (F4) and the game options the legacy galaxy toggles
## with Alt+F1..F8 (the `options` snapshot). The keys work while the galaxy is up, flyout open or not.

signal command(dict: Dictionary)
signal action(name: String)
signal closed

const SLIDE_TIME := 0.25
const INFLUENCE_NAMES := ["Influence: off", "Influence: filled", "Influence: borders"]

## Set by the frontend: the galaxy is the current screen, so the keys are live.
var keys_active := false

@onready var influence_button: Button = $Box/Influence
@onready var options_box: VBoxContainer = $Box/Options

var _tween: Tween
var _shown_left := 0.0


func _ready() -> void:
	_shown_left = offset_left
	$Box/Header/Close.pressed.connect(close)
	influence_button.pressed.connect(func(): action.emit("influence"))
	$Box/Relocations.pressed.connect(func():
		close() # the star is picked on the map
		action.emit("merge_relocations"))


func open() -> void:
	if not visible:
		_slide(true)


func close() -> void:
	if visible:
		_slide(false)
		closed.emit()


## The Alt+F1..F8 options (the `options` snapshot) as check buttons.
func show_options(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	for child in options_box.get_children():
		child.queue_free()
	for option: Dictionary in result.options:
		var id := int(option.option)
		var check := CheckButton.new()
		check.text = option.name
		check.focus_mode = Control.FOCUS_NONE
		check.tooltip_text = "Alt+F%d" % (id + 1)
		check.set_pressed_no_signal(option.on)
		check.toggled.connect(func(_on: bool): command.emit({"verb": "toggle_option", "option": id}))
		options_box.add_child(check)


func show_influence(mode: int) -> void:
	influence_button.text = INFLUENCE_NAMES[mode]


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
	var shown_x := parent_width + _shown_left
	if opening:
		show()
		position.x = parent_width
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
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
