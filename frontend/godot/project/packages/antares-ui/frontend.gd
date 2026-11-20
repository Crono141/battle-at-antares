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
@export var claimed_requests: PackedStringArray = ["message", "confirm", "warning", "text", "popup", "choose", "todo"]
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
var _keep_camera := false # the system pane closes for a fleet or a map popup: the map stays where it is
var _race_id := -1
## A fleet opened from inside a system (its pane or its map): the system to go back to when the
## fleet panel closes (owner, 2026-11-12), or -1.
var _fleet_from_system := -1
## WP3.5a: the Colonies panel's Building slot opened a colony's system and build pane; closing
## that system returns to the panel ("Return to Colonies").
var _colonies_return := false
## The right sidebar's panels (owner, 2026-11-14: they fly out from the right): action -> view.
const SIDE_PANELS := {"colonies": "colonies_panel", "planets": "planets_panel", "fleets": "fleets_panel",
	"leaders": "leaders_panel", "races": "races_panel", "info": "info_panel"}
## Side panels the galaxy isn't fitted for (owner, 2026-11-14: "The galaxy is not moved" for Races).
const SIDE_PANELS_UNFITTED := ["races_panel", "info_panel"]
var _switching_panel := false # one side panel replaces another: the camera stays fitted
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
# 35: scrapping freighters; 30, 31, 33, 34: colonising, outposts, the colony landing, unloading
# transports from the galaxy (owner, 2026-11-19: the legacy galaxy showed behind the landing).
const GALAXY_BEHIND_SCREENS := [Screens.MAIN, Screens.MAIN_NEXT_TURN, Screens.REPORTS, Screens.TURN_SUMMARY, 35, 30, 31, 33, 34]
var _question_over_galaxy := false
var _shot_popups := {} # the smoke's pictures of native popups, by name
var _turn_over_galaxy := false # quiet turns passing by themselves, shown over the native galaxy
var _turn_notice: PanelContainer
var _turn_stopping := false # a click asked the turns to stop: Esc again until they do
var _turn_clearing := -1.0 # seconds since the notice cleared, while the hand-back waits (-1: none)
var _turn_clearing_polls := 0 # the input polls when _turn_clearing started
var _end_turn_at := -100000 # when TURN was last sent (ms): its first legacy frame may be a quiet turn's
const TURN_HANDBACK_DELAY := 1.0
const TURN_HANDBACK_LIMIT := 10.0 # seconds: handed back even if the legacy screen never polls
var _turn_stop_wait := 0.0
var _legacy_after_frame := -1 # the frame count the legacy view waits past to come back, or -1
var _stale_frame: PackedByteArray # the legacy frame held while the question was up
var _legacy_waited := 0.0
var _shot_question := false # smoke: one screenshot of a turn's question over the native galaxy
## Themes (WP3.3g): the enabled theme packages' looks, the one in use, and the frame it draws
## round the whole view.
const Themes := preload("themes.gd")
var _themes: Array = [] # [{id, name, built}]
var theme_id := ""
var ui_scale := 100.0 # percent: the window's content scale (Options)
var _frame: StyleBox
var _frame_view: Control
var _built := {} # the theme in use, as its package built it


func _ready() -> void:
	game = host.game
	legacy = host.legacy
	art = host.art
	galaxy.art = art
	# Art that arrives later (a fleet's first icon) is drawn at once, not at the next zoom (owner,
	# 2026-11-13: a flag-coloured square until then).
	art.loaded.connect(func(_key: String): galaxy.redraw())
	game.native_screen_changed.connect(_on_native_screen)
	game.command_result.connect(_on_result)
	game.request.connect(_on_request)
	game.snapshot.connect(_on_snapshot)
	if game.has_signal("turn_notice"):
		game.turn_notice.connect(_on_turn_notice)
	if game.has_signal("film"):
		game.film.connect(_on_film)
	galaxy.star_clicked.connect(_on_star_clicked)
	galaxy.fleet_clicked.connect(_open_fleet)
	galaxy.star_hovered.connect(_on_star_hovered)
	galaxy.right_clicked.connect(_end_relocating)
	galaxy.planet_clicked.connect(_on_planet_clicked)
	galaxy.planet_hovered.connect(_on_planet_hovered)
	galaxy.empty_clicked.connect(_on_empty_clicked)
	galaxy.planet_right_clicked.connect(_on_planet_right_clicked)
	galaxy.system_panned.connect(func(away: bool):
		var controls: Control = _views.get("map_controls")
		if controls != null:
			controls.show_recenter(away))
	_frame_view = Control.new()
	_frame_view.name = "ScreenFrame"
	_frame_view.set_anchors_preset(Control.PRESET_FULL_RECT)
	_frame_view.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_frame_view.draw.connect(_draw_frame)
	native_layer.add_child(_frame_view)

	# The game's click on every native button (owner, 2026-11-13: forgotten), as its buttons
	# click: the game plays it (it has the sound), now and for every button added later.
	get_tree().node_added.connect(_add_click)
	for node in find_children("*", "BaseButton", true, false):
		_add_click(node)
	_load_themes()
	galaxy.flight_time = Themes.saved_flight_time(galaxy.FLIGHT_TIME)
	mode_button.pressed.connect(func(): _set_native(not native))
	$UI/Bar.hide() # the owner's layout (WP3.3f): Ctrl+G stays, the button goes
	overlay.hide()
	for item: CanvasItem in _reveal_items():
		item.modulate.a = 0.0
	var args := OS.get_cmdline_user_args()
	_set_native("--native" in args or (start_native and not "--smoke" in args))
	if "--smoke" in args and "--native" in args:
		_smoke_native()


func _add_click(node: Node) -> void:
	var button := node as BaseButton
	if button == null or not is_ancestor_of(button) or button.has_meta("no_click"):
		return
	if not button.pressed.is_connected(_click):
		button.pressed.connect(_click)


func _click() -> void:
	if native:
		game.click_sound()


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
	# Turns passing by themselves: a click or Esc stops them (the legacy galaxy's exit field).
	if _turn_over_galaxy and overlay.visible == false:
		var stop_click := event as InputEventMouseButton
		var stop_key := event as InputEventKey
		if (stop_click and stop_click.pressed) or (stop_key and stop_key.pressed and stop_key.keycode == KEY_ESCAPE):
			# Asked again until the turns stop (_process).
			_turn_stop_wait = 0.0
			_turn_stopping = true
			game.stop_auto_turns()
			get_viewport().set_input_as_handled()
			return
	# A click outside an open flyout closes it (owner, 2026-11-13); on the map it does only that.
	# The bar and the side tabs keep their own clicks (they switch or close flyouts themselves),
	# and an open help pane takes the click first (info_tip.gd).
	var press := event as InputEventMouseButton
	if press and press.pressed and press.button_index == MOUSE_BUTTON_LEFT and native_screen == Screens.MAIN:
		if _close_flyouts_outside(press.position):
			get_viewport().set_input_as_handled()
			return
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
		for what in ["empire", "galaxy", "fleets", "options", "todo"]:
			_send({"verb": "watch", "what": what})
	mode_button.text = "Legacy galaxy (Ctrl+G)" if on else "Native galaxy (Ctrl+G)"


## The game holds the sounds a legacy screen starts until its picture shows (owner, 2026-11-13:
## the news and discovery sounds came a few frames early): released once the legacy view (or a
## question over the galaxy) is up.
var _sounds_held := false


func _process(delta: float) -> void:
	if _todo_pending and _todo_may_open():
		_open_todo()
	if _sounds_held and (legacy.visible or overlay.visible or native_screen != Screens.LEGACY):
		_sounds_held = false
		game.release_sounds()
	if _turn_clearing >= 0.0:
		_turn_clearing += delta
		# Handed back only once the legacy screen waits for the player (it polls for input), or
		# after a long while: between two windows (the GNN, then a race's diplomacy) the game loads
		# the next one without polling, and the hand-back showed the legacy screen for seconds
		# (owner, 2026-11-20, over the share).
		var waiting := int(game.get_stats().get("input_polls", 0)) >= _turn_clearing_polls + 2
		if native_screen == Screens.MAIN or overlay.visible \
				or (_turn_clearing >= TURN_HANDBACK_DELAY and waiting) or _turn_clearing >= TURN_HANDBACK_LIMIT:
			_end_turn_notice()
	if _turn_stopping:
		if not _turn_over_galaxy or native_screen == Screens.MAIN or overlay.visible:
			_turn_stopping = native_screen != Screens.MAIN and _turn_over_galaxy # a question: later
		else:
			_turn_stop_wait += delta
			if _turn_stop_wait >= 0.25:
				_turn_stop_wait = 0.0
				game.stop_auto_turns()
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
	if native_screen == Screens.LEGACY and not _question_over_galaxy and not _turn_over_galaxy and not _film_active:
		galaxy.hide()
		legacy.show()


## WP3.11b (owner, 2026-11-19): the films that play in a box (the dimensional portal, planet
## destruction, Orion taken, the Antarans' attack, the amoeba) in a window sized to the film over
## the native galaxy. The game still plays them (picture and sound together); the legacy view
## shows only the film's part of its frame, raised over the native layers, in the theme's frame.
var _film_active := false
var _film_frame: PanelContainer
const FILM_LAYER := 4


func _on_film(rect: Rect2i) -> void:
	if rect.has_area():
		if not native or galaxy.stars.is_empty():
			return
		_film_active = true
		_layout_film(Rect2(rect))
	elif _film_active:
		_film_active = false
		_end_film()


func _layout_film(rect: Rect2) -> void:
	var view := get_viewport().get_visible_rect().size
	var k := minf(view.x * 0.6 / rect.size.x, view.y * 0.6 / rect.size.y)
	var inner := (rect.size * k).round()
	var at := ((view - inner) * 0.5).round()
	if _film_frame == null:
		_film_frame = PanelContainer.new()
		_film_frame.theme_type_variation = &"Pane"
		_film_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
		native_layer.add_child(_film_frame)
	var pad := 10.0
	_film_frame.position = at - Vector2(pad, pad)
	_film_frame.size = inner + Vector2(pad, pad) * 2.0
	_film_frame.show()
	_film_frame.move_to_front()
	(legacy.get_parent() as CanvasLayer).layer = FILM_LAYER
	legacy.set_anchors_preset(Control.PRESET_TOP_LEFT)
	legacy.position = at
	legacy.size = inner
	game.set_source_rect(rect)
	legacy.show()
	# The galaxy and its chrome stay round the window.
	galaxy.show()
	_frame_view.show()
	for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
		_view(chrome).show()


func _end_film() -> void:
	if _film_frame != null:
		_film_frame.hide()
	(legacy.get_parent() as CanvasLayer).layer = 1
	game.set_source_rect(Rect2())
	legacy.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	if native_screen == Screens.MAIN:
		legacy.hide()
	elif native_screen == Screens.LEGACY and not _turn_over_galaxy:
		# The galaxy stays a moment, as after a scene window: the game may come straight back.
		legacy.hide()
		_turn_over_galaxy = true
		_turn_clearing = 0.0
		_turn_clearing_polls = int(game.get_stats().get("input_polls", 0))


func _on_native_screen(screen: int) -> void:
	_legacy_after_frame = -1
	# Just after TURN the galaxy stays a moment: if the turns pass by themselves their notices
	# follow, and the legacy frame before the first one flashed (owner, 2026-11-13).
	var held: bool = screen == Screens.LEGACY and native_screen == Screens.MAIN and native and \
		Time.get_ticks_msec() - _end_turn_at < 3000 and not galaxy.stars.is_empty()
	if screen == Screens.MAIN:
		_galaxy_behind = true
	elif screen == Screens.LEGACY:
		_galaxy_behind = false
		_sounds_held = true
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
	var summary_panel := _view("summary_panel")
	if not summary_panel.item_chosen.is_connected(_on_summary_item):
		summary_panel.item_chosen.connect(_on_summary_item)
	summary_panel.set_on_galaxy(screen == Screens.MAIN)
	if held:
		_turn_over_galaxy = true
		_turn_clearing = 0.0 # handed back after the delay unless a notice comes
		_turn_clearing_polls = int(game.get_stats().get("input_polls", 0))
		legacy.hide()
		galaxy.show()
		_frame_view.show()
		for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
			_view(chrome).show()
	if _film_active:
		_layout_film(game.get_source_rect()) # a film plays on: its window stays over the galaxy
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
			var colony_pane := _view("colony_pane")
			if colony_pane.colony_id >= 0:
				colony_pane.show()
				colony_pane.refresh()
		if design_list.get_meta("was_open", false):
			design_list.set_meta("was_open", false)
			design_list.fit_bottom(_side_panel_area().end.y)
			design_list.open()
		for panel_name: String in SIDE_PANELS.values():
			var side_view: Control = _views.get(panel_name)
			if side_view != null and side_view.get_meta("was_open", false):
				side_view.set_meta("was_open", false)
				side_view.show()
				side_view.refresh()
		if _fleet_ship >= 0:
			_refresh_fleet()
	else:
		system_pane.hide() # hidden, not closed: it comes back with the galaxy
		_view("colony_pane").dismiss()
		_view("build_pane").dismiss() # a Cancel
		design_list.set_meta("was_open", design_list.visible or design_list.get_meta("was_open", false))
		design_list.hide()
		for panel_name: String in SIDE_PANELS.values():
			var side_view: Control = _views.get(panel_name)
			if side_view != null:
				# Hidden, not closed: the camera stays fitted and the panel comes back with the galaxy.
				side_view.set_meta("was_open", side_view.visible or side_view.get_meta("was_open", false))
				side_view.hide()
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


## The screen's fixed parts over the panes, so panes slide out from behind them (owner,
## 2026-11-13: they slid in over the top bar): the bar and the side tabs, then the frame over all.
## The map controls' pocket stays under the panes, which reach the bottom of the screen.
func _raise_chrome() -> void:
	for chrome in ["galaxy_bar", "side_tabs"]:
		if _views.has(chrome):
			(_views[chrome] as Control).move_to_front()
	_frame_view.move_to_front()


## Fills one of the bar's drop-downs from its query and slides it down under `center`.
func _drop_down(action_name: String, panel: Control, research_text: String, center: float) -> void:
	if action_name == "command_points":
		var points: Dictionary = await _query("command_points")
		panel.show_result(points)
	else:
		panel.set_progress(research_text)
		var research: Dictionary = await _query("research")
		panel.show_research(research)
		panel.open_in(_research_area()) # owner, 2026-11-14: right edge at the bar's, above the map controls
		return
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
	_apply_saved_scale()


## The scale slider dragged by pointer events, as a player drags it (owner, 2026-11-13: the
## scale ran away while dragging, and the number showed another scale than the one in use): to
## the right end, then to the left end. Each must land where the pointer is, the number must say
## so, and Options must stay inside the window. The player's saved scale is put back after.
func _smoke_scale_drag(options: Control, shots: String) -> bool:
	var saved := Themes.saved_ui_scale()
	var ok := true
	print("smoke: scale at start %.0f%% (saved %.0f, automatic %s at %.0f, largest %.0f; window %s; slider editable %s)" % [
		ui_scale, saved, options.scale_auto.button_pressed, automatic_ui_scale(), max_ui_scale(), get_window().size,
		options.scale_slider.editable])
	options.scale_auto.button_pressed = false # the player's own scale: the slider works
	await get_tree().create_timer(0.3).timeout
	for end: float in [1.0, 0.0]:
		var slider: HSlider = options.scale_slider
		var factor := get_window().content_scale_factor
		var track := slider.get_global_rect()
		var y := track.get_center().y * factor
		var along := clampf(track.size.x * (slider.value - slider.min_value) / maxf(slider.max_value - slider.min_value, 1.0), 4.0, track.size.x - 4.0)
		var x := (track.position.x + along) * factor
		var goal := (track.position.x + track.size.x * end) * factor
		var expected: float = slider.max_value if end > 0.5 else slider.min_value
		_smoke_mouse(true, Vector2(x, y))
		await get_tree().process_frame
		for i in range(1, 13):
			var motion := InputEventMouseMotion.new()
			motion.button_mask = MOUSE_BUTTON_MASK_LEFT
			motion.position = Vector2(lerpf(x, goal, i / 12.0), y)
			Input.parse_input_event(motion)
			await get_tree().process_frame
			await get_tree().process_frame
		_smoke_mouse(false, Vector2(goal, y))
		await get_tree().create_timer(0.5).timeout
		var used := get_window().content_scale_factor * 100.0
		var text: String = options.scale_value.get_line_edit().text
		var bottom: float = options.get_global_rect().end.y
		var room := get_viewport().get_visible_rect().size.y
		print("smoke: scale dragged to %s: in use %.0f%%, slider %.0f, box '%s' (expected %.0f; limits %.0f-%.0f); options bottom %.0f of %.0f" % [
			"the right" if end > 0.5 else "the left", used, slider.value, text, expected, slider.min_value, slider.max_value, bottom, room])
		if absf(used - expected) > 0.5 or not text.begins_with(str(int(expected))) or bottom > room + 1.0:
			ok = false
		if shots != "" and end > 0.5:
			await host.shot(shots + "/native-options-largest.png")
	options.scale_auto.button_pressed = true
	await get_tree().create_timer(0.5).timeout
	var expected_auto := clampf(automatic_ui_scale(), MIN_UI_SCALE, max_ui_scale())
	print("smoke: automatic again: in use %.0f%% (expected %.0f), saved %.0f, slider editable %s, box '%s'" % [
		get_window().content_scale_factor * 100.0, expected_auto, Themes.saved_ui_scale(), options.scale_slider.editable,
		options.scale_value.get_line_edit().text])
	if absf(get_window().content_scale_factor * 100.0 - expected_auto) > 0.5 or options.scale_slider.editable \
			or Themes.saved_ui_scale() > 0.0:
		ok = false
	Themes.save_ui_scale(saved)
	set_ui_scale(_wanted_ui_scale(), false)
	await get_tree().create_timer(0.3).timeout
	if not ok:
		await host.smoke_fail("the interface scale did not follow the drag")
	return ok


## Right-click help and clicks outside (owner, 2026-11-13): a second right click swaps the help, a
## left click only closes it, a click on the map closes an open flyout.
func _smoke_tips_and_outside_clicks(options: Control, shots: String) -> bool:
	var factor := get_window().content_scale_factor
	var bar := _view("galaxy_bar")
	var tips := preload("views/info_popup.gd")
	var first: Control = bar.get_node("Row/Research")
	var second: Control = bar.get_node("Row/Game") # left of the first: outside its help
	for target: Control in [first, second]:
		var at := target.get_global_rect().get_center() * factor
		for pressed in [true, false]:
			var right := InputEventMouseButton.new()
			right.button_index = MOUSE_BUTTON_RIGHT
			right.pressed = pressed
			right.position = at
			Input.parse_input_event(right)
			await get_tree().process_frame
		await get_tree().create_timer(0.6).timeout
		var hovered := get_viewport().gui_get_hovered_control()
		print("smoke: right click on %s at %s: over %s, help open %s" % [target.name, at, hovered.get_path() if hovered else "nothing", tips.is_open()])
	var shown_title := ""
	if tips.is_open():
		shown_title = (tips._current.find_children("*", "Label", true, false)[0] as Label).text
		if shots != "":
			await host.shot(shots + "/native-help-turn.png")
	var tip_layers := get_tree().root.get_children().filter(func(n: Node) -> bool: return n is CanvasLayer and n.layer == 120).size()
	# A left click on the map: closes the help only (Options stays open).
	var map_point := Vector2(get_viewport().get_visible_rect().size.x * 0.3, get_viewport().get_visible_rect().size.y * 0.6) * factor
	_smoke_mouse(true, map_point)
	_smoke_mouse(false, map_point)
	await get_tree().create_timer(0.4).timeout
	var tip_closed := not tips.is_open()
	var options_after_tip := options.visible
	# Another click on the map: now it closes Options.
	_smoke_mouse(true, map_point)
	_smoke_mouse(false, map_point)
	await get_tree().create_timer(0.6).timeout
	print("smoke: help on right clicks: '%s' (layers %d), left click closed it %s and left Options open %s; a map click closed Options %s" % [
		shown_title, tip_layers, tip_closed, options_after_tip, not options.visible or options._closing if "_closing" in options else not options.visible])
	if shown_title != "Game Button" or tip_layers != 1 or not tip_closed or not options_after_tip:
		await host.smoke_fail("right-click help or the outside click did not behave")
		return false
	_on_view_action("options") # open again for the steps after
	await get_tree().create_timer(0.6).timeout
	return true


func _smoke_mouse(pressed: bool, at: Vector2) -> void:
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = pressed
	click.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
	click.position = at
	Input.parse_input_event(click)


## The interface scale (owner, 2026-11-12, after a friend found everything illegibly small on a
## 2560x1600 screen): the window's content scale, so panels, text and the map's labels grow
## together and every coordinate stays in one space. Saved in the settings; with none saved, the
## screen's height decides (1080 lines: 100%), in steps of 5%. What is used is the wanted scale
## held to what the window can show (owner, 2026-11-13: the screen's 135% was shown while the
## start-up window's limit was in use, and the slider ran off the bottom of the screen).
const MIN_UI_SCALE := 75.0
const MAX_UI_SCALE := 300.0
const MIN_LOGICAL := Vector2(1280, 720) # the panes side by side over the galaxy
## Insets, gaps between the panes and the side tabs' column, across (interface pixels).
const PANES_MARGIN := 110.0
## The build pane's narrowest layout (its two columns), across (interface pixels).
const BUILD_PANE_MIN := 560.0


func _apply_saved_scale() -> void:
	if not get_window().size_changed.is_connected(_on_window_resized):
		get_window().size_changed.connect(_on_window_resized)
	set_ui_scale(_wanted_ui_scale(), false)


## The player's choice (or the command line's), else automatic.
func _wanted_ui_scale() -> float:
	var percent := Themes.saved_ui_scale()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--ui-scale="):
			percent = float(arg.substr(11))
	if percent <= 0.0:
		percent = automatic_ui_scale()
	return clampf(percent, MIN_UI_SCALE, MAX_UI_SCALE)


## Automatic (no scale saved; the Options switch, owner 2026-11-13): what a 1920x1080 window
## shows at 100%, kept for this window, in steps of 5% and never below 100%. A 2560x1440 screen's
## window gets 130%, a 2560x1600 one 135%; it follows the window when that changes.
func automatic_ui_scale() -> float:
	var window := Vector2(get_window().size)
	var fits := minf(window.x / 1920.0, window.y / 1080.0)
	return clampf(roundf(fits * 20.0) * 5.0, 100.0, MAX_UI_SCALE)


func is_ui_scale_automatic() -> bool:
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--ui-scale="):
			return false
	return Themes.saved_ui_scale() <= 0.0


## The Automatic switch: on forgets the player's scale, off keeps the one in use as theirs.
func set_ui_scale_automatic(on: bool) -> void:
	if on:
		Themes.save_ui_scale(0.0)
		set_ui_scale(_wanted_ui_scale(), false)
	else:
		set_ui_scale(ui_scale, true)


## The largest scale the window holds: the panes need MIN_LOGICAL, and the side tabs their
## whole column, in interface pixels.
func max_ui_scale() -> float:
	var window := Vector2(get_window().size)
	var need := MIN_LOGICAL
	# Across: the system, colony and build panes side by side and the side tabs (owner,
	# 2026-11-14: in a window narrower than 16:9 the build pane ran off the right edge; the
	# interface now shrinks so the three fit).
	# A fixed width for the build pane, never its measured one: that changes with the layout the
	# scale itself gives, and the two fed each other until the message queue overflowed (owner,
	# 2026-11-14: a crash dragging the window wider with the build pane open).
	need.x = maxf(need.x, 372.0 + 340.0 + BUILD_PANE_MIN + PANES_MARGIN)
	if _views.has("side_tabs"):
		var tabs: Control = _views.side_tabs
		need.y = maxf(need.y, tabs.get_combined_minimum_size().y + tabs.offset_top - tabs.offset_bottom)
	# The window can't shrink below what the panes need at the smallest scale (owner, 2026-11-14:
	# narrower windows went on shrinking the panes out of reach).
	var smallest := Vector2i((need * MIN_UI_SCALE / 100.0).ceil())
	if get_window().min_size != smallest:
		get_window().min_size = smallest
	var largest := floorf(minf(window.x / need.x, window.y / need.y) * 20.0) * 5.0
	return clampf(largest, MIN_UI_SCALE, MAX_UI_SCALE)


## A different window: the wanted scale again, held to what this one holds.
## The window's size in pixels when the scale was last worked out.
var _scaled_for := Vector2i(-1, -1)


func _on_window_resized() -> void:
	# Only a real change of the window: a new interface scale reports a size change too, and
	# answering that one again could loop (owner, 2026-11-14: "Message queue out of memory").
	if get_window().size == _scaled_for:
		return
	_scaled_for = get_window().size
	set_ui_scale(_wanted_ui_scale(), false)
	# Everything floating follows the window (owner, 2026-11-14: "Windows should ALWAYS resize or
	# shift with the window"); dialogs and drop-downs keep themselves on the screen.
	_replace_floating.call_deferred()
	_reflow_open_panes()


## After a window resize: the research drop-down and picker at the research area's right edge, the
## research window's rect, the fleet panel beside its fleet.
func _replace_floating() -> void:
	await get_tree().process_frame # the bar and tabs laid out at the new size first
	var area := _research_area()
	for panel: Control in [_views.get("research_panel"), _picker]:
		if panel != null and panel.visible:
			panel.position.x = maxf(area.end.x - panel.size.x, 8.0)
	if _scene_window != null and _scene_window.visible and _scene_window.custom_rect.has_area():
		_scene_window.custom_rect = _research_scene_rect()
		_scene_window._layout()
	var fleet_panel: Control = _views.get("fleet_panel")
	if fleet_panel != null and fleet_panel.visible and _fleet_ship >= 0:
		_place_fleet_panel(fleet_panel, _fleet_position(_fleet_ship))


## Applies a scale in percent; `remember`: saved as the player's choice.
func set_ui_scale(percent: float, remember := true) -> void:
	if remember:
		Themes.save_ui_scale(clampf(percent, MIN_UI_SCALE, MAX_UI_SCALE))
	var largest := max_ui_scale()
	percent = clampf(percent, MIN_UI_SCALE, largest)
	if not is_equal_approx(get_window().content_scale_factor, percent / 100.0):
		get_window().content_scale_factor = percent / 100.0
	ui_scale = percent
	if _views.has("options_menu"):
		_views.options_menu.show_scale(percent, largest, MIN_UI_SCALE, is_ui_scale_automatic())


