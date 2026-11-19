extends PanelContainer
## The build pane (WP3.4b, the owner's design: "CHANGE: the build pane: item details, Auto build /
## Repeat build, the build list, the buildable list with Buildings / Units tabs, OK and Cancel"; the
## tabs became one list on the owner's request, 2026-11-17: the units, a short fixed list, pinned
## above the buildings; each takes half the column and scrolls). Slides out from the colony pane's right edge. From the
## `build_options` query: what CHANGE offers (Units and Buildings, and Refit: the warships in orbit,
## each choosing a design of its size in a popup, shown in place of the list while REFIT is on), each item's details (the original's help text, or a design's parts) and the colony's
## queue, shown as the game's seven numbered slots. Items are clicked onto the end of the list or
## dragged between its items; list items drag to a new place the same way. The pane keeps a working copy of the queue and the edits made to it, by
## the original popup's rules (seven slots, a repeat stays with its item, a bought front item stays
## put); OK sends the edits (`build_commit`, which replays them through the game's own popup
## functions, ships and questions included) and closes, Cancel throws them away. Leaving any
## other way is a Cancel.

signal command(dict: Dictionary)
signal closed
signal committed

const Themes := preload("../themes.gd")
const SLIDE_TIME := 0.25
const WIDTH := 660.0
const REPEAT := -10
const HOUSING := -3
const TRADE_GOODS := -2
const SLOTS := 7
const ROW_HEIGHT := 30.0

@onready var tabs_refit: Button = $Box/Header/Refit
@onready var detail: VBoxContainer = $Box/Columns/Left/Detail/Scroll/Box
@onready var detail_scroll: ScrollContainer = $Box/Columns/Left/Detail/Scroll
@onready var queue_panel: PanelContainer = $Box/Columns/Left/Queue
@onready var queue_title: Label = $Box/Columns/Left/QueueTitle
@onready var auto_button: Button = $Box/Columns/Left/Toggles/Auto
@onready var repeat_button: Button = $Box/Columns/Left/Toggles/Repeat
@onready var queue_box: VBoxContainer = $Box/Columns/Left/Queue/Box
@onready var lists: VBoxContainer = $Box/Columns/Right/Lists
@onready var units_title: Label = $Box/Columns/Right/Lists/UnitsFrame/UnitsBox/UnitsTitle
@onready var units_scroll: ScrollContainer = $Box/Columns/Right/Lists/UnitsFrame/UnitsBox/UnitsScroll
@onready var units_box: VBoxContainer = $Box/Columns/Right/Lists/UnitsFrame/UnitsBox/UnitsScroll/Units
@onready var items_title: Label = $Box/Columns/Right/Lists/ItemsFrame/ItemsBox/ItemsTitle
@onready var items_box: VBoxContainer = $Box/Columns/Right/Lists/ItemsFrame/ItemsBox/Scroll/Items

var bottom_clear := 0.0 # set_bottom_clear
var art: Node
var colony_id := -1
var bought := false
var options := {}
var _queue: Array = [] # [{id, name, picture}], REPEAT entries included
var _edits: Array = []
var _names := {} # item id -> {name, picture} (the lists' items)
var _tween: Tween
var _shown_left := 0.0
var _drop_gap := -1 # where a drag over the list would land (-1: no drag)
var _marker: Control
var _refit_popup: Popup
var _refitting := {} # ship -> true: refits added in this pane


func _ready() -> void:
	_add_splitter()
	$Box/Header/Close.pressed.connect(cancel)
	$Box/Buttons/Cancel.pressed.connect(cancel)
	$Box/Buttons/Ok.pressed.connect(_ok)
	tabs_refit.toggled.connect(func(_on: bool): _show_items())
	preload("../themes.gd").glowing(tabs_refit)
	for b: Button in [auto_button, repeat_button]:
		preload("../themes.gd").glowing(b)
	# The list takes drags (_arm_drag): an item from the right, or one of its own moved.
	queue_box.custom_minimum_size.y = SLOTS * ROW_HEIGHT + (SLOTS - 1) * 2
	_marker = Control.new()
	_marker.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_marker.draw.connect(_draw_marker)
	queue_panel.add_child(_marker)
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()
	elif what == NOTIFICATION_DRAG_END and is_node_ready():
		_drop_gap = -1
		_marker.queue_redraw()


