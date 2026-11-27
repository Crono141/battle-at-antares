extends PanelContainer
## The game menu (WP3.3f, owner's layout): drops down from the top bar's Game button with what the
## legacy Game popup offers, but its settings (the Options flyout has them, WP3.3g). Save and Load
## show the ten slots here (owner, 2026-11-13: the legacy popup brought the legacy galaxy up): the
## `saves` query, then `save_game` (a description typed, or the game's own) or `load_game`. New
## Game and Quit ask the game's own question first (`leave_game`), answered in the native dialog.

signal command(dict: Dictionary)
signal closed

const SLIDE_TIME := 0.2

var _tween: Tween
var _closing := false


const SaveSlots := preload("save_slots.gd")

var _mode := "" # "", "save" or "load": the slots shown beside the menu
var _slots: PanelContainer # the shared slot list (save_slots.gd), beside the menu (owner, 2026-11-13: both in sight)


func _ready() -> void:
	_slots = SaveSlots.new()
	_slots.hide()
	_slots.command.connect(func(dict: Dictionary): command.emit(dict))
	_slots.finished.connect(close)
	_slots.back.connect(_back_to_menu)
	_slots.refused.connect(func(message: String): show_result({"ok": false, "message": message}))
	_slots.visibility_changed.connect(func(): _place_slots.call_deferred())
	_slots.resized.connect(func(): _place_slots.call_deferred())
	for button: Button in $Fit/Box.get_children().filter(func(child: Node) -> bool: return child is Button):
		if button.has_meta("page"):
			var mode := "save" if int(button.get_meta("page")) == 3 else "load"
			button.toggle_mode = true # armed while its slots are up
			button.pressed.connect(func():
				if _mode == mode:
					_back_to_menu()
				else:
					_show_mode(mode))
		elif button.has_meta("leave"):
			var to := String(button.get_meta("leave"))
			button.pressed.connect(func():
				close()
				command.emit({"verb": "leave_game", "to": to, "_reply_to": "show_result"}))


## Save or Load: the shared slot list beside the menu's buttons.
func _show_mode(mode: String) -> void:
	_mode = mode
	if _slots.get_parent() == null:
		get_parent().add_child(_slots)
	_slots.theme = theme
	_arm_buttons()
	_slots.open_mode(mode)


## Replies for the slot list come back to this view (it sent the commands).
func show_saves(result: Dictionary) -> void:
	_slots.show_saves(result)
	_slots.move_to_front()


func saved(result: Dictionary) -> void:
	_slots.saved(result)


## The slots' panel to the right of the menu, tops aligned.
func _place_slots() -> void:
	if _slots.visible:
		_slots.position = Vector2(position.x + size.x + 6.0, position.y)
		_slots.move_to_front()


func _arm_buttons() -> void:
	for button: Button in $Fit/Box.get_children().filter(func(child: Node) -> bool: return child is Button and child.has_meta("page")):
		var mode := "save" if int(button.get_meta("page")) == 3 else "load"
		button.set_pressed_no_signal(_mode == mode)


## The slots' panel is part of the menu (a click there doesn't close it).
func owns_point(at: Vector2) -> bool:
	return _slots.visible and _slots.get_global_rect().has_point(at)


func _back_to_menu() -> void:
	_mode = ""
	_slots.close_slots()
	_arm_buttons()


## A refusal ("can't save now") comes back in the menu.
func show_result(result: Dictionary) -> void:
	if result.get("ok", false):
		return
	$Fit/Box/Note.text = String(result.get("message", result.get("error", "")))
	$Fit/Box/Note.show()
	open_at(position.x)


## Drops down below the bar, its left edge at `x`.
func open_at(x: float) -> void:
	if visible and not _closing:
		return
	reset_size()
	position.x = x
	if _tween:
		_tween.kill()
	if not visible:
		position.y = -size.y
	_closing = false
	show()
	var top := float(get_theme_constant("content_top", "Screen"))
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:y", top, SLIDE_TIME)


func close() -> void:
	if not visible or _closing:
		return
	$Fit/Box/Note.hide()
	if _mode != "":
		_back_to_menu() # the slots go with the menu
	if _tween:
		_tween.kill()
	_closing = true
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_property(self, "position:y", -size.y, SLIDE_TIME)
	_tween.tween_callback(func():
		hide()
		_closing = false)
	closed.emit()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		if _mode != "":
			_back_to_menu() # Esc: back to the menu first
		else:
			close()
		get_viewport().set_input_as_handled()