## The open panes laid out again after the window or the theme changed. The side panels stay
## against the side tabs (owner, 2026-11-14: a wider window left a gap, a narrower one slid them
## under the tabs), and the galaxy fits beside them again; the build pane beside the colony
## pane's real width (owner, 2026-11-20: a theme change put it back at the nominal one, over the
## colony pane, as its own restyle does).
func _reflow_open_panes() -> void:
	if SIDE_PANELS.values().any(func(n: String) -> bool: return _views.has(n) and (_views[n] as Control).visible):
		await get_tree().process_frame # the tabs laid out at the new size first
		await get_tree().process_frame
		for panel_name: String in SIDE_PANELS.values():
			var side_view: Control = _views.get(panel_name)
			if side_view != null and side_view.visible:
				side_view.reflow(_side_panel_area())
				_fit_beside_panel(side_view)
	if _views.has("design_panel") and _views.design_panel.visible:
		await get_tree().process_frame
		_views.design_panel.reflow(_side_panel_area())
	if _views.has("build_pane") and _views.build_pane.visible:
		await get_tree().process_frame # the panes and tabs laid out at the new size first
		await get_tree().process_frame
		if _views.build_pane.visible:
			_place_build_pane()


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
	_reflow_open_panes()


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
		if view.has_signal("scale_changed"):
			view.scale_changed.connect(func(percent: float, final: bool): set_ui_scale(percent, final))
		if view.has_signal("flight_changed"):
			view.flight_changed.connect(func(seconds: float):
				galaxy.flight_time = seconds
				Themes.save_flight_time(seconds))
		if view.has_signal("help_requested"):
			view.help_requested.connect(show_help)
		if view.has_signal("scale_auto_changed"):
			view.scale_auto_changed.connect(set_ui_scale_automatic)
		if view.has_signal("change_build"):
			view.change_build.connect(_open_build_pane)
		if view.has_signal("committed"):
			view.committed.connect(func(): _view("colony_pane").refresh())
		if view.has_signal("colony_chosen"):
			view.colony_chosen.connect(_on_colony_chosen)
		if view.has_signal("planet_selected"):
			view.planet_selected.connect(_on_pane_planet)
		if view.has_signal("width_changed"):
			view.width_changed.connect(_fit_beside_panel.bind(view))
		if view.has_signal("fleet_marked"):
			view.fleet_marked.connect(func(ship: int):
				galaxy.selected_fleet = ship
				galaxy.redraw())
		if view.has_signal("build_chosen"):
			view.build_chosen.connect(_colonies_build)
		if view.has_signal("colony_opened"):
			view.colony_opened.connect(func(colony_id: int, star_id: int, planet_id: int):
				_colonies_build(colony_id, star_id, planet_id, false, false))
		if view.has_signal("star_marked"):
			view.star_marked.connect(func(star: int):
				galaxy.selected = star
				galaxy.redraw())
		if view.has_signal("selection_changed"):
			view.selection_changed.connect(func(): _on_star_hovered(_hovered_star))
		if view.has_signal("settle_requested"):
			view.settle_requested.connect(func(star_id: int): _open_system(star_id))
		native_layer.add_child(view)
		_raise_chrome() # the bar, the pockets, the tabs and the frame stay over every view
		_views[scene_name] = view
		_attach_view_help(scene_name, view)
		if scene_name == "side_tabs": # their column limits the scale
			_apply_saved_scale.call_deferred()
		if scene_name == "options_menu":
			view.show_flight(galaxy.flight_time)
			view.show_themes(_themes, theme_id)
			view.show_scale(ui_scale, max_ui_scale(), MIN_UI_SCALE, is_ui_scale_automatic())
		if scene_name in ["galaxy_bar", "map_controls"]: # the frame's joints follow their pockets
			view.item_rect_changed.connect(_frame_view.queue_redraw)
			view.visibility_changed.connect(_frame_view.queue_redraw)
		if scene_name == "map_controls":
			view.item_rect_changed.connect(_clear_map_controls)
		if scene_name in ["map_controls", "colony_pane", "build_pane"]:
			_clear_map_controls.call_deferred()
	return _views[scene_name]


## A view's command; one with "_reply_to" gets its result back through that method of the view
## (the system panel's colony information).
func _on_view_command(dict: Dictionary, view: Control) -> void:
	# The Fleets panel's LEADERS: the Leaders panel's Ship Officers tab (owner's design).
	if String(dict.get("verb", "")) == "open" and int(dict.get("screen", -1)) == 29 and view == _views.get("fleets_panel"):
		var leaders_view := _view("leaders_panel")
		leaders_view.tab = "ship"
		var fleets_fleet: Dictionary = (view as Control).get("fleet")
		leaders_view.want_fleet = int(fleets_fleet.get("ship", -1)) # opens on the Fleets panel's fleet (owner)
		_open_side_panel("leaders")
		return
	# The design list's EDIT: the native Ship Design panel on that slot (WP3.6, owner's mockup B2).
	if String(dict.get("verb", "")) == "open" and int(dict.get("screen", -1)) == Screens.DESIGN and view == _views.get("design_list"):
		_open_design(int(dict.get("slot", 0)))
		return
	# The Leaders panel's FLEETS: the Fleets panel on that fleet.
	if String(dict.get("verb", "")) == "open_side_panel":
		_fleet_for_panel = int(dict.get("ship", -1))
		_open_side_panel(String(dict.panel))
		return
	if dict.has("_reply_to"):
		var method: String = dict["_reply_to"]
		dict.erase("_reply_to")
		_send(dict, Callable(view, method))
	else:
		_send(dict)


## The to-do list's last answer (WP3.4d, a watched query).
var _todo := {}


## Owner, 2026-11-14: the list opens by itself when there is something to do (a new turn, or the
## count changed since it was last shown or closed), once nothing else is in the way: no question
## up, the galaxy showing, no other drop-down open.
var _todo_pending := false
## The open count the list was last shown at or closed by hand at (-1: a new turn).
var _todo_seen := -1


func _open_todo() -> void:
	var todo := _view("todo_panel")
	if not todo.item_chosen.is_connected(_on_todo_item):
		todo.item_chosen.connect(_on_todo_item)
	_todo_seen = int(_todo.get("open", 0))
	_todo_pending = false
	_bring_up(todo)
	todo.show_todo(_todo)
	var bar := _view("galaxy_bar")
	todo.open_at(bar.get_node("Row/Turn").get_global_rect().get_center().x)


func _todo_may_open() -> bool:
	if native_screen != Screens.MAIN or overlay.visible or legacy.visible or not galaxy.visible:
		return false
	for scene_name in FLYOUTS:
		var other: Control = _views.get(scene_name)
		if other != null and other.visible:
			return false
	return int(_todo.get("open", 0)) > 0


## The turn summary's notification area (owner, 2026-11-14): asked again each turn and when the
## options change.
func _refresh_summary() -> void:
	_send({"verb": "query", "what": "turn_summary"}, func(result: Dictionary):
		if result.get("ok", false):
			_view("summary_panel").show_summary(result))


## A summary message picked: its colony opens (the colony pane, as a to-do build item).
func _on_summary_item(item: Dictionary) -> void:
	var colony := int(item.get("colony", -1))
	if colony < 0 or native_screen != Screens.MAIN:
		return
	_open_system(int(item.get("star", -1)))
	await get_tree().create_timer(0.5).timeout
	if native_screen == Screens.MAIN:
		_on_colony_chosen(colony)


## A message picked in the Info panel's Turn Summary: the panel goes and its colony opens.
func _on_info_summary_item(item: Dictionary) -> void:
	if int(item.get("colony", -1)) < 0:
		return
	_view("info_panel").close()
	_on_summary_item(item)


## A to-do item picked (WP3.4d): what it needs opens; news is marked read.
func _on_todo_item(item: Dictionary) -> void:
	_view("todo_panel").close()
	var star := int(item.get("star", -1))
	match String(item.get("kind", "")):
		"build":
			# The colony and its build list, as the game's jump to the colony screen did.
			_open_colony_at(star, int(item.colony), true)
		"colonize":
			# The landing animation plays (owner, 2026-11-14) until a click, as in the original;
			# back on the galaxy the new colony's pane opens (owner, 2026-11-13: it stayed on the
			# legacy screen).
			_send({"verb": "todo_colonize", "star": star}, func(result: Dictionary):
				var colony := int(result.get("colony", -1))
				if colony < 0:
					return
				# The answer comes before the landing screen takes over: wait for it to leave
				# the galaxy (a moment), then to come back.
				var waited := 0.0
				while native_screen == Screens.MAIN and waited < 3.0:
					await get_tree().create_timer(0.1).timeout
					waited += 0.1
				waited = 0.0
				while native_screen != Screens.MAIN and waited < 900.0: # the player takes their time
					await get_tree().create_timer(0.2).timeout
					waited += 0.2
				if native_screen != Screens.MAIN:
					return
				await get_tree().create_timer(0.3).timeout
				_open_system(star)
				await get_tree().create_timer(0.5).timeout
				if native_screen == Screens.MAIN:
					_on_colony_chosen(colony))
		"hire":
			_send({"verb": "todo_hire"})
		"summary":
			_send({"verb": "todo_read", "item": int(item.item)})
			_send({"verb": "open", "screen": Screens.TURN_SUMMARY})
		"leader_level":
			# The level-up window (marks it read), over the leader's system when it has one.
			if star >= 0:
				_open_system(star)
			_send({"verb": "todo_level", "item": int(item.item)})
		"new_system":
			# The discovery window: the special it found and what it means, a marooned leader
			# (owner, 2026-11-13: a pirate cache's item only zoomed in). It flies into the system
			# itself; a plain system just zooms in.
			if item.has("special") or item.has("leader"):
				_send({"verb": "todo_system", "item": int(item.item)})
			else:
				_send({"verb": "todo_read", "item": int(item.item)})
				if star >= 0:
					_open_system(star)
		_: # other news
			_send({"verb": "todo_read", "item": int(item.item)})
			if star >= 0:
				_open_system(star)


## The drop-downs and slide-outs that buttons open (owner, 2026-11-12): one at a time, over
## everything else (the system pane, the fleet panel).
const FLYOUTS := ["game_menu", "options_menu", "design_list", "command_points", "research_panel", "tax_panel", "todo_panel", "colonies_panel", "planets_panel", "fleets_panel", "leaders_panel", "races_panel", "info_panel", "design_panel"]


## Closes the flyouts a click at `at` falls outside of; true when the click landed on the map
## (nothing else to do with it).
func _close_flyouts_outside(at: Vector2) -> bool:
	var hovered := get_viewport().gui_get_hovered_control()
	for chrome in ["galaxy_bar", "side_tabs"]:
		if hovered and _views.has(chrome) and (_views[chrome] as Control).is_ancestor_of(hovered):
			return false
	var closed := false
	for name in FLYOUTS:
		if name == "design_panel":
			continue # an open design is closed with CANCEL or BUILD, never by a stray click (WP3.6)
		if not _views.has(name) or (name in SIDE_PANELS.values() and not name in SIDE_PANELS_UNFITTED):
			continue # the side panels stay: the map beside them is part of them (owner, 2026-11-15);
			# Info and Races, over the galaxy, close like the other flyouts (owner, 2026-11-15)
		var flyout: Control = _views[name]
		if not flyout.visible or flyout.get_global_rect().has_point(at):
			continue
		if hovered and (flyout == hovered or flyout.is_ancestor_of(hovered)):
			continue
		if flyout.has_method("owns_point") and flyout.owns_point(at):
			continue # its own side panel (the game menu's slots)
		flyout.close()
		closed = true
	return closed and hovered == null


## The original's right-click help on the native views' parts (owner, 2026-11-13: "anywhere there
## was a right click tooltip in the original game"): HELP.LBX records from the game's help lists
## (EVANHELP main screen, ERICHELP building queue screen).
const VIEW_HELP := {
	"galaxy_bar": {"Row/Game": 285, "Row/Empire": 284, "Row/Money": 286, "Row/Command": 288, "Row/Food": 290,
		"Row/Freighters": 291, "Row/Research": 292, "Row/Turn": 296},
	"side_tabs": {"Colonies": 293, "Planets": 294, "Fleets": 295, "Leaders": 297, "Races": 298, "Info": 299,
		"ShipDesign": 509}, # the build screen's Design button's help (owner)
	"build_pane": {"Box/Columns/Right/Lists/UnitsFrame/UnitsBox/UnitsTitle": 504, "Box/Columns/Right/Lists/UnitsFrame/UnitsBox/UnitsScroll": 504,
		"Box/Columns/Right/Lists/ItemsFrame/ItemsBox/ItemsTitle": 501, "Box/Header/Refit": 508,
		"Box/Columns/Left/Detail": 505, "Box/Columns/Left/Queue": 506, "Box/Columns/Left/Toggles/Auto": 507,
		"Box/Columns/Left/Toggles/Repeat": 510, "Box/Buttons/Ok": 511, "Box/Buttons/Cancel": 537},
}


func _attach_view_help(scene_name: String, view: Control) -> void:
	for path: String in VIEW_HELP.get(scene_name, {}):
		var control := view.get_node_or_null(path) as Control
		if control:
			attach_help(control, int(VIEW_HELP[scene_name][path]))
	if scene_name == "build_pane":
		# The lower list: buildings, or the warships in orbit while REFIT is on.
		var list := view.get_node_or_null("Box/Columns/Right/Lists/ItemsFrame/ItemsBox/Scroll") as Control
		if list:
			attach_help(list, func() -> int:
				return 508 if view.tabs_refit.button_pressed else 501)


## A right click on `control` shows help record `topic` (or what a Callable returns) beside it.
func attach_help(control: Control, topic: Variant) -> void:
	if control.mouse_filter == Control.MOUSE_FILTER_IGNORE:
		control.mouse_filter = Control.MOUSE_FILTER_PASS
	control.gui_input.connect(func(event: InputEvent):
		var mb := event as InputEventMouseButton
		if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
			var t: int = int(topic.call()) if topic is Callable else int(topic)
			show_help(t, control)
			control.accept_event())


func show_help(topic: int, at: Control) -> void:
	var result: Dictionary = await _query_args({"what": "help", "topic": topic})
	if result.get("ok", false) and is_instance_valid(at) and at.is_visible_in_tree():
		preload("views/info_popup.gd").open(at, at, String(result.title).strip_edges(), String(result.text).strip_edges())


## A click on empty map in a system: back to the galaxy (owner, 2026-11-13).
func _on_empty_clicked() -> void:
	if _panel_star >= 0 and _view("system_pane").visible:
		_view("system_pane").close()


## Closes the other flyouts and raises `view` over every view (the frame stays on top).
func _bring_up(view: Control) -> void:
	for scene_name in FLYOUTS:
		var other: Control = _views.get(scene_name)
		if other != null and other != view and other.visible:
			other.close()
	view.move_to_front()
	_raise_chrome()


## The map controls' view-only actions.
func _on_view_action(action_name: String) -> void:
	match action_name:
		"recenter":
			if galaxy.system_star >= 0 and native_screen == Screens.MAIN:
				_frame_system(galaxy.system_star)
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
		"turn":
			# WP3.4d: with items waiting the to-do list drops down (END TURN is in it); otherwise
			# the turn ends, as before.
			var todo := _view("todo_panel")
			_todo_seen = int(_todo.get("open", 0)) # opened or closed by hand: no reopening for it
			if todo.visible:
				todo.close()
			elif int(_todo.get("open", 0)) > 0:
				_open_todo()
			else:
				_send({"verb": "end_turn"})
		"tax_rate":
			var tax := _view("tax_panel")
			if tax.visible:
				tax.close()
			else:
				_bring_up(tax)
				var bar := _view("galaxy_bar")
				tax.show_rate(int(bar.tax_rate))
				tax.open_at(bar.money.get_global_rect().get_center().x)
		"command_points", "research":
			# The bar's drop-downs (WP3.3g): under their figure, on the galaxy.
			var panel := _view("command_points" if action_name == "command_points" else "research_panel")
			if panel.visible:
				panel.close()
			else:
				_bring_up(panel)
				var bar := _view("galaxy_bar")
				var figure: Control = bar.command_label if action_name == "command_points" else bar.research
				var center := figure.get_global_rect().get_center().x
				_drop_down(action_name, panel, bar.research.text, center)
		"colonies", "planets", "fleets", "leaders", "races", "info":
			var side_view := _view(SIDE_PANELS[action_name])
			if side_view.visible:
				side_view.close()
			else:
				_open_side_panel(action_name)
		"designs":
			var designs := _view("design_list")
			var editor: Control = _views.get("design_panel")
			if editor != null and editor.visible:
				editor.close() # the tab again: the open design is cancelled
			elif designs.visible:
				designs.close()
			else:
				_bring_up(designs)
				designs.fit_bottom(_side_panel_area().end.y)
				designs.open()
				_view("side_tabs").show_open("designs", true)
		"options":
			var options := _view("options_menu")
			if options.visible:
				options.close()
			else:
				_bring_up(options)
				options.open()
				_view("side_tabs").show_open("options", true)
		"game_menu":
			var game_menu := _view("game_menu")
			if game_menu.visible:
				game_menu.close()
			else:
				_bring_up(game_menu)
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
		_panel_star = -1 # first: the colony pane's closing doesn't re-frame a closing system
		_view("colony_pane").close()
		galaxy.selected = -1
		galaxy.hide_system()
		var back: Button = _view("system_pane").get_node("Box/Back")
		back.text = "BACK TO GALAXY"
		if _colonies_return:
			_colonies_return = false
			if _keep_camera: # a fleet took the system's place: the panel doesn't come back
				galaxy.forget_fit()
			else:
				galaxy.forget_flight()
				_open_side_panel("colonies") # back to the whole galaxy, the panel as it was
		elif not _keep_camera:
			galaxy.fly_back()
		galaxy.redraw()
	elif scene_name == "colony_pane":
		_view("build_pane").close_keeping()
		if _panel_star >= 0 and _view("system_pane").visible:
			_frame_system(_panel_star) # the free area widened again
	elif scene_name == "design_list" or scene_name == "design_panel":
		_view("side_tabs").show_open("designs", false)
	elif scene_name in SIDE_PANELS.values():
		_view("side_tabs").show_open(SIDE_PANELS.find_key(scene_name), false)
		galaxy.selected = -1
		if not _colonies_return and not _switching_panel:
			galaxy.unfit() # the camera where it was before the panel
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
	if String(command.get("verb", "")) == "end_turn":
		_end_turn_at = Time.get_ticks_msec()
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
		"system":
			# The shown system each turn (owner, 2026-11-19: during a run of turns the planets kept
			# their orbits until the turns stopped).
			if result.get("ok", false) and galaxy.system_star >= 0 and int(result.get("star", -1)) == galaxy.system_star:
				galaxy.show_system(galaxy.system_star, result)
				galaxy.redraw()
		"empire":
			_view("galaxy_bar").show_empire(result)
			var colony_view: Control = _views.get("colony_pane")
			if colony_view != null and colony_view.visible:
				colony_view.refresh() # a turn, a purchase: the colony may have changed
			for panel_name: String in SIDE_PANELS.values():
				var side_view: Control = _views.get(panel_name)
				if side_view != null and side_view.visible:
					side_view.refresh()
			_have_empire = _have_empire or result.get("ok", false)
			_race_id = int(result.get("race_id", -1))
			_view("system_pane").race_id = _race_id
		"galaxy":
			galaxy.set_galaxy(result)
		"fleets":
			galaxy.set_fleets(result)
		"todo":
			if int(result.get("stardate", -1)) != int(_todo.get("stardate", -1)):
				_todo_seen = -1 # a new turn's list
				_refresh_summary() # and its summary
			_todo = result
			var open := int(result.get("open", 0))
			_view("galaxy_bar").show_todo(open)
			var todo_view: Control = _views.get("todo_panel")
			if todo_view != null and todo_view.visible:
				if open == 0:
					todo_view.close() # everything answered (owner, 2026-11-14)
				else:
					todo_view.show_todo(result)
			_todo_pending = open > 0 and open != _todo_seen
			if _fleet_ship >= 0 and native_screen == Screens.MAIN:
				_refresh_fleet() # a turn, or an order, changed the fleets
		"options":
			_view("options_menu").show_options(result)
			_refresh_summary() # the End of Turn Summary option shows or hides it


## Quiet turns passing by themselves (End Of Turn Wait off): the game's "<race>: Stardate N"
## each turn, over the native galaxy instead of the legacy one (owner, 2026-11-13). "" when the
## turns stop or another screen takes over (a combat): the legacy view comes back with its next
## frame. A click or Esc stops the turns, as on the legacy galaxy.
func _on_turn_notice(text: String) -> void:
	if text != "" and native and not galaxy.stars.is_empty():
		if not _turn_over_galaxy:
			_turn_over_galaxy = true
			var held: Image = game.get_frame_image()
			_stale_frame = held.get_data() if held != null else PackedByteArray()
		legacy.hide()
		galaxy.show()
		_frame_view.show()
		# The screen as it was (owner, 2026-11-13: only the stars and the notice was jarring); a
		# click anywhere still only stops the turns (_input).
		for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
			_view(chrome).show()
		if _turn_notice == null:
			_turn_notice = PanelContainer.new()
			_turn_notice.theme_type_variation = &"Pane"
			_turn_notice.mouse_filter = Control.MOUSE_FILTER_IGNORE
			var label := Label.new()
			label.theme_type_variation = &"Name"
			label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
			_turn_notice.add_child(label)
			native_layer.add_child(_turn_notice)
		_turn_clearing = -1.0 # turns go on: no hand-back to the legacy view
		(_turn_notice.get_child(0) as Label).text = text
		_turn_notice.reset_size()
		_turn_notice.show()
		_turn_notice.move_to_front()
		var area := get_viewport().get_visible_rect().size
		_turn_notice.position = ((area - _turn_notice.size) * 0.5).floor()
		return
	_turn_stopping = false
	if _turn_over_galaxy and native_screen != Screens.MAIN and _turn_clearing < 0.0:
		# Not at once: between two quiet turns the notice can clear for a moment, and the legacy
		# frame flashed up (owner, 2026-11-13). The hand-back waits a little (_process).
		_turn_clearing = 0.0
		_turn_clearing_polls = int(game.get_stats().get("input_polls", 0))
		return
	_end_turn_notice()


func _end_turn_notice() -> void:
	_turn_clearing = -1.0
	if _turn_notice != null:
		_turn_notice.hide()
	if _turn_over_galaxy:
		_turn_over_galaxy = false
		# Not under a question over the galaxy (owner, 2026-11-14: a GNN that came during the
		# hand-back lost the bar and tabs until its first page was clicked); its answer hands back.
		# Nor under a film's window (owner, 2026-11-19: the Antarans' attack film, from the turn's
		# reports, took the bar, tabs and frame away when the hand-back ran out); its end hands back.
		if native_screen == Screens.LEGACY and not _question_over_galaxy and not _film_active:
			_frame_view.hide()
			for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
				_view(chrome).hide()
			_legacy_after_frame = int(game.get_stats().frames)
			_legacy_waited = 0.0


func _on_request(id: int, kind: String, request: Dictionary) -> void:
	var dialog: Control = _views.get("dialog")
	if dialog == null:
		dialog = load(_views_dir() + "/dialog.tscn").instantiate()
		dialog.art = art
		dialog.answered.connect(_on_answered)
		dialog.planet_row_hovered.connect(func(planet_id: int):
			galaxy.hovered_planet = planet_id
			galaxy.redraw())
		dialog.theme = native_layer.theme
		$Overlay.add_child(dialog)
		_views["dialog"] = dialog
	overlay.show()
	# A question from the turn's legacy galaxy (the next turn, its reports, or the galaxy itself
	# while the native one hands over): the native galaxy shows behind it instead of the legacy
	# frame (owner, 2026-11-12). Other legacy screens (combat, colonies) keep their frame.
	if native and native_screen == Screens.LEGACY and not galaxy.stars.is_empty() and int(request.get("screen", -1)) in GALAXY_BEHIND_SCREENS:
		_question_over_galaxy = true
		# The galaxy as it was behind the question: its bar and tabs too (owner, 2026-11-13: the
		# freighters' slider blanked them, the count it asks about with them).
		for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
			_view(chrome).show()
		_frame_view.show()
		var held: Image = game.get_frame_image()
		_stale_frame = held.get_data() if held != null else PackedByteArray()
		legacy.hide()
		galaxy.show()
	if kind == "popup" and String(request.get("popup", "")) in ["scene", "scene_end"]:
		_on_scene_request(id, request)
		return
	if kind == "popup" and String(request.get("popup", "")) == "research_pick":
		_on_research_pick(id, request)
		return
	_start_popup_map(dialog, request)
	if kind == "popup" and String(request.get("popup", "")) == "occupation" and not request.has("scene_state"):
		# After an invasion (owner, 2026-11-19): the captured colony's pane once answered.
		_occupation_open = [int(request.get("star", -1)), int(request.get("colony", -1))]
	if request.has("scene_state"):
		# A scene's list (diplomacy): the window stays up behind it, the list where the scene
		# had it.
		_scene().show_state(request.scene_state)
		var at: Vector2 = _scene_window.list_position()
		if at != Vector2.INF:
			dialog.placement = "at"
			dialog.placement_at = at
			# Frameless text over the scene (owner, 2026-11-14), the scene left undimmed.
			dialog.bare_width = _scene_window.list_width()
			overlay.color.a = 0.0
		dialog.move_to_front()
		# The list waits for the ambassador (owner, 2026-11-14): until the arrival is over or a
		# click skips it.
		if _scene_window.arriving():
			dialog.hide()
			await _scene_window.arrived
			if not _scene_window.is_open():
				return
	dialog.race_id = _race_id
	dialog.ask(id, kind, request)


