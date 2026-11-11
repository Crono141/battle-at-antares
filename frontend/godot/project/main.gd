extends Node
## The Godot frontend. The game runs on its own thread (AntaresGame). Screens the frontend has no
## native view for show as the legacy frame, with input forwarded. With native views on (Ctrl+G,
## or `-- --native`), the galaxy (SCREEN_MAIN), the command points summary and the game's message
## boxes are native (WP3.1b): buttons send commands, the game thread runs them with the same rules
## calls as the legacy screens, and every screen not yet native opens through the legacy fallback.
## The galaxy is the home view (WP3.3a): the game's own art, its stars, fleets and lines from
## watched queries, and the star system panel. Fleets open the fleet panel (WP3.3c): selected
## ships preview their route on the hovered star and move on a star click.

@onready var game: AntaresGame = $UI/Legacy/Game
@onready var legacy: Control = $UI/Legacy
@onready var galaxy: Node2D = $Galaxy
@onready var art: Node = $Art
@onready var mode_button: Button = $UI/Bar/Mode
@onready var stats_label: Label = $UI/Bar/Stats
@onready var native_layer: Control = $UI/Native
@onready var overlay: Control = $Overlay/Shade

var native := false
var native_screen := Screens.LEGACY
var _galaxy_behind := false # the native galaxy stays visible under a native sub-screen
var _views := {} # scene name -> instance
var _pending := {} # command id -> Callable(result)
var _loaded_packs := {}
var _stats_time := 0.0
var _panel_star := -1 # the star whose system panel is open (reopened after legacy screens)
var _fleet_ship := -1 # the first ship of the fleet whose panel is open (reopened likewise)
var _hovered_star := -1


func _ready() -> void:
	art.setup(game)
	galaxy.art = art
	game.game_exited.connect(func(): get_tree().quit())
	game.native_screen_changed.connect(_on_native_screen)
	game.command_result.connect(_on_result)
	game.request.connect(_on_request)
	game.snapshot.connect(_on_snapshot)
	game.resource_packs_changed.connect(_load_packs)
	galaxy.star_clicked.connect(_on_star_clicked)
	galaxy.fleet_clicked.connect(_open_fleet)
	galaxy.star_hovered.connect(_on_star_hovered)
	galaxy.right_clicked.connect(_end_relocating)
	mode_button.pressed.connect(func(): _set_native(not native))
	overlay.hide()
	_set_native("--native" in OS.get_cmdline_user_args())
	if "--smoke" in OS.get_cmdline_user_args():
		if native:
			_smoke_native()
		else:
			_smoke()


func _input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.ctrl_pressed and key.keycode == KEY_G:
		_set_native(not native)
		get_viewport().set_input_as_handled()


## Native views on or off. The game switches at its next safe point: the legacy galaxy hands over
## between inputs, and the native loop hands back at once.
func _set_native(on: bool) -> void:
	native = on
	for screen in [Screens.MAIN, Screens.SHOW_COMMAND_POINTS]:
		game.claim_screen(screen, on)
	# The request kinds the native dialog answers; the rest ("choose") keep the legacy popup.
	game.claim_requests(PackedStringArray(["message", "confirm", "warning"]) if on else PackedStringArray())
	if on:
		for what in ["empire", "galaxy", "fleets", "options"]:
			_send({"verb": "watch", "what": what})
	mode_button.text = "Legacy galaxy (Ctrl+G)" if on else "Native galaxy (Ctrl+G)"


func _on_native_screen(screen: int) -> void:
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
	var points := _view("command_points")
	points.visible = screen == Screens.SHOW_COMMAND_POINTS
	var system_panel := _view("system_panel")
	var fleet_panel := _view("fleet_panel")
	if screen == Screens.MAIN:
		galaxy.camera.make_current()
		_send({"verb": "query", "what": "view"}, galaxy.place_view)
		if _panel_star >= 0:
			_open_system(_panel_star) # refreshed: a colony screen may have changed it
		if _fleet_ship >= 0:
			_refresh_fleet()
	else:
		system_panel.hide()
		fleet_panel.hide()
		_end_relocating()
		if screen == Screens.SHOW_COMMAND_POINTS:
			_send({"verb": "query", "what": "command_points"}, points.show_result)
	if screen == Screens.LEGACY:
		game.grab_focus()


