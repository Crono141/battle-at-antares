extends PanelContainer
## The colony pane (WP3.4, the owner's design 2A in notes/antares/ui-concepts.md): beside the
## system pane when one of the player's colonies is picked. From the `colony` query: the colony's
## name and population (morale and government are in the system pane's row), the income row, the build (its
## picture, turns, progress, CHANGE and BUY), each job's colonists with that job's output row below
## them (the original's icons, squeezed to fit as the colony screen squeezes them), TRANSPORT
## COLONISTS, the defences (those built, centred), the garrison and the buildings; VIEW COLONY sits
## under the name.
##
## Colonists move as on the colony screen: a click picks up that colonist and the identical ones
## after it in its row (the `colony` query's `cluster`), which ride on the pointer; a click on
## another job's row drops them there (`move_colonist` with `pops`: the game's own rules and
## messages), anywhere else puts them back, as do a right click, Esc and closing. BUY sends `buy`
## (the game asks first; a second BUY un-buys). CHANGE slides out the build pane (WP3.4b);
## TRANSPORT COLONISTS with colonists on the pointer lists the colonies they can go to
## (`transport_targets`: population, planet, ETA and why not) and a click sends them there
## (`send_colonists`, the game's questions); VIEW COLONY shows or hides the colony's landscape
## beside the pane (colony_landscape.gd); before, it opened the original screen's landscape alone
## (`open_colony` with `landscape`). A building is sold with a click (`sell_building`, the game's
## question) and explained with a right click (the original's help text).

signal command(dict: Dictionary)
signal closed
## Colonists sent to another colony (`count` left, 0 if the game kept them).
signal transported(count: int)
## CHANGE: the build pane opens on this colony (`bought`: its front item is bought).
signal change_build(colony_id: int, bought: bool)
## VIEW COLONY (owner, 2026-11-20): the frontend shows or hides the colony's landscape beside it.
signal landscape_toggled(colony_id: int)
## A colony shown (opened, or another one): the landscape follows, or opens by itself on wide screens.
signal colony_shown(colony_id: int)

const Themes := preload("../themes.gd")
const SLIDE_TIME := 0.25
const WIDTH := 340.0
const ICON := Vector2(24, 24)
const BIG_ICON := Vector2(28, 28)
## COLONY2.LBX's small icons by row, for the job rows' outputs (farmers: food, workers: industry,
## scientists: research).
const JOB_ROWS := ["food", "industry", "research"]
const JOB_NAMES := ["Farmers", "Workers", "Scientists"]
const SHORT_COLOR := Color(1.0, 0.35, 0.3)
## HELP.LBX records of the colony screen's parts (ERICHELP::colony_screen_help_list).
const HELP_NAME := 485
const HELP_POPULATION := 486
const HELP_PRODUCTION := 488
const HELP_BC := 489
const HELP_JOBS := [492, 493, 494] # farmers, workers, scientists
const HELP_COLONISTS := 495
const HELP_UNITS := 496
const HELP_CHANGE := 497
const HELP_BUY := 498

@onready var box: VBoxContainer = $Scroll/Box
@onready var name_label: Label = $Scroll/Box/Header/Names/Name
@onready var population: Label = $Scroll/Box/Header/Names/Population

var art: Node
var colony := {}
var colony_id := -1
var _tween: Tween
var _shown_left := 0.0
## Room kept free under the pane: the map controls' previous/next buttons (owner, 2026-11-14).
var bottom_clear := 0.0
var _carry := {} # {pops, job, view}: colonists on the pointer
var _job_panels := {} # job -> its row's panel (drop targets)
var _info_popup: Control


## The occupation policy (owner, 2026-11-19: the legacy colony screen's button, natively): for a
## colony with conquered people, keep them or annihilate them; Democracies and Federations keep them.
var _occupation: VBoxContainer
var _keep_button: Button
var _annihilate_button: Button


