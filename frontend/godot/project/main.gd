extends Node
## The Godot host of Battle at Antares. The game runs on its own thread (AntaresGame) and every
## screen shows as the game's own frame with input forwarded, so by default the game looks and
## plays exactly like the original (D26).
##
## Native views come from a frontend package: an enabled package whose Godot pack holds
## res://packages/<package id>/frontend.tscn (antares-ui ships with the game, off by default). The
## highest-priority one is instanced over the legacy view, with `host` set to this node before it
## enters the tree; its script claims the screens and request kinds it draws itself
## (packages/antares-ui/frontend.gd). What a frontend may use here:
##   game    the AntaresGame node: commands, queries, snapshots, requests, claims
##   legacy  the Control that shows the legacy frame (hidden while a native screen is up)
##   art     the sprite service (art.gd)
##   click_game(), shot(), smoke_fail()   for its smoke test
## A frontend also has detach() (the host drops it: claims off) and packs_changed().

@onready var game: AntaresGame = $UI/Legacy/Game
@onready var legacy: Control = $UI/Legacy
@onready var art: Node = $Art

var frontend: Node = null
var frontend_id := ""
var _chosen := false # a frontend has been chosen once
var loaded_packs := {} # pack path -> loaded


func _ready() -> void:
	art.setup(game)
	game.game_exited.connect(func(): get_tree().quit())
	game.resource_packs_changed.connect(_load_packs)
	var args := OS.get_cmdline_user_args()
	if "--smoke" in args:
		if "--native" in args:
			_wait_for_frontend()
		else:
			_smoke()


## Packages may ship Godot resource packs (godot/*.pck or *.zip); the game lists them lowest
## priority first, so each later pack replaces the files of the ones before. Then the frontend
## package is (re)chosen: the package set changes when a save with other packages loads.
func _load_packs() -> void:
	for path: String in game.get_resource_packs():
		if loaded_packs.has(path):
			continue
		loaded_packs[path] = ProjectSettings.load_resource_pack(path, true)
		print("frontend: resource pack %s: %s" % [path, "loaded" if loaded_packs[path] else "FAILED"])
	art.clear() # art is asked again, in case a pack changed it
	var wanted := ""
	for id: String in game.get_packages():
		if ResourceLoader.exists(_entry(id)):
			wanted = id
			break
	if wanted == frontend_id and _chosen:
		if frontend:
			frontend.packs_changed()
		return
	if frontend:
		frontend.detach()
		frontend.queue_free()
		frontend = null
	frontend_id = wanted
	_chosen = true
	if wanted != "":
		frontend = load(_entry(wanted)).instantiate()
		frontend.host = self
		add_child(frontend)
	print("frontend: %s" % ("package " + wanted if wanted != "" else "none (legacy views only)"))


func _entry(package_id: String) -> String:
	return "res://packages/%s/frontend.tscn" % package_id


## `-- --smoke --native` is the frontend package's smoke test; without one there is nothing native.
func _wait_for_frontend() -> void:
	var waited := 0.0
	while frontend == null and waited < 120.0:
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	if frontend == null:
		await smoke_fail("--native, but no frontend package is enabled (enable antares-ui)")


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
		await shot(shots + "/legacy.png")
	# Input path: click the load message's CLOSE button (game 273,253) through Godot's input.
	await click_game(Vector2(273, 253))
	await get_tree().create_timer(1.0).timeout
	if shots != "":
		await shot(shots + "/legacy-after-click.png")
	# A hotkey: P opens the Planets screen.
	game.send_key(KEY_P, false, false, false)
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		await shot(shots + "/legacy-after-key.png")
	print("smoke: stats %s" % [game.get_stats()])
	# The game ends on its own: Godot should close with it (game_exited).
	game.request_game_quit()
	print("smoke: asked the game to quit")
	await get_tree().create_timer(15.0).timeout
	print("smoke: the game did not quit within 15 s")
	get_tree().quit()


func shot(path: String) -> void:
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(path)
	print("smoke: saved %s" % path)


func click_game(at: Vector2) -> void:
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = _game_to_screen(at)
		Input.parse_input_event(click)
		await get_tree().create_timer(0.1).timeout


func _game_to_screen(game_point: Vector2) -> Vector2:
	var size := game.size
	var scale := minf(size.x / 640.0, size.y / 480.0)
	var origin := (size - Vector2(640, 480) * scale) * 0.5
	return game.global_position + origin + game_point * scale


func smoke_fail(why: String) -> void:
	print("smoke: FAILED: %s" % why)
	game.request_game_quit()
	await get_tree().create_timer(15.0).timeout
	get_tree().quit(1)
