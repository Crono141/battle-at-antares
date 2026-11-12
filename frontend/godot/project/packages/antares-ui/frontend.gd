extends Node
## antares-ui: the native galaxy and its panels (D26: a frontend package, off unless enabled).
## The host (res://main.gd) instances this scene when the package is enabled and sets `host`;
## everything not listed in `claimed_screens` and `claimed_requests` stays the legacy screen.
## The galaxy is the home view (WP3.3a): the game's own art, its stars, fleets and lines from
## watched queries. A star opens the system pane on the left (WP3.3f): the camera flies to the
## star and its planets fade in on the map; Back or Esc returns the map. The screen tabs on the
## right open their screens, Ship Design and Options their flyouts from the right; Game on the bar
## drops the game menu; fleets and colonies cycle from the bottom row. Fleets open the fleet panel (WP3.3c): selected
## ships preview their route on the hovered star and move on a star click. The bar (WP3.3d) shows
## the side bar's figures and sets the tax rate. Ctrl+G switches between these views and the
## legacy ones, for comparing (a developer key: its button is hidden, WP3.3f). Views load from views/ beside this scene, so another package's
## pack can replace any one of them by putting a file at the same path.

## The game's screens (SCREEN_* values, res://screens.gd) this package draws natively.
@export var claimed_screens: Array[int] = [Screens.MAIN, Screens.SHOW_COMMAND_POINTS]
## The request kinds its dialog answers; the rest ("choose") keep the legacy popup.
@export var claimed_requests: PackedStringArray = ["message", "confirm", "warning"]
## Start with the native views on (Ctrl+G switches); `-- --smoke` without `--native` starts off.
@export var start_native := true

var host: Node # res://main.gd, set before this scene enters the tree
var game: AntaresGame
var legacy: Control
var art: Node

@onready var galaxy: Node2D = $Galaxy
@onready var mode_button: Button = $UI/Bar/Mode
@onready var native_layer: Control = $UI/Native
@onready var overlay: Control = $Overlay/Shade

var native := false
var native_screen := Screens.LEGACY
var _galaxy_behind := false # the native galaxy stays visible under a native sub-screen
var _views := {} # scene name -> instance
var _pending := {} # command id -> Callable(result)
var _panel_star := -1 # the star whose system panel is open (reopened after legacy screens)
var _fleet_ship := -1 # the first ship of the fleet whose panel is open (reopened likewise)
var _hovered_star := -1
var _opening_fleet := false # a fleet opens from the system pane: the map stays where it is
var _race_id := -1
## A fleet opened from inside a system (its pane or its map): the system to go back to when the
## fleet panel closes (owner, 2026-11-12), or -1.
var _fleet_from_system := -1
## The camera's view of an open system: the legacy scale it flies to, and how much of the free
## area the system's orbits take.
const SYSTEM_VIEW_SCALE := 7.0
const SYSTEM_VIEW_SHARE := 0.42
## The native view comes in whole (owner, 2026-11-12: buttons popped in, the stars a moment
## later): hidden until the map is placed with its star pictures and the empire's figures are in,
## then faded in (0.25 s, owner). The wait is capped so a missing picture can't hold it back.
const REVEAL_WAIT := 4.0
const REVEAL_TIME := 0.25
var _revealing := false
var _revealed := false
var _have_empire := false
## The game screens whose legacy frame is only the galaxy: a question from one shows over the
## native galaxy instead.
const GALAXY_BEHIND_SCREENS := [Screens.MAIN, Screens.MAIN_NEXT_TURN, Screens.REPORTS, Screens.TURN_SUMMARY]
var _question_over_galaxy := false
var _legacy_after_frame := -1 # the frame count the legacy view waits past to come back, or -1
var _stale_frame: PackedByteArray # the legacy frame held while the question was up
var _legacy_waited := 0.0
var _shot_question := false # smoke: one screenshot of a turn's question over the native galaxy
## Themes (WP3.3g): the enabled theme packages' looks, the one in use, and the frame it draws
## round the whole view.
const Themes := preload("themes.gd")
var _themes: Array = [] # [{id, name, built}]
var theme_id := ""
var _frame: StyleBox
var _frame_view: Control
var _built := {} # the theme in use, as its package built it


func _ready() -> void:
	game = host.game
	legacy = host.legacy
	art = host.art
	galaxy.art = art
	game.native_screen_changed.connect(_on_native_screen)
	game.command_result.connect(_on_result)
	game.request.connect(_on_request)
	game.snapshot.connect(_on_snapshot)
	galaxy.star_clicked.connect(_on_star_clicked)
	galaxy.fleet_clicked.connect(_open_fleet)
	galaxy.star_hovered.connect(_on_star_hovered)
	galaxy.right_clicked.connect(_end_relocating)
	galaxy.planet_clicked.connect(_on_planet_clicked)
	_frame_view = Control.new()
	_frame_view.name = "ScreenFrame"
	_frame_view.set_anchors_preset(Control.PRESET_FULL_RECT)
	_frame_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_frame_view.draw.connect(_draw_frame)
	native_layer.add_child(_frame_view)
	_load_themes()
	mode_button.pressed.connect(func(): _set_native(not native))
	$UI/Bar.hide() # the owner's layout (WP3.3f): Ctrl+G stays, the button goes
	overlay.hide()
	for item: CanvasItem in _reveal_items():
		item.modulate.a = 0.0
	var args := OS.get_cmdline_user_args()
	_set_native("--native" in args or (start_native and not "--smoke" in args))
	if "--smoke" in args and "--native" in args:
		_smoke_native()


## The host is dropping this package (another save's package set, or it was switched off): every
## screen and request goes back to the legacy views.
func detach() -> void:
	_set_native(false)
	legacy.show()
	game.grab_focus()


## New resource packs arrived: views made before them are rebuilt from the new files, and the
## themes found again (a theme package may have come or gone).
func packs_changed() -> void:
	_load_themes()
	for scene_name in _views.keys():
		if scene_name != "dialog":
			_views[scene_name].queue_free()
			_views.erase(scene_name)


func _input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.ctrl_pressed and key.keycode == KEY_G:
		_set_native(not native)
		get_viewport().set_input_as_handled()


## Native views on or off. The game switches at its next safe point: the legacy galaxy hands over
## between inputs, and the native loop hands back at once.
func _set_native(on: bool) -> void:
	native = on
	for screen in claimed_screens:
		game.claim_screen(screen, on)
	game.claim_requests(claimed_requests if on else PackedStringArray())
	if on:
		for what in ["empire", "galaxy", "fleets", "options"]:
			_send({"verb": "watch", "what": what})
	mode_button.text = "Legacy galaxy (Ctrl+G)" if on else "Native galaxy (Ctrl+G)"


func _process(delta: float) -> void:
	if _legacy_after_frame < 0:
		return
	_legacy_waited += delta
	var frames := int(game.get_stats().frames)
	if frames <= _legacy_after_frame and _legacy_waited < 3.0:
		return
	_legacy_after_frame = frames
	# A new frame, but the game may still be presenting the old page: wait for a different one.
	var image: Image = game.get_frame_image()
	if image != null and image.get_data() == _stale_frame and _legacy_waited < 3.0:
		return
	_legacy_after_frame = -1
	if native_screen == Screens.LEGACY and not _question_over_galaxy:
		galaxy.hide()
		legacy.show()


