extends "side_panel.gd"
## Colonies (WP3.5a, the owner's empire panels in notes/antares/ui-concepts.md): the legacy
## Colonies screen as a panel that flies out from the right, against the side tabs, from under the
## bar to above the map controls, with the native galaxy zoomed out beside it in place of the
## screen's small map. From the `colonies`
## query: a row per colony with its name, its farmers, workers and scientists (the colonists'
## pictures), what it builds and its population; a click on a column's title sorts by it (again:
## the other way). Under the table, in a strip (owner, 2026-11-14: the table gets the whole width):
## on the left the hovered colony's planet and progress box (two columns) over its output and
## morale (the `colony` query's rows, a two-column grid), on the right the empire's totals; every
## box keeps one size whatever the colony (owner: they shifted while scrolling the list).
##
## Colonists move as on the legacy screen: a click picks up a colonist and the identical ones after
## it, which ride on the pointer; a click on another job of the same colony gives them that job
## (`move_colonist`), on another colony's job (or its name, for any job) sends them there as
## settlers (`send_colonists`: the game's questions and messages); anywhere else, a right click or
## Esc puts them back. BUY buys (the game asks first; again un-buys). A click on the Building slot
## asks the frontend for that colony's build pane (`build_chosen`), one on its name for its system
## and colony pane (`colony_opened`); a click elsewhere on a row selects it, and the galaxy marks
## its star (`star_marked`).

signal command(dict: Dictionary)
## The Building slot: the frontend opens that colony's system, colony pane and build pane.
signal build_chosen(colony_id: int, star: int, planet: int, bought: bool)
## The colony's name: the frontend opens its system and colony pane (owner, 2026-11-14).
signal colony_opened(colony_id: int, star: int, planet: int)
## The star the galaxy marks (the hovered or selected colony's), or -1.
signal star_marked(star: int)
## The panel's width changed (its rows arrived): the galaxy fits the space left of it again.
signal width_changed

const Themes := preload("../themes.gd")
## The columns: their sort key, title and width (the job columns share what is left).
const COLUMNS := [
	{"key": "name", "title": "NAME", "width": 110.0},
	{"key": "farmers", "title": "FARMERS", "width": 0.0},
	{"key": "workers", "title": "WORKERS", "width": 0.0},
	{"key": "scientists", "title": "SCIENTISTS", "width": 0.0},
	{"key": "building", "title": "BUILDING", "width": 116.0}, # owner: room to the job columns
	{"key": "population", "title": "POP", "width": 56.0},
]
const BUY_WIDTH := 50.0 # at least; the theme's button may need more (_buy_width)
## A line of the output grid (food, research; industry, BC; morale).
const OUTPUT_LINE := 18.0
const ROW_HEIGHT := 46.0
const MIN_JOB_WIDTH := 80.0
## A table row's padding: the theme's row style with its sides and ends kept small.
const ROW_PAD := Vector2(22, 2)
const PERSON := Vector2(18, 26)
const OUTPUT_ICON := Vector2(16, 16)
const SEPARATION := 4.0
## The natural first direction of each sort: names A to Z, the build list's order, the rest
## largest first (the legacy screen's own sorts).
const DESCENDING_FIRST := ["farmers", "workers", "scientists", "population"]
const JOB_OUTPUT := ["food", "industry", "research"]
const GOOD_COLOR := Color(0.45, 0.9, 0.45)
const BAD_COLOR := Color(1.0, 0.4, 0.35)

var art: Node
var colonies: Array = [] # the `colonies` query's rows, in the shown order
var empire := {}
var sort_key := "name"
var sort_descending := false
var selected := -1 # the selected colony (its row lit)
var _shown := -1 # the colony in the info column (hovered, else selected)
var _job_width := 120.0
var _carry := {} # {colony, pops, job, view}: colonists on the pointer
var _job_cells := {} # Vector2i(colony, job) -> its cell (drop targets)
var _name_cells := {} # colony -> its name cell (drop target: any job)
var _rows := {} # colony -> its row panel