## Beside the colony pane (which sits beside the system pane).
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	var step := maxi(gap, 6)
	offset_left = inset + gap + 372.0 + step + 340.0 + step
	offset_right = offset_left + WIDTH
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -maxf(inset + gap, bottom_clear)
	_shown_left = offset_left


## Room kept free under the pane: the map controls (owner, 2026-11-14), as the colony pane does.
func set_bottom_clear(room: float) -> void:
	if absf(room - bottom_clear) > 0.5:
		bottom_clear = room
		_restyle()


## Its left edge at `x` (the colony pane's actual right edge plus the gap: that pane grows with its
## contents), its right edge no further than `right` (the side tabs) or the screen's inset.
func place_at(x: float, right := INF) -> void:
	offset_left = x
	var edge := get_viewport_rect().size.x - get_theme_constant("inset", "Screen") - get_theme_constant("pane_gap", "Screen")
	var room := minf(edge, right) - x
	offset_right = x + clampf(room, 0.0, WIDTH)
	_shown_left = x


## Opens on a colony's queue (`bought`: its front item is bought and stays first).
func open(id: int, is_bought: bool) -> void:
	colony_id = id
	bought = is_bought
	_edits.clear()
	_refitting.clear()
	tabs_refit.set_pressed_no_signal(false)
	preload("../themes.gd").glow(tabs_refit)
	command.emit({"verb": "query", "what": "build_options", "colony": id, "_reply_to": "show_options"})