func _on_native_screen(screen: int) -> void:
	_legacy_after_frame = -1
	if screen == Screens.MAIN:
		_galaxy_behind = true
	elif screen == Screens.LEGACY:
		_galaxy_behind = false
	native_screen = screen
	var in_galaxy := screen == Screens.MAIN or (screen != Screens.LEGACY and _galaxy_behind)
	legacy.visible = not in_galaxy and screen == Screens.LEGACY
	galaxy.visible = in_galaxy
	galaxy.set_process_unhandled_input(screen == Screens.MAIN)
	var bar := _view("galaxy_bar")
	bar.visible = screen == Screens.MAIN
	var controls := _view("map_controls")
	controls.visible = screen == Screens.MAIN
	_view("side_tabs").visible = screen == Screens.MAIN
	_frame_view.visible = screen == Screens.MAIN
	var options := _view("options_menu")
	options.keys_active = screen == Screens.MAIN
	var game_menu := _view("game_menu")
	var points := _view("command_points")
	var research := _view("research_panel")
	var system_pane := _view("system_pane")
	var design_list := _view("design_list")
	var fleet_panel := _view("fleet_panel")
	if screen == Screens.MAIN:
		if not _revealed and not _revealing:
			_reveal()
		galaxy.camera.make_current()
		_send({"verb": "query", "what": "view"}, galaxy.place_view)
		if _panel_star >= 0:
			_open_system(_panel_star, false) # refreshed: a colony screen may have changed it
		if design_list.get_meta("was_open", false):
			design_list.set_meta("was_open", false)
			design_list.open()
		if _fleet_ship >= 0:
			_refresh_fleet()
	else:
		system_pane.hide() # hidden, not closed: it comes back with the galaxy
		design_list.set_meta("was_open", design_list.visible or design_list.get_meta("was_open", false))
		design_list.hide()
		if options.visible:
			options.close()
		if game_menu.visible:
			game_menu.hide()
			game_menu.closed.emit()
		fleet_panel.hide()
		research.dismiss()
		if screen != Screens.SHOW_COMMAND_POINTS:
			points.dismiss()
		_end_relocating()
		if screen == Screens.SHOW_COMMAND_POINTS:
			# Opened as the game's screen (from elsewhere): the same drop-down, closing it closes
			# the screen.
			_galaxy_behind = true
			galaxy.show()
			legacy.hide()
			points.as_screen = true
			_drop_down("command_points", points, "", get_viewport().get_visible_rect().size.x * 0.5)
	if screen == Screens.LEGACY:
		game.grab_focus()


## What fades in together: the starfield, the map and the panels.
func _reveal_items() -> Array[CanvasItem]:
	return [$Backdrop/Starfield, galaxy, native_layer]


func _reveal() -> void:
	_revealing = true
	var waited := 0.0
	while waited < REVEAL_WAIT and not (galaxy.ready_to_show() and _have_empire):
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
	var tween := create_tween().set_parallel()
	for item: CanvasItem in _reveal_items():
		tween.tween_property(item, "modulate:a", 1.0, REVEAL_TIME)
	_revealed = true
	_revealing = false


## Fills one of the bar's drop-downs from its query and slides it down under `center`.
func _drop_down(action_name: String, panel: Control, research_text: String, center: float) -> void:
	if action_name == "command_points":
		var points: Dictionary = await _query("command_points")
		panel.show_result(points)
	else:
		panel.set_progress(research_text)
		var research: Dictionary = await _query("research")
		panel.show_research(research)
	panel.open_at(center)


## The theme's frame round the view, and its joints where the pockets' sides meet it.
func _draw_frame() -> void:
	if _frame == null:
		return
	_frame_view.draw_style_box(_frame, Rect2(Vector2.ZERO, _frame_view.size))
	var tee: Dictionary = _built.get("tee", {})
	if tee.is_empty():
		return
	var tex: Texture2D = tee.texture
	var scale: float = tee.scale
	var c: float = tee.bar_center
	var size := Vector2(tex.get_size()) * scale
	var anchor: Vector2 = tee.anchor * scale
	for view_name in ["galaxy_bar", "map_controls"]:
		var view: Control = _views.get(view_name)
		if view == null or not view.visible:
			continue
		var box: StyleBox = view.get_theme_stylebox("panel", view.theme_type_variation)
		var grow := Vector2(9, 9)
		if box and "grow" in box:
			grow = Vector2(box.grow[0], box.grow[2])
		var r := view.get_global_rect()
		var top: bool = view_name == "galaxy_bar"
		var y: float = c if top else _frame_view.size.y - c
		for x in [r.position.x - grow.x + c, r.end.x + grow.y - c]:
			# The art is the bottom joint (a bar from above): upside down for the top.
			if top: # upside down, its top flush with the screen's top (owner): mirrored about the
				# joint's own height, so it spans y 0 to its height
				_frame_view.draw_set_transform(Vector2(0, size.y), 0.0, Vector2(1, -1))
				_frame_view.draw_texture_rect(tex, Rect2(Vector2(x - anchor.x, 0), size), false)
				_frame_view.draw_set_transform(Vector2.ZERO)
			else:
				_frame_view.draw_texture_rect(tex, Rect2(Vector2(x - anchor.x, y - anchor.y), size), false)


## The enabled theme packages' themes; the player's saved choice, else the highest priority one,
## else Godot's own look.
func _load_themes() -> void:
	_themes = Themes.discover(game.get_packages())
	var wanted := Themes.saved_choice()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--theme="):
			wanted = arg.substr(8) # the smoke's choice, not the player's
	var chosen := ""
	for entry: Dictionary in _themes:
		if entry.id == wanted:
			chosen = wanted
	if chosen == "" and not _themes.is_empty():
		chosen = _themes[0].id
	apply_theme(chosen, false)


## Puts a theme on every native view (the Options flyout's selector calls this).
func apply_theme(id: String, remember := true) -> void:
	var built := {}
	for entry: Dictionary in _themes:
		if entry.id == id:
			built = entry.built
	theme_id = id if not built.is_empty() else ""
	if built.is_empty():
		built = Themes.plain()
	native_layer.theme = built.theme
	_built = built
	if _views.has("dialog"):
		_views.dialog.theme = built.theme
	_frame = built.get("frame")
	_frame_view.queue_redraw()
	if remember and id != "":
		Themes.save_choice(id)
	if _views.has("options_menu"):
		_views.options_menu.show_themes(_themes, theme_id)
	print("frontend: theme %s" % (built.get("name", "?")))


func _views_dir() -> String:
	return scene_file_path.get_base_dir() + "/views"