@onready var rows_box: VBoxContainer = $Box/Body/Table/Scroll/Rows
@onready var columns_box: HBoxContainer = $Box/Body/Table/Columns
@onready var scroll: ScrollContainer = $Box/Body/Table/Scroll
@onready var scan_lines: GridContainer = $Box/Body/Info/Colony/Scan/Lines
@onready var output_rows: GridContainer = $Box/Body/Info/Colony/Output/Rows
@onready var empire_grid: GridContainer = $Box/Body/Info/Empire/Grid


func _ready() -> void:
	$Box/Header/Close.pressed.connect(close)
	scroll.resized.connect(func(): _fill_jobs.call_deferred())
	# The output box's three lines, filled or not: the strip keeps its height.
	$Box/Body/Info/Colony/Output.custom_minimum_size.y = 3.0 * OUTPUT_LINE + 2.0 * 3.0 + \
		_box_padding_y($Box/Body/Info/Colony/Output)
	# Its right edge stays against the tabs if it grows (the rows arrive after it opened).
	resized.connect(func():
		if visible and not _closing and _rect.size.x > 0.0:
			if _tween == null or not _tween.is_running():
				preload("../themes.gd").move_x(self, _rect.end.x - size.x)
			width_changed.emit())


## The theme's row (or lit row) as it draws, its contents pulled out over the style's wide padding
## (a table row is one line of pictures): the wrapper's negative margins.
func _style_row(panel: PanelContainer, lit: bool) -> void:
	panel.theme_type_variation = &"RowOpen" if lit else &"Row"
	var style := panel.get_theme_stylebox("panel", panel.theme_type_variation)
	var wrap := panel.get_child(0) as MarginContainer if panel.get_child_count() > 0 else null
	if wrap == null or style == null:
		return
	wrap.add_theme_constant_override("margin_left", -int(maxf(style.get_margin(SIDE_LEFT) - ROW_PAD.x, 0.0)))
	wrap.add_theme_constant_override("margin_right", -int(maxf(style.get_margin(SIDE_RIGHT) - ROW_PAD.x, 0.0)))
	wrap.add_theme_constant_override("margin_top", -int(maxf(style.get_margin(SIDE_TOP) - ROW_PAD.y, 0.0)))
	wrap.add_theme_constant_override("margin_bottom", -int(maxf(style.get_margin(SIDE_BOTTOM) - ROW_PAD.y, 0.0)))
	# The selected colony's name in the theme's "on" colour (the lit row alone didn't show in
	# every theme).
	var name_label: Label = panel.get_meta("name_label", null)
	if name_label != null:
		if lit:
			name_label.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "Button"))
		else:
			name_label.remove_theme_color_override("font_color")


## The padding a table row's contents get on the left and right.
func _row_pad(side: Side) -> float:
	var style := get_theme_stylebox("panel", &"Row")
	return minf(style.get_margin(side), ROW_PAD.x) if style != null else ROW_PAD.x


## Flies out into `area` (its right edge against the side tabs, from under the bar to above the
## map controls), as wide as the table wants but leaving the galaxy most of the screen.
func open_panel(area: Rect2) -> void:
	var width := _wanted_width(area)
	refresh()
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))


## As wide as the table wants with the narrowest job columns, but a share of the window; the job
## columns then take whatever the panel has left (_fill_jobs, owner 2026-11-14: the room went to
## the right of BUY).
func _wanted_width(area: Rect2) -> float:
	custom_minimum_size.x = 0.0
	_job_width = MIN_JOB_WIDTH # narrow first, so the panel can shrink
	_show_columns()
	_show_rows()
	return shared_width(area)


## The width Colonies and Planets both take in `area` (owner, 2026-11-15: the galaxy's zoom must
## not change between them): a share of the window, never narrower than the table with the
## narrowest job columns. Worked out from the columns, not measured: a measure taken just after
## the panel was resized still held its old width, and each switch between the two panels added
## the scroll bar's room again (the panels grew and the galaxy shrank).
func shared_width(area: Rect2) -> float:
	var table := MIN_JOB_WIDTH * 3.0 + _buy_width() + SEPARATION * 6.0 + _row_pad(SIDE_LEFT) + _row_pad(SIDE_RIGHT)
	for column: Dictionary in COLUMNS:
		table += float(column.width)
	var style := get_theme_stylebox("panel")
	var frame := style.get_minimum_size().x if style != null else 0.0
	var width := maxf(clampf(area.size.x * 0.45, 700.0, 960.0), table + 14.0 + preload("../themes.gd").SCROLL_GAP + frame) # the scroll bar and its gap
	return minf(width, area.size.x - 16.0)


