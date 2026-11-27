extends PanelContainer
## The ten save slots (WP3.3f; one shared scene since WP3.13, owner 2026-11-27: the title screen's
## Load Game is the same page as the game menu's). `open_mode("load")` lists them, and a click
## loads one; `open_mode("save")` picks a slot and a description. The owner view forwards
## `command` and passes the replies back through show_saves / show_result / saved.

signal command(dict: Dictionary)
## A load or a save was sent: the owner closes.
signal finished
## BACK (or Esc): the owner returns to its menu.
signal back
## A refusal for the owner to show ("can't save now", a load that failed).
signal refused(message: String)

const SLOT_WIDTH := 520.0

var mode := "" # "", "save" or "load"
var _box: VBoxContainer
var _name_edit: LineEdit
var _picked := -1


func _init() -> void:
	theme_type_variation = &"Pane"
	_box = VBoxContainer.new()
	_box.add_theme_constant_override("separation", 4)
	add_child(_box)


## Asks for the slots; they show when the reply comes (show_saves).
func open_mode(new_mode: String) -> void:
	mode = new_mode
	_picked = -1
	command.emit({"verb": "query", "what": "saves", "_reply_to": "show_saves"})


func close_slots() -> void:
	mode = ""
	hide()


func show_saves(result: Dictionary) -> void:
	if not result.get("ok", false) or mode == "":
		return
	if mode == "save" and not result.get("can_save", true):
		close_slots()
		back.emit()
		refused.emit("You can't save the game now.")
		return
	for child in _box.get_children():
		_box.remove_child(child)
		child.queue_free()
	var title := Label.new()
	title.theme_type_variation = &"Name"
	title.text = "SAVE GAME" if mode == "save" else "LOAD GAME"
	_box.add_child(title)
	var active := int(result.get("active", -1))
	for slot: Dictionary in result.get("slots", []):
		_box.add_child(_slot_row(slot, active))
	_name_edit = null
	if mode == "save":
		_name_edit = LineEdit.new()
		_name_edit.placeholder_text = "Description (blank: the game's own)"
		_name_edit.max_length = 36
		_name_edit.custom_minimum_size = Vector2(SLOT_WIDTH, 32)
		_name_edit.text_submitted.connect(func(_t: String): _save())
		_box.add_child(_name_edit)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 6)
	if mode == "save":
		var save := Button.new()
		save.name = "SaveButton"
		save.text = "SAVE"
		save.focus_mode = Control.FOCUS_NONE
		save.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		save.custom_minimum_size.y = 34
		save.pressed.connect(_save)
		buttons.add_child(save)
	var back_button := Button.new()
	back_button.name = "BackButton"
	back_button.text = "BACK"
	back_button.focus_mode = Control.FOCUS_NONE
	back_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	back_button.custom_minimum_size.y = 34
	back_button.pressed.connect(func():
		close_slots()
		back.emit())
	buttons.add_child(back_button)
	_box.add_child(buttons)
	show()
	if mode == "save" and active >= 0 and active < 9:
		_pick(active, result.slots[active])
	reset_size()


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
	row.toggle_mode = mode == "save"
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
		text += "A network game (load it from the multiplayer screen)"
	else:
		text += "* Invalid *"
	row.text = text
	if index == active:
		row.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "ListItem"))
	if mode == "load":
		row.disabled = state != "ok"
		row.pressed.connect(func():
			finished.emit()
			command.emit({"verb": "load_game", "slot": index, "_reply_to": "show_result"}))
	else:
		row.disabled = index == 9 # the auto save
		row.pressed.connect(func(): _pick(index, slot))
	return row


func _pick(index: int, slot: Dictionary) -> void:
	_picked = index
	for row in _box.get_children():
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
	command.emit({"verb": "save_game", "slot": _picked, "description": description, "_reply_to": "saved"})


## save_game's reply (through the owner).
func saved(result: Dictionary) -> void:
	if result.get("ok", false):
		finished.emit()
	else:
		refused.emit(String(result.get("message", result.get("error", ""))))