## A native view, instanced on first use from views/ beside this scene (so a package's resource
## pack, loaded before, can replace it).
func _view(scene_name: String) -> Control:
	if not _views.has(scene_name):
		var view: Control = load(_views_dir() + "/%s.tscn" % scene_name).instantiate()
		view.visible = false
		if view.has_signal("command"):
			view.command.connect(_on_view_command.bind(view))
		if "art" in view:
			view.art = art
		if view.has_signal("closed"):
			view.closed.connect(_on_view_closed.bind(scene_name))
		if view.has_signal("action"):
			view.action.connect(_on_view_action)
		if view.has_signal("fleet_chosen"):
			view.fleet_chosen.connect(_open_fleet_from_pane)
		if view.has_signal("theme_chosen"):
			view.theme_chosen.connect(apply_theme)
		if view.has_signal("planet_selected"):
			view.planet_selected.connect(_on_pane_planet)
		if view.has_signal("selection_changed"):
			view.selection_changed.connect(func(): _on_star_hovered(_hovered_star))
		native_layer.add_child(view)
		_frame_view.move_to_front() # the frame stays over every view
		_views[scene_name] = view
		if scene_name == "options_menu":
			view.show_themes(_themes, theme_id)
		if scene_name in ["galaxy_bar", "map_controls"]: # the frame's joints follow their pockets
			view.item_rect_changed.connect(_frame_view.queue_redraw)
			view.visibility_changed.connect(_frame_view.queue_redraw)
	return _views[scene_name]


## A view's command; one with "_reply_to" gets its result back through that method of the view
## (the system panel's colony information).
func _on_view_command(dict: Dictionary, view: Control) -> void:
	if dict.has("_reply_to"):
		var method: String = dict["_reply_to"]
		dict.erase("_reply_to")
		_send(dict, Callable(view, method))
	else:
		_send(dict)


## The map controls' view-only actions.
func _on_view_action(action_name: String) -> void:
	match action_name:
		"colony_next", "colony_previous":
			_send({"verb": "cycle_colony_star", "dir": 0 if action_name == "colony_next" else 1}, _on_star_selected)
		"influence":
			$Galaxy/Influence.cycle()
			_view("options_menu").show_influence(galaxy.influence_mode)
		"influence_off", "influence_filled", "influence_borders":
			$Galaxy/Influence.set_mode(["influence_off", "influence_filled", "influence_borders"].find(action_name))
			_view("options_menu").show_influence(galaxy.influence_mode)
		"distance":
			_view("options_menu").close()
			_send({"verb": "open", "screen": Screens.DISTANCE_BETWEEN_STARS})
		"fleet_next", "fleet_previous":
			_cycle_fleet(0 if action_name == "fleet_next" else 1)
		"command_points", "research":
			# The bar's drop-downs (WP3.3g): under their figure, on the galaxy.
			var panel := _view("command_points" if action_name == "command_points" else "research_panel")
			var other := _view("research_panel" if action_name == "command_points" else "command_points")
			other.close()
			if panel.visible:
				panel.close()
			else:
				var bar := _view("galaxy_bar")
				var figure: Control = bar.command_label if action_name == "command_points" else bar.research
				var center := figure.get_global_rect().get_center().x
				_drop_down(action_name, panel, bar.research.text, center)
		"designs":
			var designs := _view("design_list")
			if designs.visible:
				designs.close()
			else:
				_view("options_menu").close()
				designs.open()
				_view("side_tabs").show_open("designs", true)
		"options":
			var options := _view("options_menu")
			if options.visible:
				options.close()
			else:
				_view("design_list").close()
				options.open()
				_view("side_tabs").show_open("options", true)
		"game_menu":
			var game_menu := _view("game_menu")
			if game_menu.visible:
				game_menu.close()
			else:
				var bar := _view("galaxy_bar")
				game_menu.open_at(bar.game_button.get_global_rect().position.x)
				bar.show_game_menu(true)
		"merge_relocations":
			if galaxy.relocating:
				_end_relocating()
			else:
				_send({"verb": "merge_relocations"}, func(result: Dictionary):
					galaxy.relocating = result.get("ready", false)
					if galaxy.relocating:
						Input.set_default_cursor_shape(Input.CURSOR_CROSS))


func _on_view_closed(scene_name: String) -> void:
	if scene_name == "system_pane":
		_panel_star = -1
		galaxy.selected = -1
		galaxy.hide_system()
		if not _opening_fleet:
			galaxy.fly_back()
		galaxy.redraw()
	elif scene_name == "design_list":
		_view("side_tabs").show_open("designs", false)
	elif scene_name == "options_menu":
		_view("side_tabs").show_open("options", false)
	elif scene_name == "game_menu":
		_view("galaxy_bar").show_game_menu(false)
	elif scene_name == "fleet_panel":
		_fleet_ship = -1
		galaxy.selected_fleet = -1
		galaxy.preview = {}
		galaxy.redraw()
		if _fleet_from_system >= 0:
			var star := _fleet_from_system
			_fleet_from_system = -1
			if native_screen == Screens.MAIN:
				_open_system(star) # back into the system, its pane again


func _send(command: Dictionary, on_result: Callable = Callable()) -> void:
	var id: int = game.send_command(command)
	if on_result.is_valid():
		_pending[id] = on_result


func _on_result(id: int, result: Dictionary) -> void:
	if not result.get("ok", false):
		push_warning("command %d failed: %s" % [id, result.get("error", "?")])
	var on_result: Callable = _pending.get(id, Callable())
	_pending.erase(id)
	if on_result.is_valid():
		on_result.call(result)


## Snapshots of watched queries: the galaxy bar follows the empire.
func _on_snapshot(snapshot_name: String, result: Dictionary) -> void:
	match snapshot_name:
		"empire":
			_view("galaxy_bar").show_empire(result)
			_have_empire = _have_empire or result.get("ok", false)
			_race_id = int(result.get("race_id", -1))
			_view("system_pane").race_id = _race_id
		"galaxy":
			galaxy.set_galaxy(result)
		"fleets":
			galaxy.set_fleets(result)
			if _fleet_ship >= 0 and native_screen == Screens.MAIN:
				_refresh_fleet() # a turn, or an order, changed the fleets
		"options":
			_view("options_menu").show_options(result)


func _on_request(id: int, kind: String, request: Dictionary) -> void:
	var dialog: Control = _views.get("dialog")
	if dialog == null:
		dialog = load(_views_dir() + "/dialog.tscn").instantiate()
		dialog.answered.connect(_on_answered)
		dialog.theme = native_layer.theme
		$Overlay.add_child(dialog)
		_views["dialog"] = dialog
	overlay.show()
	# A question from the turn's legacy galaxy (the next turn, its reports, or the galaxy itself
	# while the native one hands over): the native galaxy shows behind it instead of the legacy
	# frame (owner, 2026-11-12). Other legacy screens (combat, colonies) keep their frame.
	if native and native_screen == Screens.LEGACY and not galaxy.stars.is_empty() and int(request.get("screen", -1)) in GALAXY_BEHIND_SCREENS:
		_question_over_galaxy = true
		var held: Image = game.get_frame_image()
		_stale_frame = held.get_data() if held != null else PackedByteArray()
		legacy.hide()
		galaxy.show()
	dialog.ask(id, kind, request)


func _on_answered(id: int, value: int) -> void:
	overlay.hide()
	if _question_over_galaxy:
		_question_over_galaxy = false
		if native_screen == Screens.LEGACY:
			# The legacy view still holds its last frame from before the native galaxy (the title
			# screen after a load): it comes back with the game's next frame (owner, 2026-11-12:
			# the title flashed before a combat).
			_legacy_after_frame = int(game.get_stats().frames)
			_legacy_waited = 0.0
	game.answer_request(id, value)