## The job columns share the table's width beyond the fixed columns, every row the same.
func _fill_jobs() -> void:
	if not visible or scroll.size.x <= 0.0:
		return
	var fixed := 0.0
	for column: Dictionary in COLUMNS:
		fixed += float(column.width)
	fixed += _buy_width() + SEPARATION * 6.0 + _row_pad(SIDE_LEFT) + _row_pad(SIDE_RIGHT)
	# The bar as it is drawn, gap included (owner 2026-11-24, 0.7.0: a fixed 14 px after the
	# scroll gap made the rows 10 px too wide; the panel grew to fit them, the columns widened
	# again, and the galaxy refitted every frame until it crashed).
	var bar := maxf(scroll.get_v_scroll_bar().get_combined_minimum_size().x, 14.0)
	var width := floorf(maxf((scroll.size.x - bar - fixed) / 3.0, MIN_JOB_WIDTH))
	if absf(width - _job_width) >= 1.0:
		_job_width = width
		_show_columns()
		_show_rows()


## The panel's width once open (the galaxy fits the space left of it).
func panel_width() -> float:
	return _rect.end.x - rest_left()


func refresh() -> void:
	command.emit({"verb": "query", "what": "colonies", "_reply_to": "show_colonies"})


func show_colonies(result: Dictionary) -> void:
	if not result.get("ok", false):
		return
	colonies = result.colonies
	empire = result.get("empire", {})
	_sort()
	var ids := colonies.map(func(c: Dictionary) -> int: return int(c.colony))
	if not ids.has(selected):
		selected = ids[0] if not ids.is_empty() else -1
	if not ids.has(_shown):
		_shown = selected
	_show_rows()
	_clear(scan_lines) # fresh figures: the info column is filled again
	_show_info(_shown)
	_show_empire()


## Selects the first colony at `star` (a click on the galaxy), scrolled into view; false if the
## player has none there.
func select_star(star: int) -> bool:
	for c: Dictionary in colonies:
		if int(c.star) == star:
			_select(int(c.colony))
			var row: Control = _rows.get(int(c.colony))
			if row != null:
				scroll.ensure_control_visible.call_deferred(row)
			return true
	return false


func _select(colony_id: int) -> void:
	selected = colony_id
	for id: int in _rows:
		_style_row(_rows[id], id == selected)
	_show_info(colony_id)


func close() -> void:
	_drop_carry()
	if visible and not _closing:
		star_marked.emit(-1)
	super.close()


func dismiss() -> void:
	_drop_carry()
	super.dismiss()


# ---- Sorting ----

func _show_columns() -> void:
	for child in columns_box.get_children():
		columns_box.remove_child(child)
		child.queue_free()
	columns_box.add_theme_constant_override("separation", int(SEPARATION))
	var lead := Control.new() # the rows' left padding, so the titles sit over their cells
	lead.custom_minimum_size.x = maxf(_row_pad(SIDE_LEFT) - SEPARATION, 0.0)
	columns_box.add_child(lead)
	for column: Dictionary in COLUMNS:
		var key := String(column.key)
		var b := Button.new()
		b.flat = true
		b.theme_type_variation = &"ListItem"
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size = Vector2(_width_of(key), 28)
		b.clip_text = true
		b.text = String(column.title)
		if key == sort_key:
			b.text += "  ▼" if sort_descending else "  ▲"
		b.tooltip_text = "Sort by this column (again: the other way)"
		if key == sort_key:
			b.add_theme_color_override("font_color", get_theme_color("font_pressed_color", "ListItem"))
		b.pressed.connect(_sort_by.bind(key))
		columns_box.add_child(b)
	var tail := Control.new() # over the BUY buttons and the rows' right padding
	tail.custom_minimum_size.x = _buy_width() + _row_pad(SIDE_RIGHT)
	columns_box.add_child(tail)


func _width_of(key: String) -> float:
	for column: Dictionary in COLUMNS:
		if column.key == key:
			return float(column.width) if float(column.width) > 0.0 else _job_width
	return 0.0


