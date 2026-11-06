extends Node
## The Godot frontend. The game runs on its own thread (AntaresGame). Screens the frontend has no
## native view for show as the legacy frame, with input forwarded. With native views on (Ctrl+G,
## or `-- --native`), the galaxy (SCREEN_MAIN), the command points summary and the game's message
## boxes are native (WP3.1b): buttons send commands, the game thread runs them with the same rules
## calls as the legacy screens, and every screen not yet native opens through the legacy fallback.

@onready var game: AntaresGame = $UI/Legacy/Game
@onready var legacy: Control = $UI/Legacy
@onready var galaxy: Node2D = $Galaxy
@onready var panel: PanelContainer = $UI/StarPanel
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


func _ready() -> void:
	game.galaxy_changed.connect(_on_galaxy_changed)
	game.game_exited.connect(func(): get_tree().quit())
	game.native_screen_changed.connect(_on_native_screen)
	game.command_result.connect(_on_result)
	game.request.connect(_on_request)
	game.resource_packs_changed.connect(_load_packs)
	galaxy.star_clicked.connect(_on_star_clicked)
	mode_button.pressed.connect(func(): _set_native(not native))
	$UI/StarPanel/Box/Close.pressed.connect(_close_panel)
	panel.hide()
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


func _process(delta: float) -> void:
	_stats_time += delta
	if _stats_time < 1.0:
		return
	_stats_time = 0.0
	var s: Dictionary = game.get_stats()
	stats_label.text = "fps %d   frames %d   upload avg %.2f ms (whole frame %.1f ms)   screen %d" % [
		Engine.get_frames_per_second(), s.frames, s.upload_ms_avg, s.full_upload_ms_avg, native_screen]


## Native views on or off. The game switches at its next safe point: the legacy galaxy hands over
## between inputs, and the native loop hands back at once.
func _set_native(on: bool) -> void:
	native = on
	for screen in [Screens.MAIN, Screens.SHOW_COMMAND_POINTS]:
		game.claim_screen(screen, on)
	game.claim_requests(on)
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
	var points := _view("command_points")
	points.visible = screen == Screens.SHOW_COMMAND_POINTS
	if screen == Screens.MAIN:
		galaxy.camera.make_current()
		_send({"verb": "query", "what": "empire"}, bar.show_empire)
	elif screen == Screens.SHOW_COMMAND_POINTS:
		_send({"verb": "query", "what": "command_points"}, points.show_result)
	else:
		_close_panel()
	if screen == Screens.LEGACY:
		game.grab_focus()


## A native view, instanced on first use from res://views (so a package's resource pack, loaded
## before, can replace it).
func _view(scene_name: String) -> Control:
	if not _views.has(scene_name):
		var view: Control = load("res://views/%s.tscn" % scene_name).instantiate()
		view.visible = false
		if view.has_signal("command"):
			view.command.connect(func(dict: Dictionary): _send(dict))
		native_layer.add_child(view)
		_views[scene_name] = view
	return _views[scene_name]


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


func _on_request(id: int, kind: String, text: String) -> void:
	var dialog: Control = _views.get("dialog")
	if dialog == null:
		dialog = load("res://views/dialog.tscn").instantiate()
		dialog.answered.connect(_on_answered)
		$Overlay.add_child(dialog)
		_views["dialog"] = dialog
	overlay.show()
	dialog.ask(id, kind, text)


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
	# Views made before the packs arrived are rebuilt from the new files.
	for scene_name in _views.keys():
		if scene_name != "dialog":
			_views[scene_name].queue_free()
			_views.erase(scene_name)


func _on_galaxy_changed() -> void:
	galaxy.set_stars(game.get_stars())


func _on_star_clicked(star: Dictionary) -> void:
	$UI/StarPanel/Box/Name.text = star.name
	var owner_text := "Unclaimed"
	if star.owner >= 0:
		owner_text = "Owner: %s" % star.owner_name
		$UI/StarPanel/Box/Owner.add_theme_color_override("font_color", galaxy.PLAYER_COLORS[star.owner_color % 8])
	else:
		$UI/StarPanel/Box/Owner.remove_theme_color_override("font_color")
	$UI/StarPanel/Box/Owner.text = owner_text
	$UI/StarPanel/Box/Class.text = "Star class: %s" % galaxy.SPECTRAL_NAMES[clampi(star.spectral_class, 0, 6)]
	panel.show()


func _close_panel() -> void:
	panel.hide()
	galaxy.selected = -1
	galaxy.queue_redraw()


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
	var empire: Dictionary = await _query("empire")
	print("smoke: empire %s" % empire)
	if shots != "":
		await get_tree().create_timer(1.0).timeout
		await _shot(shots + "/native-galaxy.png")
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


## Headless check (`-- --smoke`): the game boots, frames arrive, the galaxy snapshot fills, a star
## can be picked; optional screenshots with `--shots=<dir>` (needs a display).
func _smoke() -> void:
	var shots := ""
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--shots="):
			shots = arg.substr(8)
	var waited := 0.0
	while galaxy.stars.is_empty() and waited < 120.0:
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	await get_tree().create_timer(3.0).timeout
	print("smoke: stars %d after %.1f s, stats %s" % [galaxy.stars.size(), waited, game.get_stats()])
	if shots != "":
		await _shot(shots + "/legacy.png")
	# Input path: click the load message's CLOSE button (game 273,253) through Godot's input.
	var at := _game_to_screen(Vector2(273, 253))
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = at
		Input.parse_input_event(click)
		await get_tree().create_timer(0.15).timeout
	await get_tree().create_timer(1.0).timeout
	if shots != "":
		await _shot(shots + "/legacy-after-click.png")
	# A hotkey: P opens the Planets screen.
	var key := InputEventKey.new()
	key.keycode = KEY_P
	key.pressed = true
	Input.parse_input_event(key)
	await get_tree().create_timer(0.1).timeout
	key = key.duplicate()
	key.pressed = false
	Input.parse_input_event(key)
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		await _shot(shots + "/legacy-after-key.png")
	legacy.visible = false
	galaxy.visible = true
	galaxy.camera.make_current()
	await get_tree().create_timer(0.5).timeout
	var owned := -1
	for i in galaxy.stars.size():
		if galaxy.stars[i].owner >= 0:
			owned = i
			break
	if owned >= 0:
		var s: Dictionary = galaxy.stars[owned]
		print("smoke: first owned star %s" % s)
		galaxy.camera.position = Vector2(s.x, s.y)
		galaxy._target_zoom = 4.0
		galaxy._anchor_world = galaxy.camera.position
		galaxy._anchor_screen = get_viewport().get_visible_rect().size * 0.5
		await get_tree().create_timer(1.0).timeout
		galaxy._pick(get_viewport().get_visible_rect().size * 0.5)
		print("smoke: picked %d, panel %s: %s / %s" % [galaxy.selected, panel.visible,
			$UI/StarPanel/Box/Name.text, $UI/StarPanel/Box/Owner.text])
	if shots != "":
		await _shot(shots + "/galaxy.png")
		_close_panel()
		galaxy._fitted = false
		galaxy._fit()
		await get_tree().create_timer(0.5).timeout
		await _shot(shots + "/galaxy-overview.png")
		galaxy._target_zoom = 2.6
		galaxy._anchor_world = galaxy.camera.position
		galaxy._anchor_screen = get_viewport().get_visible_rect().size * 0.5
		await get_tree().create_timer(1.5).timeout
		await _shot(shots + "/galaxy-labels.png")
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