func _make_occupation() -> void:
	_occupation = VBoxContainer.new()
	var title := Label.new()
	title.theme_type_variation = &"Section"
	title.text = "CAPTURED PEOPLE"
	_occupation.add_child(title)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	var group := ButtonGroup.new()
	_keep_button = Button.new()
	_keep_button.text = "KEEP"
	_keep_button.tooltip_text = "They stay and work for you"
	_annihilate_button = Button.new()
	_annihilate_button.text = "ANNIHILATE"
	for b: Button in [_keep_button, _annihilate_button]:
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size = Vector2(0, 30)
		b.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(b)
	_keep_button.pressed.connect(func(): _set_occupation(false))
	_annihilate_button.pressed.connect(func(): _set_occupation(true))
	_occupation.add_child(row)
	var box: Control = $Scroll/Box
	box.add_child(_occupation)
	box.move_child(_occupation, $Scroll/Box/Transport.get_index())
	_occupation.hide()


func _show_occupation() -> void:
	var occupation: Dictionary = colony.get("occupation", {})
	_occupation.visible = not occupation.is_empty()
	if occupation.is_empty():
		return
	var annihilate := bool(occupation.get("annihilate", false))
	_keep_button.set_pressed_no_signal(not annihilate)
	_annihilate_button.set_pressed_no_signal(annihilate)
	var allowed := bool(occupation.get("can_annihilate", false))
	_annihilate_button.disabled = not allowed
	_annihilate_button.tooltip_text = "One captured population unit is removed each turn" if allowed else "Your government keeps conquered people"
	for b: Button in [_keep_button, _annihilate_button]:
		preload("../themes.gd").glow(b)


func _set_occupation(annihilate: bool) -> void:
	command.emit({"verb": "set_occupation", "colony": colony_id, "annihilate": 1 if annihilate else 0, "_reply_to": "_after_command"})


func _ready() -> void:
	_make_occupation()
	preload("../themes.gd").see_through(self)
	preload("../themes.gd").scroll_gap($Scroll)
	$Scroll/Box/Header/Close.pressed.connect(close)
	# Owner, 2026-11-20: pick up colonists, then TRANSPORT COLONISTS (the original screen's
	# list, rebuilt). Pressed with none on the pointer it says how.
	$Scroll/Box/Transport.pressed.connect(func():
		_close_info()
		_info_popup = preload("info_popup.gd").open(self, $Scroll/Box/Transport, "Transport colonists",
			"Click the colonists to move first, then TRANSPORT COLONISTS: the colonies they can go to are listed."))
	$Scroll/Box/Transport.tooltip_text = "Click colonists first, then this: the colonies they can go to, with their ETA"
	$Scroll/Box/View.pressed.connect(func(): landscape_toggled.emit(colony_id))
	# The right click's help on the colony screen's parts (ERICHELP::colony_screen_help_list).
	name_label.mouse_filter = Control.MOUSE_FILTER_PASS
	population.mouse_filter = Control.MOUSE_FILTER_PASS
	_help_on(name_label, HELP_NAME)
	_help_on(population, HELP_POPULATION)
	_help_on($Scroll/Box/Money, HELP_BC)
	_help_on($Scroll/Box/Build, HELP_PRODUCTION)
	_help_on($Scroll/Box/Garrison, HELP_UNITS)
	_restyle()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


## Beside the system pane (it sits at the screen's left), under the top bar, its foot above the
## map controls (set_bottom_clear).
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_left = inset + gap + 372.0 + maxi(gap, 6)
	offset_right = offset_left + WIDTH
	offset_top = get_theme_constant("content_top", "Screen")
	offset_bottom = -maxf(inset + gap, bottom_clear)
	_shown_left = offset_left



func set_bottom_clear(room: float) -> void:
	if absf(room - bottom_clear) > 0.5:
		bottom_clear = room
		_restyle()


## Opens (or refreshes) on a colony: asks the game for it.
func open(id: int) -> void:
	if id != colony_id:
		close_transport()
	colony_id = id
	refresh()


func refresh() -> void:
	if colony_id >= 0:
		command.emit({"verb": "query", "what": "colony", "colony": colony_id, "_reply_to": "show_colony"})