func _sort_by(key: String) -> void:
	if key == sort_key:
		sort_descending = not sort_descending
	else:
		sort_key = key
		sort_descending = key in DESCENDING_FIRST
	_show_columns()
	_sort()
	_show_rows()


func _sort() -> void:
	colonies.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var order := _compare(a, b)
		if order == 0:
			order = String(a.name).naturalnocasecmp_to(String(b.name))
		if order == 0:
			order = int(a.colony) - int(b.colony)
		return order > 0 if sort_descending else order < 0)


## Ascending order of `a` and `b` by the sort column (negative: a first).
func _compare(a: Dictionary, b: Dictionary) -> int:
	match sort_key:
		"name":
			return String(a.name).naturalnocasecmp_to(String(b.name))
		"farmers", "workers", "scientists":
			var job := ["farmers", "workers", "scientists"].find(sort_key)
			var by_count := _job_count(a, job) - _job_count(b, job)
			return by_count if by_count != 0 else int(a[JOB_OUTPUT[job]]) - int(b[JOB_OUTPUT[job]])
		"building":
			return int(a.build.rank) - int(b.build.rank)
		"population":
			return int(a.pops) - int(b.pops)
	return 0


static func _job_count(colony: Dictionary, job: int) -> int:
	return (colony.jobs[job].colonists as Array).size()


# ---- Rows ----

func _show_rows() -> void:
	for child in rows_box.get_children():
		rows_box.remove_child(child)
		child.queue_free()
	_rows.clear()
	_job_cells.clear()
	_name_cells.clear()
	for c: Dictionary in colonies:
		rows_box.add_child(_row(c))
	if colonies.is_empty():
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "No colonies"
		rows_box.add_child(none)


func _row(c: Dictionary) -> Control:
	var id := int(c.colony)
	var panel := PanelContainer.new()
	var wrap := MarginContainer.new()
	wrap.mouse_filter = Control.MOUSE_FILTER_IGNORE
	panel.add_child(wrap)
	panel.mouse_filter = Control.MOUSE_FILTER_PASS
	panel.mouse_entered.connect(func(): _show_info(id))
	panel.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT and _carry.is_empty():
			_select(id)
			star_marked.emit(int(c.star)))
	_rows[id] = panel
	panel.ready.connect(func(): _style_row(panel, id == selected)) # its theme is known in the tree
	var line := HBoxContainer.new()
	line.add_theme_constant_override("separation", int(SEPARATION))
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	wrap.add_child(line)
	# Every cell in a holder of its column's width that clips (owner, 2026-11-14: a crowded job row
	# or a long building name came out a few pixels wider and pushed POP and BUY out of line).
	var name_cell := _name_cell(c)
	panel.set_meta("name_label", name_cell.get_meta("name_label"))
	_name_cells[id] = _add_fixed(line, name_cell, _width_of("name"))
	for job in 3:
		_job_cells[Vector2i(id, job)] = _add_fixed(line, _job_cell(c, job), _job_width)
	_add_fixed(line, _build_cell(c), _width_of("building"))
	var pops := Label.new()
	pops.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	pops.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	pops.text = "%d/%d" % [int(c.pops), int(c.max_population)]
	pops.tooltip_text = "Colonists / most the planet holds"
	pops.mouse_filter = Control.MOUSE_FILTER_PASS
	_add_fixed(line, pops, _width_of("population"))
	var buy_box := CenterContainer.new()
	buy_box.mouse_filter = Control.MOUSE_FILTER_IGNORE
	buy_box.add_child(_buy_button(c))
	_add_fixed(line, buy_box, _buy_width())
	return panel


## `content` filling a holder exactly `width` wide and a row tall, clipped; returns the holder.
func _add_fixed(line: HBoxContainer, content: Control, width: float) -> Control:
	var holder := Control.new()
	holder.custom_minimum_size = Vector2(width, ROW_HEIGHT)
	holder.clip_contents = true
	holder.mouse_filter = Control.MOUSE_FILTER_IGNORE
	holder.add_child(content)
	content.custom_minimum_size.x = 0.0
	content.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	line.add_child(holder)
	return holder


