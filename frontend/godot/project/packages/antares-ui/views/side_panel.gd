extends PanelContainer
## A panel of the right sidebar's tabs (the empire panels, owner 2026-11-14: "all should appear
## flying out from the right"): open_in(rect) slides it in from the screen's right edge into
## `rect` (right edge against the side tabs, from under the bar to above the map controls);
## close() slides it back out and emits `closed`. Esc and its X close it.

signal closed

const SLIDE_TIME := 0.25

var _tween: Tween
var _closing := false
var _rect := Rect2()


## Slides in to `rect` (its height exactly; its width at least the contents').
func open_in(rect: Rect2) -> void:
	_rect = rect
	custom_minimum_size = Vector2(rect.size.x, rect.size.y)
	reset_size()
	var target := rect.end.x - size.x
	position.y = rect.position.y
	if _tween:
		_tween.kill()
	if not visible or _closing:
		position.x = get_viewport_rect().size.x
	_closing = false
	show()
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:x", target, SLIDE_TIME)


## Where it rests when open (the galaxy fits left of this).
func rest_left() -> float:
	return _rect.end.x - maxf(size.x, custom_minimum_size.x)


func close() -> void:
	if not visible or _closing:
		return
	if _tween:
		_tween.kill()
	_closing = true
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_property(self, "position:x", get_viewport_rect().size.x, SLIDE_TIME)
	_tween.tween_callback(func():
		hide()
		_closing = false)
	closed.emit()


## Hidden at once (a legacy screen took over).
func dismiss() -> void:
	if visible:
		if _tween:
			_tween.kill()
		hide()
		_closing = false
		closed.emit()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and not _closing and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