func close() -> void:
	_drop_carry()
	_close_info()
	close_transport()
	if colony_id < 0 and not visible:
		return
	colony_id = -1
	if visible:
		_slide(false)
	closed.emit()


## Hides without closing (a legacy screen took over); it comes back with the galaxy.
func dismiss() -> void:
	_drop_carry()
	_close_info()
	close_transport()
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
	_fill($Scroll/Box/Money, _icon_row(result.rows.bc.icons, WIDTH - 40.0))
	_fill($Scroll/Box/Build, _build_box(result.build))
	_show_jobs()
	_show_occupation()
	_fill($Scroll/Box/Defences, _defences_row())
	# Only what is built shows (owner): no defences, no section.
	var any_defence: bool = colony.defences.any(func(d: Dictionary) -> bool: return d.built)
	$Scroll/Box/Defences.visible = any_defence
	$Scroll/Box/DefencesTitle.visible = any_defence
	_fill($Scroll/Box/Garrison, _garrison_row())
	_fill($Scroll/Box/Buildings, _buildings_list())
	if not visible:
		_slide(true)
	if not bool(result.get("outpost", false)):
		colony_shown.emit(colony_id)


# ---- Rows ----

func _fill(holder: Control, content: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()
	holder.add_child(content)


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
			if r.kind == "short":
				row.add_child(_short_icon(int(r.sprite)))
				continue
			var icon := _sprite("COLONY2.LBX", int(r.sprite), ICON, "")
			if r.kind == "pollution" or r.kind == "import":
				icon.modulate = Color(1, 1, 1, 0.75)
			row.add_child(icon)
	if total == 0:
		var none := Control.new()
		none.custom_minimum_size = ICON
		row.add_child(none)
	return row


## A shortfall: the import icon outlined in red (COLONY::Short_Anims outlines it in colour 0xED):
## four red silhouettes a pixel and a half out, the icon on top.
static var _silhouette: ShaderMaterial


func _short_icon(sprite: int) -> Control:
	if _silhouette == null:
		var shader := Shader.new()
		shader.code = "shader_type canvas_item;\nvoid fragment() { COLOR = vec4(0.93, 0.18, 0.12, step(0.3, texture(TEXTURE, UV).a)); }\n"
		_silhouette = ShaderMaterial.new()
		_silhouette.shader = shader
	var holder := Control.new()
	holder.custom_minimum_size = ICON
	holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
	for offset in [Vector2(-1.5, 0), Vector2(1.5, 0), Vector2(0, -1.5), Vector2(0, 1.5)]:
		var edge := _sprite("COLONY2.LBX", sprite, ICON, "")
		edge.position = offset
		edge.size = ICON
		edge.material = _silhouette
		holder.add_child(edge)
	var icon := _sprite("COLONY2.LBX", sprite, ICON, "")
	icon.size = ICON
	holder.add_child(icon)
	return holder


func _build_box(build: Dictionary) -> Control:
	var v := VBoxContainer.new()
	v.add_theme_constant_override("separation", 6)
	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 10)
	v.add_child(top)
	var picture: Dictionary = build.get("picture", {})
	var pic := _sprite(String(picture.get("lbx", "")), int(picture.get("entry", -1)), Vector2(96, 72), "")
	if picture.has("icon"): # trade goods: the interface's coin
		pic.texture = Themes.icon(String(picture.icon), 44)
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
		var buy_cost := int(build.get("buy_cost", 0))
		buy.text = "BUY %s" % _thousands(buy_cost) if buy_cost > 0 else "BUY"
		buy.tooltip_text = "Finish it next turn for BC"
		# Greyed out when the treasury can't pay (owner, 2026-11-14), the price still shown.
		if build.has("treasury") and buy_cost > int(build.treasury):
			buy.disabled = true
			buy.tooltip_text = "Not enough BC: %s in the treasury" % _thousands(int(build.treasury))
	buy.pressed.connect(func(): command.emit({"verb": "buy", "colony": colony_id, "_reply_to": "_after_command"}))
	buttons.add_child(buy)
	_help_on(buy, HELP_BUY)
	var change := Button.new()
	change.text = "CHANGE"
	change.tooltip_text = "Change what is built: the build list slides out"
	change.focus_mode = Control.FOCUS_NONE
	change.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	change.custom_minimum_size.y = 32
	change.pressed.connect(func(): change_build.emit(colony_id, int(build.get("bought", 0)) > 0))
	buttons.add_child(change) # on the right, where the build pane will slide out
	_help_on(change, HELP_CHANGE)
	return v