## The name, with the legacy row's label under it: an event at the colony, or a blockade.
func _name_cell(c: Dictionary) -> Control:
	var box := VBoxContainer.new()
	box.custom_minimum_size = Vector2(_width_of("name"), ROW_HEIGHT)
	box.alignment = BoxContainer.ALIGNMENT_CENTER
	box.add_theme_constant_override("separation", -2)
	box.mouse_filter = Control.MOUSE_FILTER_PASS
	var name_label := Label.new()
	name_label.theme_type_variation = &"Name"
	name_label.text = String(c.name)
	name_label.clip_text = true
	name_label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	box.add_child(name_label)
	box.set_meta("name_label", name_label)
	var note := ""
	var color := BAD_COLOR
	if c.has("event"):
		note = String(c.event)
		color = GOOD_COLOR if c.get("event_good", false) else BAD_COLOR
	elif c.get("blockaded", false):
		note = "Blockaded"
	if note != "":
		var label := Label.new()
		label.theme_type_variation = &"Dim"
		label.text = note
		label.clip_text = true
		label.add_theme_color_override("font_color", color)
		label.add_theme_font_size_override("font_size", 12)
		label.mouse_filter = Control.MOUSE_FILTER_IGNORE
		box.add_child(label)
	box.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	box.tooltip_text = "Open the colony"
	box.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT and _carry.is_empty():
			_select(int(c.colony))
			colony_opened.emit(int(c.colony), int(c.star), int(c.planet))
			box.accept_event())
	return box


## One job's colonists, squeezed to the cell as the legacy screen squeezes them.
func _job_cell(c: Dictionary, job: int) -> Control:
	var id := int(c.colony)
	var cell := HBoxContainer.new()
	cell.custom_minimum_size = Vector2(_job_width, ROW_HEIGHT)
	cell.clip_contents = true
	cell.mouse_filter = Control.MOUSE_FILTER_PASS
	var people: Array = c.jobs[job].colonists
	if people.is_empty():
		if job == 0 and int(c.max_farms) == 0:
			var none := Label.new()
			none.theme_type_variation = &"Dim"
			none.text = "No farming"
			none.custom_minimum_size.y = ROW_HEIGHT
			none.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
			cell.add_child(none)
		return cell
	var step := PERSON.x + 2.0
	if people.size() > 1 and people.size() * step > _job_width - 4.0:
		step = maxf((_job_width - 4.0 - PERSON.x) / float(people.size() - 1), 3.0)
	cell.add_theme_constant_override("separation", int(step - PERSON.x))
	var carried: Array = _carry.get("pops", []) if int(_carry.get("colony", -1)) == id and int(_carry.get("job", -1)) == job else []
	for person: Dictionary in people:
		var icon := _sprite("RACEICON.LBX", int(person.sprite), PERSON)
		icon.mouse_filter = Control.MOUSE_FILTER_STOP
		if carried.has(int(person.pop)):
			icon.modulate.a = 0.0 # on the pointer
		var cluster: Array = person.get("cluster", [person.pop])
		icon.gui_input.connect(func(event: InputEvent) -> void:
			var mb := event as InputEventMouseButton
			if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT and _carry.is_empty():
				_pick_up(c, cluster, job)
				accept_event())
		cell.add_child(icon)
	return cell


## What the colony builds and in how many turns; a click opens its build pane.
func _build_cell(c: Dictionary) -> Control:
	var build: Dictionary = c.build
	var b := Button.new()
	b.flat = true
	b.theme_type_variation = &"ListItem"
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size = Vector2(_width_of("building"), ROW_HEIGHT)
	b.alignment = HORIZONTAL_ALIGNMENT_LEFT
	b.clip_text = true
	b.add_theme_font_size_override("font_size", 14)
	var text := String(build.get("name", "Nothing"))
	var turns := int(build.get("turns", 0))
	var bought := int(build.get("bought", 0)) > 0
	var second := "Bought" if bought else ("%d turn%s" % [turns, "" if turns == 1 else "s"] if turns > 0 else "")
	if build.get("auto", false):
		second = ("Auto" + (" Q%d" % int(build.preset) if build.has("preset") else "")) + ("  " + second if second != "" else "")
	b.text = text + ("\n" + second if second != "" else "")
	b.tooltip_text = "%s\nClick: the build list" % text
	b.pressed.connect(func():
		if _carry.is_empty():
			build_chosen.emit(int(c.colony), int(c.star), int(c.planet), bought))
	return b