## A star click: with F4's mode on it takes the merged relocations; with ships selected in the
## fleet panel it moves them there (black holes do nothing, as on the legacy map); otherwise the
## game decides, as the legacy map does, whether it opens the player's colony there (a legacy
## screen for now) or the system panel.
func _on_star_clicked(star_id: int) -> void:
	if galaxy.relocating:
		_end_relocating()
		_send({"verb": "merge_relocations", "star": star_id})
		return
	var fleet_panel := _view("fleet_panel")
	var ships: Array = fleet_panel.selected_ships() if fleet_panel.visible else []
	if not ships.is_empty():
		if not galaxy.stars[star_id].black_hole:
			_send({"verb": "move", "ships": ships, "star": star_id}, func(result: Dictionary):
				galaxy.preview = {}
				fleet_panel.order_done(result))
		return
	_send({"verb": "select_star", "star": star_id}, _on_star_selected)


func _on_star_selected(result: Dictionary) -> void:
	if result.get("ok", false) and result.get("opened", "") != "none":
		_fleet_from_system = -1 # another system opens: not the one the fleet came from
		_view("fleet_panel").dismiss() # the game closed the fleet box for the system box
	if result.get("ok", false) and result.get("opened", "") == "system":
		_open_system(int(result.star))


## Opens (or refreshes) a star's system pane; `fly` sends the camera to the star, with its planets
## filling a share of the area right of the pane.
func _open_system(star_id: int, fly := true) -> void:
	_send({"verb": "query", "what": "system", "star": star_id}, func(result: Dictionary):
		if not result.get("ok", false) or native_screen != Screens.MAIN:
			return
		_panel_star = star_id
		galaxy.selected = star_id
		var pane := _view("system_pane")
		if fly or galaxy.system_star != star_id:
			var view := get_viewport().get_visible_rect().size
			var left := 360.0
			var right := view.x - 240.0
			if right - left < view.x * 0.3: # a small window: the panes cover most of it
				left = 0.0
				right = view.x
			var zoom: float = galaxy.zoom_for_scale(SYSTEM_VIEW_SCALE)
			galaxy.system_scale = SYSTEM_VIEW_SHARE * (right - left) / (288.0 * zoom)
			galaxy.fly_to(galaxy.star_position(star_id), zoom, Vector2((left + right) * 0.5, view.y * 0.5))
		galaxy.show_system(star_id, result)
		galaxy.redraw()
		pane.race_id = _race_id
		pane.show_system(result, String(galaxy.stars[star_id].get("label", "")).trim_prefix("(").trim_suffix(")"), star_id))


## A planet clicked on the map: selected here and in the pane.
func _on_planet_clicked(planet_id: int) -> void:
	galaxy.selected_planet = planet_id
	galaxy.redraw()
	_view("system_pane").select_planet(planet_id)


func _on_pane_planet(planet_id: int) -> void:
	galaxy.selected_planet = planet_id
	galaxy.redraw()


## A fleet from the system pane: the game closes the system box for the fleet box, and the map
## stays at the system.
func _open_fleet_from_pane(ship_id: int) -> void:
	_opening_fleet = true
	_open_fleet(ship_id)


## Opens a fleet's panel (a map icon, the system panel's fleets, F1/F2): the game keeps the
## fleet box record and closes the system box, then the `fleet` query fills the panel.
func _open_fleet(ship_id: int) -> void:
	var from_star := _panel_star if _view("system_pane").visible else -1
	if from_star >= 0:
		_opening_fleet = true # the pane goes, the map stays at the system
	_send({"verb": "open_fleet", "fleet": ship_id}, func(opened: Dictionary):
		if not opened.get("ok", false):
			_opening_fleet = false
			return
		_view("system_pane").dismiss()
		_opening_fleet = false
		if from_star >= 0:
			# From a system: out a little round its star, the map free to pick a destination;
			# closing the fleet panel comes back to the system.
			_fleet_from_system = from_star
			galaxy.step_back(galaxy.star_position(from_star))
		_send({"verb": "query", "what": "fleet", "ship": int(opened.ship)}, func(result: Dictionary):
			if not result.get("ok", false) or native_screen != Screens.MAIN:
				return
			var fleet_panel := _view("fleet_panel")
			var was_visible := fleet_panel.visible
			fleet_panel.show_fleet(result, true)
			_fleet_ship = int(result.ship)
			galaxy.selected_fleet = _fleet_ship
			galaxy.redraw()
			if not was_visible:
				_place_fleet_panel(fleet_panel, Vector2(float(result.x), float(result.y)))))


## The panel's fleet again (after a turn, an order elsewhere or a legacy screen); closed when it
## is gone.
func _refresh_fleet() -> void:
	var fleet_panel := _view("fleet_panel")
	_send({"verb": "query", "what": "fleet", "ship": _fleet_ship}, func(result: Dictionary):
		if native_screen != Screens.MAIN:
			return
		if not result.get("ok", false):
			fleet_panel.close()
			return
		fleet_panel.show_fleet(result)
		_fleet_ship = int(result.ship)
		galaxy.selected_fleet = _fleet_ship
		galaxy.redraw())


## F1 / F2: the previous or next fleet of the open fleet's owner, or the player's first fleet.
func _cycle_fleet(dir: int) -> void:
	var from := _fleet_ship
	if from < 0:
		for fleet: Dictionary in galaxy.fleets:
			if int(fleet.owner) == int(galaxy.data.get("player", -1)):
				_open_fleet(int(fleet.ship))
				galaxy.glide_to(_fleet_position(int(fleet.ship)))
				return
		return
	_send({"verb": "query", "what": "next_fleet", "fleet": from, "dir": dir}, func(result: Dictionary):
		if result.get("ok", false) and int(result.ship) >= 0:
			_open_fleet(int(result.ship))
			galaxy.glide_to(_fleet_position(int(result.ship))))


func _fleet_position(ship_id: int) -> Vector2:
	for fleet: Dictionary in galaxy.fleets:
		if int(fleet.ship) == ship_id:
			return Vector2(float(fleet.x), float(fleet.y))
	return galaxy.camera.position


## The route preview for the hovered star while the fleet panel has ships selected.
func _on_star_hovered(star_id: int) -> void:
	_hovered_star = star_id
	var fleet_panel := _view("fleet_panel")
	var ships: Array = fleet_panel.selected_ships() if fleet_panel.visible else []
	if star_id < 0 or ships.is_empty() or galaxy.relocating:
		if not galaxy.preview.is_empty():
			galaxy.preview = {}
			galaxy.redraw()
		fleet_panel.show_preview({})
		return
	_send({"verb": "query", "what": "move_preview", "ships": ships, "star": star_id}, func(result: Dictionary):
		if star_id != _hovered_star:
			return # the pointer has moved on
		galaxy.preview = {"star": star_id, "line": result.get("line", "")} if result.get("ok", false) else {}
		galaxy.redraw()
		fleet_panel.show_preview(result))


func _end_relocating() -> void:
	if galaxy.relocating:
		galaxy.relocating = false
		Input.set_default_cursor_shape(Input.CURSOR_ARROW)