## A native view, instanced on first use from res://views (so a package's resource pack, loaded
## before, can replace it).
func _view(scene_name: String) -> Control:
	if not _views.has(scene_name):
		var view: Control = load("res://views/%s.tscn" % scene_name).instantiate()
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
			view.fleet_chosen.connect(_open_fleet)
		if view.has_signal("selection_changed"):
			view.selection_changed.connect(func(): _on_star_hovered(_hovered_star))
		native_layer.add_child(view)
		_views[scene_name] = view
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
	var center := get_viewport().get_visible_rect().size * 0.5
	match action_name:
		"zoom_in":
			galaxy.zoom_by(1.5, center)
		"zoom_out":
			galaxy.zoom_by(1.0 / 1.5, center)
		"colony_next", "colony_previous":
			_send({"verb": "cycle_colony_star", "dir": 0 if action_name == "colony_next" else 1}, _on_star_selected)
		"influence":
			$Galaxy/Influence.cycle()
			_view("map_controls").show_influence(galaxy.influence_mode)
		"fleet_next", "fleet_previous":
			_cycle_fleet(0 if action_name == "fleet_next" else 1)
		"merge_relocations":
			if galaxy.relocating:
				_end_relocating()
			else:
				_send({"verb": "merge_relocations"}, func(result: Dictionary):
					galaxy.relocating = result.get("ready", false)
					if galaxy.relocating:
						Input.set_default_cursor_shape(Input.CURSOR_CROSS))


func _on_view_closed(scene_name: String) -> void:
	if scene_name == "system_panel":
		_panel_star = -1
		galaxy.selected = -1
		galaxy.redraw()
	elif scene_name == "fleet_panel":
		_fleet_ship = -1
		galaxy.selected_fleet = -1
		galaxy.preview = {}
		galaxy.redraw()


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
		"galaxy":
			galaxy.set_galaxy(result)
		"fleets":
			galaxy.set_fleets(result)
			if _fleet_ship >= 0 and native_screen == Screens.MAIN:
				_refresh_fleet() # a turn, or an order, changed the fleets
		"options":
			_view("map_controls").show_options(result)


func _on_request(id: int, kind: String, request: Dictionary) -> void:
	var dialog: Control = _views.get("dialog")
	if dialog == null:
		dialog = load("res://views/dialog.tscn").instantiate()
		dialog.answered.connect(_on_answered)
		$Overlay.add_child(dialog)
		_views["dialog"] = dialog
	overlay.show()
	dialog.ask(id, kind, request)


func _on_answered(id: int, value: int) -> void:
	overlay.hide()
	game.answer_request(id, value)


## Packages may ship Godot resource packs (godot/*.pck or *.zip); the game lists them lowest
## priority first, so each later pack replaces the files of the ones before.
func _load_packs() -> void:
	for path: String in game.get_resource_packs():
		if _loaded_packs.has(path):
			continue
		_loaded_packs[path] = ProjectSettings.load_resource_pack(path, true)
		print("frontend: resource pack %s: %s" % [path, "loaded" if _loaded_packs[path] else "FAILED"])
	# Views made before the packs arrived are rebuilt from the new files, and art is asked again.
	art.clear()
	for scene_name in _views.keys():
		if scene_name != "dialog":
			_views[scene_name].queue_free()
			_views.erase(scene_name)


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
		_view("fleet_panel").dismiss() # the game closed the fleet box for the system box
	if result.get("ok", false) and result.get("opened", "") == "system":
		_open_system(int(result.star))
		if not galaxy.get_viewport_rect().has_point(galaxy.get_global_transform_with_canvas() * galaxy.star_position(int(result.star))):
			galaxy.focus(int(result.star))