var _buy_w := 0.0


## The BUY column's width: the widest the button gets in this theme (owner: Chrome's frame art is
## wider than the old 50 px, so the holder cut its left edge off).
func _buy_width() -> float:
	if _buy_w <= 0.0 and is_inside_tree():
		var probe := Button.new()
		probe.add_theme_font_size_override("font_size", 12)
		add_child(probe)
		for text in ["BUY", "BOUGHT"]:
			probe.text = text
			_buy_w = maxf(_buy_w, probe.get_combined_minimum_size().x)
		probe.free()
		_buy_w = maxf(ceilf(_buy_w), BUY_WIDTH)
	return _buy_w if _buy_w > 0.0 else BUY_WIDTH


func _buy_button(c: Dictionary) -> Control:
	var build: Dictionary = c.build
	var b := Button.new()
	b.focus_mode = Control.FOCUS_NONE
	b.custom_minimum_size = Vector2(_buy_width(), 30)
	b.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	b.add_theme_font_size_override("font_size", 12)
	if int(build.get("bought", 0)) > 0:
		b.text = "BOUGHT"
		b.toggle_mode = true
		b.set_pressed_no_signal(true)
		b.tooltip_text = "Bought: click to take the purchase back"
		Themes.glow(b)
	else:
		b.text = "BUY"
		var cost := int(build.get("buy_cost", 0))
		if build.get("can_buy", false):
			b.tooltip_text = "Finish it next turn for %s BC" % _thousands(cost)
		else:
			b.disabled = true
			b.tooltip_text = ("Not enough BC: %s BC" % _thousands(cost)) if cost > int(empire.get("reserve", 0)) else "Can't be bought"
	b.pressed.connect(func():
		if _carry.is_empty():
			command.emit({"verb": "buy", "colony": int(c.colony), "_reply_to": "_after_command"}))
	return b


# ---- The info column ----

func _row_data(colony_id: int) -> Dictionary:
	for c: Dictionary in colonies:
		if int(c.colony) == colony_id:
			return c
	return {}


## The legacy screen's scan box for a colony, its output and morale (the `colony` query), and the
## galaxy's mark on its star.
func _show_info(colony_id: int) -> void:
	var c := _row_data(colony_id)
	if not c.is_empty():
		star_marked.emit(int(c.star))
	if colony_id == _shown and scan_lines.get_child_count() > 0:
		return
	_shown = colony_id
	_clear(scan_lines)
	if c.is_empty():
		_clear(output_rows)
		return
	var scan: Dictionary = c.scan
	# Two columns of four lines, always eight cells (a missing line is blank), so the box keeps
	# its size: the planet on the left, the people and the build on the right.
	var growth := int(scan.growth)
	# The legacy box's "781k +43k (0)": the next colonist's thousands so far, the growth a turn and
	# the colonists that makes next turn.
	var growing := "Growing %dk  %+dk/turn" % [int(scan.thousands), growth]
	if int(scan.change) != 0:
		growing += "  (%+d)" % int(scan.change)
	var built := "Built %s / %s  +%d" % [_thousands(int(scan.spent)), _thousands(int(scan.cost)), int(scan.net_industry)] if int(scan.cost) > 0 else ""
	var heading := _scan_line(String(c.name))
	heading.theme_type_variation = &"Name"
	_scan_line("Population %d / %d" % [int(c.pops), int(c.max_population)])
	_scan_line("%s %s" % [scan.size, scan.climate])
	var growth_line := _scan_line(growing)
	growth_line.tooltip_text = "Toward the next colonist (1,000k), and the growth a turn"
	growth_line.mouse_filter = Control.MOUSE_FILTER_PASS
	if growth < 0:
		growth_line.add_theme_color_override("font_color", BAD_COLOR)
	_scan_line(String(scan.gravity))
	_scan_line(built)
	_scan_line(String(scan.minerals))
	_scan_line("En route: %d" % int(scan.inbound) if int(scan.inbound) > 0 else "")
	command.emit({"verb": "query", "what": "colony", "colony": colony_id, "_reply_to": "_show_output"})