## The fleet panel beside the fleet, on the side of the window away from it.
func _place_fleet_panel(fleet_panel: Control, world: Vector2) -> void:
	var view := get_viewport().get_visible_rect().size
	var at: Vector2 = galaxy.get_global_transform_with_canvas() * world
	var x := view.x - fleet_panel.size.x - 24.0 if at.x < view.x * 0.5 else 24.0
	var y := clampf(at.y - fleet_panel.size.y * 0.5, 56.0, view.y - fleet_panel.size.y - 80.0)
	fleet_panel.position = Vector2(x, y)


## Headless check of the native bridge (`-- --smoke --native`): the save opens in the native
## galaxy, the empire query answers, command points open natively and close, a turn ends by
## command and the legacy next-turn screens run until the native galaxy comes back.
func _smoke_native() -> void:
	var shots := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots = arg.substr(8)
	var t0 := Time.get_ticks_msec()
	if not await _wait_screen(Screens.MAIN, 120.0):
		await host.smoke_fail("never reached the native galaxy")
		return
	print("smoke: native galaxy after %.1f s, frontend %s, packs %s" % [(Time.get_ticks_msec() - t0) / 1000.0, host.frontend_id, host.loaded_packs])
	# The loaded save's message comes over the native galaxy a moment after it opens.
	for i in 30:
		await get_tree().create_timer(0.1).timeout
		if _smoke_answer():
			break
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--flash="):
			await _smoke_flash(arg.substr(8))
			return
	var empire: Dictionary = await _query("empire")
	print("smoke: empire %s" % empire)
	if not await _smoke_tax(int(empire.tax_rate)):
		return
	if not await _smoke_galaxy(shots):
		return
	_send({"verb": "open", "screen": Screens.SHOW_COMMAND_POINTS})
	if not await _wait_screen(Screens.SHOW_COMMAND_POINTS, 10.0):
		await host.smoke_fail("command points did not open")
		return
	await get_tree().create_timer(0.5).timeout
	var points := _view("command_points")
	print("smoke: command points as a screen: shown %s, '%s' / %s / %s" % [points.visible, points.get_node("Box/Header/Title").text,
		points.get_node("Box/Total").text, points.get_node("Box/Text").text.replace("\n", " | ")])
	points.close() # sends "close": back to the galaxy
	if not await _wait_screen(Screens.MAIN, 10.0):
		await host.smoke_fail("command points did not close")
		return
	await get_tree().create_timer(0.6).timeout
	# The bar's drop-downs (WP3.3g): command points and research stay on the galaxy.
	_on_view_action("command_points")
	await get_tree().create_timer(1.0).timeout
	print("smoke: command points from the bar: shown %s, screen %d, '%s'" % [points.visible, native_screen, points.get_node("Box/Total").text])
	if shots != "":
		await host.shot(shots + "/native-command-points.png")
	if not points.visible or native_screen != Screens.MAIN:
		await host.smoke_fail("command points did not drop down on the galaxy")
		return
	_on_view_action("research")
	await get_tree().create_timer(1.0).timeout
	var research := _view("research_panel")
	var research_state: Dictionary = await _query("research")
	var offered := 0
	for area: Dictionary in research_state.get("areas", []):
		offered += area.get("apps", []).size()
	print("smoke: research drop-down: shown %s, command points closed %s, %d areas, %d applications on offer, current field %d app %d" % [
		research.visible, not points.visible, research.areas_box.get_child_count(), offered, int(research_state.field), int(research_state.app)])
	if shots != "":
		await host.shot(shots + "/native-research.png")
	if not research.visible or research.areas_box.get_child_count() != 8:
		await host.smoke_fail("the research drop-down did not show its areas")
		return
	# Choosing what is already chosen keeps it (and the breakthrough): the command answers.
	var chose := {}
	_send({"verb": "choose_research", "field": int(research_state.field), "app": int(research_state.app)}, func(r: Dictionary): chose["r"] = r)
	while not chose.has("r"):
		await get_tree().process_frame
	print("smoke: choose_research (same choice): %s" % chose.r)
	research.close()
	await get_tree().create_timer(0.5).timeout
	# A legacy screen from the native galaxy: the legacy view must not show the stale frame from
	# before the native view (owner, 2026-11-06: Colonies flashed the old galaxy).
	var stale: Image = game.get_frame_image()
	_send({"verb": "open", "screen": Screens.COLONY_SUMMARY})
	var t_open := Time.get_ticks_msec()
	while native_screen != Screens.LEGACY and Time.get_ticks_msec() - t_open < 10000:
		await get_tree().process_frame
	var shown: Image = game.get_frame_image()
	if native_screen != Screens.LEGACY or (stale and shown and stale.get_data() == shown.get_data()):
		await host.smoke_fail("colonies showed the stale legacy frame")
		return
	print("smoke: colonies shown after %d ms with a new frame" % (Time.get_ticks_msec() - t_open))
	if shots != "":
		await host.shot(shots + "/legacy-colonies.png")
	var closing := 0.0
	while native_screen != Screens.MAIN and closing < 20.0:
		game.send_key(KEY_ESCAPE, false, false, false)
		await get_tree().create_timer(1.0).timeout
		closing += 1.0
	if native_screen != Screens.MAIN:
		await host.smoke_fail("colonies did not close back to the native galaxy")
		return
	# Research is a popup over the galaxy: after another legacy screen it must still get the
	# galaxy palette and backdrop (owner, 2026-11-06: it came up dark over the colonies list).
	_send({"verb": "open", "screen": Screens.TECH_CHANGE})
	t_open = Time.get_ticks_msec()
	while native_screen != Screens.LEGACY and Time.get_ticks_msec() - t_open < 10000:
		await get_tree().process_frame
	await get_tree().create_timer(1.0).timeout
	if shots != "":
		game.get_frame_image().save_png(shots + "/legacy-research.png")
	closing = 0.0
	while native_screen != Screens.MAIN and closing < 20.0:
		game.send_key(KEY_ESCAPE, false, false, false)
		await get_tree().create_timer(1.0).timeout
		closing += 1.0
	if native_screen != Screens.MAIN:
		await host.smoke_fail("research did not close back to the native galaxy")
		return
	print("smoke: research opened and closed")
	var turns := 1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--turns="):
			turns = int(arg.substr(8))
	for turn in turns:
		var visit: int = game.get_native_visit()
		_send({"verb": "end_turn"})
		# Next-turn processing and reports are legacy: answer them as a player would with Esc,
		# and native message boxes with OK / Yes.
		var waited := 0.0
		while (native_screen != Screens.MAIN or game.get_native_visit() == visit) and waited < 90.0:
			await get_tree().create_timer(1.0).timeout
			waited += 1.0
			if int(waited) % 10 == 0:
				print("smoke: waiting %.0f s, screen %d, frames %d" % [waited, native_screen, game.get_stats().frames])
			if not _smoke_answer() and native_screen == Screens.LEGACY:
				# Turn reports close with their CLOSE button (game 442,352); other prompts with Esc.
				if int(waited) % 2 == 0:
					await host.click_game(Vector2(442, 352))
				else:
					game.send_key(KEY_ESCAPE, false, false, false)
		if native_screen != Screens.MAIN or game.get_native_visit() == visit:
			var frame: Image = game.get_frame_image()
			if frame:
				frame.save_png("user://smoke-stuck.png")
				print("smoke: legacy frame saved to %s" % ProjectSettings.globalize_path("user://smoke-stuck.png"))
			await host.smoke_fail("the native galaxy did not come back after the turn")
			return
		empire = await _query("empire")
		print("smoke: after turn %d (%.0f s): %s" % [turn + 1, waited, empire])
	if "--quit-by-menu" in OS.get_cmdline_user_args():
		# The game menu's Quit (WP3.3f): the game's question, answered Yes natively, then it quits.
		_view("game_menu").get_node("Box/Quit").pressed.emit()
		for i in 50:
			await get_tree().create_timer(0.2).timeout
			if _smoke_answer():
				print("smoke: quit asked and answered Yes")
		print("smoke: the game menu's Quit did not end the game")
		get_tree().quit()
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")
	await get_tree().create_timer(15.0).timeout
	print("smoke: the game did not quit within 15 s")
	get_tree().quit()