func show_options(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	options = result
	_names.clear()
	for list in [result.buildings, result.units]:
		for item: Dictionary in list:
			if item.has("id"):
				_names[int(item.id)] = item
	_queue = []
	for q: Dictionary in result.queue:
		_queue.append(q.duplicate())
	auto_button.set_pressed_no_signal(result.get("auto", false))
	repeat_button.set_pressed_no_signal(false)
	preload("../themes.gd").glow(auto_button)
	preload("../themes.gd").glow(repeat_button)
	_show_items()
	_show_queue()
	_show_detail(_queue[0] if not _queue.is_empty() else {})
	if not visible:
		_slide(true)


func cancel() -> void:
	_close_refit_popup()
	if visible:
		_slide(false)
		closed.emit()


## Closed by a click away from it (the galaxy, another colony, a side panel): the changes are kept,
## as OK (owner, 2026-11-19: clicking outside after adding things acted as Cancel).
func close_keeping() -> void:
	if not visible:
		return
	if _edits.is_empty() and auto_button.button_pressed == bool(options.get("auto", false)):
		cancel()
	else:
		_close_refit_popup()
		_ok()


## Hidden at once (a legacy screen took over): a Cancel.
func dismiss() -> void:
	_close_refit_popup()
	if visible:
		hide()
		closed.emit()


func _ok() -> void:
	command.emit({"verb": "build_commit", "colony": colony_id, "edits": _edits, "auto": auto_button.button_pressed,
		"_reply_to": "_on_committed"})


func _on_committed(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	_slide(false)
	closed.emit()
	committed.emit()


# ---- The working queue: the popup's rules (COLBLDG::Add_To_Queue, Delete_From_Queue,
# Insert_Before_Queue_Slot) ----

func _add(id: int, gap := -1) -> void:
	var repeat := repeat_button.button_pressed and id != HOUSING and id != TRADE_GOODS
	if _queue.size() >= SLOTS:
		return # a full list: the popup refuses too
	var entry: Dictionary = _names.get(id, {"id": id, "name": "?"})
	_queue.append({"id": id, "name": entry.get("name", "?"), "picture": entry.get("picture", {})})
	if repeat and _queue.size() < SLOTS:
		_queue.append({"id": REPEAT, "name": "Repeat"})
	# (no room for the repeat: the item goes in alone, and the game says so when it is committed)
	_edits.append({"add": id, "repeat": repeat})
	_place_last(gap)
	_show_queue()
	_show_items() # a building queued leaves the list


## A refit (REFIT's popup chose a design): the game queues the ship, at the end.
func _refit(ship: Dictionary, target: Dictionary, gap := -1) -> void:
	_close_refit_popup()
	if _queue.size() >= SLOTS:
		return
	var id := -100 - int(ship.ship)
	_queue.append({"id": id, "name": "Refit %s" % String(ship.name), "picture": ship.get("picture", {}),
		"refit": target})
	_refitting[int(ship.ship)] = true
	_edits.append({"refit": int(ship.ship), "design": int(target.design)})
	_place_last(gap)
	_show_items()
	_show_queue()


## The item just added at the end goes to `gap` (between items; -1 or the end: stays).
func _place_last(gap: int) -> void:
	var last := _pair(_queue.size() - 1).x
	if gap >= 0 and gap < last:
		_move_to(last, gap, false)


func _delete(slot: int) -> void:
	if slot == 0 and bought:
		return # the popup keeps a bought front item
	var id := int(_queue[slot].id)
	if slot > 0 and id == REPEAT:
		_queue.remove_at(slot)
		_queue.remove_at(slot - 1)
	elif slot + 1 < _queue.size() and int(_queue[slot + 1].id) == REPEAT:
		_queue.remove_at(slot + 1)
		_queue.remove_at(slot)
	else:
		_queue.remove_at(slot)
	if id <= -100 and _refitting.has(-100 - id):
		_refitting.erase(-100 - id) # the commit puts the ship back as it was
	_edits.append({"delete": slot})
	_show_queue()
	_show_items() # a building taken off the list can be chosen again


## Moves the item at `from` ahead of the item at `to`, its repeat with it (as the popup's
## Insert_Before_Queue_Slot, which the commit calls with the same slots).
func _move(from: int, to: int, redraw := true) -> void:
	if from == to or (bought and (from == 0 or to == 0)):
		return
	var src := _pair(from)
	var dst := _pair(to)
	var moved: Array = _queue.slice(src.x, src.x + src.y)
	if src.x < dst.x:
		_queue = _queue.slice(0, src.x) + _queue.slice(src.x + src.y, dst.x + dst.y) + moved + _queue.slice(dst.x + dst.y)
	else:
		_queue = _queue.slice(0, dst.x) + moved + _queue.slice(dst.x, src.x) + _queue.slice(src.x + src.y)
	_edits.append({"move": [from, to]})
	if redraw:
		_show_queue()


## Moves the item at `slot` to `gap` (0: first, the list's size: last). The popup moves an item to
## another's place: up, ahead of the item at the gap; down, after the item before it.
func _move_to(slot: int, gap: int, redraw := true) -> void:
	var src := _pair(slot)
	if gap >= src.x and gap <= src.x + src.y:
		return # where it is
	if gap < src.x:
		_move(src.x, gap, redraw)
	else:
		_move(src.x, _pair(gap - 1).x, redraw)


## A slot's item and how many slots it takes (2 with its repeat).
func _pair(slot: int) -> Vector2i:
	if slot > 0 and int(_queue[slot].id) == REPEAT:
		return Vector2i(slot - 1, 2)
	if slot + 1 < _queue.size() and int(_queue[slot + 1].id) == REPEAT:
		return Vector2i(slot, 2)
	return Vector2i(slot, 1)


# ---- Showing ----

func _show_items() -> void:
	if not is_node_ready() or options.is_empty():
		return
	for box in [units_box, items_box]:
		for child in box.get_children():
			box.remove_child(child)
			child.queue_free()
	var refit := tabs_refit.button_pressed
	units_title.visible = not refit
	units_scroll.visible = not refit
	$Box/Columns/Right/Lists/UnitsFrame.visible = not refit # its own frame (owner, 2026-11-19)
	if _splitter:
		_splitter.visible = not refit
	items_title.text = "REFIT" if refit else "BUILDINGS"
	if refit:
		_show_refit_ships()
		return
	_fill(units_box, options.units)
	_fill(items_box, options.buildings)


func _queued(id: int) -> bool:
	return _queue.any(func(q: Dictionary) -> bool: return int(q.id) == id)


func _fill(box: VBoxContainer, list: Array) -> void:
	for item: Dictionary in list:
		# A building in the build list is built once: it leaves the list (owner, 2026-11-19: it
		# could be queued again and again). Housing and trade goods can repeat.
		if box == items_box and not item.get("separator", false) and int(item.id) != HOUSING and \
				int(item.id) != TRADE_GOODS and _queued(int(item.id)):
			continue
		if item.get("separator", false):
			var gap := Control.new()
			gap.custom_minimum_size.y = 8
			box.add_child(gap)
			continue
		var b := Button.new()
		b.text = String(item.name)
		b.flat = true
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.focus_mode = Control.FOCUS_NONE
		b.theme_type_variation = &"ListItem"
		b.add_theme_font_size_override("font_size", 19)
		var id := int(item.id)
		b.mouse_entered.connect(func(): _show_detail(item))
		b.pressed.connect(func(): _add(id))
		_arm_drag(b, {"kind": "item", "id": id, "item": item})
		box.add_child(b)


## REFIT: the warships in orbit; one chooses its new design in a popup.
func _show_refit_ships() -> void:
	var refit: Dictionary = options.get("refit", {})
	var note := ""
	if not refit.get("available", false):
		note = String(refit.get("message", "Refitting needs Chemistry and Nuclear Fission."))
	var shown := 0
	if note == "":
		for ship: Dictionary in refit.get("ships", []):
			if _refitting.has(int(ship.ship)):
				continue
			shown += 1
			var row := HBoxContainer.new()
			row.add_theme_constant_override("separation", 6)
			var pic: Dictionary = ship.get("picture", {})
			row.add_child(_picture(pic, Vector2(40, 30)))
			var b := Button.new()
			b.text = String(ship.name)
			b.flat = true
			b.alignment = HORIZONTAL_ALIGNMENT_LEFT
			b.focus_mode = Control.FOCUS_NONE
			b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			b.theme_type_variation = &"ListItem"
			b.add_theme_font_size_override("font_size", 19)
			b.mouse_entered.connect(func(): _show_detail(ship))
			b.pressed.connect(func(): _open_refit_popup(ship, b, -1))
			_arm_drag(b, {"kind": "refit", "ship": ship, "item": ship})
			row.add_child(b)
			items_box.add_child(row)
		if shown == 0:
			note = "No warships in orbit to refit."
	if note != "":
		var l := Label.new()
		l.theme_type_variation = &"Dim"
		l.text = note
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		l.custom_minimum_size.x = 200
		items_box.add_child(l)


## The designs a ship can become (the original's second refit list), beside `at`.
func _open_refit_popup(ship: Dictionary, at: Control, gap: int) -> void:
	_close_refit_popup()
	var targets: Array = ship.get("targets", [])
	_refit_popup = Popup.new()
	_refit_popup.transparent_bg = true
	_refit_popup.transparent = true
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"Pane"
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 4)
	panel.add_child(box)
	var title := Label.new()
	title.theme_type_variation = &"Section"
	title.text = "REFIT %s TO" % String(ship.name).to_upper()
	box.add_child(title)
	if targets.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "No design of its hull size. Make one in Ship Design."
		box.add_child(none)
	for target: Dictionary in targets:
		var b := Button.new()
		b.text = "%s   %s BC" % [String(target.name), _thousands(int(target.cost))]
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.focus_mode = Control.FOCUS_NONE
		b.theme_type_variation = &"SlotButton"
		var design: Dictionary = _names.get(-50 - int(target.design), {})
		b.mouse_entered.connect(func(): _show_detail(design.merged({"refit_cost": int(target.cost)}, true) if not design.is_empty() else {}))
		b.pressed.connect(func(): _refit(ship, target, gap))
		box.add_child(b)
	_refit_popup.add_child(panel)
	add_child(_refit_popup)
	var size_now := panel.get_combined_minimum_size()
	var place := at.get_screen_position() + Vector2(at.size.x - size_now.x, at.size.y + 4)
	_refit_popup.popup(Rect2i(Vector2i(place), Vector2i(size_now)))


func _close_refit_popup() -> void:
	if is_instance_valid(_refit_popup):
		_refit_popup.queue_free()
	_refit_popup = null


func _drag_preview(item: Dictionary) -> Control:
	var row := HBoxContainer.new()
	row.modulate.a = 0.85
	var pic: Dictionary = item.get("picture", {})
	row.add_child(_picture(pic, Vector2(40, 30)))
	var l := Label.new()
	l.theme_type_variation = &"Name"
	l.text = String(item.get("name", ""))
	row.add_child(l)
	return row


# ---- Dropping on the list ----

## The gap a drop at `at` (the queue panel's coordinates) lands in: before the first item whose
## middle is below it, never between an item and its repeat, never ahead of a bought item.
func _gap_at(at: Vector2) -> int:
	var y := at.y - queue_box.position.y
	var gap := _queue.size()
	for slot in _queue.size():
		if y < slot * (ROW_HEIGHT + 2) + ROW_HEIGHT / 2.0:
			gap = slot
			break
	if gap > 0 and gap < _queue.size() and int(_queue[gap].id) == REPEAT:
		gap += 1
	if bought and not _queue.is_empty():
		gap = maxi(gap, _pair(0).y)
	return gap


func _can_drop(at: Vector2, data: Variant) -> bool:
	if not data is Dictionary:
		return false
	var ok := false
	if data.has("build_slot"):
		ok = true
	elif data.has("build_item") or data.has("refit_ship"):
		ok = _queue.size() < SLOTS
	_drop_gap = _gap_at(at) if ok else -1
	_marker.queue_redraw()
	return ok


func _drop(at: Vector2, data: Variant) -> void:
	var gap := _gap_at(at)
	_drop_gap = -1
	_marker.queue_redraw()
	if data.has("build_slot"):
		_move_to(int(data.build_slot), gap)
	elif data.has("build_item"):
		_add(int(data.build_item), gap)
	elif data.has("refit_ship"):
		var ship: Dictionary = data.refit_ship
		var targets: Array = ship.get("targets", [])
		if targets.size() == 1:
			_refit(ship, targets[0], gap)
		else:
			_open_refit_popup(ship, queue_box, gap)


func _draw_marker() -> void:
	if _drop_gap < 0:
		return
	var y := queue_box.position.y + _drop_gap * (ROW_HEIGHT + 2) - 1.0
	var colour := get_theme_color("font_pressed_color", "Toggle")
	_marker.draw_rect(Rect2(queue_box.position.x, y - 1.0, queue_box.size.x, 3.0), colour)


## The seven slots, numbered, empty ones shown; each item drags to a new place.
## While a drag is on (_drag.active), the dragged item's rows are left out and a gap (_spacer)
## opens where it would land.
func _show_queue() -> void:
	for child in queue_box.get_children():
		queue_box.remove_child(child)
		if child != _spacer:
			child.queue_free()
	queue_title.text = "BUILD LIST   %d / %d" % [_queue.size(), SLOTS]
	var hidden := _drag_hidden()
	# A new item's gap takes the room of the last empty rows (a moved item's rows left their own).
	var extra := maxi(_spacer_rows() - hidden.y, 0)
	var rows_in := 0 # rows put in so far: numbered where they will be, the gap counted
	for slot in SLOTS:
		if slot >= hidden.x and slot < hidden.x + hidden.y:
			continue # the dragged item: on the pointer
		if slot >= SLOTS - extra and slot >= _queue.size():
			continue
		var place := rows_in + (_spacer_rows() if _drag_gap >= 0 and rows_in >= _drag_gap else 0)
		rows_in += 1
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 6)
		row.custom_minimum_size.y = ROW_HEIGHT
		row.mouse_filter = Control.MOUSE_FILTER_PASS
		var number := Label.new()
		number.theme_type_variation = &"Dim"
		number.text = str(place + 1)
		number.custom_minimum_size.x = 18
		number.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		row.add_child(number)
		if slot >= _queue.size():
			var empty := Label.new()
			empty.theme_type_variation = &"Dim"
			empty.text = "—" if slot > 0 or not _queue.is_empty() else "Empty: the colony makes trade goods"
			empty.modulate.a = 0.6
			row.add_child(empty)
			queue_box.add_child(row)
			continue
		var entry: Dictionary = _queue[slot]
		var id := int(entry.id)
		if id == REPEAT:
			var r := Label.new()
			r.theme_type_variation = &"Dim"
			r.text = "    ↻ repeat"
			r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_child(r)
		else:
			var pic: Dictionary = entry.get("picture", {})
			row.add_child(_picture(pic, Vector2(34, 26)))
			var name_button := Button.new()
			name_button.text = String(entry.name)
			name_button.flat = true
			name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			name_button.focus_mode = Control.FOCUS_NONE
			name_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			name_button.theme_type_variation = &"ListItem"
			name_button.add_theme_font_size_override("font_size", 17)
			name_button.clip_text = true
			var shown: Dictionary = _names.get(id, entry)
			if entry.has("refit"):
				var design: Dictionary = _names.get(-50 - int(entry.refit.design), {})
				shown = design.merged({"refit_cost": int(entry.refit.cost)}, true) if not design.is_empty() else entry
			name_button.mouse_entered.connect(func(): _show_detail(shown))
			if not (slot == 0 and bought):
				_arm_drag(name_button, {"kind": "slot", "slot": slot, "item": entry})
			row.add_child(name_button)
		if not (slot == 0 and bought):
			var remove := Button.new()
			remove.text = "X"
			remove.focus_mode = Control.FOCUS_NONE
			remove.custom_minimum_size = Vector2(30, 26)
			var s2 := slot
			remove.pressed.connect(func(): _delete(s2))
			row.add_child(remove)
		queue_box.add_child(row)
	if _spacer != null and _drag.get("active", false) and _drag_gap >= 0:
		queue_box.add_child(_spacer)
		queue_box.move_child(_spacer, mini(_drag_gap, queue_box.get_child_count() - 1))