func _scan_line(text: String) -> Label:
	var label := Label.new()
	label.theme_type_variation = &"Dim"
	label.text = text
	label.clip_text = true
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	scan_lines.add_child(label)
	return label


## The colony's output and morale as the legacy screen's small rows show them.
func _show_output(result: Dictionary) -> void:
	if not result.get("ok", false) or int(result.colony) != _shown:
		return
	_clear(output_rows)
	# Two cells a line: food | research, industry | BC, morale; each squeezes to its half.
	var cell := maxf((output_rows.size.x - 16.0) * 0.5, 80.0)
	for kind in ["food", "research", "industry", "bc"]:
		output_rows.add_child(_cell(_icon_row(result.rows[kind].icons, cell), cell))
	var morale := HBoxContainer.new()
	morale.add_theme_constant_override("separation", 0)
	morale.tooltip_text = "Morale %+d%%" % (int(result.morale) * 10)
	morale.mouse_filter = Control.MOUSE_FILTER_PASS
	var masks := int(result.get("morale_icons", 0))
	for i in masks:
		morale.add_child(_sprite("COLONY2.LBX", int(result.morale_sprite), OUTPUT_ICON))
	if masks == 0:
		var none := Label.new()
		none.theme_type_variation = &"Dim"
		none.text = "Morale steady"
		none.add_theme_font_size_override("font_size", 12)
		morale.add_child(none)
	output_rows.add_child(_cell(morale, cell))


## A fixed-size cell of the output grid (its contents clipped, never growing the box).
func _cell(content: Control, width: float) -> Control:
	var holder := Control.new()
	holder.custom_minimum_size = Vector2(width, OUTPUT_LINE)
	holder.clip_contents = true
	holder.mouse_filter = Control.MOUSE_FILTER_PASS
	holder.add_child(content)
	content.position = Vector2.ZERO
	content.size = Vector2(width, OUTPUT_LINE)
	return holder


func _box_padding_y(box: Control) -> float:
	var style := box.get_theme_stylebox("panel", box.theme_type_variation)
	return style.get_margin(SIDE_TOP) + style.get_margin(SIDE_BOTTOM) if style != null else 0.0


func _icon_row(runs: Array, width: float) -> Control:
	var total := 0
	for r: Dictionary in runs:
		total += int(r.get("count", 1)) if r.kind != "gap" else 1
	var row := HBoxContainer.new()
	row.mouse_filter = Control.MOUSE_FILTER_IGNORE
	row.custom_minimum_size.y = OUTPUT_ICON.y
	var step := OUTPUT_ICON.x + 1.0
	if total > 1 and total * step > width:
		step = maxf((width - OUTPUT_ICON.x) / float(total - 1), 3.0)
	row.add_theme_constant_override("separation", int(step - OUTPUT_ICON.x))
	for r: Dictionary in runs:
		if r.kind == "gap":
			var gap := Control.new()
			gap.custom_minimum_size = OUTPUT_ICON
			row.add_child(gap)
			continue
		for i in int(r.count):
			var icon := _sprite("COLONY2.LBX", int(r.sprite), OUTPUT_ICON)
			if r.kind == "pollution" or r.kind == "import":
				icon.modulate = Color(1, 1, 1, 0.75)
			elif r.kind == "short":
				icon.modulate = BAD_COLOR
			row.add_child(icon)
	return row


## The empire box (the legacy screen's: reserve, income, population, freighters, food, research).
func _show_empire() -> void:
	_clear(empire_grid)
	var population := str(int(empire.get("population", 0)))
	if int(empire.get("settlers_freighted", 0)) > 0:
		population += " (+%d)" % int(empire.settlers_freighted)
	var income := int(empire.get("income", 0))
	var food := int(empire.get("food", 0))
	for pair in [["Reserve", _thousands(int(empire.get("reserve", 0)))],
			["Income", "%+d" % income],
			["Population", population],
			["Freighters", str(int(empire.get("freighters", 0)))],
			["Food", "%+d" % food],
			["Research", _thousands(int(empire.get("research", 0)))]]:
		var key := Label.new()
		key.theme_type_variation = &"Dim"
		key.text = pair[0]
		key.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		empire_grid.add_child(key)
		var value := Label.new()
		value.text = pair[1]
		value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
		if (pair[0] == "Income" and income < 0) or (pair[0] == "Food" and food < 0):
			value.add_theme_color_override("font_color", BAD_COLOR)
		empire_grid.add_child(value)