func _show_jobs() -> void:
	var jobs_box: VBoxContainer = $Scroll/Box/Jobs
	for child in jobs_box.get_children():
		jobs_box.remove_child(child)
		child.queue_free()
	_job_panels.clear()
	for job_info: Dictionary in colony.jobs:
		var job := int(job_info.job)
		var panel := PanelContainer.new()
		panel.theme_type_variation = &"Row"
		_job_panels[job] = panel
		_help_on(panel, HELP_JOBS[job])
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
		var carried: Array = _carry.get("pops", []) if int(_carry.get("job", -1)) == job else []
		for c: Dictionary in colonists:
			var icon := _sprite("RACEICON.LBX", int(c.sprite), Vector2(22, 30), "")
			icon.mouse_filter = Control.MOUSE_FILTER_STOP
			if carried.has(int(c.pop)):
				icon.modulate.a = 0.0 # on the pointer
			var cluster: Array = c.get("cluster", [c.pop])
			icon.gui_input.connect(func(event: InputEvent) -> void:
				var mb := event as InputEventMouseButton
				if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT and _carry.is_empty():
					_pick_up(cluster, job)
					accept_event()
				elif mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT and _carry.is_empty():
					_ask_help(HELP_COLONISTS, icon)
					accept_event())
			people.add_child(icon)
		if colonists.is_empty():
			var empty := Label.new()
			empty.theme_type_variation = &"Dim"
			# Where the farmers would be (owner, 2026-11-14), as the legacy box says it.
			empty.text = "No farming here" if job == 0 and int(colony.max_farms) == 0 else JOB_NAMES[job]
			empty.custom_minimum_size.y = 30
			people.add_child(empty)
		rows.add_child(people)
		# The output below, also with no farming: food brought in (dimmed) or missing (outlined in
		# red: starving), as the legacy box shows it.
		rows.add_child(_icon_row(colony.rows[JOB_ROWS[job]].icons, WIDTH - 40.0))
		jobs_box.add_child(panel)


func _defences_row() -> Control:
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_CENTER
	row.add_theme_constant_override("separation", 18)
	for d: Dictionary in colony.defences:
		if not d.built:
			continue # only built defences show, centred together (owner)
		var cell := VBoxContainer.new()
		cell.alignment = BoxContainer.ALIGNMENT_CENTER
		cell.add_child(_sprite("COLONY.LBX", int(d.sprite), Vector2(48, 48), String(d.name)))
		var label := Label.new()
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
		var row := Button.new()
		row.text = String(b.name)
		row.flat = true
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.focus_mode = Control.FOCUS_NONE
		row.theme_type_variation = &"ListItem"
		row.add_theme_font_size_override("font_size", 15)
		row.button_mask = MOUSE_BUTTON_MASK_LEFT | MOUSE_BUTTON_MASK_RIGHT
		var building := int(b.building)
		var sellable := bool(b.get("sellable", false))
		row.gui_input.connect(func(event: InputEvent) -> void:
			var mb := event as InputEventMouseButton
			if mb == null or not mb.pressed:
				return
			if mb.button_index == MOUSE_BUTTON_RIGHT:
				_show_info(b, row)
				accept_event()
			elif mb.button_index == MOUSE_BUTTON_LEFT and sellable:
				# Sold for half its cost: the game asks first (and keeps its once-a-turn rule).
				command.emit({"verb": "sell_building", "colony": colony_id, "building": building, "_reply_to": "_after_command"})
				accept_event())
		v.add_child(row)
	if colony.buildings.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "None"
		v.add_child(none)
	return v