# ---- Dragging (owner, 2026-11-19: drag and drop onto the list was broken, its rows took the drop,
# and a thin line was all it showed): the row comes off and follows the pointer, and a gap opens in
# the list where it would land. A plain click on a list item still adds it at the end. ----

var _drag := {} # {kind: slot / item / refit, slot, id, ship, item, start, active}
var _ghost: Control
var _spacer: Control
var _drag_gap := -1 # the gap among the shown rows (the dragged item's rows left out)
var _spacer_tween: Tween
const DRAG_THRESHOLD := 6.0


func _arm_drag(control: Control, info: Dictionary) -> void:
	control.gui_input.connect(func(event: InputEvent):
		var press := event as InputEventMouseButton
		if press and press.button_index == MOUSE_BUTTON_LEFT and press.pressed:
			_drag = info.merged({"start": press.global_position, "active": false}, true))


## The dragged item's rows in the queue (a slot drag): its first slot and how many.
func _drag_hidden() -> Vector2i:
	if _drag.get("active", false) and String(_drag.kind) == "slot":
		return _pair(int(_drag.slot))
	return Vector2i(-1, 0)


func _spacer_rows() -> int:
	if not _drag.get("active", false) or _drag_gap < 0:
		return 0
	return _drag_hidden().y if String(_drag.kind) == "slot" else 1