## The galaxy view (WP3.3a): data and art arrive, the view zooms, a star opens its system panel,
## a right click shows colony information, F5 cycles to a colony.
func _smoke_galaxy(shots: String) -> bool:
	var waited := 0.0
	while (galaxy.stars.is_empty() or galaxy.fleets.is_empty()) and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if galaxy.stars.is_empty():
		await host.smoke_fail("no galaxy snapshot")
		return false
	await get_tree().create_timer(3.0).timeout # art for the stars in view
	var hd := 0
	var loaded := 0
	for key: String in art._sprites:
		if not art._sprites[key].is_empty():
			loaded += 1
			hd += 1 if art._sprites[key].scale > 1.0 else 0
	print("smoke: galaxy %d stars, %d nebulae, %d fleets, %d influence; %d sprites loaded (%d HD), legacy scale %.1f" % [
		galaxy.stars.size(), galaxy.data.nebulae.size(), galaxy.fleets.size(), galaxy.data.influence.size(),
		loaded, hd, galaxy.legacy_scale()])
	if loaded == 0:
		await host.smoke_fail("no art arrived")
		return false
	if shots != "":
		await host.shot(shots + "/native-galaxy.png")
	# The player's first visited colony star with something to show.
	var star := -1
	for s: Dictionary in galaxy.stars:
		if int(s.owner) == int(galaxy.data.player) and s.visited:
			star = int(s.id)
			break
	if star < 0:
		await host.smoke_fail("no colony star")
		return false
	# Prefer one with a governor, to see the portrait (WP3.3f).
	for s: Dictionary in galaxy.stars:
		if int(s.owner) != int(galaxy.data.player) or not s.visited:
			continue
		var box := {}
		_send({"verb": "query", "what": "system", "star": int(s.id)}, func(result: Dictionary): box["r"] = result)
		while not box.has("r"):
			await get_tree().process_frame
		if box.r.has("governor"):
			star = int(s.id)
			break
	galaxy.focus(star)
	galaxy.zoom_by(3.0, get_viewport().get_visible_rect().size * 0.5)
	_on_view_action("influence")
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		await host.shot(shots + "/native-galaxy-zoomed.png")
	_on_star_clicked(star)
	var pane := _view("system_pane")
	waited = 0.0
	while not pane.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if not pane.visible:
		await host.smoke_fail("star %d did not open its system pane" % star)
		return false
	await get_tree().create_timer(1.5).timeout # the flight, the fade and the art
	var governor: Dictionary = pane.system.get("governor", {})
	print("smoke: system pane '%s', %d planets, %d fleets, governor '%s', camera at scale %.1f, orbits alpha %.2f" % [
		pane.star_name.text, pane.system.get("planets", []).size(), pane.system.get("fleets", []).size(),
		governor.get("name", ""), galaxy.legacy_scale(), $Galaxy/Orbits.modulate.a])
	print("smoke: governor skills %s" % governor.get("skills", []))
	# Select each colonisable planet in turn (as a map click does); its potential arrives.
	for planet: Dictionary in pane.system.get("planets", []):
		if planet.type != "gas_giant":
			_on_planet_clicked(int(planet.planet))
			await get_tree().create_timer(0.5).timeout
			var colony: Dictionary = planet.get("colony", {})
			print("smoke: planet '%s' (%s %s %s): potential %s, colony %s" % [planet.get("name", ""),
				planet.get("size_name", ""), planet.get("climate_name", ""), planet.get("minerals_name", ""),
				pane._potential.get(int(planet.planet), {}).get("max_population", "?"),
				{} if colony.is_empty() else {"pop": colony.population, "morale": colony.get("morale", "-"), "government": colony.get("government", "-")}])
	if pane._potential.is_empty():
		await host.smoke_fail("no colony potential arrived in the pane")
		return false
	if shots != "":
		await host.shot(shots + "/native-system-pane.png")
	# The governor opens the colony leaders on this system (legacy); Esc comes back to the pane.
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	pane.governor.gui_input.emit(click)
	if not await _wait_screen(Screens.LEGACY, 10.0):
		await host.smoke_fail("the governor did not open the leaders")
		return false
	await get_tree().create_timer(2.0).timeout
	if shots != "":
		game.get_frame_image().save_png(shots + "/legacy-leaders.png")
	game.send_key(KEY_ESCAPE, false, false, false)
	if not await _wait_screen(Screens.MAIN, 10.0):
		await host.smoke_fail("the leaders did not return to the galaxy")
		return false
	await get_tree().create_timer(1.0).timeout
	print("smoke: leaders opened from the governor and closed; pane back: %s" % pane.visible)
	pane.close()
	await get_tree().create_timer(1.0).timeout
	print("smoke: pane closed, camera back at scale %.1f" % galaxy.legacy_scale())
	_on_view_action("influence")
	_on_view_action("influence")
	# F5: the next colony system; with auto-select colony off it opens the pane.
	var before: int = galaxy.selected
	var key := InputEventKey.new()
	key.keycode = KEY_F5
	key.pressed = true
	Input.parse_input_event(key)
	await get_tree().create_timer(1.0).timeout
	print("smoke: F5 from %d: pane %s, star %d, screen %d" % [before, pane.visible, galaxy.selected, native_screen])
	pane.close()
	await get_tree().create_timer(1.0).timeout
	# Ship Design: the tab slides the design list out from the right.
	_on_view_action("designs")
	var designs := _view("design_list")
	waited = 0.0
	while designs.rows.get_child_count() == 0 and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(1.0).timeout
	print("smoke: ship designs: %s" % ", ".join(designs.rows.get_children().map(
		func(row: Control) -> String: return row.find_children("*", "Label", true, false)[0].text)))
	if designs.rows.get_child_count() == 0:
		await host.smoke_fail("the design list stayed empty")
		return false
	if shots != "":
		await host.shot(shots + "/native-designs.png")
	# Edit opens the game's designer on that slot; Cancel (Esc) comes back to the galaxy and the list.
	var edit: Button = designs.rows.get_child(0).find_children("*", "Button", true, false)[0]
	edit.pressed.emit()
	if not await _wait_screen(Screens.LEGACY, 10.0):
		await host.smoke_fail("Edit did not open the designer")
		return false
	await get_tree().create_timer(2.0).timeout
	if shots != "":
		game.get_frame_image().save_png(shots + "/legacy-designer.png")
	game.send_key(KEY_ESCAPE, false, false, false)
	if not await _wait_screen(Screens.MAIN, 10.0):
		await host.smoke_fail("the designer did not return to the galaxy")
		return false
	await get_tree().create_timer(1.0).timeout
	print("smoke: designer opened and cancelled; design list back: %s" % designs.visible)
	designs.close()
	await get_tree().create_timer(0.5).timeout
	# Options (WP3.3f): its tab slides out influence, merge relocations and the game options.
	_on_view_action("options")
	var options := _view("options_menu")
	await get_tree().create_timer(0.8).timeout
	var opts: Dictionary = await _query("options")
	var vol := {}
	_send({"verb": "set_volume", "music": int(opts.music), "sound": int(opts.sound)}, func(r: Dictionary): vol["r"] = r)
	while not vol.has("r"):
		await get_tree().process_frame
	print("smoke: volume music %d sound %d (sliders %d / %d), set_volume %s" % [int(opts.music), int(opts.sound),
		int(options.music_slider.value), int(options.sound_slider.value), vol.r.get("ok", false)])
	# Music at 100 and back (owner, 2026-11-12: the flyout grew past the frame and stayed).
	var tall_before: float = options.size.y
	_send({"verb": "set_volume", "music": 100})
	await get_tree().create_timer(0.8).timeout
	var tall_at_100: float = options.size.y
	_send({"verb": "set_volume", "music": int(opts.music)})
	await get_tree().create_timer(0.8).timeout
	print("smoke: options height %.0f, at music 100 %.0f, after %.0f" % [tall_before, tall_at_100, options.size.y])
	print("smoke: options flyout %s: influence %s, %d game options, themes %s (in use '%s')" % [options.visible,
		options.influence_buttons.map(func(b: Button) -> bool: return b.button_pressed), options.options_box.get_child_count(),
		_themes.map(func(t: Dictionary) -> String: return t.id), theme_id])
	if shots != "":
		await host.shot(shots + "/native-options.png")
	# Switching themes while playing (WP3.3g): every other enabled theme in turn, then back.
	var first := theme_id
	for entry: Dictionary in _themes:
		if entry.id != first:
			options.theme_button.select(_themes.find(entry))
			options.theme_button.item_selected.emit(_themes.find(entry))
			await get_tree().create_timer(0.6).timeout
			print("smoke: theme switched to '%s' (%s)" % [theme_id, entry.name])
			if shots != "":
				await host.shot(shots + "/native-options-%s.png" % String(entry.name).to_lower())
	if first != "":
		apply_theme(first, false)
	await get_tree().create_timer(0.3).timeout
	if not options.visible or options.options_box.get_child_count() == 0:
		await host.smoke_fail("the options flyout did not open with the game options")
		return false
	_on_view_action("options")
	# The game menu drops from the bar; Settings opens that page and Esc comes straight back.
	_on_view_action("game_menu")
	var game_menu := _view("game_menu")
	await get_tree().create_timer(0.6).timeout
	if shots != "":
		await host.shot(shots + "/native-game-menu.png")
	print("smoke: game menu %s at %s" % [game_menu.visible, game_menu.position])
	game_menu.get_node("Box/Load").pressed.emit()
	if not await _wait_screen(Screens.LEGACY, 10.0):
		await host.smoke_fail("Load did not open")
		return false
	await get_tree().create_timer(2.0).timeout
	if shots != "":
		game.get_frame_image().save_png(shots + "/legacy-load.png")
	game.send_key(KEY_ESCAPE, false, false, false)
	if not await _wait_screen(Screens.MAIN, 10.0):
		await host.smoke_fail("Load did not return to the galaxy")
		return false
	print("smoke: load page opened and closed straight back to the galaxy; menu %s" % game_menu.visible)
	await get_tree().create_timer(0.5).timeout
	if not await _smoke_fleet(shots):
		return false
	galaxy._view_placed = false
	_send({"verb": "query", "what": "view"}, galaxy.place_view)
	await get_tree().create_timer(0.5).timeout
	return true