## A building's help text (the original's right click on the landscape), beside its row.
func _show_info(b: Dictionary, at: Control) -> void:
	_close_info()
	var footer := ""
	if bool(b.get("sellable", false)):
		footer = "Click to sell for %s BC" % _thousands(int(b.get("refund", 0)))
	_info_popup = preload("info_popup.gd").open(self, at, String(b.name), String(b.get("description", "")), footer)


## A right click on `control` opens help record `topic` beside it.
func _help_on(control: Control, topic: int) -> void:
	control.gui_input.connect(func(event: InputEvent) -> void:
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT and _carry.is_empty():
			_ask_help(topic, control)
			control.accept_event())


var _help_at: Control


func _ask_help(topic: int, at: Control) -> void:
	_help_at = at
	command.emit({"verb": "query", "what": "help", "topic": topic, "_reply_to": "_show_help"})


func _show_help(result: Dictionary) -> void:
	if not result.get("ok", false) or not is_instance_valid(_help_at) or not visible:
		return
	_close_info()
	_info_popup = preload("info_popup.gd").open(self, _help_at, String(result.title).strip_edges(), String(result.text).strip_edges())


func _close_info() -> void:
	if is_instance_valid(_info_popup):
		preload("info_popup.gd").close_current()
	_info_popup = null


# ---- Colonists on the pointer (the colony screen's Pickup_Colonists / Send_Cluster) ----

func _pick_up(cluster: Array, job: int) -> void:
	var pops: Array = cluster.map(func(p: Variant) -> int: return int(p))
	var sprites: Array = []
	var natives := false
	for job_info: Dictionary in colony.jobs:
		for c: Dictionary in job_info.colonists:
			if pops.has(int(c.pop)):
				sprites.append(int(c.sprite))
				natives = natives or String(c.state) == "native"
	if natives:
		# The game refuses natives at pickup, with its own message (unless a rule allows it).
		command.emit({"verb": "move_colonist", "colony": colony_id, "pops": pops, "job": job, "_reply_to": "_after_command"})
		return
	var view := HBoxContainer.new()
	view.top_level = true
	view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	view.add_theme_constant_override("separation", -8 if sprites.size() > 6 else 0)
	view.z_index = 100
	for sprite: int in sprites:
		view.add_child(_sprite("RACEICON.LBX", sprite, Vector2(22, 30), ""))
	add_child(view)
	_carry = {"pops": pops, "job": job, "view": view}
	Input.set_default_cursor_shape(Input.CURSOR_DRAG)
	_follow_pointer()
	_show_jobs()


func _follow_pointer() -> void:
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.global_position = get_global_mouse_position() + Vector2(10, 6)


## Puts carried colonists back where they were (nothing was sent).
func _drop_carry() -> void:
	if _carry.is_empty():
		return
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.queue_free()
	_carry = {}
	Input.set_default_cursor_shape(Input.CURSOR_ARROW)
	if is_node_ready() and not colony.is_empty():
		_show_jobs()


func _input(event: InputEvent) -> void:
	if _carry.is_empty():
		return
	if event is InputEventMouseMotion:
		_follow_pointer()
		return
	var key := event as InputEventKey
	if key and key.pressed and key.keycode == KEY_ESCAPE:
		close_transport() # also puts the colonists back
		get_viewport().set_input_as_handled()
		return
	var mb := event as InputEventMouseButton
	if mb == null or not mb.pressed:
		return
	# The transport list open: its rows take the click (the colonists ride along until one is
	# picked, owner 2026-11-20); anywhere else puts them back and closes it.
	if _transport_flyout != null and _transport_flyout.visible:
		if mb.button_index == MOUSE_BUTTON_LEFT and _transport_flyout.get_global_rect().has_point(mb.global_position):
			return
		close_transport()
		get_viewport().set_input_as_handled()
		return
	get_viewport().set_input_as_handled() # the click is the drop, nothing else
	if mb.button_index != MOUSE_BUTTON_LEFT:
		_drop_carry()
		return
	var transport: Control = $Scroll/Box/Transport
	if transport.is_visible_in_tree() and transport.get_global_rect().has_point(mb.global_position):
		ask_transport(_carry.pops) # they stay on the pointer until a colony is picked
		return
	var target := -1
	for job: int in _job_panels:
		var panel: Control = _job_panels[job]
		if panel.is_visible_in_tree() and panel.get_global_rect().has_point(mb.global_position):
			target = job
	_drop_on(target)