func _input(event: InputEvent) -> void:
	if _drag.is_empty() or not visible:
		return
	var motion := event as InputEventMouseMotion
	if motion:
		if not _drag.active and motion.global_position.distance_to(_drag.start) > DRAG_THRESHOLD:
			if String(_drag.kind) != "slot" and _queue.size() >= SLOTS:
				_drag = {} # a full list takes nothing new
				return
			_begin_drag()
		if _drag.get("active", false):
			_follow(motion.global_position)
		return
	var release := event as InputEventMouseButton
	if release and release.button_index == MOUSE_BUTTON_LEFT and not release.pressed:
		if _drag.get("active", false):
			get_viewport().set_input_as_handled() # not a click on the button under it
			_finish_drag(release.global_position)
		_drag = {}


func _begin_drag() -> void:
	_drag.active = true
	_ghost = PanelContainer.new()
	_ghost.theme_type_variation = &"RowOpen"
	_ghost.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_ghost.top_level = true
	_ghost.z_index = 10
	var preview := _drag_preview(_drag.item)
	preview.modulate.a = 1.0
	_ghost.add_child(preview)
	add_child(_ghost)
	_ghost.custom_minimum_size = Vector2(queue_box.size.x, ROW_HEIGHT * _drag_hidden().y if String(_drag.kind) == "slot" else ROW_HEIGHT)
	_spacer = Control.new()
	_spacer.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_drag_gap = -1
	_follow(get_global_mouse_position())


