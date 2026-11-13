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


const SLOT_WIDTH := 520.0

var _mode := "" # "", "save" or "load": the slots shown instead of the menu
var _slots_box: VBoxContainer
var _name_edit: LineEdit
var _picked := -1 # save: the slot picked, saved with the description typed


var _slots_panel: PanelContainer # beside the menu (owner, 2026-11-13: both in sight)


func _ready() -> void:
	_slots_panel = PanelContainer.new()
	_slots_panel.theme_type_variation = &"Pane"
	_slots_panel.hide()
	_slots_box = VBoxContainer.new()
	_slots_box.add_theme_constant_override("separation", 4)
	_slots_panel.add_child(_slots_box)
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


## Save or Load: the slots from the `saves` query, in place of the menu's buttons.
func _show_mode(mode: String) -> void:
	_mode = mode
	_picked = -1
	command.emit({"verb": "query", "what": "saves", "_reply_to": "show_saves"})


func show_saves(result: Dictionary) -> void:
	if not result.get("ok", false) or _mode == "":
		return
	if _mode == "save" and not result.get("can_save", true):
		_back_to_menu()
		show_result({"ok": false, "message": "You can't save the game now."})
		return
	_arm_buttons()
	for child in _slots_box.get_children():
		_slots_box.remove_child(child)
		child.queue_free()
	if _slots_panel.get_parent() == null:
		get_parent().add_child(_slots_panel)
	_slots_panel.theme = theme
	_slots_panel.show()
	var title := Label.new()
	title.theme_type_variation = &"Name"
	title.text = "SAVE GAME" if _mode == "save" else "LOAD GAME"
	_slots_box.add_child(title)
	var active := int(result.get("active", -1))
	for slot: Dictionary in result.get("slots", []):
		_slots_box.add_child(_slot_row(slot, active))
	if _mode == "save":
		_name_edit = LineEdit.new()
		_name_edit.placeholder_text = "Description (blank: the game's own)"
		_name_edit.max_length = 36
		_name_edit.custom_minimum_size = Vector2(SLOT_WIDTH, 32)
		_name_edit.text_submitted.connect(func(_t: String): _save())
		_slots_box.add_child(_name_edit)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 6)
	if _mode == "save":
		var save := Button.new()
		save.name = "SaveButton"
		save.text = "SAVE"
		save.focus_mode = Control.FOCUS_NONE
		save.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		save.custom_minimum_size.y = 34
		save.pressed.connect(_save)
		buttons.add_child(save)
	var back := Button.new()
	back.text = "BACK"
	back.focus_mode = Control.FOCUS_NONE
	back.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	back.custom_minimum_size.y = 34
	back.pressed.connect(_back_to_menu)
	buttons.add_child(back)
	_slots_box.add_child(buttons)
	if _mode == "save" and active >= 0 and active < 9:
		_pick(active, result.slots[active])
	_slots_panel.reset_size()
	_place_slots.call_deferred()


## The slots' panel to the right of the menu, tops aligned.
func _place_slots() -> void:
	if _slots_panel.visible:
		_slots_panel.position = Vector2(position.x + size.x + 6.0, position.y)
		_slots_panel.move_to_front()


func _arm_buttons() -> void:
	for button: Button in $Fit/Box.get_children().filter(func(child: Node) -> bool: return child is Button and child.has_meta("page")):
		var mode := "save" if int(button.get_meta("page")) == 3 else "load"
		button.set_pressed_no_signal(_mode == mode)


## The slots' panel is part of the menu (a click there doesn't close it).
func owns_point(at: Vector2) -> bool:
	return _slots_panel.visible and _slots_panel.get_global_rect().has_point(at)


## One slot: number, description, stardate and file date; a click loads it, or picks it to save.
func _slot_row(slot: Dictionary, active: int) -> Control:
	var index := int(slot.slot)
	var state := String(slot.get("state", "empty"))
	var row := Button.new()
	row.flat = true
	row.focus_mode = Control.FOCUS_NONE
	row.theme_type_variation = &"ListItem"
	row.alignment = HORIZONTAL_ALIGNMENT_LEFT
	row.custom_minimum_size = Vector2(SLOT_WIDTH, 30)
	row.toggle_mode = _mode == "save"
	row.set_meta("slot", index)
	var text := "%2d.  " % (index + 1)
	if state == "ok":
		text += String(slot.get("description", ""))
		var stardate := int(slot.get("stardate", 0))
		if stardate > 0:
			text += "   ·   %d.%d" % [stardate / 10, stardate % 10]
		if String(slot.get("date", "")) != "":
			text += "   ·   " + String(slot.date)
	elif state == "empty":
		text += "Empty"
	elif state == "network":
		text += "A network game (load it from the main menu)"
	else:
		text += "* Invalid *"
	row.text = text
	if index == active:
		row.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "ListItem"))
	if _mode == "load":
		row.disabled = state != "ok"
		row.pressed.connect(func():
			close()
			command.emit({"verb": "load_game", "slot": index, "_reply_to": "show_result"}))
	else:
		row.disabled = index == 9 # the auto save
		row.pressed.connect(func(): _pick(index, slot))
	return row


func _pick(index: int, slot: Dictionary) -> void:
	_picked = index
	for row in _slots_box.get_children():
		if row is Button and row.has_meta("slot"):
			(row as Button).set_pressed_no_signal(int(row.get_meta("slot")) == index)
	if _name_edit:
		var current := String(slot.get("description", "")) if String(slot.get("state", "")) == "ok" else ""
		_name_edit.text = current
		_name_edit.grab_focus()
		_name_edit.caret_column = current.length()


func _save() -> void:
	if _picked < 0:
		return
	var description := _name_edit.text.strip_edges() if _name_edit else ""
	command.emit({"verb": "save_game", "slot": _picked, "description": description, "_reply_to": "_saved"})


func _saved(result: Dictionary) -> void:
	if result.get("ok", false):
		close()
	else:
		show_result(result)


func _back_to_menu() -> void:
	_mode = ""
	_slots_panel.hide()
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