## The bar's tax menu (WP3.3d): another rate is checked in the treasury's menu, then the old one
## goes back. (The bar doesn't show the rate, WP3.3g.)
func _smoke_tax(old_rate: int) -> bool:
	var bar := _view("galaxy_bar")
	var rate := 30 if old_rate != 30 else 40
	var popup: PopupMenu = bar.money.get_popup()
	popup.id_pressed.emit(rate)
	var waited := 0.0
	while not popup.is_item_checked(popup.get_item_index(rate)) and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	print("smoke: bar after tax %d%%: '%s' | '%s' | '%s' | '%s'" % [rate, bar.money.text, bar.command_label.text, bar.research.text, bar.freighters_label.text])
	if not popup.is_item_checked(popup.get_item_index(rate)):
		await host.smoke_fail("the tax rate did not change in the treasury's menu")
		return false
	popup.id_pressed.emit(old_rate)
	await get_tree().create_timer(0.5).timeout
	return true


## The fleet panel (WP3.3c): the player's first fleet opens with its ships, a route preview to
## another star answers, ALL clears the selection, and the panel closes.
func _smoke_fleet(shots: String) -> bool:
	var own := -1
	for fleet: Dictionary in galaxy.fleets:
		if int(fleet.owner) == int(galaxy.data.player) and not fleet.moving:
			own = int(fleet.ship)
			break
	if own < 0:
		print("smoke: no fleet of the player's at a star; fleet panel not checked")
		return true
	_open_fleet(own)
	var fleet_panel := _view("fleet_panel")
	var waited := 0.0
	while not fleet_panel.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if not fleet_panel.visible:
		await host.smoke_fail("fleet %d did not open its panel" % own)
		return false
	var ships: Array = fleet_panel.selected_ships()
	print("smoke: fleet panel '%s', %d ships, %d selected, status '%s', buttons %s" % [fleet_panel.title.text,
		fleet_panel.fleet.ships.size(), ships.size(), fleet_panel.status.text, fleet_panel.fleet.buttons])
	var target := -1
	for s: Dictionary in galaxy.stars:
		if int(s.id) != int(fleet_panel.fleet.star) and not s.black_hole:
			target = int(s.id)
			break
	if not ships.is_empty() and target >= 0:
		_on_star_hovered(target)
		await get_tree().create_timer(0.5).timeout
		print("smoke: preview to %d: line '%s', status '%s'" % [target, galaxy.preview.get("line", ""), fleet_panel.status.text])
		if galaxy.preview.is_empty():
			await host.smoke_fail("no route preview")
			return false
	await get_tree().create_timer(1.0).timeout # ship pictures
	if shots != "":
		await host.shot(shots + "/native-fleet-panel.png")
	if "--fleet-orders" in OS.get_cmdline_user_args():
		return await _smoke_fleet_orders(shots)
	if not ships.is_empty():
		fleet_panel._on_all()
		print("smoke: after ALL, %d selected" % fleet_panel.selected_ships().size())
	var fleet_star := int(fleet_panel.fleet.star)
	fleet_panel.close()
	_on_star_hovered(-1)
	await get_tree().create_timer(0.6).timeout
	# From inside a system (WP3.3g, owner): the fleet opens from the pane, the camera steps back a
	# little with the map free, and closing the panel comes back into the system with its pane.
	if fleet_star >= 0:
		_on_star_clicked(fleet_star)
		var pane := _view("system_pane")
		waited = 0.0
		while not pane.visible and waited < 5.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		await get_tree().create_timer(1.2).timeout
		var in_system: float = galaxy.legacy_scale()
		var pane_fleets: Array = pane.system.get("fleets", [])
		if pane.visible and not pane_fleets.is_empty():
			pane.fleet_chosen.emit(int(pane_fleets[0].ship))
			waited = 0.0
			while not fleet_panel.visible and waited < 5.0:
				await get_tree().create_timer(0.1).timeout
				waited += 0.1
			await get_tree().create_timer(1.2).timeout
			print("smoke: fleet from the system: panel %s, pane %s, scale %.1f -> %.1f, panning free %s, returns to %d" % [
				fleet_panel.visible, pane.visible, in_system, galaxy.legacy_scale(), galaxy.system.is_empty(), _fleet_from_system])
			fleet_panel.close()
			waited = 0.0
			while not pane.visible and waited < 5.0:
				await get_tree().create_timer(0.1).timeout
				waited += 0.1
			await get_tree().create_timer(1.2).timeout
			print("smoke: fleet closed: back in the system %s, pane %s, scale %.1f" % [galaxy.system_star == fleet_star, pane.visible, galaxy.legacy_scale()])
			if not pane.visible or galaxy.system_star != fleet_star:
				await host.smoke_fail("closing a fleet opened from a system did not come back to it")
				return false
		pane.close()
		await get_tree().create_timer(1.0).timeout
	return true