# ---- Colonists on the pointer ----

func _pick_up(c: Dictionary, cluster: Array, job: int) -> void:
	var pops: Array = cluster.map(func(p: Variant) -> int: return int(p))
	var sprites: Array = []
	var natives := false
	for person: Dictionary in c.jobs[job].colonists:
		if pops.has(int(person.pop)):
			sprites.append(int(person.sprite))
			natives = natives or String(person.state) == "native"
	if natives:
		# The game refuses natives at pickup, with its own message (unless a rule allows it).
		command.emit({"verb": "move_colonist", "colony": int(c.colony), "pops": pops, "job": job, "_reply_to": "_after_command"})
		return
	var view := HBoxContainer.new()
	view.top_level = true
	view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	view.add_theme_constant_override("separation", -8 if sprites.size() > 6 else 0)
	view.z_index = 100
	for sprite: int in sprites:
		view.add_child(_sprite("RACEICON.LBX", sprite, PERSON))
	add_child(view)
	_carry = {"colony": int(c.colony), "pops": pops, "job": job, "view": view}
	Input.set_default_cursor_shape(Input.CURSOR_DRAG)
	_follow_pointer()
	_show_rows()


func _follow_pointer() -> void:
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.global_position = get_global_mouse_position() + Vector2(10, 6)


func _drop_carry() -> void:
	if _carry.is_empty():
		return
	var view: Control = _carry.get("view")
	if is_instance_valid(view):
		view.queue_free()
	_carry = {}
	Input.set_default_cursor_shape(Input.CURSOR_ARROW)
	if is_node_ready() and visible:
		_show_rows()


func _input(event: InputEvent) -> void:
	if _carry.is_empty():
		return
	if event is InputEventMouseMotion:
		_follow_pointer()
		return
	var key := event as InputEventKey
	if key and key.pressed and key.keycode == KEY_ESCAPE:
		_drop_carry()
		get_viewport().set_input_as_handled()
		return
	var mb := event as InputEventMouseButton
	if mb == null or not mb.pressed or mb.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		return
	get_viewport().set_input_as_handled() # the click is the drop, nothing else
	if mb.button_index != MOUSE_BUTTON_LEFT:
		_drop_carry()
		return
	var to := -1
	var job := -1
	for cell_key: Vector2i in _job_cells:
		var cell: Control = _job_cells[cell_key]
		if cell.is_visible_in_tree() and cell.get_global_rect().has_point(mb.global_position):
			to = cell_key.x
			job = cell_key.y
	if to < 0:
		for id: int in _name_cells:
			var cell: Control = _name_cells[id]
			if cell.is_visible_in_tree() and cell.get_global_rect().has_point(mb.global_position):
				to = id
	var from := int(_carry.colony)
	var from_job := int(_carry.job)
	var pops: Array = _carry.pops
	_drop_carry()
	if to < 0:
		return
	if to == from:
		if job >= 0 and job != from_job:
			command.emit({"verb": "move_colonist", "colony": from, "pops": pops, "job": job, "_reply_to": "_after_command"})
	else:
		command.emit({"verb": "send_colonists", "from": from, "pop": int(pops[0]), "to": to, "job": job, "_reply_to": "_after_command"})


## After a move or a purchase: the list again (the game recalculated the colonies).
func _after_command(_result: Dictionary) -> void:
	refresh()


# ---- Helpers ----

func _clear(holder: Control) -> void:
	for child in holder.get_children():
		holder.remove_child(child)
		child.queue_free()


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


func _unhandled_key_input(event: InputEvent) -> void:
	if not _carry.is_empty():
		return
	super._unhandled_key_input(event)


static func _thousands(value: int) -> String:
	var digits := str(absi(value))
	var out := ""
	while digits.length() > 3:
		out = "," + digits.substr(digits.length() - 3) + out
		digits = digits.substr(0, digits.length() - 3)
	return ("-" if value < 0 else "") + digits + out