## The ghost on the pointer; over the list, the gap where it would land.
func _follow(at: Vector2) -> void:
	if _ghost == null:
		return
	_ghost.global_position = at - Vector2(24.0, ROW_HEIGHT * 0.5)
	var gap := _gap_for(at) if queue_panel.get_global_rect().has_point(at) else -1
	if gap == _drag_gap:
		return
	_drag_gap = gap
	if _spacer_tween:
		_spacer_tween.kill()
	_spacer.custom_minimum_size.y = 0.0
	_show_queue()
	if gap >= 0:
		_spacer_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
		_spacer_tween.tween_property(_spacer, "custom_minimum_size:y", _spacer_rows() * (ROW_HEIGHT + 2.0) - 2.0, 0.12)


## The gap among the shown rows under `at`: whole items only (an item and its repeat stay
## together), never ahead of a bought first item.
func _gap_for(at: Vector2) -> int:
	var hidden := _drag_hidden()
	var shown: Array = [] # the queue's slots still in the list, in order
	for slot in _queue.size():
		if not (slot >= hidden.x and slot < hidden.x + hidden.y):
			shown.append(slot)
	var pitch := ROW_HEIGHT + 2.0
	var y := (queue_box.get_global_transform().affine_inverse() * at).y
	# Rows under the open gap sit lower by its height; the gap itself keeps the place.
	var open := _spacer_rows() * pitch
	if _drag_gap >= 0:
		if y >= _drag_gap * pitch and y < _drag_gap * pitch + open:
			return _drag_gap
		if y >= _drag_gap * pitch + open:
			y -= open
	var gap := clampi(roundi(y / pitch), 0, shown.size())
	while gap > 0 and gap < shown.size() and int(_queue[shown[gap]].id) == REPEAT:
		gap += 1
	if bought and not shown.is_empty() and int(shown[0]) == 0:
		gap = maxi(gap, _pair(0).y)
	return gap


