extends PanelContainer
## The colony pane (WP3.4, the owner's design 2A in notes/antares/ui-concepts.md): beside the
## system pane when one of the player's colonies is picked. From the `colony` query: the colony's
## name and population, the morale row (government and masks), the income row, the build (its
## picture, turns, progress, CHANGE and BUY), each job's colonists with that job's output row below
## them (the original's icons, squeezed to fit as the colony screen squeezes them), TRANSPORT
## COLONISTS, the defences (lit when built), the garrison and the buildings, and View colony.
##
## Colonists move by dragging one to another job's row (`move_colonist`: the game's own rules and
## messages). BUY sends `buy` (the game asks first; a second BUY un-buys). CHANGE, TRANSPORT
## COLONISTS and View colony open the original colony screen for now (the build pane is WP3.4b).

signal command(dict: Dictionary)
signal closed

const SLIDE_TIME := 0.25
const WIDTH := 340.0
const ICON := Vector2(24, 24)
const BIG_ICON := Vector2(28, 28)
## COLONY2.LBX's small icons by row, for the job rows' outputs (farmers: food, workers: industry,
## scientists: research).
const JOB_ROWS := ["food", "industry", "research"]
const JOB_NAMES := ["Farmers", "Workers", "Scientists"]
const SHORT_COLOR := Color(1.0, 0.35, 0.3)

@onready var box: VBoxContainer = $Scroll/Box
@onready var name_label: Label = $Scroll/Box/Header/Names/Name
@onready var population: Label = $Scroll/Box/Header/Names/Population

var art: Node
var colony := {}
var colony_id := -1
var _tween: Tween
var _shown_left := 0.0


func _ready() -> void:
	$Scroll/Box/Header/Close.pressed.connect(close)
	$Scroll/Box/Transport.pressed.connect(_open_original)
	$Scroll/Box/View.pressed.connect(_open_original)
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


## Beside the system pane (it sits at the screen's left), under the top bar.
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_left = inset + gap + 372.0 + maxi(gap, 6)
	offset_right = offset_left + WIDTH
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -(inset + gap)
	_shown_left = offset_left


## Opens (or refreshes) on a colony: asks the game for it.
func open(id: int) -> void:
	colony_id = id
	refresh()


func refresh() -> void:
	if colony_id >= 0:
		command.emit({"verb": "query", "what": "colony", "colony": colony_id, "_reply_to": "show_colony"})


func close() -> void:
	if colony_id < 0 and not visible:
		return
	colony_id = -1
	if visible:
		_slide(false)
	closed.emit()


## Hides without closing (a legacy screen took over); it comes back with the galaxy.
func dismiss() -> void:
	hide()


func show_colony(result: Dictionary) -> void:
	if not result.get("ok", false):
		if visible:
			close()
		return
	colony = result
	name_label.text = String(result.name)
	population.text = "%s · %d / %d" % [result.race, int(result.population), int(result.max_population)]
	if int(result.growth) != 0:
		population.text += "  (%+d.%03d)" % [int(result.growth) / 1000, absi(int(result.growth)) % 1000]
	_fill($Scroll/Box/Morale, _morale_row())
	_fill($Scroll/Box/Money, _icon_row(result.rows.bc.icons, WIDTH - 40.0))
	_fill($Scroll/Box/Build, _build_box(result.build))
	_show_jobs()
	_fill($Scroll/Box/Defences, _defences_row())
	_fill($Scroll/Box/Garrison, _garrison_row())
	_fill($Scroll/Box/Buildings, _buildings_list())
	if not visible:
		_slide(true)


func _open_original() -> void:
	command.emit({"verb": "open_colony", "planet": int(colony.get("planet", -1))})


# ---- Rows ----