## The carried colonists dropped on `job`'s row (-1: anywhere else: they go back).
func _drop_on(job: int) -> void:
	if _carry.is_empty():
		return
	var pops: Array = _carry.pops
	var from := int(_carry.job)
	_drop_carry()
	if job >= 0 and job != from:
		command.emit({"verb": "move_colonist", "colony": colony_id, "pops": pops, "job": job, "_reply_to": "_after_command"})


# ---- Transport (owner, 2026-11-20: the original's transport list, rebuilt) ----

var _transport_flyout: PanelContainer
var _transport_pops: Array = []


## The colonies the colonists `pops` (a cluster) can go to.
func ask_transport(pops: Array) -> void:
	if pops.is_empty():
		return
	_transport_pops = pops
	command.emit({"verb": "query", "what": "transport_targets", "colony": colony_id, "pop": int(pops[0]),
		"_reply_to": "show_transport"})


## The `transport_targets` answer: a list beside the pane, level with TRANSPORT COLONISTS.
func show_transport(result: Dictionary) -> void:
	if not result.get("ok", false) or not visible:
		return
	if _transport_flyout == null:
		_transport_flyout = PanelContainer.new()
		_transport_flyout.theme_type_variation = &"Pane"
		_transport_flyout.name = "TransportFlyout"
		_transport_flyout.top_level = true # placed by hand, beside the pane
		add_child(_transport_flyout)
	for child in _transport_flyout.get_children():
		_transport_flyout.remove_child(child)
		child.queue_free()
	var outer := VBoxContainer.new()
	outer.add_theme_constant_override("separation", 6)
	var header := HBoxContainer.new()
	var count := _transport_pops.size()
	var title := Label.new()
	title.text = "Transport %d colonist%s" % [count, "" if count == 1 else "s"]
	title.theme_type_variation = &"Name"
	title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(title)
	var x := Button.new()
	x.text = "X"
	x.focus_mode = Control.FOCUS_NONE
	x.tooltip_text = "Close (Esc): the colonists stay"
	x.pressed.connect(close_transport)
	header.add_child(x)
	outer.add_child(header)
	var note := Label.new()
	note.theme_type_variation = &"Dim"
	note.add_theme_font_size_override("font_size", 13)
	note.text = "Freighters %d, carrying %d colonists" % [int(result.get("freighters", 0)), int(result.get("settlers_freighted", 0))]
	outer.add_child(note)
	if result.has("blocked"):
		outer.add_child(_warning(String(result.blocked)))
	var scroll := ScrollContainer.new()
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	preload("../themes.gd").scroll_gap(scroll)
	var list := VBoxContainer.new()
	list.add_theme_constant_override("separation", 4)
	list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(list)
	var targets: Array = result.get("targets", [])
	# Those that can take them without a question first, then by ETA (full ones last).
	var rank := func(t: Dictionary) -> int: return 0 if t.get("can_send", false) and not t.has("reason") else (1 if t.get("can_send", false) else 2)
	targets.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		return [rank.call(a), int(a.eta), String(a.name)] < [rank.call(b), int(b.eta), String(b.name)])
	if targets.is_empty():
		list.add_child(_warning("No other colony to go to."))
	for target: Dictionary in targets:
		list.add_child(_transport_row(target))
	outer.add_child(scroll)
	_transport_flyout.add_child(outer)
	_transport_flyout.custom_minimum_size = Vector2(380, 0)
	# Beside the pane, its top level with the pane's, at most the pane's height.
	var head := 64.0 + (30.0 if result.has("blocked") else 0.0)
	scroll.custom_minimum_size.y = minf(size.y - head - 24.0, 12.0 + 74.0 * maxi(targets.size(), 1))
	_transport_flyout.reset_size()
	_transport_flyout.global_position = Vector2(global_position.x + size.x + 6.0, global_position.y)
	_transport_flyout.show()
	_transport_flyout.move_to_front()