func _finish_drag(at: Vector2) -> void:
	var gap := _drag_gap
	var info := _drag
	if _ghost != null:
		_ghost.queue_free()
		_ghost = null
	if _spacer_tween:
		_spacer_tween.kill()
	_drag = {}
	_drag_gap = -1
	if _spacer != null:
		if _spacer.get_parent() != null:
			_spacer.get_parent().remove_child(_spacer)
		_spacer.queue_free()
		_spacer = null
	if gap < 0 or not queue_panel.get_global_rect().has_point(at):
		_show_queue() # dropped elsewhere: nothing changes
		return
	match String(info.kind):
		"slot":
			# The gap counts the shown rows; in the queue's own slots the dragged rows come back.
			var src := _pair(int(info.slot))
			var to := gap if gap <= src.x else gap + src.y
			_move_to(src.x, to)
			_show_queue()
		"item":
			_add(int(info.id), gap)
		"refit":
			var ship: Dictionary = info.ship
			var targets: Array = ship.get("targets", [])
			if targets.size() == 1:
				_refit(ship, targets[0], gap)
			else:
				_open_refit_popup(ship, queue_box, gap)


# ---- The partition between Units and Buildings (owner, 2026-11-19): drag it to share the column
# as you like; kept in the interface settings. ----

const SPLIT_MIN := 0.15
var _splitter: Control
var _splitting := false


func _add_splitter() -> void:
	var units_frame: Control = $Box/Columns/Right/Lists/UnitsFrame
	lists.add_theme_constant_override("separation", 0)
	_splitter = Control.new()
	_splitter.name = "Splitter"
	_splitter.custom_minimum_size.y = 12.0
	_splitter.mouse_filter = Control.MOUSE_FILTER_STOP
	_splitter.mouse_default_cursor_shape = Control.CURSOR_VSPLIT
	_splitter.tooltip_text = "Drag to share the room between units and buildings"
	_splitter.draw.connect(func():
		# A short grip in the middle, brighter while hovered or dragged.
		var hot := _splitting or _splitter.get_global_rect().has_point(_splitter.get_global_mouse_position())
		var color := get_theme_color("font_color", "Label")
		color.a = 0.75 if hot else 0.35
		var mid := _splitter.size * 0.5
		for dy in [-2.0, 2.0]:
			_splitter.draw_line(Vector2(mid.x - 18.0, mid.y + dy), Vector2(mid.x + 18.0, mid.y + dy), color, 1.5, true))
	_splitter.mouse_entered.connect(_splitter.queue_redraw)
	_splitter.mouse_exited.connect(_splitter.queue_redraw)
	_splitter.gui_input.connect(_on_splitter_input)
	lists.add_child(_splitter)
	lists.move_child(_splitter, units_frame.get_index() + 1)
	_set_split(float(preload("../themes.gd").saved_value("build_pane", "units_share", 0.5)))


func _set_split(share: float) -> void:
	share = clampf(share, SPLIT_MIN, 1.0 - SPLIT_MIN)
	($Box/Columns/Right/Lists/UnitsFrame as Control).size_flags_stretch_ratio = share
	($Box/Columns/Right/Lists/ItemsFrame as Control).size_flags_stretch_ratio = 1.0 - share