## Smoke: the GNN window from the popup preview (an intro page and one story): it opens, both
## pages answer to a click, and it closes.
func _smoke_scene(shots: String) -> bool:
	var box := {}
	_send({"verb": "preview_popup", "popup": "gnn", "event": 3}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	while (_scene_window == null or not _scene_window.visible) and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if _scene_window == null or not _scene_window.visible:
		print("smoke: FAILED: the GNN window didn't open")
		return false
	var heights: Array = []
	for i in 8:
		heights.append(roundi(_scene_window._frame.size.y))
		await get_tree().process_frame
	print("smoke: scene opening heights %s" % [heights])
	for page in 2:
		await get_tree().create_timer(2.5).timeout # the studio's frames arrive
		print("smoke: scene page %d: request page %s, text '%s'" % [page, _scene_window._state.get("page", -1), String(_scene_window._state.get("text", "")).left(80)])
		if shots != "":
			await host.shot(shots + "/native-scene-page-%d.png" % page)
		_scene_window._frame.gui_input.emit(_smoke_left_click())
	waited = 0.0
	while not box.has("r") and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	print("smoke: scene closed %s, overlay %s, answer %s" % [not _scene_window.visible, overlay.visible, box.get("r", {})])
	return box.has("r") and not _scene_window.visible


## Smoke: diplomacy with the first computer player met, as the Races screen opens it: the window
## opens with the room, the ambassador arrives, the main list sits in the window; EXIT leaves and
## the window closes. Nothing is proposed.
func _smoke_diplomacy(shots: String) -> bool:
	var box := {}
	_send({"verb": "preview_popup", "popup": "diplomacy"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	var steps: Array = []
	var shot_intro := false
	while not box.has("r") and waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		if _scene_window == null or not _scene_window.visible:
			continue
		if not shot_intro and waited > 1.0:
			shot_intro = true
			print("smoke: diplomacy scene '%s', title '%s', text '%s'" % [_scene_window._state.get("scene", ""), _scene_window._title.text, _scene_window._text.text.left(80)])
			if shots != "":
				await host.shot(shots + "/native-diplomacy-intro.png")
			await get_tree().create_timer(5.0).timeout # the ambassador's arrival, then the loop
			waited += 5.0
		var dialog: Control = _views.get("dialog")
		if overlay.visible and dialog != null and dialog.visible and dialog._kind == "choose":
			steps.append("choose %d options at %s" % [dialog.options.size(), dialog.position])
			if shots != "" and steps.size() == 1:
				await get_tree().create_timer(1.0).timeout
				await host.shot(shots + "/native-diplomacy-arriving.png")
				await get_tree().create_timer(6.0).timeout # the art, the ambassador's arrival
				print("smoke: diplomacy scene '%s', title '%s', text '%s', intro frame %d, actor %s" % [_scene_window._state.get("scene", ""),
					_scene_window._title.text, _scene_window._text.text.left(80), _scene_window._intro_frame, _scene_window._actor.texture != null])
				await host.shot(shots + "/native-diplomacy-list.png")
			dialog._answer(dialog.options.size() - 1) # EXIT, the last
			await get_tree().create_timer(0.5).timeout
		elif _scene_window._id >= 0:
			steps.append("page '%s'" % _scene_window._text.text.left(40))
			_scene_window._frame.gui_input.emit(_smoke_left_click())
	print("smoke: diplomacy steps %s; result %s, window %s" % [steps, box.get("r", {}), _scene_window.visible if _scene_window else false])
	return box.has("r") and bool(box.r.get("ok", false)) and _scene_window != null and not _scene_window.visible


## Smoke: the research window from the popup preview (two discoveries, then the picker): both
## pages answer to a click, the monitor closes, the picker comes down over the room with its frame
## on the window's (same top, right and bottom), a pick answers and both close.
func _smoke_research(shots: String) -> bool:
	var box := {}
	_send({"verb": "preview_popup", "popup": "research"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	var pages := 0
	var aligned := false
	while not box.has("r") and waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		if _scene_window == null or not _scene_window.visible:
			continue
		if _picker != null and _picker.visible and not _picker._flashing:
			await get_tree().create_timer(1.0).timeout # slid down
			var scene_rect: Rect2 = _scene_window._frame.get_global_rect()
			var picker_rect: Rect2 = _picker.get_global_rect()
			aligned = absf(scene_rect.end.x - picker_rect.end.x) < 1.5 and absf(scene_rect.end.y - picker_rect.end.y) < 1.5 and \
				absf(scene_rect.position.y - picker_rect.position.y) < 1.5
			print("smoke: research picker %s over window %s, edges shared %s, monitor layers %s" % [picker_rect, scene_rect, aligned,
				_scene_window._layers.map(func(l): return "%s:%s" % [l.key, "hidden" if l.hidden else str(l.frame)])])
			if shots != "":
				await host.shot(shots + "/native-research-picker.png")
			# The first application on offer.
			var first: Button = null
			for b in _picker.find_children("*", "Button", true, false):
				if b.theme_type_variation == &"ListItem" and b.is_visible_in_tree() and not (b as Button).disabled:
					first = b
					break
			if first == null:
				print("smoke: FAILED: no research choice in the picker")
				return false
			first.pressed.emit()
			await get_tree().create_timer(1.5).timeout
			waited += 1.5
		elif _scene_window._id >= 0:
			if pages == 0:
				await get_tree().create_timer(2.5).timeout # slides down, the monitor opens, the art arrives
				print("smoke: research page 0: monitor '%s' shown %s, text '%s'" % [_scene_window._monitor_title.text,
					_scene_window._monitor.visible, _scene_window._text.text.left(60)])
				if shots != "":
					await host.shot(shots + "/native-research-page-0.png")
			pages += 1
			_scene_window._frame.gui_input.emit(_smoke_left_click())
			await get_tree().create_timer(0.5).timeout
	print("smoke: research pages %d, aligned %s, result %s, window %s" % [pages, aligned, box.get("r", {}), _scene_window.visible if _scene_window else false])
	return box.has("r") and bool(box.r.get("ok", false)) and pages == 2 and aligned and _scene_window != null and not _scene_window.visible


## WP3.5a (`-- --smoke --native --colonies`): the Colonies panel opens beside the fitted galaxy,
## sorts, follows a star click, moves a colonist between jobs and back, sends one to another
## colony, opens a colony's build pane from its Building slot and returns to the panel, and puts
## the camera back when it closes.
func _smoke_colonies(shots: String) -> bool:
	var zoom_before: float = galaxy.camera.zoom.x
	var position_before: Vector2 = galaxy.camera.position
	_on_view_action("colonies")
	await get_tree().create_timer(0.6).timeout
	var panel := _view("colonies_panel")
	var rows: int = panel._rows.size()
	var area: Rect2 = panel.get_global_rect()
	var tab: Button = _view("side_tabs").get_node("Colonies")
	print("smoke: Colonies tab armed while open: %s" % tab.button_pressed)
	if not tab.button_pressed:
		return false
	print("smoke: colonies panel %s, %d rows, rect %s, galaxy zoom %.3f (was %.3f); view %s, area %s, min %s" % [panel.visible, rows, area, galaxy.camera.zoom.x, zoom_before,
		get_viewport().get_visible_rect().size, _research_area(), panel.get_combined_minimum_size()])
	if shots != "":
		await host.shot(shots + "/native-colonies.png")
	if not panel.visible or rows == 0:
		return false
	# The whole galaxy left of the panel.
	var far := galaxy.get_global_transform_with_canvas() * Vector2(float(galaxy.data.width), float(galaxy.data.height))
	var near := galaxy.get_global_transform_with_canvas() * Vector2.ZERO
	print("smoke: galaxy spans x %.0f..%.0f, y %.0f..%.0f; panel left %.0f" % [near.x, far.x, near.y, far.y, area.position.x])
	if far.x > area.position.x + 1.0 or near.x < -1.0:
		print("smoke: FAILED: the galaxy doesn't fit left of the panel")
		return false
	# Sort by population, largest first; again: smallest first.
	for b in panel.columns_box.get_children():
		if b is Button and (b as Button).text.begins_with("POP"):
			(b as Button).pressed.emit()
	var pops: Array = panel.colonies.map(func(c: Dictionary) -> int: return int(c.pops))
	var sorted := pops.duplicate()
	sorted.sort()
	sorted.reverse()
	print("smoke: sorted by population %s" % [pops])
	if pops != sorted:
		print("smoke: FAILED: population sort")
		return false
	# A star click selects its colony's row.
	var last: Dictionary = panel.colonies[panel.colonies.size() - 1]
	_on_star_clicked(int(last.star))
	await get_tree().create_timer(0.3).timeout
	print("smoke: star %d clicked: selected %d (wanted %d), system pane %s" % [int(last.star), panel.selected, int(last.colony), _view("system_pane").visible])
	if panel.selected != int(last.colony) and int(_row_star_first(panel, int(last.star))) != panel.selected:
		return false
	# A worker to science and back (the panel's own pick-up and drop).
	var moved := false
	for c: Dictionary in panel.colonies:
		var workers: Array = c.jobs[1].colonists
		if workers.is_empty():
			continue
		var id := int(c.colony)
		var before := workers.size()
		var person: Dictionary = workers[0]
		panel._pick_up(c, [int(person.pop)], 1)
		await get_tree().process_frame
		var target: Control = panel._job_cells[Vector2i(id, 2)]
		panel._input(_smoke_click_at(target.get_global_rect().get_center()))
		await get_tree().create_timer(0.4).timeout
		var after: int = (panel._row_data(id).jobs[1].colonists as Array).size()
		print("smoke: %s workers %d -> %d" % [c.name, before, after])
		if after != before - 1:
			return false
		var back: Dictionary = panel._row_data(id).jobs[2].colonists[0]
		_send({"verb": "move_colonist", "colony": id, "pops": [int(back.pop)], "job": 1})
		await get_tree().create_timer(0.3).timeout
		panel.refresh()
		await get_tree().create_timer(0.3).timeout
		moved = true
		break
	if not moved:
		print("smoke: no workers to move (skipped)")
	if shots != "":
		await host.shot(shots + "/native-colonies-sorted.png")
	# One colonist to another colony as settlers (the game's questions are answered).
	if panel.colonies.size() >= 2:
		var from: Dictionary = panel.colonies[0]
		var to: Dictionary = panel.colonies[1]
		var person := {}
		for job in 3:
			if not (from.jobs[job].colonists as Array).is_empty():
				person = from.jobs[job].colonists[0]
		if not person.is_empty():
			var box := {}
			_send({"verb": "send_colonists", "from": int(from.colony), "pop": int(person.pop), "to": int(to.colony), "job": -1}, func(r: Dictionary): box["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.25).timeout
				_smoke_answer()
				if box.has("r"):
					break
			print("smoke: send_colonists %s -> %s: %s" % [from.name, to.name, box.get("r", "no answer")])
			if not box.has("r") or not box.r.get("ok", false):
				return false
	# The Building slot: the system, its colony pane and build pane; back to the panel.
	var sort_key: String = panel.sort_key
	var first_row: Dictionary = panel.colonies[0]
	panel.build_chosen.emit(int(first_row.colony), int(first_row.star), int(first_row.planet), false)
	await get_tree().create_timer(0.7).timeout
	var back_button: Button = _view("system_pane").get_node("Box/Back")
	print("smoke: build detour: panel %s, system %s, colony pane %d, build pane %s, back '%s'" % [panel.visible,
		_view("system_pane").visible, _view("colony_pane").colony_id, _view("build_pane").visible, back_button.text])
	if shots != "":
		await host.shot(shots + "/native-colonies-build.png")
	if panel.visible or not _view("build_pane").visible or back_button.text != "RETURN TO COLONIES":
		return false
	back_button.pressed.emit()
	await get_tree().create_timer(0.7).timeout
	print("smoke: returned: panel %s, sort %s (was %s), back '%s', zoom %.3f" % [panel.visible, panel.sort_key, sort_key, back_button.text, galaxy.camera.zoom.x])
	if shots != "":
		await host.shot(shots + "/native-colonies-returned.png")
	if not panel.visible or panel.sort_key != sort_key or _view("build_pane").visible:
		return false
	# The name: the system and its colony pane, no build pane; back again.
	var named: Dictionary = panel.colonies[0]
	panel.colony_opened.emit(int(named.colony), int(named.star), int(named.planet))
	await get_tree().create_timer(0.7).timeout
	print("smoke: name detour: panel %s, system %s, colony pane %d (wanted %d), build pane %s" % [panel.visible,
		_view("system_pane").visible, _view("colony_pane").colony_id, int(named.colony), _view("build_pane").visible])
	if panel.visible or _view("colony_pane").colony_id != int(named.colony) or _view("build_pane").visible:
		return false
	back_button.pressed.emit()
	await get_tree().create_timer(0.7).timeout
	if not panel.visible:
		return false
	if shots != "":
		await host.shot(shots + "/native-colonies-strip.png")
	print("smoke: colonies width %.0f" % panel.size.x)
	if not await _smoke_side_resize(panel, shots, "colonies"):
		return false
	panel.close()
	await get_tree().create_timer(0.6).timeout
	print("smoke: closed: zoom %.3f (was %.3f), position %s (was %s)" % [galaxy.camera.zoom.x, zoom_before, galaxy.camera.position, position_before])
	return absf(galaxy.camera.zoom.x - zoom_before) < zoom_before * 0.01 and galaxy.camera.position.distance_to(position_before) < 2.0


## WP3.5b (`-- --smoke --native --planets`): the Planets panel opens beside the fitted galaxy with
## its tab armed, the toggles and a sort change the rows, a star click selects, a colony ship is
## sent (the game's questions answered) and recalled, and the camera comes back on close.
func _smoke_planets(shots: String) -> bool:
	var zoom_before: float = galaxy.camera.zoom.x
	var position_before: Vector2 = galaxy.camera.position
	_on_view_action("planets")
	await get_tree().create_timer(0.6).timeout
	var panel := _view("planets_panel")
	var tab: Button = _view("side_tabs").get_node("Planets")
	var area: Rect2 = panel.get_global_rect()
	var far := galaxy.get_global_transform_with_canvas() * Vector2(float(galaxy.data.width), 0.0)
	print("smoke: planets panel %s, tab armed %s, %d of %d rows, rect %s, galaxy right edge %.0f, zoom %.3f" % [panel.visible,
		tab.button_pressed, panel.planets.size(), panel._all.size(), area, far.x, galaxy.camera.zoom.x])
	if shots != "":
		await host.shot(shots + "/native-planets.png")
	if not panel.visible or not tab.button_pressed or panel._all.is_empty() or far.x > area.position.x + 1.0:
		return false
	# The in-range toggle off shows more (or as many); every toggle on shows fewer (or as many).
	var in_range: int = panel.planets.size()
	var toggles: Array = panel.get_node("Box/Filters").get_children()
	(toggles[0] as Button).button_pressed = false
	var all: int = panel.planets.size()
	for b: Button in toggles:
		b.button_pressed = true
	var strict: int = panel.planets.size()
	print("smoke: planets in range %d, all %d, every toggle %d" % [in_range, all, strict])
	if all < in_range or strict > in_range:
		return false
	for i in toggles.size():
		(toggles[i] as Button).button_pressed = i == 0
	# Sort by size, largest first.
	for b in panel.columns_box.get_children():
		if b is Button and (b as Button).text.begins_with("SIZE"):
			(b as Button).pressed.emit()
	var pops: Array = panel.planets.map(func(p: Dictionary) -> int: return int(p.max_pop))
	var sorted := pops.duplicate()
	sorted.sort()
	sorted.reverse()
	print("smoke: planets by size %s" % [pops.slice(0, 10)])
	if pops != sorted:
		return false
	if panel.planets.is_empty():
		print("smoke: no planet in range (send skipped)")
	else:
		# An empty planet: on one with a colony the screen shows no marker (nor can it recall).
		var target: Dictionary = panel.planets[0]
		for p: Dictionary in panel.planets:
			if not p.has("owner"):
				target = p
				break
		_on_star_clicked(int(target.star))
		await get_tree().create_timer(0.3).timeout
		print("smoke: star %d clicked: selected %d (wanted the first at that star), system pane %s" % [int(target.star), panel.selected, _view("system_pane").visible])
		if panel.selected < 0 or _view("system_pane").visible:
			return false
		if not panel.colony_button.disabled:
			panel.colony_button.button_pressed = true
			await get_tree().process_frame
			if shots != "":
				await host.shot(shots + "/native-planets-armed.png")
			# The planet is selected already: the button sends there itself (owner, 2026-11-20).
			for i in 40:
				await get_tree().create_timer(0.25).timeout
				_smoke_answer()
			var after: Dictionary = {}
			for p: Dictionary in panel._all:
				if int(p.planet) == int(target.planet):
					after = p
			print("smoke: sent a colony ship to %s: on its way %s, still armed %s" % [target.name, after.get("colony_eta", -1), panel.send_kind])
			if panel.send_kind != "colony" and not panel.colony_button.disabled:
				return false
			if shots != "":
				await host.shot(shots + "/native-planets-sent.png")
			if int(after.get("colony_eta", -1)) < 0:
				return false
			var box := {}
			_send({"verb": "planets_recall", "planet": int(target.planet), "kind": "colony"}, func(r: Dictionary): box["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.25).timeout
				_smoke_answer()
				if box.has("r"):
					break
			print("smoke: recalled: %s" % [box.get("r", "no answer")])
			if not box.has("r") or not box.r.get("recalled", false):
				return false
			panel.refresh()
			await get_tree().create_timer(0.3).timeout
		else:
			print("smoke: no free colony ship (send skipped)")
	print("smoke: planets width %.0f" % panel.size.x)
	# Back and forth with Colonies (owner, 2026-11-15: each switch made them wider): one width.
	var widths: Array[float] = []
	for i in 3:
		_on_view_action("colonies")
		await get_tree().create_timer(0.7).timeout
		widths.append(_view("colonies_panel").size.x)
		_on_view_action("planets")
		await get_tree().create_timer(0.7).timeout
		widths.append(panel.size.x)
	print("smoke: planets and colonies by turns: widths %s" % str(widths))
	for w in widths:
		if absf(w - widths[0]) > 0.5:
			return false
	if not await _smoke_side_resize(panel, shots, "planets"):
		return false
	panel.close()
	await get_tree().create_timer(0.6).timeout
	print("smoke: planets closed: tab armed %s, zoom %.3f (was %.3f)" % [tab.button_pressed, galaxy.camera.zoom.x, zoom_before])
	return not tab.button_pressed and absf(galaxy.camera.zoom.x - zoom_before) < zoom_before * 0.01 and galaxy.camera.position.distance_to(position_before) < 2.0


## Smoke (owner, 2026-11-14): a side panel follows a window drag, its right edge against the side
## tabs; the Colonies rows fill the table.
func _smoke_side_resize(panel: Control, shots: String, tag: String) -> bool:
	get_window().mode = Window.MODE_WINDOWED
	var ok := true
	for size in [Vector2i(1700, 1000), Vector2i(1300, 900), Vector2i(1500, 950)]:
		for step in 12: # a drag, a size a frame
			get_window().size = Vector2i(Vector2(get_window().size).lerp(Vector2(size), 0.4)) if step < 11 else size
			await get_tree().process_frame
		await get_tree().create_timer(0.4).timeout
		var tabs_left := INF
		for b in _view("side_tabs").find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				tabs_left = minf(tabs_left, (b as Control).get_global_rect().position.x)
		var right: float = panel.get_global_rect().end.x
		var gap := tabs_left - right
		var fill := ""
		if "scroll" in panel and panel.has_method("_fill_jobs") and not panel._rows.is_empty():
			var row: Control = panel._rows.values()[0]
			fill = " row %.0f of table %.0f" % [row.size.x, panel.scroll.size.x]
			ok = ok and row.size.x >= panel.scroll.size.x - 30.0
		print("smoke: %s at %s: panel right %.0f, width %.0f, tabs %.0f (gap %.0f)%s" % [tag, size, right, panel.size.x, tabs_left, gap, fill])
		ok = ok and gap >= 0.0 and gap <= 14.0
	if shots != "":
		await host.shot(shots + "/native-%s-resized.png" % tag)
	return ok


## WP3.5c (`--fleets`): the Fleets panel opens on one of the player's fleets with its tab armed,
## the cycler moves to another fleet, the filters change the grid, a ship's detail shows, ALL picks,
## the picked ships go to a star (then back), RELOCATE takes a from and a to star and the same
## star again cancels, the panel follows a window drag, and the camera comes back on close.
func _smoke_fleets_panel(shots: String) -> bool:
	var zoom_before: float = galaxy.camera.zoom.x
	var position_before: Vector2 = galaxy.camera.position
	_on_view_action("fleets")
	await get_tree().create_timer(0.7).timeout
	var panel := _view("fleets_panel")
	var tab: Button = _view("side_tabs").get_node("Fleets")
	print("smoke: fleets panel %s, tab armed %s, fleet '%s' with %d ships, marked %d" % [panel.visible, tab.button_pressed,
		panel.fleet.get("title", ""), (panel.fleet.get("ships", []) as Array).size(), galaxy.selected_fleet])
	if shots != "":
		await host.shot(shots + "/native-fleets.png")
	if not panel.visible or not tab.button_pressed or panel.fleet.is_empty():
		return false
	var first_fleet := int(panel.fleet.ship)
	panel._cycle(0)
	await get_tree().create_timer(0.4).timeout
	print("smoke: fleets next: '%s' (was ship %d, now %d)" % [panel.fleet.get("title", ""), first_fleet, int(panel.fleet.ship)])
	# A fleet with warships, for the detail and the filters.
	var tries := 0
	while tries < 12 and not (panel.fleet.ships as Array).any(func(s: Dictionary) -> bool: return s.get("combat", false)):
		panel._cycle(0)
		await get_tree().create_timer(0.3).timeout
		tries += 1
	var all_count: int = panel._tiles.size()
	panel._set_filter("support")
	var support_count: int = panel._tiles.size()
	panel._set_filter("combat")
	var combat_count: int = panel._tiles.size()
	panel._set_filter("")
	print("smoke: fleets filters: all %d, support %d, combat %d" % [all_count, support_count, combat_count])
	if support_count + combat_count > all_count:
		return false
	await get_tree().create_timer(0.4).timeout
	var detail_lines: int = panel._detail.find_children("*", "Label", true, false).size()
	print("smoke: fleets detail for ship %d: %d lines" % [panel._detail_ship, detail_lines])
	if shots != "":
		await host.shot(shots + "/native-fleets-detail.png")
	if detail_lines == 0:
		return false
	# ALL, a move to a nearby star and back (the fleet's own star).
	panel._on_all()
	var picked: Array = panel.selected_ships()
	var home := int(panel.fleet.get("star", -1))
	print("smoke: fleets ALL picked %d of %d; fleet at star %d" % [picked.size(), panel._tiles.size(), home])
	if picked.is_empty():
		return false
	if home >= 0:
		var target := -1
		var best := INF
		var here: Vector2 = galaxy.star_position(home)
		for star: Dictionary in galaxy.stars:
			var d: float = here.distance_to(Vector2(float(star.x), float(star.y)))
			if int(star.id) != home and not star.black_hole and d < best:
				best = d
				target = int(star.id)
		_on_star_clicked(target)
		for i in 12:
			await get_tree().create_timer(0.25).timeout
			_smoke_answer()
		var moved := false
		for s: Dictionary in panel.fleet.get("ships", []):
			moved = moved or true
		print("smoke: fleets sent to star %d: fleet now '%s' (%s)" % [target, panel.fleet.get("title", ""), panel.fleet.get("status", "")])
		if shots != "":
			await host.shot(shots + "/native-fleets-moved.png")
		panel._on_all() # back home
		if panel.selected_ships().is_empty():
			panel._on_all()
		_on_star_clicked(home)
		for i in 8:
			await get_tree().create_timer(0.25).timeout
			_smoke_answer()
	# RELOCATE: from a star with a colony of the player's, to another, then cancelled.
	var colony_star := -1
	for star: Dictionary in galaxy.stars:
		if int(star.get("owner", -1)) == int(galaxy.data.get("player", -2)):
			colony_star = int(star.id)
			break
	if colony_star >= 0:
		panel._relocate.button_pressed = true
		# Real clicks on the map (owner, 2026-11-15: a map click closed the panel; calling the
		# handler directly had hidden it).
		var at: Vector2 = galaxy.get_global_transform_with_canvas() * galaxy.star_position(colony_star)
		_smoke_mouse(true, at)
		_smoke_mouse(false, at)
		await get_tree().create_timer(0.4).timeout
		var hint_from: String = panel._hint.text
		print("smoke: fleets relocate: a real click at %s: panel still open %s" % [at, panel.visible])
		if not panel.visible:
			return false
		_smoke_mouse(true, at) # the same star: cancels (the game's message)
		_smoke_mouse(false, at)
		for i in 8:
			await get_tree().create_timer(0.25).timeout
			_smoke_answer()
		print("smoke: fleets relocate from %d: '%s', then the same star: '%s'" % [colony_star, hint_from, panel._hint.text])
		if not hint_from.begins_with("Now click"):
			return false
		panel._relocate.button_pressed = false
	# A real click on another of the player's fleets on the map: the panel shows that fleet.
	for f: Dictionary in galaxy.fleets:
		if int(f.owner) == int(galaxy.data.get("player", -1)) and int(f.ship) != int(panel.fleet.ship) and not f.moving:
			var at: Vector2 = galaxy.get_global_transform_with_canvas() * Vector2(float(f.x), float(f.y))
			galaxy._unhandled_input(_smoke_click_at(at))
			var up := _smoke_click_at(at)
			up.pressed = false
			galaxy._unhandled_input(up)
			await get_tree().create_timer(0.5).timeout
			print("smoke: fleets: a click on fleet %d's icon: panel open %s, shows %d" % [int(f.ship), panel.visible, int(panel.fleet.get("ship", -1))])
			if shots != "":
				await host.shot(shots + "/native-fleets-clicked.png")
			if not panel.visible:
				return false
			break
	if not await _smoke_side_resize(panel, shots, "fleets"):
		return false
	# LEADERS opens Ship Officers on this fleet (owner), then back to Fleets.
	if not panel.fleet.is_empty():
		var fleet_id := int(panel.fleet.ship)
		_on_view_command({"verb": "open", "screen": 29}, panel)
		await get_tree().create_timer(0.8).timeout
		var leaders := _view("leaders_panel")
		var wanted: int = leaders._fleet_place(fleet_id)
		print("smoke: fleets: LEADERS on fleet %d: leaders %s, tab %s, place %d (fleet's %d)" % [fleet_id, leaders.visible, leaders.tab,
			leaders.place, wanted])
		if not leaders.visible or leaders.tab != "ship" or (wanted >= 0 and leaders.place != wanted):
			return false
		leaders._set_tab("colony")
		_on_view_action("fleets")
		await get_tree().create_timer(0.8).timeout
	panel.close()
	await get_tree().create_timer(0.6).timeout
	print("smoke: fleets closed: tab armed %s, zoom %.3f (was %.3f), marked %d" % [tab.button_pressed, galaxy.camera.zoom.x, zoom_before, galaxy.selected_fleet])
	return not tab.button_pressed and absf(galaxy.camera.zoom.x - zoom_before) < zoom_before * 0.01 and galaxy.selected_fleet == -1


## WP3.7 (`--leaders`): the Leaders panel opens with its tab armed on the colony leaders and a
## system shown, a star click moves the cycler, a governor goes to the pool and is assigned back
## (the game's questions answered), the Ship Officers tab lists fleets, and the panel follows a
## window drag.
func _smoke_leaders_panel(shots: String) -> bool:
	_on_view_action("leaders")
	await get_tree().create_timer(0.8).timeout
	var panel := _view("leaders_panel")
	var tab: Button = _view("side_tabs").get_node("Leaders")
	print("smoke: leaders panel %s, tab armed %s, %d colony leaders, %d systems, shown '%s', system drawn %s" % [panel.visible,
		tab.button_pressed, panel.leaders.size(), panel.places.size(), panel._place_label.text, not panel._system_view.system.is_empty()])
	if shots != "":
		await host.shot(shots + "/native-leaders.png")
	if not panel.visible or not tab.button_pressed:
		return false
	if panel.places.size() >= 2:
		var star := int(panel.places[1].star)
		_on_star_clicked(star)
		await get_tree().create_timer(0.4).timeout
		print("smoke: leaders: star %d clicked: shown '%s' (wanted '%s')" % [star, panel._place_label.text, panel.places[1].name])
		if panel.place != 1:
			return false
	# A governor to the pool and back.
	for leader: Dictionary in panel.leaders:
		if int(leader.status) == 1 and leader.has("star"):
			var id := int(leader.leader)
			var home := int(leader.star)
			panel.picked = id
			panel._act("pool")
			for i in 8:
				await get_tree().create_timer(0.2).timeout
				_smoke_answer()
			var pooled: Dictionary = panel._leader(id)
			panel.picked = id
			panel.select_star(home)
			panel._act("assign")
			for i in 8:
				await get_tree().create_timer(0.2).timeout
				_smoke_answer()
			var back: Dictionary = panel._leader(id)
			print("smoke: leaders: %s pooled (status %d) and assigned back (status %d, '%s')" % [leader.card.name, int(pooled.get("status", -1)),
				int(back.get("status", -1)), back.get("state", "")])
			if int(pooled.get("status", -1)) != 0 or int(back.get("status", -1)) != 1:
				return false
			break
	panel._set_tab("ship")
	await get_tree().create_timer(0.5).timeout
	# The biggest fleet: its ships scroll inside the fixed top, the cards keep two cards' room.
	var biggest := 0
	for i in panel.places.size():
		if (panel.places[i].get("ships", []) as Array).size() > (panel.places[biggest].get("ships", []) as Array).size():
			biggest = i
	if not panel.places.is_empty():
		panel.place = biggest
		panel._show_place()
		await get_tree().create_timer(0.3).timeout
	# A fleet icon on the map picks that fleet in the cycler (owner).
	if panel.places.size() >= 2:
		var other := 0 if panel.place != 0 else 1
		var fleet_id := int(panel.places[other].get("fleet", -1))
		_open_fleet(fleet_id)
		await get_tree().create_timer(0.3).timeout
		print("smoke: leaders: fleet %d clicked on the map: place %d (wanted %d), fleet box %s" % [fleet_id, panel.place, other,
			_view("fleet_panel").visible])
		if panel.place != other or _view("fleet_panel").visible:
			return false
		panel.place = biggest
		panel._show_place()
		await get_tree().create_timer(0.3).timeout
	# FLEETS opens the Fleets panel on the cycler's fleet, then back here.
	if not panel.places.is_empty():
		var shown_fleet := int(panel.places[panel.place].get("fleet", -1))
		panel._fleets_button.pressed.emit()
		await get_tree().create_timer(0.8).timeout
		var fleets_view := _view("fleets_panel")
		print("smoke: leaders: FLEETS on fleet %d: fleets panel %s on %d" % [shown_fleet, fleets_view.visible, int(fleets_view.fleet.get("ship", -1))])
		if not fleets_view.visible or int(fleets_view.fleet.get("ship", -1)) != shown_fleet:
			return false
		_on_view_command({"verb": "open", "screen": 29}, fleets_view)
		await get_tree().create_timer(0.8).timeout
		if not panel.visible or panel.tab != "ship":
			return false
	var card_room: float = panel._cards_scroll.size.y
	var two_cards: float = panel._cards_scroll.custom_minimum_size.y
	print("smoke: leaders: ship officers %d, fleets %d, ships shown %d, top %.0f px, cards room %.0f px (two cards %.0f)" % [panel.leaders.size(),
		panel.places.size(), panel._ships.get_child_count(), panel._view_holder.size.y, card_room, two_cards])
	if shots != "":
		await host.shot(shots + "/native-leaders-ships.png")
	if panel._view_holder.size.y > panel.VIEW_HEIGHT + 1.0 or card_room + 1.0 < two_cards:
		return false
	if not await _smoke_side_resize(panel, shots, "leaders"):
		return false
	panel._set_tab("colony")
	panel.close()
	await get_tree().create_timer(0.6).timeout
	return not tab.button_pressed


## Clicks the last spy icon of the `from` strip, then the `to` strip (the panel's pick-up and
## put-down, as the pointer does it).
func _smoke_carry_spy(panel: Control, from: int, to: int) -> void:
	for step in 2:
		var owner_player := from if step == 0 else to
		for strip: Control in panel._strips:
			if strip.owner_player != owner_player:
				continue
			var at := Vector2(float(strip.count - 1) * strip._spacing() + 4.0, strip.size.y * 0.5) if step == 0 else strip.size * 0.5
			var click := InputEventMouseButton.new()
			click.button_index = MOUSE_BUTTON_LEFT
			click.pressed = true
			click.position = strip.get_global_transform_with_canvas() * at
			click.global_position = click.position
			Input.parse_input_event(click)
			var release := click.duplicate()
			release.pressed = false
			Input.parse_input_event(release)
			await get_tree().create_timer(0.25).timeout
			print("smoke: races: click on %d's spies: carrying %s" % [owner_player, panel._carry.get("n", 0)])
			break
	await get_tree().create_timer(0.4).timeout


## WP3.7 (`--races`): the Race Relations panel opens with its tab armed and the galaxy not moved,
## a met empire picked, a spy goes there and comes home, the mission changes and back, IGNORE
## toggles twice, and the panel follows a window drag.
func _smoke_races_panel(shots: String) -> bool:
	var zoom_before: float = galaxy.camera.zoom.x
	_on_view_action("races")
	await get_tree().create_timer(0.8).timeout
	var panel := _view("races_panel")
	var tab: Button = _view("side_tabs").get_node("Races")
	var eliminated := 0
	for r: Dictionary in panel.races:
		eliminated += 1 if r.eliminated else 0
	print("smoke: races: %d eliminated shown with their portraits" % eliminated)
	print("smoke: races panel %s, tab armed %s, %d empires, picked %d, galaxy zoom %.3f (was %.3f); %s; %s" % [panel.visible, tab.button_pressed,
		panel.races.size(), panel.picked, galaxy.camera.zoom.x, zoom_before, panel._bonuses.text, panel._agents.text])
	if shots != "":
		await host.shot(shots + "/native-races.png")
	if not panel.visible or not tab.button_pressed or absf(galaxy.camera.zoom.x - zoom_before) > zoom_before * 0.01:
		return false
	if panel.picked >= 0:
		var r: Dictionary = panel._race(panel.picked)
		var spies := int(r.spies)
		var mission := int(r.mission)
		var agents := int(panel.result.agents)
		if agents > 0:
			# A spy picked up at home with a click and put down on the empire with another, and back.
			await _smoke_carry_spy(panel, panel.HOME, panel.picked)
			var more := int(panel._race(panel.picked).spies)
			await _smoke_carry_spy(panel, panel.picked, panel.HOME)
			var back := int(panel._race(panel.picked).spies)
			print("smoke: races: spies carried %d -> %d and back to %d" % [spies, more, back])
			if more != spies + 1 or back != spies:
				return false
		_send({"verb": "set_spies", "player": panel.picked, "mission": (mission + 1) % 3})
		await get_tree().create_timer(0.2).timeout
		_send({"verb": "set_spies", "player": panel.picked, "mission": mission})
		panel._act("IGNORE")
		await get_tree().create_timer(0.3).timeout
		var ignoring: bool = panel._race(panel.picked).get("ignoring", false)
		panel._act("IGNORE")
		await get_tree().create_timer(0.3).timeout
		print("smoke: races: ignore toggled to %s and back to %s" % [ignoring, panel._race(panel.picked).get("ignoring", false)])
		if ignoring == panel._race(panel.picked).get("ignoring", false):
			return false
	if not await _smoke_side_resize(panel, shots, "races"):
		return false
	panel.close()
	await get_tree().create_timer(0.6).timeout
	return not tab.button_pressed


## WP3.6 (`--design`): EDIT on a design opens the Ship Design panel; a weapon is added (the first
## beam, then one more), its arc changed, a special added, the size changed; CANCEL leaves the slot
## as it was; again, a weapon added and BUILD writes the slot and brings the list back.
func _smoke_design_panel(shots: String) -> bool:
	_on_view_action("designs")
	await get_tree().create_timer(1.0).timeout
	var designs := _view("design_list")
	var before: Dictionary = await _query_args({"what": "designs"})
	if (before.get("designs", []) as Array).is_empty():
		print("smoke: design: no designs to edit")
		return false
	# AUTO DESIGN (owner, 2026-11-20): asks first; confirmed, every slot is redesigned and the
	# list shows the new designs.
	designs.auto_button.pressed.emit()
	await get_tree().process_frame
	var asked: bool = designs._auto_confirm.visible
	if shots != "":
		await host.shot(shots + "/native-design-auto-confirm.png")
	designs._auto_confirm.hide()
	designs._auto_confirm.confirmed.emit()
	await get_tree().create_timer(1.0).timeout
	var auto: Dictionary = await _query_args({"what": "designs"})
	var describe := func(list: Dictionary) -> Array:
		return (list.get("designs", []) as Array).map(func(d: Dictionary) -> String: return "%s %s %d" % [d.name, d.hull_name, int(d.cost)])
	print("smoke: design: auto design button %s, asked %s; before %s; after %s; list rows %d" % [designs.auto_button.visible, asked,
		describe.call(before), describe.call(auto), designs.rows.get_child_count()])
	if not designs.auto_button.visible or not asked or not auto.get("ok", false) or not designs.visible:
		return false
	before = auto
	var slot := int(before.designs[0].slot)
	var cost_before := int(before.designs[0].cost)
	_on_view_command({"verb": "open", "screen": Screens.DESIGN, "slot": slot}, designs)
	await get_tree().create_timer(1.2).timeout
	var panel := _view("design_panel")
	print("smoke: design: panel %s, '%s' %s, space %d/%d, cost %d, %d weapons, %d specials, list %s" % [panel.visible,
		panel.design.get("name", ""), panel.design.get("size", -1), int(panel.design.get("space_used", 0)), int(panel.design.get("hull_space", 0)),
		int(panel.design.get("cost", 0)), (panel.design.get("weapons", []) as Array).size(), (panel.design.get("specials", []) as Array).size(),
		designs.visible])
	if shots != "":
		await host.shot(shots + "/native-design.png")
	if not panel.visible or panel.design.is_empty() or designs.visible:
		return false
	# + ADD WEAPON: the parts list comes out to the left of the panel; the first beam picked with
	# the back arc, added; one more.
	var parts: Control = panel._parts
	if parts != null and parts.visible:
		return false # folded until asked for
	panel._open_parts("weapons")
	await get_tree().create_timer(0.5).timeout
	parts = panel._parts
	var beam := {}
	for w: Dictionary in panel._weapons:
		if String(w.category) == "beam":
			beam = w
			break
	if beam.is_empty():
		print("smoke: design: no beam weapon researched")
		return false
	var weapons_before := (panel.design.weapons as Array).size()
	parts._pick_weapon(beam)
	parts.arc = 3
	parts._ask_preview()
	await get_tree().create_timer(0.5).timeout
	parts.refresh()
	print("smoke: design: parts list %s at x %.0f (panel at %.0f), %d lines, card '%s'" % [parts.visible, parts.position.x, panel.position.x,
		parts._rows.get_child_count(), parts.preview.get("damage", "")])
	if shots != "":
		await host.shot(shots + "/native-design-parts.png")
	if not parts.visible or parts.get_global_rect().end.x > panel.get_global_rect().position.x:
		return false
	# The card in place (owner: every click made it jump for a frame): a dial click turns the arc,
	# a mod toggles, and the card stays the same node at the same height.
	var card: Control = parts._card
	var card_children := card.get_child_count()
	var first_child: Node = card.get_child(0) if card_children > 0 else null
	if parts._dial != null:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = true
		parts._dial.gui_input.emit(click)
		await get_tree().create_timer(0.3).timeout
	print("smoke: design: dial clicked: arc %d (was 3), card rebuilt %s" % [parts.arc, card.get_child(0) != first_child])
	if parts._dial != null and (parts.arc != 4 or card.get_child(0) != first_child):
		return false
	# All five arcs: the card keeps its size (owner: FORWARD EXTENDED, the widest, moved it).
	if parts._dial != null:
		var sizes: Array[Vector2] = []
		for i in 5:
			var turn := InputEventMouseButton.new()
			turn.button_index = MOUSE_BUTTON_LEFT
			turn.pressed = true
			parts._dial.gui_input.emit(turn)
			await get_tree().process_frame
			await get_tree().process_frame
			sizes.append(parts._card_frame.size)
		print("smoke: design: card size through the arcs %s" % str(sizes))
		for v in sizes:
			if v != sizes[0]:
				return false
	parts.arc = 3
	if parts._dial != null:
		parts._dial.arc = 3
	# Sorting by damage, the biggest first.
	parts._sort_heading("DMG", "damage", 50).pressed.emit()
	await get_tree().create_timer(0.2).timeout
	print("smoke: design: sorted by damage: first '%s'" % parts._rows.get_child(0).tooltip_text.get_slice("\n", 0))
	panel.add_weapon(-1, int(beam.weapon), 3, 1, [])
	await get_tree().create_timer(0.5).timeout
	var added: Dictionary = (panel.design.weapons as Array).back() if not (panel.design.weapons as Array).is_empty() else {}
	print("smoke: design: %s added (back arc %d), weapons %d -> %d" % [beam.name, int(added.get("arc", -1)), weapons_before,
		(panel.design.weapons as Array).size()])
	if (panel.design.weapons as Array).size() != weapons_before + 1 or int(added.get("arc", -1)) != 3:
		return false
	panel.weapon_count(int(added.slot), 1)
	await get_tree().create_timer(0.5).timeout
	# + ADD SPECIAL: a special that fits.
	panel._open_parts("systems")
	await get_tree().create_timer(0.4).timeout
	var special := {}
	for sp: Dictionary in panel._specials:
		if sp.get("fits", false) and not sp.get("fitted", false) and not sp.has("excluded_by"):
			special = sp
			break
	var specials_before := (panel.design.specials as Array).size()
	if not special.is_empty():
		parts._pick_special(int(special.special))
		await get_tree().create_timer(0.3).timeout
		if shots != "":
			await host.shot(shots + "/native-design-special.png")
		panel.add_special(int(special.special))
		await get_tree().create_timer(0.5).timeout
	print("smoke: design: special '%s' (group '%s'), specials %d -> %d" % [special.get("name", "-"), special.get("group", ""), specials_before,
		(panel.design.specials as Array).size()])
	if not special.is_empty() and (panel.design.specials as Array).size() != specials_before + 1:
		return false
	# Battle Pods (negative space): the bar shows them as more hull, its parts add up to what is used.
	var pods := {}
	for sp: Dictionary in panel._specials:
		if int(sp.space) < 0 and not sp.get("fitted", false):
			pods = sp
	if not pods.is_empty():
		panel.add_special(int(pods.special))
		await get_tree().create_timer(0.5).timeout
		var bar: Control = panel._space_bar
		var other: int = bar.used - bar.weapons - bar.specials
		print("smoke: design: %s fitted: '%s', bar hull %d used %d (other %d, weapons %d, specials %d), game %d/%d" % [pods.name,
			panel._space_label.text, bar.hull, bar.used, other, bar.weapons, bar.specials, int(panel.design.space_used), int(panel.design.hull_space)])
		if other < 0 or bar.hull - bar.used != int(panel.design.hull_space) - int(panel.design.space_used):
			return false
		if shots != "":
			await host.shot(shots + "/native-design-pods.png")
	parts.fold()
	await get_tree().create_timer(0.3).timeout
	if parts.visible:
		return false
	# Another hull, then back.
	var size_before := int(panel.design.size)
	panel._edit({"size": (size_before + 1) % 4})
	await get_tree().create_timer(0.4).timeout
	print("smoke: design: size %d -> %d, '%s', space %d/%d" % [size_before, int(panel.design.size), panel.design.name, int(panel.design.space_used),
		int(panel.design.hull_space)])
	if shots != "":
		await host.shot(shots + "/native-design-edited.png")
	# CANCEL: the slot as it was.
	panel.close()
	await get_tree().create_timer(0.6).timeout
	var after_cancel: Dictionary = await _query_args({"what": "designs"})
	print("smoke: design: cancelled: panel %s, cost %d (was %d)" % [panel.visible, int(after_cancel.designs[0].cost), cost_before])
	if panel.visible or int(after_cancel.designs[0].cost) != cost_before:
		return false
	# Again: a weapon and BUILD.
	_on_view_command({"verb": "open", "screen": Screens.DESIGN, "slot": slot}, designs)
	await get_tree().create_timer(1.0).timeout
	panel._send({"verb": "design_clear"})
	await get_tree().create_timer(0.5).timeout
	panel._send({"verb": "design_weapon", "slot": -1, "weapon": int(beam.weapon), "arc": 0, "rack": 1, "mods": []})
	await get_tree().create_timer(0.5).timeout
	var cost_built := int(panel.design.cost)
	panel._build.pressed.emit()
	await get_tree().create_timer(1.2).timeout
	var after_build: Dictionary = await _query_args({"what": "designs"})
	var built_cost := -1
	for d: Dictionary in after_build.designs:
		if int(d.slot) == slot:
			built_cost = int(d.cost)
	print("smoke: design: built: panel %s, list %s, slot cost %d (editor showed %d)" % [panel.visible, _view("design_list").visible, built_cost, cost_built])
	if shots != "":
		await host.shot(shots + "/native-design-built.png")
	if panel.visible or not _view("design_list").visible or built_cost != cost_built:
		return false
	_view("design_list").close()
	await get_tree().create_timer(0.5).timeout
	return true


## WP3.7 (`--info`): the Info panel opens over the galaxy (not moved) with its tab armed, every
## tab fills (the history's players, a tech's picture, the race cards, the summary, a reference
## category and a topic), the tab choice is written back, and the panel follows a window drag.
func _smoke_info_panel(shots: String) -> bool:
	var zoom_before: float = galaxy.camera.zoom.x
	_on_view_action("info")
	await get_tree().create_timer(0.8).timeout
	var panel := _view("info_panel")
	var tab: Button = _view("side_tabs").get_node("Info")
	print("smoke: info panel %s, tab armed %s, stardate '%s', tab %d, net '%s', galaxy zoom %.3f (was %.3f)" % [panel.visible, tab.button_pressed,
		panel._stardate.text, panel.tab, panel.info.get("net_income_text", ""), galaxy.camera.zoom.x, zoom_before])
	if not panel.visible or not tab.button_pressed or panel.info.is_empty() or absf(galaxy.camera.zoom.x - zoom_before) > zoom_before * 0.01:
		return false
	await get_tree().create_timer(2.0).timeout # the portrait's HD art
	var names := ["history", "tech", "races", "summary", "reference"]
	for i in 5:
		panel._set_tab(i)
		await get_tree().create_timer(0.6).timeout
		var shown: bool = panel._content.get_child_count() > 0 and (panel._content.get_child(0) as Control).get_child_count() > 0
		var picture: String = "%s/%d" % [panel._picture.lbx, panel._picture.entry]
		print("smoke: info: %s tab '%s' filled %s, picture %s" % [names[i], panel._tabs[i].text, shown, picture])
		if shots != "":
			await host.shot(shots + "/native-info-%s.png" % names[i])
		if not shown:
			return false
		if i == 1 and panel._picture.lbx != "APP_PICS.LBX" and panel._detail_title.text != "":
			return false
		if i == 4:
			# A category, then its first topic (a picture when the record has one).
			var categories: Dictionary = await _query_args({"what": "reference"})
			if not (categories.get("categories", []) as Array).is_empty():
				var category := int(categories.categories[0].category)
				_send({"verb": "query", "what": "reference_category", "category": category}, Callable(panel, "_show_category"))
				await get_tree().create_timer(0.8).timeout
				print("smoke: info: reference category %d: '%s', topic '%s' (%d chars), picture %s/%d" % [category,
					panel._content.get_child(0).get_child(0).get_child(1).text, panel._detail_title.text, panel._detail_text.text.length(),
					panel._picture.lbx, panel._picture.entry])
				if shots != "":
					await host.shot(shots + "/native-info-reference-topic.png")
				if panel._detail_text.text.length() == 0:
					return false
	var saved: Dictionary = await _query_args({"what": "info"})
	print("smoke: info: saved tab %d (last shown 4)" % int(saved.get("tab", -1)))
	if int(saved.get("tab", -1)) != 4:
		return false
	panel._set_tab(0)
	await get_tree().create_timer(0.4).timeout
	if not await _smoke_side_resize(panel, shots, "info"):
		return false
	# A click on the galaxy left of it closes it (owner: as Ship Design and Options).
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	click.position = Vector2(60, get_viewport().get_visible_rect().size.y * 0.5)
	click.global_position = click.position
	Input.parse_input_event(click)
	var release := click.duplicate()
	release.pressed = false
	Input.parse_input_event(release)
	await get_tree().create_timer(0.6).timeout
	print("smoke: info: a click on the galaxy: panel %s, tab armed %s" % [panel.visible, tab.button_pressed])
	return not panel.visible and not tab.button_pressed


func _row_star_first(panel: Control, star: int) -> int:
	for c: Dictionary in panel.colonies:
		if int(c.star) == star:
			return int(c.colony)
	return -1


func _smoke_click_at(at: Vector2) -> InputEventMouseButton:
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	click.position = at
	click.global_position = at
	return click


func _smoke_left_click() -> InputEventMouseButton:
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	return click


## The scene window (GNN news; owner, 2026-11-14): its pages are answered by a click, without the
## galaxy's hand-back between them; "scene_end" closes it and then answers.
var _scene_window: Control


func _scene() -> Control:
	if _scene_window == null:
		_scene_window = preload("views/scene_window.gd").new()
		_scene_window.name = "SceneWindow"
		_scene_window.art = art
		_scene_window.theme = native_layer.theme
		$Overlay.add_child(_scene_window)
		_scene_window.page_done.connect(func(page_id: int): game.answer_request(page_id, 0))
	return _scene_window


func _on_scene_request(id: int, request: Dictionary) -> void:
	_scene()
	if String(request.popup) == "scene_end":
		var ended: Dictionary = _scene_window._state
		_scene_window.close(func(): _on_answered(id, 0))
		if String(ended.get("scene", "")) == "landing":
			_open_landed_colony(int(ended.get("star", -1)), int(ended.get("colony", -1)))
		return
	var state: Dictionary = request.get("scene_state", request)
	_place_scene(state)
	if String(state.get("scene", "")) == "landing" and not _scene_window.is_open():
		_prepare_landed_colony(int(state.get("star", -1)), int(state.get("colony", -1)), state)
	_scene_window.show_page(id, request)


## After the colony landing (WP3.11a, owner 2026-11-19): the new colony and its build list, so the
## click that ends the landing arrives at the build decision.
func _open_landed_colony(star: int, colony: int, with_build := true) -> void:
	if star < 0 or colony < 0:
		return
	var waited := 0.0
	while native_screen != Screens.MAIN and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if native_screen != Screens.MAIN:
		return
	_open_colony_at(star, colony, with_build)


## A colony with its system (and its build list): the colony pane first, so the system's one
## flight already frames the system beside both panes (owner, 2026-11-19: the camera jerked as the
## colony pane narrowed the free area mid-flight, then went on as the build list opened).
func _open_colony_at(star: int, colony: int, with_build: bool) -> void:
	var pane := _view("colony_pane")
	if colony != pane.colony_id:
		_view("build_pane").close_keeping()
	pane.move_to_front()
	_raise_chrome()
	pane.open(colony)
	_open_system(star)
	if not with_build:
		return
	await get_tree().create_timer(0.5).timeout
	if native_screen != Screens.MAIN:
		return
	# The colony pane first (owner, 2026-11-19: after a landing the build list came up without it).
	if not pane.visible or pane.colony_id != colony:
		pane.move_to_front()
		pane.open(colony)
		await get_tree().create_timer(0.4).timeout
	var build := _view("build_pane")
	if not build.visible:
		_open_build_pane(colony, false)
	elif int(build.colony_id) == colony:
		build.open(colony, false) # refresh what the landing showed


## The landing has begun: the camera flies to the new colony's system behind the window, framed
## for the colony pane (its data comes when the landing ends: queries wait for the game), so
## the panes slide in where the view already is (owner, 2026-11-19).
func _prepare_landed_colony(star: int, colony: int, state: Dictionary) -> void:
	if star < 0 or colony < 0 or galaxy.stars.is_empty():
		return
	# The system, the colony and its build list open behind the window from the landing's own
	# copy of their answers (owner, 2026-11-19: they should be open when the film ends).
	var colony_pane := _view("colony_pane")
	if colony != colony_pane.colony_id:
		_view("build_pane").close_keeping()
	colony_pane.colony_id = colony
	var colony_data: Dictionary = state.get("colony_data", {})
	if colony_data.get("ok", false):
		colony_pane.move_to_front()
		colony_pane.show_colony(colony_data)
	var system: Dictionary = state.get("system", {})
	if system.get("ok", false):
		_show_system_result(star, system, true)
	else:
		galaxy.selected = star
		_panel_star = star
		_frame_system(star)
	var options: Dictionary = state.get("build_options", {})
	if options.get("ok", false):
		var build := _view("build_pane")
		_place_build_pane()
		build.move_to_front()
		_raise_chrome()
		build.colony_id = colony
		build.bought = false
		build.show_options(options)


## The research area (owner, 2026-11-14): from under the bar to above the map controls, its right
## edge at the bar's right end. The research drop-down rests there, and the research window is
## the scene at 4:3 with the same top, right and bottom edges, so the picker's frame lies on the
## window's and the two read as one.
func _research_area() -> Rect2:
	var view := get_viewport().get_visible_rect().size
	var bar: Control = _view("galaxy_bar")
	var top := float(bar.get_theme_constant("content_top", "Screen"))
	var right := bar.get_global_rect().end.x
	var bottom := view.y - 8.0
	var controls: Control = _views.get("map_controls")
	if controls != null and controls.is_visible_in_tree():
		bottom = controls.get_global_rect().position.y - maxf(float(bar.get_theme_constant("pane_gap", "Screen")), 6.0)
	return Rect2(0.0, top, right, maxf(bottom - top, 200.0))


func _research_scene_rect() -> Rect2:
	var area := _research_area()
	var m: Rect2 = _scene()._margins()
	var width := (area.size.y - m.size.y) * 4.0 / 3.0 + m.size.x
	return Rect2(maxf(area.end.x - width, 8.0), area.position.y, width, area.size.y)


func _place_scene(state: Dictionary) -> void:
	if String(state.get("placement", "")) == "research":
		_scene().custom_rect = _research_scene_rect()


var _picker: Control


func _research_picker() -> Control:
	if _picker == null:
		_picker = load(_views_dir() + "/research_panel.tscn").instantiate()
		_picker.name = "ResearchPicker"
		_picker.theme = native_layer.theme
		_picker.pick_mode = true
		_picker.hide()
		$Overlay.add_child(_picker)
	return _picker


## The research window's picker (science.cpp, owner 2026-11-14): the monitor closes, then the
## research drop-down comes down over the room's right side, its right and bottom edges on the
## window's; a pick answers field * 256 + application.
func _on_research_pick(id: int, request: Dictionary) -> void:
	var state: Dictionary = request.get("scene_state", {})
	_place_scene(state)
	_scene().show_state(state)
	if _scene_window.arriving():
		await _scene_window.arrived
	var picker := _research_picker()
	var bar := _view("galaxy_bar")
	picker.set_progress(bar.research.text)
	picker.show_research((request.get("research", {}) as Dictionary).merged({"ok": true})) # a query's reply carries "ok"
	picker.move_to_front()
	var rect: Rect2 = _scene_window._frame_rect()
	picker.open_in(Rect2(0.0, rect.position.y, rect.end.x, rect.size.y), rect.size.y)
	var choice: Array = await picker.picked
	picker.close()
	_on_answered(id, int(choice[0]) * 256 + int(choice[1]) if int(choice[0]) >= 0 else -1)


## A popup that works with the map (WP3.4c): a system-zoom one ("zoom": the camera flies into the
## star, its planets show and a click on one answers, the window beside it) or a star pick
## ("pick_star": the monster's new target, the window above the map). The shade lets the map's
## clicks through; the view comes back when it is answered.
var _popup_zoom := -1
var _popup_pick_star := false


func _start_popup_map(dialog: Control, request: Dictionary) -> void:
	_popup_zoom = int(request.get("zoom", -1))
	if _scene_window != null and _scene_window.is_open():
		_popup_zoom = -1 # over a scene (the landing's occupation question): the map stays
	_popup_pick_star = bool(request.get("pick_star", false))
	var on_map: bool = (_popup_zoom >= 0 or _popup_pick_star) and galaxy.visible and not galaxy.stars.is_empty()
	dialog.placement = ("left" if _popup_zoom >= 0 else "top") if on_map else ""
	dialog.bare_width = 0.0
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE if on_map else Control.MOUSE_FILTER_STOP
	overlay.color.a = 0.0 if on_map else 0.45
	if not on_map:
		_popup_zoom = -1
		_popup_pick_star = false
		return
	if _popup_zoom >= 0 and galaxy.system_star != _popup_zoom:
		# Another system's pane goes first (owner, 2026-11-14: a to-do item's window flew to its
		# system with the old system's pane still up); the camera flies on from where it is.
		var system_pane: Control = _views.get("system_pane")
		if system_pane != null and system_pane.visible:
			_keep_camera = true # no flight back to the galaxy on its closing
			system_pane.dismiss()
			_keep_camera = false
		galaxy.selected = _popup_zoom
		_frame_system(_popup_zoom)
		galaxy.show_system(_popup_zoom, request.get("system", {}))
		galaxy.redraw()


func _end_popup_map() -> void:
	overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	overlay.color.a = 0.45
	if _popup_zoom >= 0 and _panel_star != _popup_zoom:
		galaxy.selected = _panel_star
		galaxy.hide_system()
		galaxy.fly_back()
	_popup_zoom = -1
	_popup_pick_star = false


## A map click while a map popup waits: a planet of the zoomed system, or the star picked.
func _popup_map_click(planet_id: int, star_id: int) -> bool:
	if not overlay.visible or not _views.has("dialog"):
		return false
	var dialog: Control = _views.dialog
	if _popup_zoom >= 0:
		if planet_id >= 0:
			var value: int = dialog.answer_for_planet(planet_id)
			if value >= 0:
				dialog._answer(value)
		return true
	if _popup_pick_star:
		if star_id >= 0:
			dialog._answer(dialog.OPTION_ANSWER + star_id)
		return true
	return false


var _occupation_open: Array = [] # [star, colony] to open once the occupation question is answered


func _on_answered(id: int, value: int, text := "") -> void:
	if not _occupation_open.is_empty():
		var open: Array = _occupation_open
		_occupation_open = []
		_open_landed_colony(int(open[0]), int(open[1]), false)
	if _scene_window != null and _scene_window.is_open():
		# A list over a scene: the scene goes on (no hand-back to the legacy view between its
		# requests; scene_end closes it and comes back here).
		if text != "" or (_views.has("dialog") and _views.dialog._kind == "text"):
			game.answer_request_text(id, value, text)
		else:
			game.answer_request(id, value)
		return
	overlay.hide()
	_end_popup_map()
	if _question_over_galaxy:
		_question_over_galaxy = false
		if native_screen == Screens.LEGACY and not _turn_over_galaxy:
			# The galaxy stays a moment, as after TURN (owner, 2026-11-19: closing the GNN or the
			# research window flashed the whole screen: the bar and tabs went, the turn's legacy
			# frame showed, and the galaxy came back as the reports ended). The hand-back to the
			# legacy view comes only if the game stays on a legacy screen (_end_turn_notice).
			_turn_over_galaxy = true
			_turn_clearing = 0.0
			_turn_clearing_polls = int(game.get_stats().get("input_polls", 0))
		elif native_screen == Screens.LEGACY:
			# The legacy view still holds its last frame from before the native galaxy (the title
			# screen after a load): it comes back with the game's next frame (owner, 2026-11-12:
			# the title flashed before a combat).
			_legacy_after_frame = int(game.get_stats().frames)
			_legacy_waited = 0.0
	if text != "" or (_views.has("dialog") and _views.dialog._kind == "text"):
		game.answer_request_text(id, value, text)
	else:
		game.answer_request(id, value)


## A star click: with F4's mode on it takes the merged relocations; with ships selected in the
## fleet panel it moves them there (black holes do nothing, as on the legacy map); otherwise the
## game decides, as the legacy map does, whether it opens the player's colony there (a legacy
## screen for now) or the system panel.
## A right sidebar panel (Colonies WP3.5a, Planets WP3.5b): opening it closes the system, colony
## and build panes, the fleet panel and another side panel, and the galaxy fits the space left of it.
## WP3.6: the Ship Design panel on design slot `slot`, in place of the design list; BUILD brings
## the list back with the new design.
func _open_design(slot: int) -> void:
	var panel := _view("design_panel")
	if not panel.built.is_connected(_on_design_built):
		panel.built.connect(_on_design_built)
		panel.help_requested.connect(show_help)
	_view("design_list").close()
	_bring_up(panel)
	panel.open_panel(_side_panel_area(), slot)
	_view("side_tabs").show_open("designs", true)


func _on_design_built(_slot: int) -> void:
	var designs := _view("design_list")
	_bring_up(designs)
	designs.fit_bottom(_side_panel_area().end.y)
	designs.open()
	_view("side_tabs").show_open("designs", true)


## The fleet the next Fleets panel opens on (the Leaders panel's FLEETS), or -1.
var _fleet_for_panel := -1


func _open_side_panel(action_name: String) -> void:
	var side_view := _view(SIDE_PANELS[action_name])
	var fleet_shown := _fleet_ship if _fleet_for_panel < 0 else _fleet_for_panel # Fleets opens on the fleet whose box was open
	_fleet_for_panel = -1
	var system_shown := _panel_star # Leaders opens on the system whose pane was open
	var fleets_view: Control = _views.get("fleets_panel")
	if action_name == "leaders" and fleets_view != null and fleets_view.visible and not fleets_view.fleet.is_empty():
		side_view.want_fleet = int(fleets_view.fleet.ship) # Ship Officers opens on the Fleets panel's fleet (owner)
	_view("build_pane").close_keeping()
	var system_pane := _view("system_pane")
	if system_pane.visible:
		_keep_camera = true # the panel's fit is the next flight; the view before the system is kept
		system_pane.close()
		_keep_camera = false
	var fleet_panel := _view("fleet_panel")
	if fleet_panel.visible:
		_fleet_from_system = -1
		fleet_panel.close()
	_switching_panel = true # another side panel closes: the camera stays fitted for this one
	_bring_up(side_view)
	_switching_panel = false
	if "fleets" in side_view:
		side_view.player = int(galaxy.data.get("player", -1))
		side_view.fleets = galaxy.fleets
		side_view.open_panel(_side_panel_area(), fleet_shown)
	elif action_name == "leaders":
		side_view.stars = galaxy.stars
		side_view.open_panel(_side_panel_area(), system_shown)
	else:
		if action_name == "planets":
			side_view.width_like = _view("colonies_panel") # the same width: the galaxy's zoom stays
		if action_name == "info":
			if not side_view.item_chosen.is_connected(_on_info_summary_item):
				side_view.item_chosen.connect(_on_info_summary_item)
				side_view.help_requested.connect(show_help)
		side_view.open_panel(_side_panel_area())
	_view("side_tabs").show_open(action_name, true)
	_fit_beside_panel(side_view)


## The whole galaxy in the space left of a side panel (again when the panel's width changes).
func _fit_beside_panel(colonies: Control) -> void:
	if not colonies.visible or native_screen != Screens.MAIN:
		return
	for unfitted: String in SIDE_PANELS_UNFITTED:
		if colonies == _views.get(unfitted):
			return
	var area := _side_panel_area()
	var inset := get_theme_constant_from(colonies, "inset")
	var gap := maxf(get_theme_constant_from(colonies, "pane_gap"), 6.0)
	var left := inset + gap
	var right: float = colonies.rest_left() - gap
	galaxy.fit_in(Rect2(left, area.position.y, maxf(right - left, 60.0), area.size.y))


## Where the right sidebar's panels rest (owner, 2026-11-14: they fly out from the right): against
## the side tabs, from under the bar to above the map controls.
func _side_panel_area() -> Rect2:
	var area := _research_area()
	var gap := maxf(get_theme_constant_from(_view("galaxy_bar"), "pane_gap"), 6.0)
	var right := get_viewport().get_visible_rect().size.x
	var tabs: Control = _view("side_tabs")
	if tabs.visible:
		for b in tabs.find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				right = minf(right, (b as Control).get_global_rect().position.x - gap)
	return Rect2(0.0, area.position.y, right, area.size.y)


## The Colonies panel's Building slot (or its name, `with_build` false): the panel goes, the
## colony's system opens with its colony pane (and build pane), and the system pane's way back
## reads RETURN TO COLONIES (owner, 2026-11-14).
func _colonies_build(colony_id: int, star_id: int, planet_id: int, bought: bool, with_build := true) -> void:
	_colonies_return = true
	galaxy.keep_fit_for_return()
	_view("colonies_panel").close()
	_send({"verb": "query", "what": "system", "star": star_id}, func(result: Dictionary):
		if not result.get("ok", false) or native_screen != Screens.MAIN:
			_colonies_return = false
			galaxy.unfit()
			return
		_panel_star = star_id
		galaxy.selected = star_id
		var pane := _view("system_pane")
		_frame_system(star_id)
		galaxy.show_system(star_id, result)
		galaxy.redraw()
		_send({"verb": "watch", "what": "system", "star": star_id}) # its planets move each turn
		pane.race_id = _race_id
		pane.show_system(result, String(galaxy.stars[star_id].get("label", "")).trim_prefix("(").trim_suffix(")"), star_id)
		(pane.get_node("Box/Back") as Button).text = "RETURN TO COLONIES"
		pane.select_planet(planet_id) # its colony pane (colony_chosen)
		if with_build:
			_open_build_pane(colony_id, bought))


func _on_star_clicked(star_id: int) -> void:
	if _popup_map_click(-1, star_id):
		return
	for panel_name: String in SIDE_PANELS.values():
		var side_view: Control = _views.get(panel_name)
		if side_view != null and side_view.visible:
			if "fleets" in side_view:
				side_view.fleets = galaxy.fleets
			side_view.select_star(star_id) # the panel's row; the map stays (owner, 2026-11-14)
			return
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
		_show_system_result(star_id, result, fly))


## A system's answer on the galaxy and in the system pane (the `system` query's, or the colony
## landing's own copy while it plays).
func _show_system_result(star_id: int, result: Dictionary, fly: bool) -> void:
	# A system and a fleet don't share the screen (owner, 2026-11-13: a fleet's panel stayed
	# over the system the to-do list flew into): the fleet panel closes, without coming back.
	var fleet_panel := _view("fleet_panel")
	if fleet_panel.visible:
		_fleet_from_system = -1
		fleet_panel.close()
	# Another system's colony and build list close first (owner, 2026-11-19: a scouts' arrival
	# picked from the to-do list opened its system under the old colony's panes); the build
	# list keeps its changes, as a click away does.
	var colony_pane := _view("colony_pane")
	var colony_here := false # the open colony is in this system (a to-do build item opens it first)
	for planet: Dictionary in result.get("planets", []):
		if int(planet.get("colony", {}).get("id", -1)) == int(colony_pane.colony_id):
			colony_here = true
	if _panel_star != star_id and colony_pane.visible and not colony_here:
		_panel_star = -1 # its closing doesn't re-frame the old system
		colony_pane.close()
	_panel_star = star_id
	galaxy.selected = star_id
	var pane := _view("system_pane")
	if fly or galaxy.system_star != star_id:
		_frame_system(star_id)
	galaxy.show_system(star_id, result)
	galaxy.redraw()
	_send({"verb": "watch", "what": "system", "star": star_id}) # its planets move each turn
	pane.race_id = _race_id
	pane.show_system(result, String(galaxy.stars[star_id].get("label", "")).trim_prefix("(").trim_suffix(")"), star_id)
	if int(colony_pane.colony_id) >= 0:
		pane.mark_colony(int(colony_pane.colony_id))


## Flies to a system with its orbits filling a share of the area the left panes leave free (the
## system pane, and the colony pane when it is open: WP3.4).
const SYSTEM_ZOOM_CLOSER := 1.2 * 1.2 # two wheel notches (galaxy.gd's 1.2 a notch)
const SYSTEM_ROOM_PER_HEIGHT := 1.15


func _frame_system(star_id: int) -> void:
	var view := get_viewport().get_visible_rect().size
	var left := 360.0
	# Where the panes rest when open, not where they are now: they slide by moving, and a pane
	# still off-screen from the last close gave the whole screen (owner, 2026-11-13).
	var system_pane: Control = _views.get("system_pane")
	if system_pane != null:
		left = float(system_pane._shown_left) + 372.0
	# The zoom is the one that fits the space right of both panes, open or not (owner,
	# 2026-11-13: the colony pane changed it); only the centre moves when it opens or closes.
	var colony_pane: Control = _views.get("colony_pane")
	var left_both := left + 6.0 + 340.0
	if colony_pane != null:
		left_both = float(colony_pane._shown_left) + float(colony_pane.WIDTH)
	if colony_pane != null and colony_pane.colony_id >= 0:
		left = left_both
	# The free area runs to the side tabs (owner, 2026-11-13: the star sat near the screen's centre
	# instead of the middle of what the pane leaves).
	var right := view.x - 48.0
	var tabs: Control = _views.get("side_tabs")
	if tabs != null and tabs.visible:
		for b in tabs.find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				right = minf(right, (b as Control).get_global_rect().position.x - 8.0)
	if right - left < view.x * 0.3: # a small window: the panes cover most of it
		left = 0.0
		right = view.x
	var room := right - left_both if right - left_both >= view.x * 0.3 else right - left
	# Never wider than the height allows (owner, 2026-11-14: on a 32:9 window the free width
	# zoomed the system in past its art, muddy); a 16:9 screen's room is about this already.
	room = minf(room, view.y * SYSTEM_ROOM_PER_HEIGHT)
	var zoom: float = galaxy.zoom_for_scale(SYSTEM_VIEW_SCALE)
	# The orbits as they looked two wheel notches closer (owner, 2026-11-13), but the camera (and
	# so the star) at the framing zoom (owner, 2026-11-14: the star was drawn too large, over the
	# planets behind it).
	galaxy.system_scale = SYSTEM_VIEW_SHARE * room / (288.0 * zoom) * SYSTEM_ZOOM_CLOSER
	galaxy.fly_to(galaxy.star_position(star_id), zoom, Vector2((left + right) * 0.5, view.y * 0.5))


## A planet clicked on the map: selected here and in the pane.
## A planet's right click on the map: its card (planet_card.gd) by the pointer, from the colony
## ship's window while one waits (it carries the planets), else from the system pane; the game's
## own box only without either.
func _on_planet_right_clicked(planet_id: int) -> void:
	var at := Rect2(get_viewport().get_mouse_position() + Vector2(12, -12), Vector2.ONE)
	if overlay.visible and _views.has("dialog") and _views.dialog.show_planet_card(planet_id, at):
		return
	var pane := _view("system_pane")
	if pane.visible and not pane._planet_entry(planet_id).is_empty():
		pane.show_planet_card(planet_id, at)
		return
	if not overlay.visible:
		_send({"verb": "planet_info", "planet": planet_id})


## A planet of the open system under the pointer (or -1): its row in the system pane lights up;
## a colony ship's window over the system shows it (owner, 2026-11-13).
func _on_planet_hovered(planet_id: int) -> void:
	if overlay.visible and _popup_zoom >= 0 and _views.has("dialog"):
		_views.dialog.hover_planet(planet_id)
	var pane := _view("system_pane")
	if pane.visible:
		pane.hover_planet(planet_id)


func _on_planet_clicked(planet_id: int) -> void:
	if _popup_map_click(planet_id, -1):
		return
	galaxy.selected_planet = planet_id
	galaxy.redraw()
	_view("system_pane").select_planet(planet_id)


## One of the player's colonies picked in the system pane (WP3.4): the colony pane opens beside it;
## another planet closes it.
func _on_colony_chosen(colony_id: int) -> void:
	var pane := _view("colony_pane")
	var was_open: bool = pane.colony_id >= 0
	if colony_id != pane.colony_id:
		_view("build_pane").close_keeping() # another colony: its edits are kept, as OK
	if colony_id < 0:
		pane.close() # its `closed` re-frames the system
		return
	pane.move_to_front()
	_raise_chrome()
	pane.open(colony_id)
	if not was_open and _panel_star >= 0:
		_frame_system(_panel_star) # the free area narrowed


func get_theme_constant_from(control: Control, name: String) -> float:
	return float(control.get_theme_constant(name, "Screen"))


## CHANGE (WP3.4b): the build pane slides out beside the colony pane, over the map.
func _open_build_pane(colony_id: int, bought: bool) -> void:
	var pane := _view("build_pane")
	if pane.visible and int(pane.colony_id) == colony_id:
		pane.cancel() # CHANGE again: closes it, as Cancel (owner, 2026-11-13)
		return
	_place_build_pane()
	pane.move_to_front()
	_raise_chrome()
	pane.open(colony_id, bought)


## The colony and build panes end above the map controls, so the previous/next buttons stay in reach
## (owner, 2026-11-14).
func _clear_map_controls() -> void:
	var controls: Control = _views.get("map_controls")
	if controls == null:
		return
	for pane_name in ["colony_pane", "build_pane"]:
		var pane: Control = _views.get(pane_name)
		if pane != null:
			var gap := maxf(get_theme_constant_from(pane, "pane_gap"), 6.0)
			pane.set_bottom_clear(get_viewport().get_visible_rect().size.y - controls.get_global_rect().position.y + gap)


## Beside the colony pane, clear of the side tabs (Glass keeps them on the screen's right edge,
## beside the panes); again when the window changes size (owner, 2026-11-14: windowed, it stayed
## under the side tabs).
func _place_build_pane() -> void:
	var pane := _view("build_pane")
	var colony_pane: Control = _view("colony_pane")
	var gap := maxf(get_theme_constant_from(pane, "pane_gap"), 6.0)
	var right := get_viewport().get_visible_rect().size.x
	var tabs: Control = _view("side_tabs")
	if tabs.visible:
		for b in tabs.find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				right = minf(right, (b as Control).get_global_rect().position.x - gap)
	# Where the colony pane rests when open, not where it is now (owner, 2026-11-19: opened while
	# the colony pane was still sliding in, as after a landing, the build list sat over it), at
	# its real width: it grows past WIDTH to fit its rows (the nominal width put the build list
	# over it on the owner's screen). Sliding moves it, it doesn't resize it.
	var colony_end := float(colony_pane._shown_left) + maxf(colony_pane.size.x, float(colony_pane.WIDTH))
	pane.place_at(colony_end + gap, right)


func _on_pane_planet(planet_id: int) -> void:
	galaxy.selected_planet = planet_id
	galaxy.redraw()


## A fleet from the system pane: the game closes the system box for the fleet box, and the map
## stays at the system.
func _open_fleet_from_pane(ship_id: int) -> void:
	_keep_camera = true
	_open_fleet(ship_id)


## Opens a fleet's panel (a map icon, the system panel's fleets, F1/F2): the game keeps the
## fleet box record and closes the system box, then the `fleet` query fills the panel.
func _open_fleet(ship_id: int) -> void:
	# With the Fleets panel open, a click on one of the player's fleets shows it there (owner,
	# 2026-11-15); other fleets still open their box.
	var fleets_view: Control = _views.get("fleets_panel")
	if fleets_view != null and fleets_view.visible:
		for f: Dictionary in galaxy.fleets:
			if int(f.ship) != ship_id:
				continue
			if fleets_view.relocating and int(f.get("star", -1)) >= 0:
				fleets_view.star_clicked(int(f.star)) # RELOCATE: a fleet at a star is its star
				return
			if int(f.owner) == int(galaxy.data.get("player", -1)):
				fleets_view._show(ship_id)
				return
	# With Leaders open on Ship Officers, one of the player's fleets goes to its cycler (owner).
	var leaders_view: Control = _views.get("leaders_panel")
	if leaders_view != null and leaders_view.visible and leaders_view.tab == "ship":
		for f: Dictionary in galaxy.fleets:
			if int(f.ship) == ship_id and int(f.owner) == int(galaxy.data.get("player", -1)):
				leaders_view.select_fleet(ship_id) # a fleet without warships has no officers to show
				return
	var from_star := _panel_star if _view("system_pane").visible else -1
	if from_star >= 0:
		_keep_camera = true # the pane goes, the map stays at the system
	_send({"verb": "open_fleet", "fleet": ship_id}, func(opened: Dictionary):
		if not opened.get("ok", false):
			_keep_camera = false
			return
		_view("system_pane").dismiss()
		_keep_camera = false
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
	var fleets_view: Control = _views.get("fleets_panel")
	if fleets_view != null and fleets_view.visible and not fleets_view.relocating:
		fleet_panel = fleets_view # the Fleets panel's picked ships get the route preview
		ships = fleets_view.selected_ships()
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
	var fleets_view: Control = _views.get("fleets_panel")
	if fleets_view != null and fleets_view.relocating:
		fleets_view._set_relocating(false) # a right click leaves the Fleets panel's RELOCATE too
	if galaxy.relocating:
		galaxy.relocating = false
		Input.set_default_cursor_shape(Input.CURSOR_ARROW)


## The fleet panel beside the fleet, on the side of the window away from it.
func _place_fleet_panel(fleet_panel: Control, world: Vector2) -> void:
	var view := get_viewport().get_visible_rect().size
	var at: Vector2 = galaxy.get_global_transform_with_canvas() * world
	# Beside the fleet (owner, 2026-11-19: it opened at the far edge of the screen): to its right,
	# or to its left where there is no room, clear of the side tabs.
	var right_edge := view.x - 60.0
	var gap := view.x * 0.2 # a fifth of the screen away (owner, 2026-11-19: right beside it was too close)
	var x := at.x + gap
	if x + fleet_panel.size.x > right_edge:
		x = at.x - gap - fleet_panel.size.x
	x = clampf(x, 12.0, right_edge - fleet_panel.size.x)
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
	if "--new-game" in OS.get_cmdline_user_args():
		await _smoke_new_game(shots)
		return
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
		if arg == "--film":
			await _smoke_film(shots)
			return
		if arg == "--system-fleet":
			var star_with_fleet := -1
			for fleet: Dictionary in galaxy.fleets:
				if int(fleet.get("star", -1)) >= 0 and bool(fleet.get("mine", false)):
					star_with_fleet = int(fleet.star)
					break
			if star_with_fleet < 0:
				for fleet: Dictionary in galaxy.fleets:
					if int(fleet.get("star", -1)) >= 0:
						star_with_fleet = int(fleet.star)
						break
			_open_system(star_with_fleet)
			await get_tree().create_timer(3.0).timeout
			print("smoke: system fleet: star %d, fleets drawn at alpha %.2f" % [star_with_fleet, galaxy.system_fleets_alpha])
			if shots != "":
				await host.shot(shots + "/native-system-fleet.png")
			game.request_game_quit()
			return
		if arg == "--todo-other-system":
			# A to-do item about another system (a scouts' arrival) while a colony is open there.
			var first := -1
			var first_colony := -1
			for c in 64:
				var data: Dictionary = await _query_args({"what": "colony", "colony": c})
				if data.get("ok", false) and not data.get("outpost", false):
					first = int(data.star)
					first_colony = c
					break
			_open_colony_at(first, first_colony, true)
			await get_tree().create_timer(2.5).timeout
			var other := -1
			for fleet: Dictionary in galaxy.fleets:
				if int(fleet.get("star", -1)) >= 0 and int(fleet.star) != first:
					other = int(fleet.star)
					break
			_on_todo_item({"kind": "news", "star": other, "item": -1})
			await get_tree().create_timer(2.5).timeout
			var pane := _view("system_pane")
			print("smoke: to-do to another system: %d -> %d; system pane %s on %d, map system %d, others %.2f, colony pane %s, build pane %s" % [
				first, other, pane.visible, int(pane.star), galaxy.system_star, galaxy.others_alpha,
				_view("colony_pane").visible, _view("build_pane").visible])
			if shots != "":
				await host.shot(shots + "/native-todo-other-system.png")
			if not pane.visible or int(pane.star) != other or galaxy.system_star != other or _view("colony_pane").visible or _view("build_pane").visible:
				await host.smoke_fail("the to-do item's system did not replace the open one")
				return
			game.request_game_quit()
			return
		if arg == "--pane-heights":
			# Every star's system pane: none may be taller than the screen (owner, 2026-11-19, Glass).
			var pane := _view("system_pane")
			var limit := get_viewport().get_visible_rect().size.y
			var worst := 0.0
			for star in galaxy.stars.size():
				_open_system(star, false)
				await get_tree().create_timer(0.35).timeout
				var h: float = pane.get_combined_minimum_size().y
				worst = maxf(worst, h)
				if h > limit - 56.0 or pane.position.y < 0.0:
					print("smoke: pane too tall at star %d (%s): min %.0f, at y %.0f, size %.0f, view %s, fleets %d" % [star,
						galaxy.stars[star].get("label", ""), h, pane.position.y, pane.size.y, pane.system.get("view", "?"), pane.system.get("fleets", []).size()])
					for child: Control in pane.get_node("Box").get_children():
						print("smoke:   %s min %.0f visible %s" % [child.name, child.get_combined_minimum_size().y, child.visible])
			print("smoke: pane heights: tallest %.0f of %.0f" % [worst, limit])
			# The owner's stuck stretch: content briefly taller than the screen while the pane
			# slides in. Once the content shrinks back, the pane must too.
			var settled: Rect2 = pane.get_rect()
			pane.dismiss()
			await get_tree().create_timer(0.35).timeout
			var box: Control = pane.get_node("Box")
			box.custom_minimum_size.y = limit * 3.0
			_open_system(0, false)
			await get_tree().create_timer(0.1).timeout
			box.custom_minimum_size.y = 0.0
			await get_tree().create_timer(0.4).timeout
			print("smoke: pane after a tall frame mid-slide: %s (settled %s)" % [pane.get_rect(), settled])
			if pane.size.y > limit or pane.position.y < 0.0:
				await host.smoke_fail("the system pane stayed stretched after a tall frame")
				return
			game.request_game_quit()
			return
		if arg == "--government-help":
			var home := -1
			for c in 64:
				var data: Dictionary = await _query_args({"what": "colony", "colony": c})
				if data.get("ok", false) and not data.get("outpost", false):
					home = int(data.star)
					break
			_open_system(home)
			await get_tree().create_timer(2.5).timeout
			var pane := _view("system_pane")
			for planet: Dictionary in pane.system.get("planets", []):
				if planet.has("colony") or planet.get("owner_color", -1) != -1 or planet.get("colonized", false):
					pane.select_planet(int(planet.planet))
					break
			if pane.selected_planet < 0 and not pane.system.get("planets", []).is_empty():
				for planet: Dictionary in pane.system.planets:
					pane._expanded[int(planet.planet)] = true
				pane._show_planets()
			await get_tree().create_timer(1.5).timeout
			var found: Array = pane.find_children("*", "Control", true, false).filter(
				func(n: Node) -> bool: return (n as Control).tooltip_text in ["Government", "Morale"] and (n as Control).is_visible_in_tree())
			print("smoke: government help: system %d, %d icons" % [home, found.size()])
			for icon: Control in found.slice(0, 1) + found.filter(func(n: Node) -> bool: return (n as Control).tooltip_text == "Morale").slice(0, 1):
				var at := icon.get_global_rect().get_center() * get_window().content_scale_factor
				var press := InputEventMouseButton.new()
				press.button_index = MOUSE_BUTTON_RIGHT
				press.pressed = true
				press.position = at
				Input.parse_input_event(press)
				var release := press.duplicate()
				release.pressed = false
				Input.parse_input_event(release)
				await get_tree().create_timer(0.8).timeout
				var open: bool = preload("views/info_popup.gd").is_open()
				print("smoke: right click on %s: help open %s" % [icon.tooltip_text, open])
				if not open:
					await host.smoke_fail("a right click on the system pane's %s showed no help" % icon.tooltip_text)
					return
				if shots != "":
					await host.shot(shots + "/native-help-%s.png" % icon.tooltip_text.to_lower())
				preload("views/info_popup.gd").close_current()
			game.request_game_quit()
			return
		if arg == "--queue-drag":
			await _smoke_queue_drag(shots)
			return
		if arg == "--settle":
			await _smoke_settle(shots)
			return
		if arg == "--council":
			await _smoke_council(shots)
			return
		if arg == "--occupation":
			await _smoke_occupation(shots)
			return
		if arg == "--antarans":
			await _smoke_antarans(shots)
			return
		if arg.begins_with("--scene-flash="):
			await _smoke_scene_flash(arg.substr(14))
			return
	if "--popups" in OS.get_cmdline_user_args():
		await _smoke_popups(shots)
		return
	# The side panels' steps, any of them in one launch (one game start for several checks), with
	# quick camera flights: the steps wait on the flights.
	var panel_steps: Array = ["--colonies", "--planets", "--fleets", "--leaders", "--races", "--info", "--design"].filter(func(f: String) -> bool: return f in OS.get_cmdline_user_args())
	if not panel_steps.is_empty():
		galaxy.flight_time = 0.15
		for step: String in panel_steps:
			var passed := false
			if step == "--colonies":
				passed = await _smoke_colonies(shots)
			elif step == "--planets":
				passed = await _smoke_planets(shots)
			elif step == "--fleets":
				passed = await _smoke_fleets_panel(shots)
			elif step == "--leaders":
				passed = await _smoke_leaders_panel(shots)
			elif step == "--info":
				passed = await _smoke_info_panel(shots)
			elif step == "--design":
				passed = await _smoke_design_panel(shots)
			else:
				passed = await _smoke_races_panel(shots)
			if not passed:
				await host.smoke_fail(step.trim_prefix("--") + " panel")
				return
			print("smoke: %s OK" % step.trim_prefix("--"))
		print("smoke: panels OK")
		game.request_game_quit()
		return
	if "--resize-test" in OS.get_cmdline_user_args():
		await _smoke_resize(shots)
		game.request_game_quit()
		return
	if "--pan-test" in OS.get_cmdline_user_args():
		await _smoke_pan(shots)
		game.request_game_quit()
		return
	if "--todo-only" in OS.get_cmdline_user_args():
		# WP3.4d: only the turns and the to-do list (saves whose other steps don't fit).
		if await _smoke_turns() and await _smoke_todo(shots):
			game.request_game_quit()
			print("smoke: asked the game to quit")
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
	print("smoke: command points as a screen: shown %s, '%s' / %s / %s" % [points.visible, points.get_node("Fit/Box/Header/Title").text,
		points.get_node("Fit/Box/Total").text, points.get_node("Fit/Box/Text").text.replace("\n", " | ")])
	points.close() # sends "close": back to the galaxy
	if not await _wait_screen(Screens.MAIN, 10.0):
		await host.smoke_fail("command points did not close")
		return
	await get_tree().create_timer(0.6).timeout
	# The bar's drop-downs (WP3.3g): command points and research stay on the galaxy.
	_on_view_action("command_points")
	await get_tree().create_timer(1.0).timeout
	print("smoke: command points from the bar: shown %s, screen %d, '%s'" % [points.visible, native_screen, points.get_node("Fit/Box/Total").text])
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
	# The arrow follows the pointer (owner, 2026-11-12): the second application of an area with
	# two or more, or the first, as if hovered.
	var hovered := 0
	for branch in research.areas_box.find_children("*", "Control", true, false):
		if branch.get_script() == preload("views/research_branch.gd") and branch.rows.size() > 0 and hovered == 0:
			branch.hover(mini(1, branch.rows.size() - 1))
			hovered = branch.rows.size()
			print("smoke: research arrow on hover: %d rows, pointing at %s (gives all %s)" % [branch.rows.size(), branch.pointed, branch.gives_all])
	await get_tree().create_timer(0.3).timeout
	if shots != "":
		await host.shot(shots + "/native-research-hover.png")
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
	# One flyout at a time, over the system pane (owner, 2026-11-12).
	var stack_star := -1
	for st: Dictionary in galaxy.stars:
		if int(st.owner) == int(galaxy.data.player) and st.visited:
			stack_star = int(st.id)
			break
	_on_star_clicked(stack_star)
	await get_tree().create_timer(1.5).timeout
	_on_view_action("research")
	await get_tree().create_timer(1.0).timeout
	_on_view_action("game_menu")
	await get_tree().create_timer(0.8).timeout
	var pane_index: int = _view("system_pane").get_index()
	var menu := _view("game_menu")
	print("smoke: flyouts: game menu shown %s over the system pane %s, research closed %s" % [menu.visible,
		menu.get_index() > pane_index, not research.visible])
	if not menu.visible or menu.get_index() < pane_index or research.visible:
		await host.smoke_fail("flyouts not one at a time over the system pane")
		return
	if shots != "":
		await host.shot(shots + "/native-flyout-over-pane.png")
	menu.close()
	_view("system_pane").close()
	await get_tree().create_timer(1.0).timeout
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
	if not await _smoke_turns():
		return
	# The to-do list (WP3.4d): the turn's items parked instead of shown one by one; TURN reads
	# TO DO (n) and drops the list; each kind opens what it needs.
	if not await _smoke_todo(shots):
		return
	# The turn summary again from the galaxy (WP3.4c): a native popup, then back to the galaxy.
	var summary_visit := int(game.get_native_visit())
	_send({"verb": "open", "screen": 40})
	var summary_wait := 0.0
	var summary_seen := false
	while summary_wait < 20.0 and not (summary_seen and native_screen == Screens.MAIN and int(game.get_native_visit()) > summary_visit):
		if overlay.visible and String(_views["dialog"].popup_name) == "turn_summary":
			summary_seen = true
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		summary_wait += 0.25
	print("smoke: turn summary from the galaxy: shown natively %s, back on the galaxy %s" % [summary_seen, native_screen == Screens.MAIN])
	if "--quit-by-menu" in OS.get_cmdline_user_args():
		# The game menu's Quit (WP3.3f): the game's question, answered Yes natively, then it quits.
		_view("game_menu").get_node("Fit/Box/Quit").pressed.emit()
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
	print("smoke: governor skills %s" % str(governor.get("skills", [])))
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
	# A colony's planet clicked on the map (owner, 2026-11-13: it did nothing): its colony pane.
	for planet: Dictionary in pane.system.get("planets", []):
		var colony_id: int = pane._player_colony(int(planet.planet))
		var world: Vector2 = $Galaxy/Orbits.center_of(int(planet.planet))
		if colony_id < 0 or not world.is_finite():
			continue
		_on_planet_clicked(-1)
		_on_colony_chosen(-1)
		await get_tree().create_timer(0.6).timeout
		var at: Vector2 = galaxy.get_global_transform_with_canvas() * world * get_window().content_scale_factor
		var zoom_before: float = galaxy.camera.zoom.x
		_smoke_mouse(true, at)
		await get_tree().process_frame
		_smoke_mouse(false, at)
		await get_tree().create_timer(1.5).timeout
		var colony_pane: Control = _view("colony_pane")
		# The colony pane only pans the system over (owner, 2026-11-13: the zoom changed).
		var star_x: float = (galaxy.get_global_transform_with_canvas() * galaxy.star_position(galaxy.system_star)).x
		print("smoke: colony pane opened: zoom %.3f -> %.3f, star at x %.0f (pane edge %.0f, view %.0f)" % [zoom_before,
			galaxy.camera.zoom.x, star_x, colony_pane.get_global_rect().end.x, get_viewport().get_visible_rect().size.x])
		if absf(galaxy.camera.zoom.x - zoom_before) > zoom_before * 0.01:
			await host.smoke_fail("the colony pane changed the system's zoom")
			return false
		print("smoke: map click on planet %d: selected %d, colony pane %s on colony %d (expected %d)" % [int(planet.planet),
			galaxy.selected_planet, colony_pane.visible, colony_pane.colony_id, colony_id])
		if colony_pane.colony_id != colony_id:
			await host.smoke_fail("a colony's planet clicked on the map did not open its colony pane")
			return false
		# A right click's help (owner: the colony pane had almost none): the population line.
		var label: Control = colony_pane.population
		var help_at: Vector2 = label.get_global_rect().get_center() * get_window().content_scale_factor
		for pressed in [true, false]:
			var right := InputEventMouseButton.new()
			right.button_index = MOUSE_BUTTON_RIGHT
			right.pressed = pressed
			right.position = help_at
			Input.parse_input_event(right)
			await get_tree().process_frame
		await get_tree().create_timer(0.8).timeout
		var popup: Control = colony_pane._info_popup
		var shown := is_instance_valid(popup) and popup.visible and not popup.is_queued_for_deletion()
		print("smoke: colony help on a right click: shown %s, '%s'" % [shown,
			(popup.find_children("*", "Label", true, false)[0] as Label).text if shown else ""])
		if shots != "":
			await host.shot(shots + "/native-colony-help.png")
		if not shown:
			await host.smoke_fail("no help on a right click in the colony pane")
			return false
		colony_pane._close_info()
		_on_colony_chosen(-1)
		await get_tree().create_timer(0.6).timeout
		break
	if shots != "":
		await host.shot(shots + "/native-system-pane.png")
	# The colony pane (WP3.4): the first of the player's colonies here opens beside the pane.
	if not await _smoke_colony(pane, shots):
		return false
	# The governor opens the colony leaders' flyout (owner, 2026-11-13); a leader picked there
	# governs the system (the pane shows it), and the flyout closes.
	var click := InputEventMouseButton.new()
	click.button_index = MOUSE_BUTTON_LEFT
	click.pressed = true
	pane.governor.gui_input.emit(click)
	await get_tree().create_timer(1.0).timeout
	var flyout: Control = pane._leaders_flyout
	var leader_rows: Array = flyout.find_children("*", "Button", true, false).filter(func(b: Button) -> bool: return b.theme_type_variation == &"ListItem") if flyout else []
	print("smoke: governor flyout: shown %s, %d leaders, screen %d" % [flyout != null and flyout.visible, leader_rows.size(), native_screen])
	if shots != "":
		await host.shot(shots + "/native-governor-flyout.png")
	if flyout == null or not flyout.visible or native_screen != Screens.MAIN:
		await host.smoke_fail("the governor did not open the leaders' flyout")
		return false
	var governor_before := int(pane.system.get("governor", {}).get("leader", -1))
	var pick: Button = null
	for leader_row: Button in leader_rows:
		if not leader_row.disabled:
			pick = leader_row
			break
	if pick:
		pick.pressed.emit()
		await get_tree().create_timer(1.0).timeout
		var after := int(pane.system.get("governor", {}).get("leader", -1))
		print("smoke: governor assigned from the flyout: %d -> %d, flyout closed %s" % [governor_before, after, not flyout.visible])
		if after == governor_before or after < 0 or flyout.visible:
			await host.smoke_fail("the flyout did not assign a governor")
			return false
	else:
		pane.close_leaders()
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
	var controls_top := INF
	for b: Control in _view("map_controls").find_children("*", "Button", true, false):
		if b.is_visible_in_tree():
			controls_top = minf(controls_top, b.get_global_rect().position.y)
	print("smoke: ship designs end at %.0f, map controls start at %.0f" % [designs.get_global_rect().end.y, controls_top])
	if designs.get_global_rect().end.y > controls_top:
		await host.smoke_fail("the design list covers the map controls")
		return false
	if shots != "":
		await host.shot(shots + "/native-designs.png")
	# Edit opens the native Ship Design panel on that slot (WP3.6); Esc cancels it.
	var edit: Button = designs.rows.get_child(0).find_children("*", "Button", true, false)[0]
	edit.pressed.emit()
	await get_tree().create_timer(1.2).timeout
	var editor := _view("design_panel")
	if not editor.visible or editor.design.is_empty():
		await host.smoke_fail("Edit did not open the Ship Design panel")
		return false
	if shots != "":
		await host.shot(shots + "/native-designer.png")
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.pressed = true
	Input.parse_input_event(esc)
	await get_tree().create_timer(0.8).timeout
	print("smoke: designer '%s' opened and cancelled (Esc): panel %s, screen %d" % [editor.design.get("name", ""), editor.visible, native_screen])
	if editor.visible or native_screen != Screens.MAIN:
		await host.smoke_fail("Esc did not cancel the Ship Design panel")
		return false
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
	if not await _smoke_scale_drag(options, shots):
		return false
	if not await _smoke_tips_and_outside_clicks(options, shots):
		return false
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
	# Save and Load in the menu itself (owner, 2026-11-13: they were the legacy popup): saved to
	# slot 5 with a description, then loaded from there; the native galaxy comes back.
	game_menu.get_node("Fit/Box/Save").pressed.emit()
	await get_tree().create_timer(0.8).timeout
	var rows: Array = game_menu._slots_box.get_children().filter(func(c: Node) -> bool: return c is Button and c.has_meta("slot"))
	print("smoke: save page: %d slots shown natively, legacy screen %s" % [rows.size(), native_screen == Screens.LEGACY])
	if rows.size() != 10 or native_screen != Screens.MAIN:
		await host.smoke_fail("Save did not show the slots natively")
		return false
	(rows[4] as Button).pressed.emit()
	game_menu._name_edit.text = "Smoke save"
	if shots != "":
		await host.shot(shots + "/native-save.png")
	game_menu._save()
	await get_tree().create_timer(1.5).timeout
	var saves: Dictionary = await _query("saves")
	var slot5: Dictionary = saves.get("slots", [{}, {}, {}, {}, {}])[4]
	print("smoke: saved to slot 5: '%s' (%s), menu closed %s" % [slot5.get("description", ""), slot5.get("date", ""), not game_menu.visible])
	if String(slot5.get("description", "")) != "Smoke save":
		await host.smoke_fail("save_game did not save to slot 5")
		return false
	_on_view_action("game_menu")
	await get_tree().create_timer(0.6).timeout
	game_menu.get_node("Fit/Box/Load").pressed.emit()
	await get_tree().create_timer(0.8).timeout
	rows = game_menu._slots_box.get_children().filter(func(c: Node) -> bool: return c is Button and c.has_meta("slot"))
	if shots != "":
		await host.shot(shots + "/native-load.png")
	var visit := int(game.get_native_visit())
	(rows[4] as Button).pressed.emit()
	var left := false
	var load_wait := 0.0
	while load_wait < 40.0 and not (left and native_screen == Screens.MAIN and load_wait > 2.0):
		left = left or int(game.get_native_visit()) > visit # the galaxy was left and entered again
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		load_wait += 0.25
	print("smoke: loaded slot 5: left the galaxy %s, back on the native galaxy %s after %.1f s" % [left, native_screen == Screens.MAIN, load_wait])
	if not left or native_screen != Screens.MAIN:
		await host.smoke_fail("load_game did not come back to the native galaxy")
		return false
	await get_tree().create_timer(1.0).timeout
	await get_tree().create_timer(0.5).timeout
	if not await _smoke_fleet(shots):
		return false
	galaxy._view_placed = false
	_send({"verb": "query", "what": "view"}, galaxy.place_view)
	await get_tree().create_timer(0.5).timeout
	return true


## The colony pane (WP3.4): opens for one of the player's colonies, moves a colonist to another
## job and back, buys and un-buys (the game's question answered Yes).
func _smoke_colony(pane: Control, shots: String) -> bool:
	var target := -1
	for planet: Dictionary in pane.system.get("planets", []):
		var c: Dictionary = planet.get("colony", {})
		if c.get("mine", false) and not c.get("outpost", false):
			target = int(planet.planet)
			break
	if target < 0:
		print("smoke: no colony of the player's in this system; colony pane not checked")
		return true
	_on_planet_clicked(target)
	var colony_pane := _view("colony_pane")
	var waited := 0.0
	while (colony_pane.colony.is_empty() or not colony_pane.visible) and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(1.5).timeout
	var c: Dictionary = colony_pane.colony
	if c.is_empty():
		await host.smoke_fail("the colony pane did not open")
		return false
	var counts: Array = c.jobs.map(func(j: Dictionary) -> int: return j.colonists.size())
	print("smoke: colony pane '%s' (%s), jobs %s, build '%s' %s turns, buy %s, defences %s, garrison %d/%d, %d buildings" % [c.name,
		pane.system.get("name", ""), counts, c.build.name, c.build.get("turns", "-"), c.build.get("buy_cost", "-"),
		c.defences.map(func(d: Dictionary) -> String: return "%s%s" % [d.name, "+" if d.built else "-"]),
		int(c.garrison.marines), int(c.garrison.armor), c.buildings.size()])
	if shots != "":
		await host.shot(shots + "/native-colony-pane.png")
	# Colonists on the pointer (the colony screen's pickup): a click takes the clicked one and the
	# identical ones after it; dropped elsewhere they go back; on another row they move, together.
	var farmers: Array = c.jobs[0].colonists.filter(func(p: Dictionary) -> bool: return p.state == "citizen")
	if farmers.size() >= 2:
		var cluster: Array = farmers[farmers.size() - 2].get("cluster", [])
		colony_pane._pick_up(cluster, 0)
		await get_tree().create_timer(0.5).timeout
		if shots != "":
			await host.shot(shots + "/native-colony-carry.png")
		colony_pane._drop_on(-1)
		await get_tree().create_timer(0.8).timeout
		var same: Dictionary = await _query_args({"what": "colony", "colony": int(c.colony)})
		var same_counts: Array = same.jobs.map(func(j: Dictionary) -> int: return j.colonists.size())
		colony_pane._pick_up(cluster, 0)
		colony_pane._drop_on(2)
		var waited_move := 0.0
		var after: Dictionary = {}
		while waited_move < 5.0:
			await get_tree().create_timer(0.3).timeout
			waited_move += 0.3
			after = await _query_args({"what": "colony", "colony": int(c.colony)})
			if int(after.jobs[2].colonists.size()) != int(counts[2]):
				break
		var after_counts: Array = after.jobs.map(func(j: Dictionary) -> int: return j.colonists.size())
		print("smoke: carry %s: dropped outside, jobs %s (unchanged %s); on research, jobs %s" % [cluster, same_counts,
			same_counts == counts, after_counts])
		if same_counts != counts or int(after_counts[2]) != int(counts[2]) + cluster.size():
			await host.smoke_fail("carried colonists did not go back, or did not all move")
			return false
		await _command({"verb": "move_colonist", "colony": int(c.colony), "pops": cluster, "job": 0})
	# A building's info (right click) and VIEW COLONY's landscape alone.
	if not c.buildings.is_empty():
		var rows: Array = colony_pane.find_children("*", "Button", true, false).filter(func(b: Button) -> bool: return b.text == String(c.buildings[0].name))
		if not rows.is_empty():
			colony_pane._show_info(c.buildings[0], rows[0])
			await get_tree().create_timer(0.6).timeout
			if shots != "":
				await host.shot(shots + "/native-building-info.png")
			colony_pane._close_info()
		print("smoke: building '%s' sellable %s refund %s, description %d chars" % [c.buildings[0].name, c.buildings[0].get("sellable"),
			c.buildings[0].get("refund"), String(c.buildings[0].get("description", "")).length()])
	colony_pane._open_landscape()
	var t_land := Time.get_ticks_msec()
	while native_screen != Screens.LEGACY and Time.get_ticks_msec() - t_land < 10000:
		await get_tree().process_frame
	await get_tree().create_timer(1.5).timeout
	if shots != "":
		game.get_frame_image().save_png(shots + "/legacy-landscape.png")
	var leaving := 0.0
	while native_screen != Screens.MAIN and leaving < 10.0:
		game.send_key(KEY_ESCAPE, false, false, false)
		await get_tree().create_timer(1.0).timeout
		leaving += 1.0
	await get_tree().create_timer(1.0).timeout
	print("smoke: landscape view shown and left: back on the galaxy %s, colony pane %s" % [native_screen == Screens.MAIN, colony_pane.visible])
	if native_screen != Screens.MAIN:
		await host.smoke_fail("the landscape view did not close back to the galaxy")
		return false
	# BUY (Yes), then BUY again: un-bought, the BC back.
	if int(c.build.get("buy_cost", 0)) > 0:
		var before: Dictionary = await _query("empire")
		var bought: Dictionary = await _command({"verb": "buy", "colony": int(c.colony)}, true)
		var unbought: Dictionary = await _command({"verb": "buy", "colony": int(c.colony)}, true)
		var after_bc: Dictionary = await _query("empire")
		print("smoke: buy %s (bc %s), buy again %s (bc %s), treasury %d -> %d" % [bought.get("bought", "?"), bought.get("bc", "?"),
			unbought.get("bought", "?"), unbought.get("bc", "?"), int(before.bc), int(after_bc.bc)])
	# The build pane (WP3.4b): CHANGE, add an item, Cancel (nothing changes); again, OK (it's on).
	var before_queue: Array = (await _query_args({"what": "colony", "colony": int(c.colony)})).build.queue
	var build := _view("build_pane")
	_open_build_pane(int(c.colony), false)
	waited = 0.0
	while (build.options.is_empty() or not build.visible) and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(0.8).timeout
	var pick := -1
	for item: Dictionary in build.options.buildings:
		if int(item.id) > 0:
			pick = int(item.id)
			break
	print("smoke: build pane: %d buildings, %d units, queue %s, auto %s; adding %d" % [build.options.buildings.size(),
		build.options.units.size(), build.options.queue.map(func(q: Dictionary) -> String: return q.name), build.options.auto, pick])
	if pick < 0 or build.options.units.is_empty():
		await host.smoke_fail("the build pane offered nothing")
		return false
	# Beside the colony pane at its real width (owner, 2026-11-19: the build list sat over it).
	var cp_right: float = _view("colony_pane").get_global_rect().end.x
	var bp_left: float = build.get_global_rect().position.x
	print("smoke: build pane starts at %.0f, colony pane ends at %.0f (width %.0f)" % [bp_left, cp_right, _view("colony_pane").size.x])
	if bp_left < cp_right:
		await host.smoke_fail("the build list overlaps the colony pane")
		return false
	build._add(pick)
	build._show_detail(build._names.get(pick, {}))
	await get_tree().create_timer(0.8).timeout
	if shots != "":
		await host.shot(shots + "/native-build-pane.png")
		# Trade goods and housing (owner, 2026-11-13: blank, and the dimensional portal).
		for special in [[-2, "trade-goods"], [-3, "housing"]]:
			if build._names.has(special[0]):
				print("smoke: build picture of %s: %s" % [special[1], build._names[special[0]].get("picture", {})])
				build._show_detail(build._names[special[0]])
				await get_tree().create_timer(1.0).timeout
				await host.shot(shots + "/native-build-%s.png" % special[1])
	# The build pane's right-click help (the build screen's list): Repeat.
	var repeat: Control = build.get_node("Box/Columns/Left/Toggles/Repeat")
	var repeat_at := repeat.get_global_rect().get_center() * get_window().content_scale_factor
	for pressed in [true, false]:
		var right := InputEventMouseButton.new()
		right.button_index = MOUSE_BUTTON_RIGHT
		right.pressed = pressed
		right.position = repeat_at
		Input.parse_input_event(right)
		await get_tree().process_frame
	await get_tree().create_timer(0.6).timeout
	var tips := preload("views/info_popup.gd")
	print("smoke: build pane help on Repeat: %s" % ((tips._current.find_children("*", "Label", true, false)[0] as Label).text if tips.is_open() else "none"))
	tips.close_current()
	build.cancel()
	await get_tree().create_timer(0.6).timeout
	var after_cancel: Array = (await _query_args({"what": "colony", "colony": int(c.colony)})).build.queue
	_open_build_pane(int(c.colony), false)
	waited = 0.0
	while not build.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(0.6).timeout
	build._add(pick)
	# A building queued leaves the BUILDINGS list; taken off the queue it comes back (owner,
	# 2026-11-19: the same building could be queued again and again).
	var listed := func() -> bool:
		for b in build.items_box.get_children():
			if b is Button and (b as Button).text == String(build._names.get(pick, {}).get("name", "")):
				return true
		return false
	var hidden_after_add: bool = pick in [build.HOUSING, build.TRADE_GOODS] or not listed.call()
	var queued_slot: int = build._queue.size() - 1
	build._delete(queued_slot)
	var back_after_delete: bool = listed.call()
	print("smoke: build list: building %d hidden once queued %s, back once removed %s" % [pick, hidden_after_add, back_after_delete])
	if not hidden_after_add or not back_after_delete:
		await host.smoke_fail("a queued building stayed in the BUILDINGS list, or did not come back")
		return false
	build._add(pick)
	build._ok()
	waited = 0.0
	while build.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(0.8).timeout
	var after_ok: Array = (await _query_args({"what": "colony", "colony": int(c.colony)})).build.queue
	print("smoke: build list %d items; after Cancel %d (unchanged %s); after OK %d (last '%s'); colony pane shows %d" % [before_queue.size(),
		after_cancel.size(), after_cancel.size() == before_queue.size(), after_ok.size(), after_ok[-1].name if not after_ok.is_empty() else "",
		colony_pane.colony.build.queue.size()])
	# A full list (7, the game's slots) can't take the item: OK leaves it as it was.
	var wanted_after_ok := mini(before_queue.size() + 1, 7)
	if after_cancel.size() != before_queue.size() or after_ok.size() != wanted_after_ok:
		await host.smoke_fail("the build pane's Cancel or OK did not do what it should")
		return false
	# A ship design from Units: at OK the game makes its ship (queued as -100 - ship).
	var design := -1
	for item: Dictionary in build.options.units:
		if int(item.get("id", 0)) <= -50 and int(item.get("id", 0)) > -100:
			design = int(item.id)
			break
	if design != -1:
		_open_build_pane(int(c.colony), false)
		waited = 0.0
		while not build.visible and waited < 5.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		await get_tree().create_timer(0.6).timeout
		build._add(design)
		build._ok()
		waited = 0.0
		while build.visible and waited < 5.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		await get_tree().create_timer(0.6).timeout
		var with_ship: Array = (await _query_args({"what": "colony", "colony": int(c.colony)})).build.queue
		print("smoke: build design %d: queue %s" % [design, with_ship.map(func(q: Dictionary) -> String: return "%s (%d)" % [q.name, int(q.id)])])
		if with_ship.is_empty() or int(with_ship[-1].id) > -100:
			await host.smoke_fail("a design added in the build pane did not become a queued ship")
			return false
	# A drop between items (WP3.4b review): the item lands at that gap; then the Refit tab.
	_open_build_pane(int(c.colony), false)
	waited = 0.0
	while not build.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(0.6).timeout
	var first_gap := 1 if build.bought else 0
	var before_drop: Array = build._queue.map(func(q: Dictionary) -> int: return int(q.id))
	var drop_pick := -1
	for item: Dictionary in build.options.units:
		if item.has("id") and int(item.id) > -50 and int(item.id) != pick:
			drop_pick = int(item.id) # a unit that isn't a design (no ship made)
			break
	print("smoke: build pane %s wide (min %s): header %s, columns %s (left %s, right %s)" % [build.size.x,
		build.get_combined_minimum_size().x, build.get_node("Box/Header").get_combined_minimum_size().x,
		build.get_node("Box/Columns").get_combined_minimum_size().x, build.get_node("Box/Columns/Left").get_combined_minimum_size().x,
		build.get_node("Box/Columns/Right").get_combined_minimum_size().x])
	if drop_pick != -1 and build._queue.size() < build.SLOTS:
		build._add(drop_pick, first_gap)
	print("smoke: build drop %d at gap %d: %s -> %s" % [drop_pick, first_gap, before_drop, build._queue.map(func(q: Dictionary) -> int: return int(q.id))])
	build._show_detail(build._names.get(drop_pick, {}))
	await get_tree().create_timer(0.6).timeout
	if shots != "":
		await host.shot(shots + "/native-build-drop.png")
	var refit: Dictionary = build.options.get("refit", {})
	print("smoke: refit available %s, %d ships, targets %s" % [refit.get("available", "?"), refit.get("ships", []).size(),
		refit.get("ships", []).map(func(sh: Dictionary) -> int: return sh.get("targets", []).size())])
	build.tabs_refit.button_pressed = true
	await get_tree().create_timer(0.6).timeout
	if shots != "":
		await host.shot(shots + "/native-build-refit.png")
	build._ok()
	waited = 0.0
	while build.visible and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(0.6).timeout
	var after_drop: Array = (await _query_args({"what": "colony", "colony": int(c.colony)})).build.queue
	print("smoke: after the drop's OK: %s" % [after_drop.map(func(q: Dictionary) -> int: return int(q.id))])
	if drop_pick != -1 and (after_drop.size() <= first_gap or int(after_drop[first_gap].id) != drop_pick):
		await host.smoke_fail("an item dropped between others did not land there")
		return false
	# A refit taken off again before OK (the fix to the original): the ship is still there, as it was.
	var refit_colony := -1
	var refit_ship := {}
	for col_id in 250:
		var o: Dictionary = await _query_args({"what": "build_options", "colony": col_id})
		if not o.get("ok", false):
			continue
		for sh: Dictionary in o.get("refit", {}).get("ships", []):
			if not sh.get("targets", []).is_empty():
				refit_colony = int(o.colony)
				refit_ship = sh
				break
		if refit_colony >= 0:
			break
	if refit_colony < 0:
		print("smoke: refit: no colony has a warship in orbit with a design to refit to")
	else:
		_open_build_pane(refit_colony, false)
		waited = 0.0
		while (not build.visible or int(build.options.get("colony", -1)) != refit_colony) and waited < 5.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		await get_tree().create_timer(0.6).timeout
		build.tabs_refit.button_pressed = true
		build._open_refit_popup(refit_ship, build.items_box, -1)
		await get_tree().create_timer(0.6).timeout
		if shots != "":
			await host.shot(shots + "/native-build-refit-popup.png")
		build._refit(refit_ship, refit_ship.targets[0])
		var refit_slot: int = build._queue.size() - 1
		build._delete(refit_slot)
		build._ok()
		waited = 0.0
		while build.visible and waited < 5.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		var again: Dictionary = await _query_args({"what": "build_options", "colony": refit_colony})
		var back := false
		for sh: Dictionary in again.get("refit", {}).get("ships", []):
			if int(sh.ship) == int(refit_ship.ship) and String(sh.name) == String(refit_ship.name):
				back = true
		print("smoke: refit %s (%d) to %s, taken off, OK: ship back as it was %s" % [refit_ship.name, int(refit_ship.ship),
			refit_ship.targets[0].name, back])
		if not back:
			await host.smoke_fail("a refit taken off the list lost its ship")
			return false
	await get_tree().create_timer(0.8).timeout
	return true


## Sends a command and waits for its result; `answer` says Yes to the game's question meanwhile.
func _command(dict: Dictionary, answer := false) -> Dictionary:
	var box := {}
	_send(dict, func(result: Dictionary): box["r"] = result)
	var waited := 0.0
	while not box.has("r") and waited < 10.0:
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
		if answer:
			_smoke_answer()
	return box.get("r", {})


func _query_args(args: Dictionary) -> Dictionary:
	var dict := {"verb": "query"}
	dict.merge(args)
	return await _command(dict)


## The bar's tax menu (WP3.3d): another rate is checked in the treasury's menu, then the old one
## goes back. (The bar doesn't show the rate, WP3.3g.)
func _smoke_tax(old_rate: int) -> bool:
	# A click on the treasury drops the rates down (owner, 2026-11-13); a rate is set and the
	# drop-down goes back up; the bar shows the new rate.
	var bar := _view("galaxy_bar")
	var rate := 30 if old_rate != 30 else 40
	bar.money.pressed.emit()
	await get_tree().create_timer(0.6).timeout
	var tax := _view("tax_panel")
	var shown: bool = tax.visible
	for b: Button in tax.rates.get_children():
		if int(b.get_meta("rate")) == rate:
			b.pressed.emit()
	var waited := 0.0
	while int(bar.tax_rate) != rate and waited < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	print("smoke: tax drop-down shown %s; bar after tax %d%%: '%s' | '%s' | '%s' | '%s'" % [shown, rate, bar.money.text, bar.command_label.text, bar.research.text, bar.freighters_label.text])
	if not shown or int(bar.tax_rate) != rate:
		await host.smoke_fail("the tax rate did not change from the treasury's drop-down")
		return false
	_send({"verb": "set_tax_rate", "rate": old_rate})
	await get_tree().create_timer(0.5).timeout
	# The freighters (owner, 2026-11-13): the game's scrap step with its slider, native; Cancel.
	var visit := int(game.get_native_visit())
	bar.freighters_label.pressed.emit()
	var seen := false
	var behind := false
	var waited_scrap := 0.0
	while waited_scrap < 15.0 and not (seen and native_screen == Screens.MAIN and int(game.get_native_visit()) > visit):
		if overlay.visible and String(_views["dialog"].popup_name) == "slider":
			seen = true
			behind = galaxy.visible and not legacy.visible
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		waited_scrap += 0.25
	print("smoke: scrap freighters: slider shown natively %s, galaxy behind %s, back on the galaxy %s" % [seen, behind, native_screen == Screens.MAIN])
	if not seen or native_screen != Screens.MAIN:
		await host.smoke_fail("the freighters' slider did not show natively")
		return false
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


## `--new-game` (no save loaded): the new game flow through the legacy menus (the render suite's
## clicks), then the first turn's home star naming must come as a native "text" question over the
## native galaxy, not the legacy one (owner, 2026-11-12); the typed name lands on the star.
func _smoke_new_game(shots: String) -> void:
	await get_tree().create_timer(12.0).timeout # the main menu after -skip-intro
	for step in [[Vector2(527, 229), 4.0], [Vector2(470, 400), 6.0], [Vector2(410, 352), 4.0]]:
		await host.click_game(step[0])
		await get_tree().create_timer(step[1]).timeout
	# The ruler's name (a native "text" question over the new game screen: accepted as offered),
	# the banner (Return), then the home star's name over the native galaxy.
	var dialog: Control = null
	var waited := 0.0
	var next_key := 3.0
	while waited < 90.0:
		dialog = _views.get("dialog")
		if dialog != null and dialog.visible and dialog._kind == "text":
			if native_screen == Screens.MAIN:
				break
			print("smoke: new game question '%s' answered with '%s'" % [dialog.get_node("Fit/Box/Text").text, dialog.get_node("Fit/Box/Entry").text])
			if shots != "":
				await host.shot(shots + "/native-ruler-name.png")
			dialog._answer(1)
		elif native_screen != Screens.MAIN and waited >= next_key:
			game.send_key(KEY_ENTER, false, false, false) # the banner
			next_key = waited + 3.0
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	if dialog == null or not dialog.visible or dialog._kind != "text":
		if shots != "":
			game.get_frame_image().save_png(shots + "/new-game-stuck.png")
		await host.smoke_fail("no native home star question (screen %d)" % native_screen)
		return
	await get_tree().create_timer(1.0).timeout
	print("smoke: home star question '%s', value '%s', native galaxy %s, behind it the native view %s" % [dialog.get_node("Fit/Box/Text").text,
		dialog.get_node("Fit/Box/Entry").text, native_screen == Screens.MAIN, galaxy.visible])
	if shots != "":
		await host.shot(shots + "/native-home-star-name.png")
	var home := String(dialog.get_node("Fit/Box/Entry").text)
	dialog.get_node("Fit/Box/Entry").text = "Smoke Prime"
	dialog._answer(1)
	await get_tree().create_timer(3.0).timeout
	# The map's labels (cut to the width the map leaves the name, as the input box would).
	var text := JSON.stringify(await _query("galaxy"))
	var at := text.find("\"Smoke")
	var named := at >= 0
	print("smoke: home star '%s' renamed: %s (%s)" % [home, named, text.substr(at, 16) if named else "-"])
	if shots != "":
		await host.shot(shots + "/native-home-star-named.png")
	if not named or native_screen != Screens.MAIN:
		await host.smoke_fail("the home star was not renamed natively")
		return
	if "--auto-turns" in OS.get_cmdline_user_args():
		await _smoke_auto_turns(shots)
		return
	# Research on the first turn: hovering an application points the arrow at it, or at all of the
	# field's applications where every race gets them all.
	_on_view_action("research")
	await get_tree().create_timer(1.5).timeout
	var research := _view("research_panel")
	var shown := 0
	for branch in research.areas_box.find_children("*", "Control", true, false):
		if branch.get_script() != preload("views/research_branch.gd") or branch.rows.size() < 2:
			continue
		branch.hover(1)
		print("smoke: new game research hover: %d rows, gives all %s, arrow at %s" % [branch.rows.size(), branch.gives_all, branch.pointed])
		shown += 1
	await get_tree().create_timer(0.3).timeout
	if shots != "":
		await host.shot(shots + "/native-research-hover.png")
	# A real click on a give-all field's second application (owner, 2026-11-13: while the button
	# was held the arrow fell back to one, and picking left the drop-down open).
	for branch in research.areas_box.find_children("*", "Control", true, false):
		if branch.get_script() != preload("views/research_branch.gd") or not branch.gives_all or branch.rows.size() < 2:
			continue
		var factor := get_window().content_scale_factor
		var click_at: Vector2 = (branch.rows[1] as Control).get_global_rect().get_center() * factor
		var motion := InputEventMouseMotion.new()
		motion.position = click_at
		Input.parse_input_event(motion)
		await get_tree().create_timer(0.3).timeout
		var hovered: Array = branch.pointed.duplicate()
		_smoke_mouse(true, click_at)
		await get_tree().create_timer(0.3).timeout
		print("smoke: research click: hovered %s, held %s" % [hovered, branch.pointed])
		if shots != "":
			await host.shot(shots + "/native-research-held.png")
		_smoke_mouse(false, click_at)
		await get_tree().create_timer(1.0).timeout
		print("smoke: research picked: drop-down still shown %s" % (research.visible and not research._closing))
		break
	research.close()
	# Ship designs pre-warp (owner, 2026-11-13): the flyout shows none until the build list
	# would; their art in the player's colour, not red.
	var designs: Dictionary = await _query("designs")
	var buildable: Array = designs.get("designs", []).map(func(d: Dictionary) -> bool: return d.get("buildable", false))
	print("smoke: new game designs: colour %d, buildable %s" % [int(designs.get("color", -1)), buildable])
	_on_view_action("designs")
	await get_tree().create_timer(1.0).timeout
	var listed: int = _view("design_list").rows.get_children().filter(func(c: Node) -> bool: return c is PanelContainer).size()
	print("smoke: design list shows %d designs" % listed)
	if shots != "":
		await host.shot(shots + "/native-designs-prewarp.png")
		if not designs.get("designs", []).is_empty():
			var picture := int(designs.designs[0].picture)
			art.sprite("SHIPS.LBX", picture)
			await get_tree().create_timer(2.0).timeout
			var tex: Texture2D = art.texture("SHIPS.LBX", picture)
			if tex:
				tex.get_image().save_png(shots + "/ship-%d.png" % picture)
				print("smoke: saved ship art %d" % picture)
	_view("design_list").close()
	game.request_game_quit()
	print("smoke: asked the game to quit")


## Quiet turns passing by themselves (`--new-game --auto-turns`): End Of Turn Wait off, a turn
## ended; each turn's notice must show over the native galaxy, and a click stops them.
func _smoke_auto_turns(shots: String) -> void:
	var opts: Dictionary = await _query("options")
	for option: Dictionary in opts.get("options", []):
		if int(option.option) in [0, 1] and option.on:
			_send({"verb": "toggle_option", "option": int(option.option)}) # summary and wait off
			await get_tree().create_timer(0.3).timeout
			_smoke_answer()
	# Trade goods at the home colony, so its queue never runs dry (that opens the colony screen).
	for colony_id in 16:
		var colony: Dictionary = await _query_args({"what": "colony", "colony": colony_id})
		if colony.get("ok", false):
			_send({"verb": "build_commit", "colony": colony_id, "edits": [{"add": -2, "repeat": false}], "auto": false})
			await get_tree().create_timer(0.3).timeout
			_smoke_answer()
			break
	# A long research, so the next turns ask nothing.
	var research: Dictionary = await _query("research")
	var longest := {}
	for area: Dictionary in research.get("areas", []):
		if not area.get("apps", []).is_empty() and int(area.get("cost", 0)) > int(longest.get("cost", 0)):
			longest = area
	if not longest.is_empty():
		_send({"verb": "choose_research", "field": int(longest.field), "app": int(longest.apps[0].app)})
	await get_tree().create_timer(0.5).timeout
	var notices: Array[String] = []
	var seen := func(text: String) -> void:
		if text != "":
			notices.append(text)
		print("smoke: turn notice '%s': native galaxy %s, legacy %s" % [text, galaxy.visible, legacy.visible])
	game.turn_notice.connect(seen)
	await _smoke_clear_todo()
	_send({"verb": "end_turn"})
	var waited := 0.0
	while notices.size() < 3 and waited < 60.0:
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	if shots != "":
		await host.shot(shots + "/native-auto-turn.png")
	print("smoke: auto turns: %d notices, galaxy shown %s, legacy shown %s" % [notices.size(), galaxy.visible, legacy.visible])
	var shown_ok := galaxy.visible and not legacy.visible
	_smoke_mouse(true, get_viewport().get_visible_rect().size * 0.5 * get_window().content_scale_factor)
	_smoke_mouse(false, get_viewport().get_visible_rect().size * 0.5 * get_window().content_scale_factor)
	waited = 0.0
	var count_at_click := notices.size()
	while native_screen != Screens.MAIN and waited < 30.0 and not (legacy.visible and waited > 3.0):
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	# Stopped: back on the native galaxy, or a legacy popup of the last turn (a leader for hire)
	# with the legacy view up for it; no more notices either way.
	var stopped := notices.size() <= count_at_click + 2 and (native_screen == Screens.MAIN or legacy.visible)
	print("smoke: auto turns stopped by a click: %s, native galaxy %s, legacy view %s after %.1f s" % [stopped,
		native_screen == Screens.MAIN, legacy.visible, waited])
	if shots != "":
		var frame: Image = game.get_frame_image()
		if frame:
			frame.save_png(shots + "/legacy-after-auto-turns.png")
	if notices.size() < 2 or not shown_ok or not stopped:
		await host.smoke_fail("quiet turns did not pass over the native galaxy")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## `--popups` (WP3.4c): each legacy window over the galaxy previewed natively (`preview_popup`):
## a system with free planets for the planet pickers, one with another empire's colony for the
## attack choice and the invade/bomb window, the player's home system for a discovery and the
## monster's demand. Each is shot (`--shots`), then answered: the pickers by a planet clicked on
## the zoomed map (the answer comes back as that planet, nothing is colonised), the others by
## their first button.
func _smoke_popups(shots: String) -> void:
	var free_star := -1
	var enemy_star := -1
	var enemy_colony := -1
	var free_planet := -1
	var own_star := -1
	for star: Dictionary in galaxy.stars:
		var sys: Dictionary = await _query_args({"what": "system", "star": int(star.id)})
		if String(sys.get("view", "")) != "full":
			continue
		for planet: Dictionary in sys.get("planets", []):
			var colony: Dictionary = planet.get("colony", {})
			if colony.is_empty() and String(planet.get("type", "")) == "planet" and free_star < 0:
				free_star = int(star.id)
				free_planet = int(planet.planet)
			elif not colony.is_empty() and not colony.get("mine", false) and enemy_star < 0:
				enemy_star = int(star.id)
				enemy_colony = int(colony.id)
			elif not colony.is_empty() and colony.get("mine", false) and own_star < 0:
				own_star = int(star.id)
		if free_star >= 0 and enemy_star >= 0 and own_star >= 0:
			break
	print("smoke: popups: free planet %d at star %d, enemy colony %d at star %d, own star %d" % [free_planet, free_star, enemy_colony, enemy_star, own_star])
	var cases: Array = []
	if own_star >= 0:
		cases.append({"popup": "new_system", "star": own_star, "special": 3}) # a pirate cache: its text shows
		cases.append({"popup": "monster_tribute", "star": own_star})
		if free_star >= 0 and free_star != own_star:
			cases.append({"popup": "monster_tribute", "star": own_star, "pick": free_star}) # PAY, then a star
	if free_star >= 0:
		cases.append({"popup": "planet_pick", "star": free_star, "click": free_planet})
		cases.append({"popup": "outpost_pick", "star": free_star, "click": free_planet})
	cases.append({"popup": "choose"})
	if enemy_star >= 0:
		cases.append({"popup": "attack_target", "star": enemy_star})
		cases.append({"popup": "colony_combat", "colony": enemy_colony})
	var failed := not await _smoke_scene(shots)
	if not await _smoke_diplomacy(shots):
		failed = true
	if not await _smoke_research(shots):
		failed = true
	for case: Dictionary in cases:
		var box := {}
		var verb := {"verb": "preview_popup"}
		verb.merge(case)
		verb.erase("click")
		verb.erase("pick")
		_send(verb, func(r: Dictionary): box["r"] = r)
		var waited := 0.0
		while not overlay.visible and waited < 10.0:
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
		await get_tree().create_timer(1.2).timeout # the camera's flight
		var dialog: Control = _views.get("dialog")
		var shown := overlay.visible and dialog != null and dialog.visible
		print("smoke: popup %s: shown %s as '%s', zoom %d, placement '%s', %d options" % [case.popup, shown,
			dialog.popup_name if dialog else "", _popup_zoom, dialog.placement if dialog else "", dialog.options.size() if dialog else 0])
		if shots != "":
			await host.shot(shots + "/native-popup-preview-%s.png" % case.popup)
		if not shown:
			failed = true
			continue
		if case.popup == "attack_target":
			# Owner, 2026-11-14: the window follows a resize (it fell off the screen), and a row's
			# hover lights its planet on the map and the map's lights its row.
			get_window().mode = Window.MODE_WINDOWED
			get_window().size = Vector2i(1700, 1000)
			await get_tree().create_timer(0.4).timeout
			get_window().size = Vector2i(1100, 760)
			await get_tree().create_timer(0.4).timeout
			var view := get_viewport().get_visible_rect().size
			var inside := Rect2(Vector2.ZERO, view).encloses(dialog.get_global_rect())
			var linked := false
			if not dialog._planet_rows.is_empty():
				var planet_id: int = dialog._planet_rows.keys()[0]
				(dialog._planet_rows[planet_id] as Button).mouse_entered.emit()
				var map_lit: bool = galaxy.hovered_planet == planet_id
				_on_planet_hovered(planet_id)
				var row_lit: bool = (dialog._planet_rows[planet_id] as Button).has_theme_color_override("font_color")
				linked = map_lit and row_lit
			print("smoke: attack_target after a resize to %s: window %s inside %s; hover linked both ways %s" % [view, dialog.get_global_rect(), inside, linked])
			if shots != "":
				await host.shot(shots + "/native-popup-attack-resized.png")
			if not inside or not linked:
				failed = true
		var expected := -1
		if case.has("pick"):
			(dialog._popup_buttons.get_child(1) as Button).pressed.emit() # PAY
			await get_tree().create_timer(0.8).timeout
			print("smoke: popup monster_target: shown %s, pick a star %s, placement '%s'" % [overlay.visible, _popup_pick_star, dialog.placement])
			if shots != "":
				await host.shot(shots + "/native-popup-preview-monster_target.png")
			expected = int(case.pick)
			_on_star_clicked(expected) # as the map's click on that star
		elif case.has("click") and _popup_zoom >= 0:
			expected = int(case.click)
			_on_planet_clicked(expected) # as the map's click on that planet
			# A colony ship's pick asks to confirm (native too): Yes.
			await get_tree().create_timer(0.5).timeout
			while overlay.visible and (_views.dialog._kind == "confirm" or _views.dialog.popup_name == "colony_confirm"):
				if shots != "" and _views.dialog.popup_name == "colony_confirm":
					await host.shot(shots + "/native-popup-preview-colony_confirm.png")
				_views.dialog._answer(0 if _views.dialog.popup_name == "colony_confirm" else 1) # YES
				await get_tree().create_timer(0.5).timeout
			# The first colony at a star may ask for its name: keep it.
			while overlay.visible and _views.dialog._kind == "text":
				_views.dialog._answer(0)
				await get_tree().create_timer(0.5).timeout
		else:
			var buttons: Control = dialog._popup_buttons
			if buttons.get_child_count() > 0:
				(buttons.get_child(0) as Button).pressed.emit()
		waited = 0.0
		while not box.has("r") and waited < 10.0:
			if overlay.visible and _views.dialog.visible:
				_views.dialog._answer(-1) # anything that follows (a help line): closed
			await get_tree().create_timer(0.2).timeout
			waited += 0.2
		var result: Dictionary = box.get("r", {})
		print("smoke: popup %s answered %s" % [case.popup, result])
		if not result.get("ok", false) or (expected >= 0 and int(result.get("answer", -1)) != expected):
			failed = true
		await get_tree().create_timer(1.0).timeout # the flight back
	if failed or cases.is_empty():
		await host.smoke_fail("a native popup did not show or answer")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## Ends `--turns=N` turns (default 1), answering what they ask; false when the galaxy did not come back.
func _smoke_turns() -> bool:
	var empire := {}
	var turns := 1
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--turns="):
			turns = int(arg.substr(8))
	for turn in turns:
		if not await _smoke_clear_todo():
			return false
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
			return false
		empire = await _query("empire")
		print("smoke: after turn %d (%.0f s): %s" % [turn + 1, waited, empire])
	return true


func _smoke_todo(shots: String) -> bool:
	var summary_panel := _view("summary_panel")
	var summary: Dictionary = await _query("turn_summary")
	if not bool(summary.get("on", false)):
		await _command({"verb": "toggle_option", "option": 0}, true) # End of Turn Summary on
		await _query("options")
		await get_tree().create_timer(0.5).timeout
		summary = await _query("turn_summary")
	print("smoke: turn summary: option %s, %d messages, panel shown %s ('%s')" % [summary.get("on", "?"), summary.get("items", []).size(),
		summary_panel.visible, summary_panel.button.text])
	if summary_panel.visible:
		summary_panel.button.pressed.emit()
		await get_tree().create_timer(0.6).timeout
		if shots != "":
			await host.shot(shots + "/native-summary.png")
		summary_panel.button.pressed.emit()
	var todo: Dictionary = await _query("todo")
	var kinds := {}
	for item: Dictionary in todo.get("items", []):
		kinds[String(item.kind)] = int(kinds.get(String(item.kind), 0)) + 1
	var bar_text := String(_view("galaxy_bar").get_node("Row/Turn").text)
	print("smoke: to-do after the turns: %d open, %d builds, kinds %s, bar '%s'" % [int(todo.get("open", 0)), int(todo.get("builds", 0)), kinds, bar_text])
	for item: Dictionary in todo.get("items", []):
		print("smoke: to-do item: %s '%s' done %s" % [item.kind, item.text, item.done])
	if int(todo.get("open", 0)) > 0 and not bar_text.begins_with("TO DO"):
		await host.smoke_fail("TURN did not read TO DO (n)")
		return false
	if int(todo.get("open", 0)) == 0:
		return true
	# The list opens by itself (owner, 2026-11-14); TO DO closes and reopens it.
	var panel := _view("todo_panel")
	var waited_open := 0.0
	while not panel.visible and waited_open < 4.0:
		await get_tree().create_timer(0.1).timeout
		waited_open += 0.1
	var opened_itself: bool = panel.visible
	_on_view_action("turn")
	await get_tree().create_timer(0.5).timeout
	var closed_by_bar: bool = not panel.visible
	_on_view_action("turn")
	await get_tree().create_timer(0.8).timeout
	print("smoke: to-do list opened by itself %s, TO DO closes it %s, reopens %s with %d rows" % [opened_itself,
		closed_by_bar, panel.visible, panel.items_box.get_child_count()])
	if shots != "":
		await host.shot(shots + "/native-todo.png")
	if not opened_itself or not closed_by_bar or not panel.visible:
		await host.smoke_fail("the to-do list did not open by itself, or TO DO did not toggle it")
		return false
	# Nothing may be ignored: the game refuses the turn while items are open.
	var refused := {}
	_send({"verb": "end_turn"}, func(result: Dictionary): refused["r"] = result)
	var waited_refusal := 0.0
	while not refused.has("r") and waited_refusal < 5.0:
		await get_tree().create_timer(0.1).timeout
		waited_refusal += 0.1
	var refusal: Dictionary = refused.get("r", {})
	print("smoke: end_turn with items open answered %s" % refusal)
	if refusal.get("ok", true) or String(refusal.get("error", "")) != "refused":
		await host.smoke_fail("the turn could end with to-do items open")
		return false
	# A news item: read once (it drops off), its system opened.
	for item: Dictionary in todo.get("items", []):
		if bool(item.news) and not bool(item.done) and String(item.kind) != "summary":
			_on_todo_item(item)
			await get_tree().create_timer(1.5).timeout
			# A discovery with a special or a leader asks its window first (shot, then OK).
			for step in 6:
				if not overlay.visible:
					break
				print("smoke: to-do news window '%s': %s" % [_views.dialog.popup_name, _views.dialog.get_node("Fit/Box/Text").text])
				if step == 0:
					await host.shot(shots + "/native-todo-discovery.png")
				_smoke_answer()
				await get_tree().create_timer(0.8).timeout
			var again: Dictionary = await _query("todo")
			var still := false
			for other: Dictionary in again.get("items", []):
				if int(other.item) == int(item.item) and not bool(other.done):
					still = true
			print("smoke: to-do news '%s' read: dropped off %s, system pane %s" % [item.text, not still, _view("system_pane").visible])
			if still:
				await host.smoke_fail("a read news item stayed on the list")
				return false
			_view("system_pane").close()
			await get_tree().create_timer(0.8).timeout
			break
	# A colony with nothing to build: the colony pane and its build list open.
	for item: Dictionary in todo.get("items", []):
		if String(item.kind) == "build":
			_on_todo_item(item)
			await get_tree().create_timer(2.0).timeout
			print("smoke: to-do build for colony %d: colony pane %s on %d, build pane %s" % [int(item.colony), _view("colony_pane").visible,
				int(_view("colony_pane").colony_id), _view("build_pane").visible])
			if shots != "":
				await host.shot(shots + "/native-todo-build.png")
			# A planet's right click card (planet_card.gd) from the system pane.
			var sp := _view("system_pane")
			for planet: Dictionary in sp.system.get("planets", []):
				if String(planet.type) == "planet":
					sp.show_planet_card(int(planet.planet), sp.planet_row_rect(int(planet.planet)))
					await get_tree().create_timer(1.5).timeout
					print("smoke: planet card for %d shown %s" % [int(planet.planet), preload("views/info_popup.gd").is_open()])
					if shots != "":
						await host.shot(shots + "/native-planet-card.png")
					preload("views/info_popup.gd").close_current()
					break
			_view("build_pane").cancel()
			_view("system_pane").close()
			await get_tree().create_timer(0.8).timeout
			break
	# A colony ship that arrived: the chain's prompt for that star (native popups: a preselected
	# planet's Yes/No, or the planet picker zoomed into the system), answered to colonise.
	for item: Dictionary in todo.get("items", []):
		if String(item.kind) == "colonize" and not bool(item.done):
			# Another system open first: its pane must go when the picker flies elsewhere (owner,
			# 2026-11-14).
			for other: Dictionary in galaxy.stars:
				if int(other.id) != int(item.get("star", -1)) and not bool(other.get("black_hole", false)):
					_open_system(int(other.id))
					await get_tree().create_timer(2.0).timeout
					print("smoke: to-do colonize from another system: pane on %d (item's star %d)" % [int(_view("system_pane").star), int(item.get("star", -1))])
					break
			_on_todo_item(item)
			var waited := 0.0
			var asked: Array = []
			while waited < 15.0:
				await get_tree().create_timer(0.4).timeout
				waited += 0.4
				if not overlay.visible:
					if not asked.is_empty() and waited > 3.0:
						break
					continue
				if _scene_window != null and _scene_window.visible:
					# The landing in the scene window (WP3.11a): the pods land, the planet's data
					# follows, a click ends it.
					if not "scene/landing" in asked:
						asked.append("scene/landing")
						await get_tree().create_timer(5.0).timeout
						print("smoke: landing scene '%s', title '%s', text '%s', intro frame %d, text shown %s" % [_scene_window._state.get("scene", ""),
							_scene_window._title.text, _scene_window._text.text.replace("\n", " | "), _scene_window._intro_frame, _scene_window._text.visible])
						print("smoke: landing ground %s: visible %s, texture %s, rect %s; actor texture %s rect %s" % [_scene_window._state.get("ground", {}),
							_scene_window._ground.visible, _scene_window._ground.texture, Rect2(_scene_window._ground.position, _scene_window._ground.size),
							_scene_window._actor.texture, Rect2(_scene_window._actor.position, _scene_window._actor.size)])
						if shots != "":
							await host.shot(shots + "/native-landing.png")
							for part in [["ground", _scene_window._ground], ["actor", _scene_window._actor], ["backdrop", _scene_window._backdrop]]:
								if part[1].texture != null:
									part[1].texture.get_image().save_png(shots + "/landing-%s.png" % part[0])
					_scene_window._frame.gui_input.emit(_smoke_left_click())
					continue
				var dialog: Control = _views["dialog"]
				asked.append("%s/%s" % [dialog._kind, dialog.popup_name])
				if asked.size() == 1:
					await get_tree().create_timer(1.0).timeout
					print("smoke: to-do colonize asked: system pane visible %s on %d, galaxy system %d" % [_view("system_pane").visible,
						int(_view("system_pane").star), galaxy.system_star])
					# The map shows the asking system, not every star dimmed (owner, 2026-11-19: from
					# another system's pane the galaxy flew there but never brightened the new star).
					if galaxy.system_star != int(item.get("star", -1)):
						await host.smoke_fail("the question's system is not the one shown on the map")
						return false
				if shots != "" and asked.size() == 1:
					await host.shot(shots + "/native-todo-colonize.png")
				if dialog._kind == "popup" and dialog.options.size() > 0:
					for i in dialog.options.size():
						if bool(dialog.options[i].get("enabled", true)):
							dialog._answer(dialog.OPTION_ANSWER + i)
							break
				elif dialog._kind == "popup" and dialog.popup_name == "colony_confirm":
					if shots != "":
						await get_tree().create_timer(1.5).timeout # the flight into the system
						await host.shot(shots + "/native-todo-colony-confirm.png")
					dialog._answer(0) # YES
				elif dialog._kind == "text":
					dialog._answer(0) # keep the star's name
				else:
					dialog._answer(1) # Yes / OK
			# The landing (legacy, with animations on) plays, then the galaxy comes back.
			var back := 0.0
			while native_screen != Screens.MAIN and back < 40.0:
				await get_tree().create_timer(1.0).timeout
				back += 1.0
				# The landing waits for a click (owner, 2026-11-14): a key ends it.
				if not _smoke_answer() and native_screen != Screens.MAIN and back >= 6.0 and int(back) % 3 == 0:
					print("smoke: ending the landing (screen %d) with a click" % native_screen)
					_smoke_click(get_viewport().get_visible_rect().size * 0.5)
			await get_tree().create_timer(1.0).timeout
			var again: Dictionary = await _query("todo")
			var done := false
			for other: Dictionary in again.get("items", []):
				if int(other.item) == int(item.item):
					done = bool(other.done)
			await get_tree().create_timer(1.5).timeout
			print("smoke: to-do colonize '%s': asked %s, done %s, galaxy back after %.0f s, colony pane %s on %d" % [item.text, asked, done, back,
				_view("colony_pane").visible, int(_view("colony_pane").colony_id)])
			if shots != "":
				await host.shot(shots + "/native-todo-colonized.png")
			_view("system_pane").close()
			await get_tree().create_timer(0.8).timeout
			if not done:
				await host.smoke_fail("colonising from the to-do list left the item open")
				return false
			break
	# A leader's offer: the native leader_hire popup, answered REJECT.
	for item: Dictionary in todo.get("items", []):
		if String(item.kind) == "hire" and not bool(item.done):
			_on_todo_item(item)
			var waited := 0.0
			while not overlay.visible and waited < 5.0:
				await get_tree().create_timer(0.2).timeout
				waited += 0.2
			print("smoke: to-do hire: popup '%s' shown %s" % [_views["dialog"].popup_name if _views.has("dialog") else "", overlay.visible])
			if overlay.visible:
				(_views["dialog"]._popup_buttons.get_child(0) as Button).pressed.emit()
			await get_tree().create_timer(1.0).timeout
			break
	# The rest answered (news read, empty queues given trade goods, ships' prompts cancelled):
	# then the bar reads TURN again and END TURN unlocks.
	if not await _smoke_clear_todo():
		return false
	await get_tree().create_timer(0.5).timeout
	bar_text = String(_view("galaxy_bar").get_node("Row/Turn").text)
	print("smoke: to-do worked through: bar '%s', list hidden %s" % [bar_text, not _view("todo_panel").visible])
	if bar_text != "TURN" or _view("todo_panel").visible:
		await host.smoke_fail("the bar did not read TURN, or the list stayed open, with the list worked through")
		return false
	return true


## Answers every open to-do item as a quick player would (news read, trade goods for an empty
## queue, a colony ship's prompt or a leader's offer cancelled/declined: answers too); false when
## the list does not empty.
func _smoke_clear_todo() -> bool:
	for attempt in 40:
		var todo: Dictionary = await _query("todo")
		if int(todo.get("open", 0)) == 0:
			if attempt > 0:
				print("smoke: to-do list worked through in %d steps" % attempt)
			return true
		var item := {}
		for it: Dictionary in todo.get("items", []):
			if not bool(it.get("done", false)):
				item = it
				break
		if item.is_empty():
			break
		match String(item.kind):
			"build":
				_send({"verb": "build_commit", "colony": int(item.colony), "edits": [{"add": -2, "repeat": false}], "auto": false})
				await get_tree().create_timer(0.3).timeout
				_smoke_answer()
			"colonize", "hire":
				if String(item.kind) == "colonize":
					_send({"verb": "todo_colonize", "star": int(item.star)})
				else:
					_send({"verb": "todo_hire"})
				var waited := 0.0
				var answered := 0
				while waited < 15.0:
					await get_tree().create_timer(0.3).timeout
					waited += 0.3
					if overlay.visible:
						# CANCEL / NO / REJECT (the colonise question's NO is its second button)
						_views["dialog"]._answer(1 if _views["dialog"].popup_name == "colony_confirm" else 0)
						answered += 1
					elif answered > 0 and native_screen == Screens.MAIN:
						break
				print("smoke: to-do %s '%s' answered no (%d prompts)" % [item.kind, item.text, answered])
			_:
				_send({"verb": "todo_read", "item": int(item.item)})
		await get_tree().create_timer(0.3).timeout
	await host.smoke_fail("the to-do list did not empty")
	return false


## The smoke test answers native message boxes with OK / Yes.
## A real mouse click at a screen point (the legacy screens take the mouse, not keys, to go on).
func _smoke_click(at: Vector2) -> void:
	for pressed in [true, false]:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_LEFT
		click.pressed = pressed
		click.position = at
		click.global_position = at
		Input.parse_input_event(click)
		await get_tree().create_timer(0.1).timeout


func _smoke_answer() -> bool:
	if not overlay.visible:
		return false
	print("smoke: answering request '%s' (native galaxy behind: %s, size %s)" % [_views["dialog"].get_node("Fit/Box/Text").text,
		galaxy.visible and not legacy.visible, _views["dialog"].size])
	# A game window shown natively (a "popup"): its picture once, answered on the next call.
	var popup_name := String(_views["dialog"].popup_name)
	if popup_name != "" and not _shot_popups.has(popup_name):
		_shot_popups[popup_name] = true
		print("smoke: popup '%s' shown natively" % popup_name)
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--shots="):
				host.shot(arg.substr(8) + "/native-popup-%s.png" % popup_name)
		return true
	if not _shot_question and galaxy.visible and not legacy.visible:
		_shot_question = true
		for arg in OS.get_cmdline_user_args():
			if arg.begins_with("--shots="):
				host.shot(arg.substr(8) + "/native-turn-question.png")
	_views["dialog"]._answer(-1 if popup_name == "slider" else 1) # a slider: Cancel (nothing scrapped)
	return true


func _query(what: String) -> Dictionary:
	var box := {}
	_send({"verb": "query", "what": what}, func(result: Dictionary): box["r"] = result)
	var waited := 0.0
	while not box.has("r") and waited < 10.0:
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
	return box.get("r", {})


## Reordering the build list by dragging (owner, 2026-11-19): with real mouse events, the third
## item is pressed and moved over the first: it follows the pointer and a gap opens there; released,
## it is first. A drag let go outside the list changes nothing.
func _smoke_queue_drag(shots: String) -> void:
	var colony := -1
	for c in 64:
		var data: Dictionary = await _query_args({"what": "colony", "colony": c})
		if data.get("ok", false) and not data.get("outpost", false):
			colony = c
			break
	var star: int = int((await _query_args({"what": "colony", "colony": colony})).star)
	_open_colony_at(star, colony, true)
	var build := _view("build_pane")
	var waited := 0.0
	while (not build.visible or build.options.is_empty()) and waited < 10.0:
		await get_tree().create_timer(0.2).timeout
		waited += 0.2
	await get_tree().create_timer(1.0).timeout
	for item: Dictionary in build.options.buildings:
		if build._queue.size() >= 3:
			break
		if not item.get("separator", false):
			build._add(int(item.id))
	await get_tree().create_timer(0.5).timeout
	var system_pane := _view("system_pane")
	var marked: int = system_pane.selected_planet
	var colony_planet := int((await _query_args({"what": "colony", "colony": colony})).get("planet", -2))
	print("smoke: colony opened from outside: system pane marks planet %d (the colony's %d)" % [marked, colony_planet])
	if marked != colony_planet:
		await host.smoke_fail("the open colony's planet is not marked in the system pane")
		return
	var before: Array = build._queue.map(func(q: Dictionary) -> int: return int(q.id))
	var rows: Array = build.queue_box.get_children()
	var factor := get_window().content_scale_factor
	var row_center := func(i: int) -> Vector2:
		var now: Array = build.queue_box.get_children() # the list is rebuilt by each drop
		var name_button: Control = null
		for b in (now[i] as Node).get_children():
			if b is Button and (b as Button).text != "X":
				name_button = b
		return (name_button.get_global_rect().get_center() if name_button else (now[i] as Control).get_global_rect().get_center())
	var from: Vector2 = row_center.call(2)
	var to: Vector2 = (rows[0] as Control).get_global_rect().position + Vector2(40.0, 4.0)
	_smoke_mouse(true, from * factor)
	await get_tree().process_frame
	for step in 12:
		var motion := InputEventMouseMotion.new()
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		motion.position = from.lerp(to, (step + 1) / 12.0) * factor
		Input.parse_input_event(motion)
		await get_tree().create_timer(0.03).timeout
	await get_tree().create_timer(0.3).timeout
	var ghost_on: bool = build._ghost != null and build._ghost.visible
	var gap: int = build._drag_gap
	var spacer_h: float = build._spacer.custom_minimum_size.y if build._spacer != null else 0.0
	print("smoke: queue drag: %s, ghost %s, gap %d, gap height %.0f" % [before, ghost_on, gap, spacer_h])
	if shots != "":
		await host.shot(shots + "/native-queue-drag.png")
	_smoke_mouse(false, to * factor)
	await get_tree().create_timer(0.4).timeout
	var after: Array = build._queue.map(func(q: Dictionary) -> int: return int(q.id))
	print("smoke: queue drag dropped: %s" % [after])
	var moved_ok: bool = before.size() >= 3 and after.size() == before.size() and int(after[0]) == int(before[2])
	# Out of the list: no change.
	var again: Array = after.duplicate()
	var row2: Vector2 = row_center.call(1)
	_smoke_mouse(true, row2 * factor)
	await get_tree().process_frame
	var away := Vector2(row2.x + 700.0, row2.y)
	for step in 8:
		var motion := InputEventMouseMotion.new()
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		motion.position = row2.lerp(away, (step + 1) / 8.0) * factor
		Input.parse_input_event(motion)
		await get_tree().create_timer(0.03).timeout
	_smoke_mouse(false, away * factor)
	await get_tree().create_timer(0.4).timeout
	var unchanged: bool = build._queue.map(func(q: Dictionary) -> int: return int(q.id)) == again
	print("smoke: queue drag let go outside: unchanged %s, ghost gone %s" % [unchanged, build._ghost == null])
	# The partition between Units and Buildings: dragged down, the units get more room.
	var themes_script := preload("themes.gd")
	var saved_share = themes_script.saved_value("build_pane", "units_share", null)
	var units_frame: Control = build.get_node("Box/Columns/Right/Lists/UnitsFrame")
	var units_before := units_frame.size.y
	var grip: Vector2 = build._splitter.get_global_rect().get_center()
	var below := grip + Vector2(0.0, 120.0)
	_smoke_mouse(true, grip * factor)
	await get_tree().process_frame
	for step in 8:
		var motion := InputEventMouseMotion.new()
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		motion.position = grip.lerp(below, (step + 1) / 8.0) * factor
		Input.parse_input_event(motion)
		await get_tree().process_frame
	_smoke_mouse(false, below * factor)
	await get_tree().create_timer(0.3).timeout
	var units_after := units_frame.size.y
	print("smoke: build lists partition: units %.0f -> %.0f px" % [units_before, units_after])
	if shots != "":
		await host.shot(shots + "/native-build-partition.png")
	if saved_share == null:
		var cfg := ConfigFile.new()
		if cfg.load(themes_script.SETTINGS) == OK and cfg.has_section_key("build_pane", "units_share"):
			cfg.erase_section_key("build_pane", "units_share")
			cfg.save(themes_script.SETTINGS)
	else:
		themes_script.save_value("build_pane", "units_share", saved_share)
	if units_after < units_before + 60.0:
		await host.smoke_fail("the partition between units and buildings did not move")
		return
	if not ghost_on or gap != 0 or spacer_h < build.ROW_HEIGHT * 0.5 or not moved_ok or not unchanged or build._ghost != null:
		await host.smoke_fail("the build list drag did not lift, open its gap, or land")
		return
	# A theme change with the build pane open (owner, 2026-11-20): it stays clear of the colony
	# pane, at that pane's real width, in every other theme and back.
	var colony_view: Control = _view("colony_pane")
	var first_theme := theme_id
	var overlaps := []
	for entry: Dictionary in _themes + [{"id": first_theme}]:
		if entry.id == theme_id:
			continue
		apply_theme(entry.id, false)
		await get_tree().create_timer(0.4).timeout
		var colony_right := colony_view.get_global_rect().end.x
		var build_left := build.get_global_rect().position.x
		print("smoke: theme %s with the build pane open: colony right %.0f, build left %.0f" % [entry.id, colony_right, build_left])
		if colony_view.visible and build_left < colony_right - 1.0:
			overlaps.append(entry.id)
	print("smoke: theme back to %s (started on %s)" % [theme_id, first_theme])
	if not overlaps.is_empty():
		await host.smoke_fail("the build pane overlapped the colony pane after a theme change: %s" % [overlaps])
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## Settling from the system view (owner, 2026-11-19): a planet with a colony ship in orbit has
## COLONIZE; it asks the game's question, the landing plays with the panes opening behind it, and
## the system, the colony and its build list are up when it ends.
func _smoke_settle(shots: String) -> void:
	var target_star := -1
	var target_planet := -1
	for star_id in galaxy.stars.size():
		var sys: Dictionary = await _query_args({"what": "system", "star": star_id})
		for planet: Dictionary in sys.get("planets", []):
			if planet.has("colonize_fleet"):
				target_star = star_id
				target_planet = int(planet.planet)
				break
		if target_star >= 0:
			break
	if target_star < 0:
		await host.smoke_fail("no planet with COLONIZE in this save")
		return
	# A system pane closed and another opened comes back on screen (owner, 2026-11-19: after the
	# first, uncolonised systems showed no pane).
	var other: int = (target_star + 1) % galaxy.stars.size()
	_open_system(other)
	await get_tree().create_timer(1.5).timeout
	_view("system_pane").close()
	await get_tree().create_timer(1.0).timeout
	_open_system(target_star)
	await get_tree().create_timer(1.5).timeout
	var sp_rect: Rect2 = _view("system_pane").get_global_rect()
	var on_screen := sp_rect.intersects(get_viewport().get_visible_rect())
	print("smoke: settle: the reopened system pane on screen %s at %s" % [on_screen, sp_rect])
	if not on_screen:
		await host.smoke_fail("the system pane reopened off screen")
		return
	_open_system(target_star)
	await get_tree().create_timer(2.0).timeout
	var pane := _view("system_pane")
	var button: Button = null
	for b in pane.find_children("*", "Button", true, false):
		if (b as Button).text == "COLONIZE" and (b as Button).is_visible_in_tree():
			button = b
			break
	print("smoke: settle: star %d planet %d, COLONIZE button %s" % [target_star, target_planet, button != null])
	if shots != "":
		await host.shot(shots + "/native-settle-button.png")
	if button == null:
		await host.smoke_fail("the system pane has no COLONIZE button")
		return
	button.pressed.emit()
	var waited := 0.0
	var asked: Array = []
	var landing_seen := false
	while waited < 60.0:
		await get_tree().create_timer(0.4).timeout
		waited += 0.4
		if _scene_window != null and _scene_window.visible:
			if not landing_seen:
				landing_seen = true
				await get_tree().create_timer(4.0).timeout
				print("smoke: settle landing: behind it system pane %s, colony pane %s (%d), build pane %s" % [_view("system_pane").visible,
					_view("colony_pane").visible, _view("colony_pane").colony_id, _view("build_pane").visible])
				if shots != "":
					await host.shot(shots + "/native-settle-landing.png")
			_scene_window._frame.gui_input.emit(_smoke_left_click())
			continue
		var dialog: Control = _views.get("dialog")
		if overlay.visible and dialog != null and dialog.visible:
			asked.append("%s/%s" % [dialog._kind, dialog.popup_name])
			if dialog._kind == "text":
				dialog._answer(0)
			else:
				dialog._answer(0) # YES
			continue
		if landing_seen and native_screen == Screens.MAIN:
			break
	await get_tree().create_timer(2.5).timeout
	var colony_pane := _view("colony_pane")
	print("smoke: settle done: asked %s, landing %s; system pane %s, colony pane %s on %d, build pane %s" % [asked, landing_seen,
		_view("system_pane").visible, colony_pane.visible, colony_pane.colony_id, _view("build_pane").visible])
	if shots != "":
		await host.shot(shots + "/native-settled.png")
	if not landing_seen or not colony_pane.visible or not _view("build_pane").visible or not _view("system_pane").visible:
		await host.smoke_fail("settling did not end with the system, the colony and its build list open")
		return
	# The build list beside the colony pane, not over it (owner, 2026-11-19: BuildLeft.png).
	var build_left: float = _view("build_pane").get_global_rect().position.x
	var colony_right: float = colony_pane.get_global_rect().end.x
	print("smoke: settle: build list starts at %.0f, colony pane ends at %.0f" % [build_left, colony_right])
	if build_left < colony_right:
		await host.smoke_fail("the build list overlaps the colony pane")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## WP3.11d: a council meeting in the scene window (popup preview, a scratch save): its pages
## answer a click, the vote comes as a list over the scene (abstain), the ruling list (accept);
## it closes back to the galaxy.
func _smoke_council(shots: String) -> void:
	var box := {}
	_send({"verb": "preview_popup", "popup": "council"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	var pages := 0
	var lists: Array = []
	var shot_page := false
	while not box.has("r") and waited < 120.0:
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
		var dialog: Control = _views.get("dialog")
		if dialog != null and dialog.visible and overlay.visible:
			await get_tree().create_timer(1.0).timeout
			lists.append(dialog.get_node("Fit/Box/Text").text.left(60))
			if shots != "":
				await host.shot(shots + "/native-council-list-%d.png" % lists.size())
			# The vote: the third line is ABSTAIN; the ruling: the first is ACCEPT.
			dialog._answer(dialog.OPTION_ANSWER + (2 if lists.size() == 1 else 0) if dialog._kind == "popup" else (2 if lists.size() == 1 else 0))
			continue
		if _scene_window != null and _scene_window.visible and not _scene_window.arriving():
			if not shot_page:
				await get_tree().create_timer(2.5).timeout
				shot_page = true
				print("smoke: council scene title '%s', text '%s', layers %d, galaxy %s, bar %s" % [_scene_window._title.text, _scene_window._text.text.left(80),
					_scene_window._layers.size(), galaxy.visible, _view("galaxy_bar").visible])
				if shots != "":
					await host.shot(shots + "/native-council.png")
			pages += 1
			_scene_window._frame.gui_input.emit(_smoke_left_click())
	await get_tree().create_timer(1.5).timeout
	print("smoke: council over %s after %d clicks, lists %s, window %s, galaxy %s, legacy %s" % [box.has("r"), pages, lists,
		_scene_window != null and _scene_window.visible, galaxy.visible, legacy.visible])
	if not box.has("r") or (_scene_window != null and _scene_window.visible) or legacy.visible:
		await host.smoke_fail("the council did not finish back on the galaxy")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## The occupation question natively (owner, 2026-11-19): asked beside the colony's system, the
## colony's pane after; and the colony pane's KEEP / ANNIHILATE on a colony with captured people.
func _smoke_occupation(shots: String) -> void:
	var box := {}
	_send({"verb": "preview_popup", "popup": "occupation"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	var dialog: Control = null
	while waited < 10.0:
		await get_tree().create_timer(0.2).timeout
		waited += 0.2
		dialog = _views.get("dialog")
		if dialog != null and dialog.visible and dialog.popup_name == "occupation":
			break
	if dialog == null or not dialog.visible or dialog.popup_name != "occupation":
		print("smoke: occupation: no question (the government keeps conquered people: %s)" % box.get("r", {}))
	else:
		await get_tree().create_timer(1.5).timeout
		print("smoke: occupation question '%s', placement %s, galaxy system %d" % [dialog.get_node("Fit/Box/Text").text.left(80), dialog.placement, galaxy.system_star])
		if shots != "":
			await host.shot(shots + "/native-occupation.png")
		dialog._answer(1) # NO: keep
	waited = 0.0
	while waited < 6.0 and _view("colony_pane").colony_id < 0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	print("smoke: occupation answered %s; colony pane %s on %d" % [box.get("r", {}), _view("colony_pane").visible, _view("colony_pane").colony_id])
	var found := -1
	for c in 64:
		var colony: Dictionary = await _query_args({"what": "colony", "colony": c})
		if colony.get("ok", false) and colony.has("occupation"):
			found = c
			print("smoke: colony %d has captured people: %s" % [c, colony.occupation])
			break
	if found >= 0:
		_on_colony_chosen(found)
		await get_tree().create_timer(1.5).timeout
		var pane := _view("colony_pane")
		print("smoke: occupation control visible %s, keep %s, annihilate %s (disabled %s)" % [pane._occupation.visible,
			pane._keep_button.button_pressed, pane._annihilate_button.button_pressed, pane._annihilate_button.disabled])
		if shots != "":
			await host.shot(shots + "/native-occupation-pane.png")
	else:
		print("smoke: no colony with captured people in this save")
	game.request_game_quit()
	print("smoke: asked the game to quit")


## WP3.11c: the Antarans' message in the scene window (popup preview): the room loops, the message
## shows, a click closes it back to the galaxy.
func _smoke_antarans(shots: String) -> void:
	var box := {}
	_send({"verb": "preview_popup", "popup": "antarans"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	while (_scene_window == null or not _scene_window.visible) and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if _scene_window == null or not _scene_window.visible:
		await host.smoke_fail("no Antaran window")
		return
	await get_tree().create_timer(3.0).timeout
	print("smoke: antarans scene '%s', text '%s' shown %s, backdrop frame %d, galaxy %s, bar %s" % [_scene_window._state.get("scene", ""),
		_scene_window._text.text.left(80), _scene_window._text.visible, _scene_window._backdrop_frame, galaxy.visible, _view("galaxy_bar").visible])
	if shots != "":
		await host.shot(shots + "/native-antarans.png")
	_scene_window._frame.gui_input.emit(_smoke_left_click())
	waited = 0.0
	while not box.has("r") and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(1.0).timeout
	print("smoke: antarans closed %s, galaxy %s, legacy %s, answer %s" % [not _scene_window.visible, galaxy.visible, legacy.visible, box.get("r", {})])
	if _scene_window.visible or not galaxy.visible or legacy.visible:
		await host.smoke_fail("the Antaran window did not close back to the galaxy")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## WP3.11b: a film in its window (the amoeba's, from the popup preview): it opens over the galaxy
## sized to the film, plays, and closes with the galaxy still up.
func _smoke_film(shots: String) -> void:
	var box := {}
	_send({"verb": "preview_popup", "popup": "film"}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	while not _film_active and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if not _film_active:
		await host.smoke_fail("no film window")
		return
	for i in 2:
		await get_tree().create_timer(1.5).timeout
		print("smoke: film %d: window %s, source %s, legacy %s at %s, layer %d, galaxy %s, bar %s" % [i, Rect2(_film_frame.position, _film_frame.size),
			game.get_source_rect(), legacy.visible, Rect2(legacy.position, legacy.size), (legacy.get_parent() as CanvasLayer).layer, galaxy.visible, _view("galaxy_bar").visible])
		if shots != "":
			await host.shot(shots + "/native-film-%d.png" % i)
	waited = 0.0
	while _film_active and waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	await get_tree().create_timer(1.5).timeout
	print("smoke: film over %s after %.1f s: legacy %s (full screen %s, layer %d), galaxy %s, bar %s, answer %s" % [not _film_active, waited,
		legacy.visible, legacy.size == get_viewport().get_visible_rect().size, (legacy.get_parent() as CanvasLayer).layer, galaxy.visible,
		_view("galaxy_bar").visible, box.get("r", {})])
	if _film_active or legacy.visible or not galaxy.visible:
		await host.smoke_fail("the film window did not close back to the galaxy")
		return
	game.request_game_quit()
	print("smoke: asked the game to quit")


## The scene-close flicker (owner, 2026-11-19: "a full screen flicker" as the GNN window closes,
## "the UI being cleared and rebuilt"): `--scene-flash=<dir>` with a profile that forces an event
## every turn ends the turn, clicks through the GNN window, and from the first click until 2.5 s
## back on the galaxy saves every frame the window shows (`<dir>/NNN.png`, half size) and prints
## what was visible in each.
func _smoke_scene_flash(dir: String) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	# The cheapest research, so it is done this turn and the research window comes (a loaded save
	# keeps its own rules, so a profile can't force an event).
	var research: Dictionary = await _query("research")
	var cheapest := {}
	for area: Dictionary in research.get("areas", []):
		if not area.get("apps", []).is_empty() and (cheapest.is_empty() or int(area.get("cost", 0)) < int(cheapest.get("cost", 0))):
			cheapest = area
	if not cheapest.is_empty():
		_send({"verb": "choose_research", "field": int(cheapest.field), "app": int(cheapest.apps[0].app)})
		await get_tree().create_timer(0.5).timeout
		print("smoke: scene flash: research %s, cost %d, have %s" % [cheapest.get("name", "?"), int(cheapest.get("cost", 0)), research.get("progress", "?")])
	await _smoke_clear_todo()
	var turns := 0
	var waited := 0.0
	while (_scene_window == null or not _scene_window.visible) and turns < 4:
		_send({"verb": "end_turn"})
		turns += 1
		waited = 0.0
		while (_scene_window == null or not _scene_window.visible) and waited < 30.0:
			_smoke_answer()
			await get_tree().create_timer(0.1).timeout
			waited += 0.1
			if waited > 3.0 and native_screen == Screens.MAIN:
				await _smoke_clear_todo()
				break
		_smoke_answer()
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if _scene_window == null or not _scene_window.visible:
		await host.smoke_fail("no scene window after the turn")
		return
	await get_tree().create_timer(3.0).timeout
	var n := 0
	var back_at := -1
	var t0 := Time.get_ticks_msec()
	var next_click := 0
	while n < 400 and Time.get_ticks_msec() - t0 < 40000:
		var now := Time.get_ticks_msec()
		if _scene_window.visible and now >= next_click:
			next_click = now + 1500
			if _picker != null and _picker.visible and not _picker._flashing:
				for b in _picker.find_children("*", "Button", true, false):
					if b.theme_type_variation == &"ListItem" and b.is_visible_in_tree() and not (b as Button).disabled:
						(b as Button).pressed.emit() # the first application on offer
						break
			else:
				_scene_window._frame.gui_input.emit(_smoke_left_click())
		var on_galaxy: bool = native_screen == Screens.MAIN and not _scene_window.visible
		if on_galaxy and back_at < 0:
			back_at = now
		if back_at >= 0 and now - back_at > 2500:
			break
		var image := get_viewport().get_texture().get_image()
		image.resize(image.get_width() / 2, image.get_height() / 2)
		image.save_png("%s/%03d.png" % [dir, n])
		print("smoke: frame %03d t=%d screen %d scene %s a=%.2f legacy %s galaxy %s bar %s tabs %s frame %s overlay %s" % [n, now - t0,
			native_screen, _scene_window.visible, _scene_window._stage.modulate.a, legacy.visible, galaxy.visible,
			_view("galaxy_bar").visible, _view("side_tabs").visible, _frame_view.visible, overlay.visible])
		n += 1
		await get_tree().process_frame
	print("smoke: scene flash: %d frames, back on the galaxy %s" % [n, back_at >= 0])
	game.request_game_quit()
	print("smoke: asked the game to quit")


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


## Owner, 2026-11-14: open a system, pan, click another star, back to the galaxy, then a third
## star: its pane came up blank. Real mouse input: drags and clicks on the map.
func _smoke_pan(shots: String) -> void:
	var pane := _view("system_pane")
	var report := func(step: String):
		var controls: Control = _views.get("map_controls")
		print("smoke: pan %s: pane visible %s alpha %.2f x %.0f, star %d, %d planet rows, galaxy system %d, panned %s, recenter %s, camera %s zoom %.4f" % [step,
			pane.visible, pane.modulate.a, pane.position.x, int(pane.star), pane.planets_box.get_child_count(), galaxy.system_star, galaxy.panned,
			controls.get_node("Row/Recenter").visible if controls else false, galaxy.camera.position, galaxy.camera.zoom.x])
	var on_screen := func(star_id: int) -> Vector2:
		return galaxy.get_global_transform_with_canvas() * galaxy.star_position(star_id)
	var visible_stars := func() -> Array:
		var out: Array = []
		var rect := get_viewport().get_visible_rect().grow(-150)
		for star: Dictionary in galaxy.stars:
			var at: Vector2 = on_screen.call(int(star.id))
			if rect.has_point(at) and at.x > 450 and not bool(star.get("black_hole", false)) and int(star.id) != galaxy.system_star:
				out.append(int(star.id))
		return out
	var first: Array = visible_stars.call()
	print("smoke: pan stars on screen %s" % [first])
	await _smoke_click(on_screen.call(first[0]))
	await get_tree().create_timer(2.5).timeout
	report.call("first star")
	# A drag on the map, away from the star.
	var from := Vector2(900, 800)
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = from
	press.global_position = from
	Input.parse_input_event(press)
	for step in 12:
		await get_tree().create_timer(0.03).timeout
		var move := InputEventMouseMotion.new()
		move.position = from - Vector2(20, 10) * (step + 1)
		move.global_position = move.position
		move.relative = Vector2(-20, -10)
		move.button_mask = MOUSE_BUTTON_MASK_LEFT
		Input.parse_input_event(move)
	var release := press.duplicate()
	release.pressed = false
	release.position = from - Vector2(240, 120)
	release.global_position = release.position
	Input.parse_input_event(release)
	await get_tree().create_timer(0.8).timeout
	report.call("panned")
	if shots != "":
		await host.shot(shots + "/pan-panned.png")
	var second: Array = visible_stars.call()
	print("smoke: pan stars on screen now %s" % [second])
	if second.is_empty():
		return
	await _smoke_click(on_screen.call(second[0]))
	await get_tree().create_timer(2.5).timeout
	report.call("second star")
	await _smoke_click(pane.get_node("Box/Back").get_global_rect().get_center())
	await get_tree().create_timer(2.5).timeout
	report.call("back to galaxy")
	for i in 2:
		var stars: Array = visible_stars.call()
		if stars.size() <= i:
			break
		await _smoke_click(on_screen.call(stars[i]))
		await get_tree().create_timer(2.5).timeout
		report.call("star %d" % i)
		if shots != "":
			await host.shot(shots + "/pan-star-%d.png" % i)
		pane.close()
		await get_tree().create_timer(2.0).timeout


## Owner, 2026-11-14: windowed, with the build pane open, dragging the window wider crashed.
func _smoke_resize(shots: String) -> void:
	_on_view_action("colony_next")
	await get_tree().create_timer(2.5).timeout
	var colony_id := -1
	for planet: Dictionary in _view("system_pane").system.get("planets", []):
		var colony: Dictionary = planet.get("colony", {})
		if not colony.is_empty() and colony.get("mine", false) and not colony.get("outpost", false):
			colony_id = int(colony.id)
			break
	if colony_id >= 0:
		_on_colony_chosen(colony_id)
		await get_tree().create_timer(2.0).timeout
	print("smoke: resize: colony pane %s on %d" % [_view("colony_pane").visible, colony_id])
	if colony_id >= 0:
		_open_build_pane(colony_id, false)
		await get_tree().create_timer(2.0).timeout
	print("smoke: resize: build pane %s, window %s, mode %d, scale %.2f" % [_view("build_pane").visible, get_window().size,
		get_window().mode, get_window().content_scale_factor])
	get_window().mode = Window.MODE_WINDOWED
	# A drag: one size a frame, as a window manager sends them.
	for width in range(1330, 1450, 1): # across where the scale steps (about 1382 wide)
		get_window().size = Vector2i(width, 1000)
		await get_tree().process_frame
	for width in range(1200, 1900, 7):
		get_window().size = Vector2i(width, 1000)
		await get_tree().process_frame
	print("smoke: resize: dragged to %s" % get_window().size)
	for width in range(1900, 1100, -9):
		get_window().size = Vector2i(width, 1000 - (1900 - width) / 10)
		await get_tree().process_frame
	print("smoke: resize: dragged back to %s" % get_window().size)
	for width in [1300, 1450, 1600, 1750, 1900, 1200, 1000, 1700]:
		get_window().size = Vector2i(width, 1000)
		for step in 10:
			await get_tree().process_frame
		await get_tree().create_timer(0.6).timeout
		print("smoke: resize: window %s, scale %.2f, build pane right %.0f of %.0f" % [get_window().size, get_window().content_scale_factor,
			_view("build_pane").get_global_rect().end.x, get_viewport().get_visible_rect().size.x])
		if shots != "":
			await host.shot(shots + "/resize-%d.png" % width)

