extends PanelContainer
## The game menu (WP3.3f, owner's layout): drops down from the top bar's Game button with what the
## legacy Game popup offers. Save, Load and Settings open that page of the popup (the legacy screen
## for now) and come back to the galaxy; New Game and Quit ask the game's own question first
## (`leave_game`), answered in the native dialog.

signal command(dict: Dictionary)
signal closed

const SLIDE_TIME := 0.2

var _tween: Tween
var _closing := false


func _ready() -> void:
	for button: Button in $Box.get_children().filter(func(child: Node) -> bool: return child is Button):
		if button.has_meta("page"):
			var page: int = button.get_meta("page")
			button.pressed.connect(func():
				close()
				command.emit({"verb": "open", "screen": Screens.GAME, "page": page, "_reply_to": "show_result"}))
		elif button.has_meta("leave"):
			var to := String(button.get_meta("leave"))
			button.pressed.connect(func():
				close()
				command.emit({"verb": "leave_game", "to": to, "_reply_to": "show_result"}))


## A refusal ("can't save now") comes back in the menu.
func show_result(result: Dictionary) -> void:
	if result.get("ok", false):
		return
	$Box/Note.text = String(result.get("message", result.get("error", "")))
	$Box/Note.show()
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
	$Box/Note.hide()
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
		close()
		get_viewport().set_input_as_handled()