## `--fleet-orders` (a save with a fleet that can colonise, such as fixture SAVE6): colonise
## through the panel's button and confirmation, then move the next fleet by a star click.
func _smoke_fleet_orders(shots: String) -> bool:
	var fleet_panel := _view("fleet_panel")
	var colonized := false
	for fleet: Dictionary in galaxy.fleets.duplicate():
		if int(fleet.owner) != int(galaxy.data.player) or fleet.moving:
			continue
		_open_fleet(int(fleet.ship))
		await get_tree().create_timer(0.5).timeout
		if not fleet_panel.visible or not fleet_panel.fleet.buttons.colonize:
			continue
		var before: Dictionary = await _query("empire")
		fleet_panel.get_node("Box/Orders/Colonize").pressed.emit()
		var picker: PopupMenu = fleet_panel.planets
		await get_tree().create_timer(0.5).timeout
		if picker.visible:
			picker.id_pressed.emit(picker.get_item_id(0))
			picker.hide()
			await get_tree().create_timer(0.5).timeout
		var confirm: ConfirmationDialog = fleet_panel.confirm_colony
		print("smoke: colonise confirmation '%s'" % confirm.dialog_text.replace("\n", " | "))
		if shots != "":
			await host.shot(shots + "/native-colonize-confirm.png")
		confirm.confirmed.emit()
		confirm.hide()
		await get_tree().create_timer(1.0).timeout
		var after: Dictionary = await _query("empire")
		print("smoke: colonised from fleet %d: freighters %d -> %d, panel %s" % [int(fleet.ship),
			int(before.freighters), int(after.freighters), fleet_panel.visible])
		colonized = true
		break
	if not colonized:
		await host.smoke_fail("no fleet could colonise")
		return false
	# Move: the first fleet of the player's at a star, all ships, to the nearest other star.
	for fleet: Dictionary in galaxy.fleets:
		if int(fleet.owner) != int(galaxy.data.player) or fleet.moving or int(fleet.star) < 0:
			continue
		_open_fleet(int(fleet.ship))
		await get_tree().create_timer(0.5).timeout
		if fleet_panel.selected_ships().is_empty():
			fleet_panel._on_all()
		var from: Vector2 = galaxy.star_position(int(fleet.star))
		var target := -1
		var best := INF
		for s: Dictionary in galaxy.stars:
			var d := from.distance_to(Vector2(float(s.x), float(s.y)))
			if int(s.id) != int(fleet.star) and not s.black_hole and d < best:
				best = d
				target = int(s.id)
		_on_star_clicked(target)
		await get_tree().create_timer(1.0).timeout
		print("smoke: moved fleet %d to star %d: status '%s'" % [int(fleet.ship), target, fleet_panel.status.text])
		if shots != "":
			await host.shot(shots + "/native-fleet-moved.png")
		fleet_panel.close()
		return true
	await host.smoke_fail("no fleet to move")
	return false


func _wait_screen(screen: int, timeout: float) -> bool:
	var waited := 0.0
	while native_screen != screen and waited < timeout:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
		_smoke_answer()
	return native_screen == screen


## The smoke test answers native message boxes with OK / Yes.
func _smoke_answer() -> bool:
	if not overlay.visible:
		return false
	print("smoke: answering request '%s' (native galaxy behind: %s, size %s)" % [_views["dialog"].get_node("Box/Text").text,
		galaxy.visible and not legacy.visible, _views["dialog"].size])
	if not _shot_question and galaxy.visible and not legacy.visible:
		_shot_question = true
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--shots="):
				host.shot(arg.substr(8) + "/native-turn-question.png")
	_views["dialog"]._answer(1)
	return true


func _query(what: String) -> Dictionary:
	var box := {}
	_send({"verb": "query", "what": what}, func(result: Dictionary): box["r"] = result)
	var waited := 0.0
	while not box.has("r") and waited < 10.0:
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
	return box.get("r", {})


## The subscreen flash (owner, 2026-11-07): `-- --smoke --native --flash=<dir>` opens each of the
## bar's legacy screens from the native galaxy, twice round, and saves every legacy frame shown in
## the first 1.5 s after the switch (`<dir>/<round>-<screen>-<n>.png`), so a frame of the previous
## screen shows up as file 0.
func _smoke_flash(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	for pass_i in 2:
		for screen in [Screens.COLONY_SUMMARY, 32, 4, 29, 6, 9]:
			_send({"verb": "open", "screen": screen})
			var t_open := Time.get_ticks_msec()
			while native_screen != Screens.LEGACY and Time.get_ticks_msec() - t_open < 10000:
				await get_tree().process_frame
			var t_shown := Time.get_ticks_msec()
			var last_frames := -1
			var n := 0
			while Time.get_ticks_msec() - t_shown < 1500 and n < 12:
				var frames: int = game.get_stats().frames
				if frames != last_frames:
					last_frames = frames
					game.get_frame_image().save_png("%s/%d-%02d-%02d.png" % [dir, pass_i, screen, n])
					n += 1
				await get_tree().process_frame
			print("smoke: flash round %d screen %d: %d frames saved" % [pass_i, screen, n])
			var closing := 0.0
			while native_screen != Screens.MAIN and closing < 20.0:
				game.send_key(KEY_ESCAPE, false, false, false)
				await get_tree().create_timer(1.0).timeout
				closing += 1.0
			if native_screen != Screens.MAIN:
				await host.smoke_fail("screen %d did not close back to the native galaxy" % screen)
				return
	game.request_game_quit()
	await get_tree().create_timer(15.0).timeout
	get_tree().quit()
