extends PanelContainer
## The build pane (WP3.4b, the owner's design: "CHANGE: the build pane: item details, Auto build /
## Repeat build, the build list, the buildable list with Buildings / Units tabs, OK and Cancel").
## Slides out from the colony pane's right edge. From the `build_options` query: what CHANGE offers
## and the colony's queue. The pane keeps a working copy of the queue and the edits made to it, by
## the original popup's rules (seven slots, a repeat stays with its item, a bought front item stays
## put); OK sends the edits (`build_commit`, which replays them through the game's own popup
## functions, ships and questions included) and closes, Cancel throws them away. Leaving any
## other way is a Cancel.

signal command(dict: Dictionary)
signal closed
signal committed

const SLIDE_TIME := 0.25
const WIDTH := 600.0
const REPEAT := -10
const HOUSING := -3
const TRADE_GOODS := -2
const SLOTS := 7

@onready var tabs_buildings: Button = $Box/Header/Buildings
@onready var tabs_units: Button = $Box/Header/Units
@onready var detail: VBoxContainer = $Box/Columns/Left/Detail/Box
@onready var auto_button: Button = $Box/Columns/Left/Toggles/Auto
@onready var repeat_button: Button = $Box/Columns/Left/Toggles/Repeat
@onready var queue_box: VBoxContainer = $Box/Columns/Left/Queue/Box
@onready var items_box: VBoxContainer = $Box/Columns/Right/Scroll/Items

var art: Node
var colony_id := -1
var bought := false
var options := {}
var _queue: Array = [] # [{id, name, picture}], REPEAT entries included
var _edits: Array = []
var _names := {} # item id -> {name, picture} (the lists' items)
var _tween: Tween
var _shown_left := 0.0


func _ready() -> void:
	$Box/Header/Close.pressed.connect(cancel)
	$Box/Buttons/Cancel.pressed.connect(cancel)
	$Box/Buttons/Ok.pressed.connect(_ok)
	var group := ButtonGroup.new()
	for b: Button in [tabs_buildings, tabs_units]:
		b.button_group = group
		b.toggled.connect(func(_on: bool): _show_items())
		preload("../themes.gd").glowing(b)
	tabs_buildings.set_pressed_no_signal(true)
	for b: Button in [auto_button, repeat_button]:
		preload("../themes.gd").glowing(b)
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


## Beside the colony pane (which sits beside the system pane).
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	var step := maxi(gap, 6)
	offset_left = inset + gap + 372.0 + step + 340.0 + step
	offset_right = offset_left + WIDTH
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -(inset + gap)
	_shown_left = offset_left


## Its left edge at `x` (the colony pane's actual right edge plus the gap: that pane grows with its
## contents).
func place_at(x: float) -> void:
	offset_left = x
	offset_right = x + WIDTH
	_shown_left = x


## Opens on a colony's queue (`bought`: its front item is bought and stays first).
func open(id: int, is_bought: bool) -> void:
	colony_id = id
	bought = is_bought
	_edits.clear()
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
	if visible:
		_slide(false)
		closed.emit()


## Hidden at once (a legacy screen took over): a Cancel.
func dismiss() -> void:
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

func _add(id: int) -> void:
	var repeat := repeat_button.button_pressed and id != HOUSING and id != TRADE_GOODS
	if _queue.size() >= SLOTS:
		return # a full list: the popup refuses too
	var entry: Dictionary = _names.get(id, {"id": id, "name": "?"})
	_queue.append({"id": id, "name": entry.get("name", "?"), "picture": entry.get("picture", {})})
	if repeat and _queue.size() < SLOTS:
		_queue.append({"id": REPEAT, "name": "Repeat"})
	# (no room for the repeat: the item goes in alone, and the game says so when it is committed)
	_edits.append({"add": id, "repeat": repeat})
	_show_queue()


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
	_edits.append({"delete": slot})
	_show_queue()


## Moves the item at `from` ahead of the item at `to`, its repeat with it (as the popup's
## Insert_Before_Queue_Slot, which the commit calls with the same slots).
func _move(from: int, to: int) -> void:
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
	_show_queue()


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
	for child in items_box.get_children():
		items_box.remove_child(child)
		child.queue_free()
	var list: Array = options.buildings if tabs_buildings.button_pressed else options.units
	for item: Dictionary in list:
		if item.get("separator", false):
			var gap := Control.new()
			gap.custom_minimum_size.y = 12
			items_box.add_child(gap)
			continue
		var b := Button.new()
		b.text = String(item.name)
		b.flat = true
		b.alignment = HORIZONTAL_ALIGNMENT_LEFT
		b.focus_mode = Control.FOCUS_NONE
		b.theme_type_variation = &"ListItem"
		b.add_theme_font_size_override("font_size", 19)
		b.tooltip_text = "Add to the build list (Repeat build on: it repeats)"
		var id := int(item.id)
		b.mouse_entered.connect(func(): _show_detail(item))
		b.pressed.connect(func(): _add(id))
		items_box.add_child(b)


func _show_queue() -> void:
	for child in queue_box.get_children():
		queue_box.remove_child(child)
		child.queue_free()
	for slot in _queue.size():
		var entry: Dictionary = _queue[slot]
		var id := int(entry.id)
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 6)
		if id == REPEAT:
			var r := Label.new()
			r.theme_type_variation = &"Dim"
			r.text = "    ↻ repeat"
			r.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			row.add_child(r)
		else:
			var pic: Dictionary = entry.get("picture", {})
			row.add_child(_sprite(String(pic.get("lbx", "")), int(pic.get("entry", -1)), Vector2(34, 26)))
			var name_button := Button.new()
			name_button.text = String(entry.name)
			name_button.flat = true
			name_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
			name_button.focus_mode = Control.FOCUS_NONE
			name_button.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			name_button.theme_type_variation = &"ListItem"
			name_button.add_theme_font_size_override("font_size", 17)
			name_button.mouse_entered.connect(func(): _show_detail(_names.get(id, entry)))
			row.add_child(name_button)
			if slot > 0 and not (bought and slot == 1):
				var up := Button.new()
				up.text = "▲"
				up.tooltip_text = "Build it sooner"
				up.focus_mode = Control.FOCUS_NONE
				up.custom_minimum_size = Vector2(30, 26)
				var target := _pair(slot - 1).x
				var s := slot
				up.pressed.connect(func(): _move(s, target))
				row.add_child(up)
		if not (slot == 0 and bought):
			var remove := Button.new()
			remove.text = "X"
			remove.tooltip_text = "Take it off the list"
			remove.focus_mode = Control.FOCUS_NONE
			remove.custom_minimum_size = Vector2(30, 26)
			var s2 := slot
			remove.pressed.connect(func(): _delete(s2))
			row.add_child(remove)
		queue_box.add_child(row)
	if _queue.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "Nothing: the colony will make trade goods"
		queue_box.add_child(none)


## The detail box: picture, name, cost here, build time, upkeep; a design's parts.
func _show_detail(item: Dictionary) -> void:
	for child in detail.get_children():
		detail.remove_child(child)
		child.queue_free()
	if item.is_empty():
		return
	var pic: Dictionary = item.get("picture", {})
	var picture := _sprite(String(pic.get("lbx", "")), int(pic.get("entry", -1)), Vector2(140, 96))
	picture.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
	detail.add_child(picture)
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(item.get("name", ""))
	detail.add_child(name_label)
	var lines: Array = []
	if item.has("cost"):
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
