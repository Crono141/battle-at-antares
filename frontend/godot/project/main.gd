extends Node
## WP3.0 trial, path A: the legacy game as a texture, plus a native galaxy scene and panel.
## Ctrl+G (or the button) switches between them; the game keeps running underneath.

@onready var game: AntaresGame = $UI/Legacy/Game
@onready var legacy: Control = $UI/Legacy
@onready var galaxy: Node2D = $Galaxy
@onready var panel: PanelContainer = $UI/StarPanel
@onready var mode_button: Button = $UI/Bar/Mode
@onready var stats_label: Label = $UI/Bar/Stats

var _stats_time := 0.0


func _ready() -> void:
	game.galaxy_changed.connect(_on_galaxy_changed)
	game.game_exited.connect(func(): get_tree().quit())
	galaxy.star_clicked.connect(_on_star_clicked)
	mode_button.pressed.connect(_toggle_mode)
	$UI/StarPanel/Box/Close.pressed.connect(_close_panel)
	panel.hide()
	_set_mode(false)
	if "--smoke" in OS.get_cmdline_user_args():
		_smoke()


func _input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.ctrl_pressed and key.keycode == KEY_G:
		_toggle_mode()
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	_stats_time += delta
	if _stats_time < 1.0:
		return
	_stats_time = 0.0
	var s: Dictionary = game.get_stats()
	stats_label.text = "fps %d   frames %d   upload avg %.2f ms (whole frame %.1f ms)   stars %d" % [
		Engine.get_frames_per_second(), s.frames, s.upload_ms_avg, s.full_upload_ms_avg, galaxy.stars.size()]


func _set_mode(galaxy_mode: bool) -> void:
	legacy.visible = not galaxy_mode
	galaxy.visible = galaxy_mode
	galaxy.set_process_unhandled_input(galaxy_mode)
	mode_button.text = "Legacy screen (Ctrl+G)" if galaxy_mode else "Galaxy scene (Ctrl+G)"
	if galaxy_mode:
		galaxy.camera.make_current()
	else:
		_close_panel()
		game.grab_focus()


func _toggle_mode() -> void:
	_set_mode(not galaxy.visible)


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
	_set_mode(true)
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
