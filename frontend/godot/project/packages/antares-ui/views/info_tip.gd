extends PanelContainer
## The right click's help pane (info_popup.gd opens it). One at a time, over everything. A left
## click anywhere only closes it (owner, 2026-11-13: it should clear the tip and nothing else); a
## right click on it only closes it too; a right click elsewhere closes it and goes on, so another
## item's help swaps in.


func _input(event: InputEvent) -> void:
	var click := event as InputEventMouseButton
	if click == null or not click.pressed:
		return
	if click.button_index == MOUSE_BUTTON_LEFT:
		get_viewport().set_input_as_handled()
		close()
	elif click.button_index == MOUSE_BUTTON_RIGHT:
		if get_global_rect().has_point(get_global_mouse_position()):
			get_viewport().set_input_as_handled() # on the tip: only closes (what's under it is hidden)
		close() # outside it the click goes on: its target may open its own help


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and key.keycode == KEY_ESCAPE:
		get_viewport().set_input_as_handled()
		close()


func close() -> void:
	if is_queued_for_deletion():
		return
	# Out of the way at once (a right click outside it goes on to its target this same frame).
	hide()
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	var layer := get_parent()
	queue_free()
	if layer is CanvasLayer:
		layer.queue_free()