func _fill(holder: Control, content: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()
	holder.add_child(content)


func _morale_row() -> Control:
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_theme_constant_override("separation", 8)
	row.add_child(_sprite("COLONY2.LBX", int(colony.government_sprite), BIG_ICON, "Government"))
	var masks := int(colony.morale_icons)
	var runs := []
	if masks > 0:
		runs.append({"kind": "net", "sprite": int(colony.morale_sprite), "count": masks})
	row.add_child(_icon_row(runs, WIDTH - 90.0))
	return row


## A row of icon runs (the `colony` query's `icons`): the colony screen's overlapping icons, packed
## to fit `width` the way it squeezes them; gaps are a step of space; shortages in red.
func _icon_row(runs: Array, width: float) -> Control:
	var total := 0
	for r: Dictionary in runs:
		total += int(r.get("count", 1)) if r.kind != "gap" else 1
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var step := ICON.x + 2.0
	if total > 1 and total * step > width:
		step = maxf((width - ICON.x) / float(total - 1), 4.0)
	row.add_theme_constant_override("separation", int(step - ICON.x))
	for r: Dictionary in runs:
		if r.kind == "gap":
			var gap := Control.new()
			gap.custom_minimum_size = Vector2(ICON.x, ICON.y)
			gap.mouse_filter = Control.MOUSE_FILTER_IGNORE
			row.add_child(gap)
			continue
		for i in int(r.count):
			var icon := _sprite("COLONY2.LBX", int(r.sprite), ICON, "")
			if r.kind == "short":
				icon.modulate = SHORT_COLOR
			elif r.kind == "pollution" or r.kind == "import":
				icon.modulate = Color(1, 1, 1, 0.75)
			row.add_child(icon)
	if total == 0:
		var none := Control.new()
		none.custom_minimum_size = ICON
		row.add_child(none)
	return row


func _build_box(build: Dictionary) -> Control:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 10)
	v.add_child(top)
	var picture: Dictionary = build.get("picture", {})
	var pic := _sprite(String(picture.get("lbx", "")), int(picture.get("entry", -1)), Vector2(96, 72), "")
	top.add_child(pic)
	var info := VBoxContainer.new()
	info.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	info.add_theme_constant_override("separation", 2)
	top.add_child(info)
	var item := Label.new()
	item.theme_type_variation = &"Name"
	item.text = String(build.get("name", ""))
	item.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	info.add_child(item)
	if build.get("auto", false):
		var auto := Label.new()
		auto.theme_type_variation = &"Dim"
		auto.text = "Auto build"
		info.add_child(auto)
	if build.has("cost"):
		var turns := Label.new()
		turns.theme_type_variation = &"Dim"
		var t := int(build.turns)
		turns.text = "Bought: next turn" if int(build.bought) > 0 else ("Never at this rate" if t >= 10000 else ("%d turn%s" % [t, "" if t == 1 else "s"]))
		info.add_child(turns)
		var bar := ProgressBar.new()
		bar.show_percentage = false
		bar.custom_minimum_size.y = 8
		bar.max_value = maxf(1.0, float(build.cost))
		bar.value = float(build.cost) if int(build.bought) > 0 else float(build.spent)
		info.add_child(bar)
		var amounts := Label.new()
		amounts.theme_type_variation = &"Dim"
		amounts.text = "%s / %s" % [_thousands(int(build.spent) + int(build.bought)), _thousands(int(build.cost))]
		info.add_child(amounts)
	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 6)
	v.add_child(buttons)
	var change := Button.new()
	change.text = "CHANGE"
	change.tooltip_text = "Change what is built (the original build list for now)"
	change.focus_mode = Control.FOCUS_NONE
	change.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	change.custom_minimum_size.y = 32
	change.pressed.connect(_open_original)
	buttons.add_child(change)
	var buy := Button.new()
	buy.focus_mode = Control.FOCUS_NONE
	buy.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	buy.custom_minimum_size.y = 32
	if int(build.get("bought", 0)) > 0:
		buy.text = "BOUGHT"
		buy.toggle_mode = true
		buy.set_pressed_no_signal(true)
		buy.tooltip_text = "Bought: click to take the purchase back"
		preload("../themes.gd").glow(buy)
	else:
		buy.text = "BUY %s" % _thousands(int(build.get("buy_cost", 0))) if int(build.get("buy_cost", 0)) > 0 else "BUY"
		buy.tooltip_text = "Finish it next turn for BC"
	buy.pressed.connect(func(): command.emit({"verb": "buy", "colony": colony_id, "_reply_to": "_after_command"}))
	buttons.add_child(buy)
	return v