func close_transport() -> void:
	if _transport_flyout:
		_transport_flyout.hide()
	_transport_pops = []
	_drop_carry() # colonists still on the pointer go back


func _warning(text: String) -> Label:
	var label := Label.new()
	label.text = text
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", Color(1.0, 0.55, 0.4))
	return label


## One destination: name and system, ETA; population and the planet; why not, if so. A click
## sends the colonists (the game still asks where there is no room or a blockade).
func _transport_row(target: Dictionary) -> Control:
	var row := PanelContainer.new()
	row.theme_type_variation = &"Row"
	var can_send: bool = target.get("can_send", false)
	row.mouse_filter = Control.MOUSE_FILTER_STOP if can_send else Control.MOUSE_FILTER_IGNORE
	row.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND if can_send else Control.CURSOR_ARROW
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 0)
	box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.add_child(box)
	var top := HBoxContainer.new()
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var name_label := Label.new()
	name_label.text = String(target.name)
	name_label.theme_type_variation = &"Name"
	top.add_child(name_label)
	var system := Label.new()
	system.text = String(target.get("system", ""))
	system.theme_type_variation = &"Dim"
	system.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(system)
	var eta := int(target.get("eta", 0))
	var eta_label := Label.new()
	eta_label.text = "Arrives now" if eta <= 0 else "%d turn%s" % [eta, "" if eta == 1 else "s"]
	top.add_child(eta_label)
	box.add_child(top)
	var words: Array = ["Pop %d / %d" % [int(target.population), int(target.max_population)],
		target.get("size_name", ""), target.get("climate_name", ""), target.get("minerals_name", "")]
	if target.has("special_name"):
		words.append(target.special_name)
	var line := Label.new()
	line.text = " · ".join(words.filter(func(w: Variant) -> bool: return String(w) != ""))
	line.theme_type_variation = &"Dim"
	line.add_theme_font_size_override("font_size", 13)
	line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	box.add_child(line)
	if target.has("reason"):
		box.add_child(_warning(String(target.reason)))
	if not can_send:
		row.modulate.a = 0.55
	row.gui_input.connect(func(event: InputEvent):
		var click := event as InputEventMouseButton
		if click and click.pressed and click.button_index == MOUSE_BUTTON_LEFT and can_send:
			send_transport(int(target.colony)))
	return row


## The colonists to colony `to`, as the original's list sends them (Send_Cluster).
func send_transport(to: int) -> void:
	if _transport_pops.is_empty():
		return
	command.emit({"verb": "send_colonists", "from": colony_id, "pop": int(_transport_pops[0]), "to": to, "job": -1,
		"_reply_to": "_after_transport"})
	close_transport()


func _after_transport(result: Dictionary) -> void:
	transported.emit(int(result.get("moved", 0)))
	refresh()


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
		preload("../themes.gd").move_x(self, hidden_x)
	_tween = create_tween().set_parallel().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_method(func(x: float): preload("../themes.gd").move_x(self, x), position.x, _shown_left if opening else hidden_x, SLIDE_TIME)
	_tween.tween_property(self, "modulate:a", 1.0 if opening else 0.0, SLIDE_TIME)
	if not opening:
		_tween.chain().tween_callback(hide)


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	var build_pane := get_parent().get_node_or_null("BuildPane")
	if build_pane != null and build_pane.visible:
		return # Esc cancels the build pane first
	if key and key.pressed and key.keycode == KEY_ESCAPE and _transport_flyout != null and _transport_flyout.visible:
		close_transport() # the transport list first; the colonists stay
		get_viewport().set_input_as_handled()
		return
	if visible and key and key.pressed and key.keycode == KEY_ESCAPE and _carry.is_empty():
		close()
		get_viewport().set_input_as_handled()


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return ("-" if value < 0 else "") + digits + out