func _open_system(star_id: int) -> void:
	_send({"verb": "query", "what": "system", "star": star_id}, func(result: Dictionary):
		if not result.get("ok", false) or native_screen != Screens.MAIN:
			return
		_panel_star = star_id
		galaxy.selected = star_id
		galaxy.redraw()
		var system_panel := _view("system_panel")
		var was_visible := system_panel.visible
		system_panel.show_system(result, galaxy.legacy_pixel() * 0.85)
		if not was_visible:
			_place_panel(system_panel, star_id))


## Opens a fleet's panel (a map icon, the system panel's fleets, F1/F2): the game keeps the
## fleet box record and closes the system box, then the `fleet` query fills the panel.
func _open_fleet(ship_id: int) -> void:
	_send({"verb": "open_fleet", "fleet": ship_id}, func(opened: Dictionary):
		if not opened.get("ok", false):
			return
		_view("system_panel").dismiss()
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
				return
		return
	_send({"verb": "query", "what": "next_fleet", "fleet": from, "dir": dir}, func(result: Dictionary):
		if result.get("ok", false) and int(result.ship) >= 0:
			_open_fleet(int(result.ship))
			galaxy.focus_point(_fleet_position(int(result.ship))))


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


## Beside the star, on the side of the window away from it (the legacy box takes the quadrant away
## from the star).
func _place_panel(system_panel: Control, star_id: int) -> void:
	var view := get_viewport().get_visible_rect().size
	var star_screen: Vector2 = galaxy.get_global_transform_with_canvas() * galaxy.star_position(star_id)
	var x := view.x - system_panel.size.x - 24.0 if star_screen.x < view.x * 0.5 else 24.0
	var y := clampf(star_screen.y - system_panel.size.y * 0.5, 56.0, view.y - system_panel.size.y - 80.0)
	system_panel.position = Vector2(x, y)


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
		await _smoke_fail("never reached the native galaxy")
		return
	print("smoke: native galaxy after %.1f s, packs %s" % [(Time.get_ticks_msec() - t0) / 1000.0, _loaded_packs])
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
		await _smoke_fail("command points did not open")
		return
	await get_tree().create_timer(0.5).timeout
	var points := _view("command_points")
	print("smoke: OK button '%s'" % points.get_node("Box/Ok").text)
	if shots != "":
		await _shot(shots + "/native-command-points.png")
	print("smoke: command points '%s' / %s / %s" % [points.get_node("Box/Title").text,
		points.get_node("Box/Total").text, points.get_node("Box/Text").text.replace("\n", " | ")])
	_send({"verb": "close"})
	if not await _wait_screen(Screens.MAIN, 10.0):
		await _smoke_fail("command points did not close")
		return
	# A legacy screen from the native galaxy: the legacy view must not show the stale frame from
	# before the native view (owner, 2026-11-06: Colonies flashed the old galaxy).
	var stale: Image = game.get_frame_image()
	_send({"verb": "open", "screen": Screens.COLONY_SUMMARY})
	var t_open := Time.get_ticks_msec()
	while native_screen != Screens.LEGACY and Time.get_ticks_msec() - t_open < 10000:
		await get_tree().process_frame
	var shown: Image = game.get_frame_image()
	if native_screen != Screens.LEGACY or (stale and shown and stale.get_data() == shown.get_data()):
		await _smoke_fail("colonies showed the stale legacy frame")
		return
	print("smoke: colonies shown after %d ms with a new frame" % (Time.get_ticks_msec() - t_open))
	if shots != "":
		await _shot(shots + "/legacy-colonies.png")
	var closing := 0.0
	while native_screen != Screens.MAIN and closing < 20.0:
		game.send_key(KEY_ESCAPE, false, false, false)
		await get_tree().create_timer(1.0).timeout
		closing += 1.0
	if native_screen != Screens.MAIN:
		await _smoke_fail("colonies did not close back to the native galaxy")
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
		await _smoke_fail("research did not close back to the native galaxy")
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
					await _click_game(Vector2(442, 352))
				else:
					game.send_key(KEY_ESCAPE, false, false, false)
		if native_screen != Screens.MAIN or game.get_native_visit() == visit:
			var frame: Image = game.get_frame_image()
			if frame:
				frame.save_png("user://smoke-stuck.png")
				print("smoke: legacy frame saved to %s" % ProjectSettings.globalize_path("user://smoke-stuck.png"))
			await _smoke_fail("the native galaxy did not come back after the turn")
			return
		empire = await _query("empire")
		print("smoke: after turn %d (%.0f s): %s" % [turn + 1, waited, empire])
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
		await _smoke_fail("no galaxy snapshot")
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
		await _smoke_fail("no art arrived")
		return false
	if shots != "":
		await _shot(shots + "/native-galaxy.png")
	# The player's first visited colony star with something to show.
	var star := -1
	for s: Dictionary in galaxy.stars:
		if int(s.owner) == int(galaxy.data.player) and s.visited:
			star = int(s.id)
			break
	if star < 0:
		await _smoke_fail("no colony star")
		return false
	galaxy.focus(star)
	galaxy.zoom_by(3.0, get_viewport().get_visible_rect().size * 0.5)
	_on_view_action("influence")
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		await _shot(shots + "/native-galaxy-zoomed.png")
	_on_star_clicked(star)
	var system_panel := _view("system_panel")
	waited = 0.0
	while not system_panel.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if not system_panel.visible:
		await _smoke_fail("star %d did not open its system panel" % star)
		return false
	print("smoke: system panel '%s', %d planets, %d fleets" % [system_panel.title.text,
		system_panel.system.get("planets", []).size(), system_panel.system.get("fleets", []).size()])
	await get_tree().create_timer(1.5).timeout
	# Hover the first planet with a picture, then right-click it for its colony information.
	for planet: Dictionary in system_panel.system.get("planets", []):
		if int(planet.sprite) >= 0 and planet.type != "gas_giant":
			var u: float = system_panel._unit()
			var at: Vector2 = (Vector2(float(planet.box_x), float(planet.box_y)) + Vector2.ONE * float(planet.dimension) * 0.5) * u
			system_panel._set_hover(system_panel._hit(at))
			print("smoke: hover '%s'" % system_panel.info_text.text.replace("\n", " | "))
			_send({"verb": "query", "what": "potential_colony", "planet": int(planet.planet)}, system_panel.show_potential)
			break
	await get_tree().create_timer(1.0).timeout
	print("smoke: colony info '%s'" % system_panel.info_text.text.replace("\n", " | "))
	if shots != "":
		await _shot(shots + "/native-system-panel.png")
	system_panel.close()
	_on_view_action("influence")
	_on_view_action("influence")
	# F5: the next colony system; with auto-select colony off it opens the panel.
	var before: int = galaxy.selected
	var key := InputEventKey.new()
	key.keycode = KEY_F5
	key.pressed = true
	Input.parse_input_event(key)
	await get_tree().create_timer(1.0).timeout
	print("smoke: F5 from %d: panel %s, star %d, screen %d" % [before, system_panel.visible, galaxy.selected, native_screen])
	system_panel.close()
	if not await _smoke_fleet(shots):
		return false
	galaxy._view_placed = false
	_send({"verb": "query", "what": "view"}, galaxy.place_view)
	await get_tree().create_timer(0.5).timeout
	return true


## The bar's tax menu (WP3.3d): another rate shows on the bar, then the old one goes back.
func _smoke_tax(old_rate: int) -> bool:
	var bar := _view("galaxy_bar")
	var rate := 30 if old_rate != 30 else 40
	bar.money.get_popup().id_pressed.emit(rate)
	var waited := 0.0
	while not bar.money.text.ends_with("Tax %d%%" % rate) and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	print("smoke: bar after tax %d%%: '%s' | '%s' | '%s'" % [rate, bar.money.text, bar.research.text, bar.freighters_label.text])
	if not bar.money.text.ends_with("Tax %d%%" % rate):
		await _smoke_fail("the tax rate did not change on the bar")
		return false
	bar.money.get_popup().id_pressed.emit(old_rate)
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
		await _smoke_fail("fleet %d did not open its panel" % own)
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
			await _smoke_fail("no route preview")
			return false
	await get_tree().create_timer(1.0).timeout # ship pictures
	if shots != "":
		await _shot(shots + "/native-fleet-panel.png")
	if "--fleet-orders" in OS.get_cmdline_user_args():
		return await _smoke_fleet_orders(shots)
	if not ships.is_empty():
		fleet_panel._on_all()
		print("smoke: after ALL, %d selected" % fleet_panel.selected_ships().size())
	fleet_panel.close()
	_on_star_hovered(-1)
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
			await _shot(shots + "/native-colonize-confirm.png")
		confirm.confirmed.emit()
		confirm.hide()
		await get_tree().create_timer(1.0).timeout
		var after: Dictionary = await _query("empire")
		print("smoke: colonised from fleet %d: freighters %d -> %d, panel %s" % [int(fleet.ship),
			int(before.freighters), int(after.freighters), fleet_panel.visible])
		colonized = true
		break
	if not colonized:
		await _smoke_fail("no fleet could colonise")
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
			await _shot(shots + "/native-fleet-moved.png")
		fleet_panel.close()
		return true
	await _smoke_fail("no fleet to move")
	return false


func _click_game(at: Vector2) -> void:
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = _game_to_screen(at)
		Input.parse_input_event(click)
		await get_tree().create_timer(0.1).timeout


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
	print("smoke: answering request '%s'" % _views["dialog"].get_node("Box/Text").text)
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


func _smoke_fail(why: String) -> void:
	print("smoke: FAILED: %s" % why)
	game.request_game_quit()
	await get_tree().create_timer(15.0).timeout
	get_tree().quit(1)


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
				await _smoke_fail("screen %d did not close back to the native galaxy" % screen)
				return
	game.request_game_quit()
	await get_tree().create_timer(15.0).timeout
	get_tree().quit()


## Headless check (`-- --smoke`): the game boots, frames arrive, input reaches the legacy screen;
## optional screenshots with `--shots=<dir>` (needs a display).
func _smoke() -> void:
	var shots := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots = arg.substr(8)
	var waited := 0.0
	while game.get_stats().frames < 10 and waited < 120.0:
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	await get_tree().create_timer(3.0).timeout
	print("smoke: %d frames after %.1f s, stats %s" % [game.get_stats().frames, waited, game.get_stats()])
	if shots != "":
		await _shot(shots + "/legacy.png")
	# Input path: click the load message's CLOSE button (game 273,253) through Godot's input.
	await _click_game(Vector2(273, 253))
	await get_tree().create_timer(1.0).timeout
	if shots != "":
		await _shot(shots + "/legacy-after-click.png")
	# A hotkey: P opens the Planets screen.
	game.send_key(KEY_P, false, false, false)
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		await _shot(shots + "/legacy-after-key.png")
	print("smoke: stats %s" % [game.get_stats()])
	# The game ends on its own: Godot should close with it (game_exited).
	game.request_game_quit()
	print("smoke: asked the game to quit")
	await get_tree().create_timer(15.0).timeout
	print("smoke: the game did not quit within 15 s")
	get_tree().quit()


func _shot(path: String) -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(path)
	print("smoke: saved %s" % path)


func _game_to_screen(game_point: Vector2) -> Vector2:
	var size := game.size
	var scale := minf(size.x / 640.0, size.y / 480.0)
	var origin := (size - Vector2(640, 480) * scale) * 0.5
	return game.global_position + origin + game_point * scale