func _show_jobs() -> void:
	var jobs_box: VBoxContainer = $Scroll/Box/Jobs
	for child in jobs_box.get_children():
		jobs_box.remove_child(child)
		child.queue_free()
	for job_info: Dictionary in colony.jobs:
		var job := int(job_info.job)
		var panel := PanelContainer.new()
		panel.theme_type_variation = &"Row"
		panel.tooltip_text = "%s: drag a colonist here to give it this job" % JOB_NAMES[job]
		# A dropped colonist takes this row's job.
		panel.set_drag_forwarding(Callable(), func(_at: Vector2, data: Variant) -> bool:
			return data is Dictionary and data.get("colonist", false) and int(data.job) != job,
			func(_at: Vector2, data: Variant) -> void:
				command.emit({"verb": "move_colonist", "colony": colony_id, "pop": int(data.pop), "job": job, "_reply_to": "_after_command"}))
		var rows := VBoxContainer.new()
		rows.add_theme_constant_override("separation", 2)
		rows.mouse_filter = Control.MOUSE_FILTER_IGNORE
		panel.add_child(rows)
		var people := HBoxContainer.new()
		people.mouse_filter = Control.MOUSE_FILTER_PASS
		var colonists: Array = job_info.colonists
		var step := 26.0
		var avail := WIDTH - 50.0
		if colonists.size() > 1 and colonists.size() * step > avail:
			step = maxf((avail - 22.0) / float(colonists.size() - 1), 4.0)
		people.add_theme_constant_override("separation", int(step - 22.0))
		for c: Dictionary in colonists:
			var icon := _sprite("RACEICON.LBX", int(c.sprite), Vector2(22, 30), "%s (drag to another job)" % String(c.state).capitalize())
			icon.mouse_filter = Control.MOUSE_FILTER_PASS
			var pop := int(c.pop)
			icon.set_drag_forwarding(func(_at: Vector2) -> Variant:
				var preview := _sprite("RACEICON.LBX", int(c.sprite), Vector2(22, 30), "")
				preview.modulate.a = 0.8
				set_drag_preview(preview)
				return {"colonist": true, "pop": pop, "job": job},
				Callable(), Callable())
			people.add_child(icon)
		if colonists.is_empty():
			var empty := Label.new()
			empty.theme_type_variation = &"Dim"
			empty.text = JOB_NAMES[job]
			empty.custom_minimum_size.y = 30
			people.add_child(empty)
		rows.add_child(people)
		if job == 0 and int(colony.max_farms) == 0:
			var no_farming := Label.new()
			no_farming.theme_type_variation = &"Dim"
			no_farming.text = "No farming here"
			rows.add_child(no_farming)
		else:
			rows.add_child(_icon_row(colony.rows[JOB_ROWS[job]].icons, WIDTH - 40.0))
		jobs_box.add_child(panel)


func _defences_row() -> Control:
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 18)
	for d: Dictionary in colony.defences:
		var cell := VBoxContainer.new()
		cell.alignment = BoxContainer.ALIGNMENT_CENTER
		var icon := _sprite("COLONY.LBX", int(d.sprite), Vector2(48, 48), String(d.name) + ("" if d.built else ": not built"))
		icon.modulate = Color.WHITE if d.built else Color(1, 1, 1, 0.25)
		cell.add_child(icon)
		var label := Label.new()
		label.theme_type_variation = &"Dim" if not d.built else &""
		label.text = String(d.name)
		label.add_theme_font_size_override("font_size", 12)
		label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
		cell.add_child(label)
		row.add_child(cell)
	return row


func _garrison_row() -> Control:
	var g: Dictionary = colony.garrison
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 10)
	for pair in [["marine_sprite", "marines", "Marines"], ["armor_sprite", "armor", "Armor"]]:
		row.add_child(_sprite("RACEICON.LBX", int(g[pair[0]]), Vector2(34, 40), pair[2]))
		var count := Label.new()
		count.theme_type_variation = &"Name"
		count.text = str(int(g[pair[1]]))
		count.custom_minimum_size.x = 40
		row.add_child(count)
	return row


func _buildings_list() -> Control:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 0)
	for b: Dictionary in colony.buildings:
		var label := Label.new()
		label.text = String(b.name)
		label.add_theme_font_size_override("font_size", 15)
		v.add_child(label)
	if colony.buildings.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "None"
		v.add_child(none)
	return v


## After a move or a purchase: the colony again (the game recalculated it).
func _after_command(_result: Dictionary) -> void:
	refresh()


func _sprite(lbx: String, entry: int, min_size: Vector2, tip: String) -> TextureRect:
	var rect: TextureRect = preload("sprite_rect.gd").new()
	rect.custom_minimum_size = min_size
	rect.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	rect.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	rect.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	rect.tooltip_text = tip
	rect.mouse_filter = Control.MOUSE_FILTER_PASS if tip != "" else Control.MOUSE_FILTER_IGNORE
	rect.bind(art)
	if lbx != "" and entry >= 0:
		rect.show_sprite(lbx, entry)
	return rect


## Slides in from behind the system pane (or back).
func _slide(opening: bool) -> void:
	if _tween:
		_tween.kill()
	var hidden_x := _shown_left - 60.0
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
		close()
		get_viewport().set_input_as_handled()


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return ("-" if value < 0 else "") + digits + out