func _on_splitter_input(event: InputEvent) -> void:
	var button := event as InputEventMouseButton
	if button and button.button_index == MOUSE_BUTTON_LEFT:
		_splitting = button.pressed
		if not _splitting:
			preload("../themes.gd").save_value("build_pane", "units_share",
				($Box/Columns/Right/Lists/UnitsFrame as Control).size_flags_stretch_ratio)
		_splitter.queue_redraw()
		_splitter.accept_event()
		return
	var motion := event as InputEventMouseMotion
	if motion and _splitting:
		# The share of the column above the pointer (the grip's own height left out).
		var room := lists.size.y - _splitter.size.y
		if room > 0.0:
			# The event's position is the grip's own; the grip sits in the column.
			var y := _splitter.position.y + motion.position.y - _splitter.size.y * 0.5
			_set_split(y / room)
		_splitter.accept_event()


## The detail box: picture, name, cost here, build time, upkeep; a design's parts.
func _show_detail(item: Dictionary) -> void:
	for child in detail.get_children():
		detail.remove_child(child)
		child.queue_free()
	if item.is_empty():
		return
	var pic: Dictionary = item.get("picture", {})
	detail_scroll.scroll_vertical = 0
	var picture := _picture(pic, Vector2(180, 120))
	picture.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	detail.add_child(picture)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(item.get("name", ""))
	detail.add_child(name_label)
	var lines: Array = []
	if item.has("refit_cost"):
		lines.append("Refit cost %s" % _thousands(int(item.refit_cost)))
	elif item.has("cost"):
		lines.append("Cost here %s · Build time %d" % [_thousands(int(item.cost)), int(item.get("turns", 0))])
	if int(item.get("upkeep", 0)) > 0:
		lines.append("Upkeep %d BC" % int(item.upkeep))
	if item.has("hull"):
		lines.append("%s · %s · %s" % [item.hull, item.shield, item.computer])
		lines.append("%s · %s" % [item.armor, item.drive])
		var weapons: Array = item.get("weapons", []).map(func(w: Dictionary) -> String: return w.text)
		if not weapons.is_empty():
			lines.append(", ".join(weapons))
		var specials: Array = item.get("specials", []).map(func(w: Dictionary) -> String: return w.text)
		if not specials.is_empty():
			lines.append(", ".join(specials))
	for text in lines:
		var l := Label.new()
		l.theme_type_variation = &"Dim"
		l.text = text
		l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		detail.add_child(l)
	var description := String(item.get("description", "")).strip_edges()
	if description != "":
		var gap := Control.new()
		gap.custom_minimum_size.y = 6
		detail.add_child(gap)
		var d := Label.new()
		d.text = description
		d.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		d.add_theme_font_size_override("font_size", 16)
		detail.add_child(d)


## An item's picture: a game sprite {lbx, entry}, or the interface's own {icon} (trade goods).
func _picture(pic: Dictionary, min_size: Vector2) -> TextureRect:
	var rect := _sprite(String(pic.get("lbx", "")), int(pic.get("entry", -1)), min_size)
	# Ships at the game's scale in their frame: a Frigate small, a Titan large (owner, 2026-11-19:
	# every hull was stretched to the box, as the design panel was before 2026-11-15).
	if String(pic.get("lbx", "")).begins_with("SHIPS.LBX"):
		rect.set("whole_frame", true)
	if pic.has("icon"):
		rect.texture = Themes.icon(String(pic.icon), int(min_size.y * 0.6))
	return rect


func _sprite(lbx: String, entry: int, min_size: Vector2) -> TextureRect:
	var rect: TextureRect = preload("sprite_rect.gd").new()
	rect.custom_minimum_size = min_size
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.mouse_filter = Control.MOUSE_FILTER_IGNORE
	rect.bind(art)
	if lbx != "" and entry >= 0:
		rect.show_sprite(lbx, entry)
	return rect


## Slides out from behind the colony pane (or back).
func _slide(opening: bool) -> void:
	if _tween:
		_tween.kill()
	var hidden_x := _shown_left - 80.0
	if opening:
		show()
		modulate.a = 0.0
		position.x = hidden_x
	_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:x", _shown_left if opening else hidden_x, SLIDE_TIME)
	_tween.tween_property(self, "modulate:a", 1.0 if opening else 0.0, SLIDE_TIME)
	if not opening:
		_tween.chain().tween_callback(hide)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE:
		cancel()
		get_viewport().set_input_as_handled()


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return ("-" if value < 0 else "") + digits + out
