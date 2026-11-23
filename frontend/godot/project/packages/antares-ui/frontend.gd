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
@export var claimed_requests: PackedStringArray = ["message", "confirm", "warning", "text", "popup", "choose", "todo", "combat", "animate"]
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
var _side_panel_opens := 0 # side panels opened so far: a system asked for before one opened stays shut
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
	galaxy.right_clicked.connect(func(): _set_measuring(false))
	galaxy.right_clicked.connect(func(): _end_relocation_pick())
	galaxy.planet_clicked.connect(_on_planet_clicked)
	galaxy.planet_hovered.connect(_on_planet_hovered)
	galaxy.fleet_owner_hovered.connect(func(owner: int):
		if overlay.visible and _views.has("dialog") and _views.dialog.visible:
			_views.dialog.hover_fleet_owner(owner))
	galaxy.empty_clicked.connect(_on_empty_clicked)
	galaxy.planet_right_clicked.connect(_on_planet_right_clicked)
	galaxy.system_panned.connect(func(away: bool):
		var controls: Control = _views.get("map_controls")
		if controls != null:
			controls.show_recenter(away))
	# Newest pane on top (owner 2026-11-23): every view in the layer rises when it opens.
	native_layer.child_entered_tree.connect(_watch_layer_child)
	for child in native_layer.get_children():
		_watch_layer_child(child)
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
	# Picking a relocation target: Esc goes back to the system.
	if not galaxy.relocation_pick.is_empty():
		var pick_key := event as InputEventKey
		if pick_key and pick_key.pressed and pick_key.keycode == KEY_ESCAPE:
			_end_relocation_pick()
			get_viewport().set_input_as_handled()
			return
	# Measuring (F9): Esc stops it.
	if galaxy.measuring:
		var measure_key := event as InputEventKey
		if measure_key and measure_key.pressed and measure_key.keycode == KEY_ESCAPE:
			_set_measuring(false)
			get_viewport().set_input_as_handled()
			return
	# Turns passing by themselves: a click or Esc stops them (the legacy galaxy's exit field). Not
	# while a film plays in its window: the click is the film's, to skip it (owner, 2026-11-20).
	# Nor during a native battle (WP3.9): its clicks are the battle's.
	if _turn_over_galaxy and overlay.visible == false and not _film_active and not _in_battle():
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
	game.claim_requests(_request_kinds() if on else PackedStringArray())
	if on:
		for what in ["empire", "galaxy", "fleets", "options", "todo"]:
			_send({"verb": "watch", "what": what})
	mode_button.text = "Legacy galaxy (Ctrl+G)" if on else "Native galaxy (Ctrl+G)"


## The request kinds claimed: battles ("combat", "animate") only while native battles are on (WP3.9:
## a preview, off by default; the smoke's battle steps turn it on).
func _request_kinds() -> PackedStringArray:
	var kinds := PackedStringArray()
	for kind in claimed_requests:
		if kind in ["combat", "animate"] and not native_battles:
			continue
		kinds.append(kind)
	return kinds


func _set_native_battles(on: bool) -> void:
	native_battles = on
	Themes.save_value("combat", "native", on)
	if native:
		game.claim_requests(_request_kinds()) # from the next battle on


## The game holds the sounds a legacy screen starts until its picture shows (owner, 2026-11-13:
## the news and discovery sounds came a few frames early): released once the legacy view (or a
## question over the galaxy) is up.
var _sounds_held := false


func _process(delta: float) -> void:
	_check_orphan_system(delta)
	if todo_auto:
		_auto_todo_step()
	elif _gain_discovery_step():
		pass
	elif _todo_pending and _todo_may_open():
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
		# A native battle waits for the player in its own scene (WP3.9; it never polls the legacy
		# screen): the turn's galaxy ends at once, or it took the battle's clicks for 10 s.
		if native_screen == Screens.MAIN or overlay.visible or _in_battle() \
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
	if native_screen == Screens.LEGACY and not _question_over_galaxy and not _turn_over_galaxy and not _film_active and not _in_battle():
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
	# The legacy view goes with the film's window in every case (owner, 2026-11-20: clicking
	# through the Antarans' arrival during the turn showed the legacy galaxy full screen for a
	# moment: the turn's galaxy left it up until its next notice).
	legacy.hide()
	if native_screen == Screens.LEGACY and not _turn_over_galaxy:
		# The galaxy stays a moment, as after a scene window: the game may come straight back.
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
	legacy.visible = not in_galaxy and screen == Screens.LEGACY and not _in_battle()
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
		var todo_back: Control = _views.get("todo_panel")
		if todo_back != null and todo_back.get_meta("was_open", false):
			todo_back.set_meta("was_open", false)
			if _todo_total() > 0:
				# With what came while it was hidden (owner 2026-11-23: it came back with the 2
				# items it had, and the turn's full list opened only once that was closed).
				todo_back.show_todo(_todo)
				todo_back.show()
				_todo_seen = _todo_total()
				_todo_shown_stardate = int(_todo.get("stardate", -1))
				_todo_pending = false
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
		_set_measuring(false)
		_end_relocation_pick(false)
		var landscape_view: Control = _views.get("colony_landscape")
		if landscape_view != null and landscape_view.visible:
			landscape_view.close()
		# The to-do list too (owner, 2026-11-20: it stayed over a combat); back with the galaxy.
		var todo_hide: Control = _views.get("todo_panel")
		if todo_hide != null:
			todo_hide.set_meta("was_open", todo_hide.visible or todo_hide.get_meta("was_open", false))
			todo_hide.hide()
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


## A point of the legacy 640x480 screen in window pixels: the game draws it 4:3, centred in its
## control (AntaresGame::Frame_Rect), so on a wide window the sides are bars.
func _smoke_legacy_point(spot: Vector2) -> Vector2:
	var rect := legacy.get_global_rect()
	var frame_size := Vector2(minf(rect.size.x, rect.size.y * 4.0 / 3.0), minf(rect.size.y, rect.size.x * 3.0 / 4.0))
	var origin := rect.position + (rect.size - frame_size) * 0.5
	return (origin + spot * frame_size / Vector2(640.0, 480.0)) * get_window().content_scale_factor


## SMOKE_WINDOW ("2556x1369") and SMOKE_SCALE ("1.3"): a player's screen and interface scale.
func _smoke_window_setting() -> void:
	var wide := OS.get_environment("SMOKE_WINDOW").split("x")
	if wide.size() == 2:
		get_window().size = Vector2i(int(wide[0]), int(wide[1]))
	if OS.get_environment("SMOKE_SCALE") != "":
		get_window().content_scale_factor = float(OS.get_environment("SMOKE_SCALE"))
	if wide.size() == 2 or OS.get_environment("SMOKE_SCALE") != "":
		await get_tree().create_timer(0.6).timeout


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
	if _views.has("colony_landscape") and _views.colony_landscape.visible:
		await get_tree().process_frame
		_place_landscape()


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
	# The overlay's windows took the theme when made (owner, 2026-11-20: the discovery window stayed
	# Glass after a switch to Chrome): every themed one in the overlay takes the new one.
	for node in $Overlay.get_children():
		if node is Control and (node as Control).theme != null:
			(node as Control).theme = built.theme
	if _views.has("dialog"):
		_views.dialog.theme = built.theme
	if _combat != null:
		_combat.theme = built.theme # the battle follows a theme switch too (owner, 2026-11-21)
		_combat.frame_style = built.get("frame")
		_combat.tee = built.get("tee", {})
	_frame = built.get("frame")
	_frame_view.queue_redraw()
	if remember and id != "":
		Themes.save_choice(id)
	if _views.has("options_menu"):
		_views.options_menu.show_themes(_themes, theme_id)
	print("frontend: theme %s" % (built.get("name", "?")))
	_rescale_for_theme()
	_reflow_open_panes()


## The scale again under a new theme (owner, 2026-11-20: 130% on Glass stayed 130% on Chrome, whose
## taller side tabs hold 125% at most, and buttons ran off the bottom): once the tabs are laid out
## in the theme, the wanted scale held to what it holds. The player's saved choice is kept, so
## the old theme gets it back.
func _rescale_for_theme() -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	set_ui_scale(_wanted_ui_scale(), false)


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
		if view.has_signal("native_battles_changed"):
			view.native_battles_changed.connect(_set_native_battles)
		if view.has_signal("todo_auto_changed"):
			view.todo_auto_changed.connect(func(on: bool):
				todo_auto = on
				Themes.save_value("todo", "auto", on)
				_todo_auto_last = {}
				if on:
					_view("options_menu").close() # the next to-do comes as soon as the galaxy is free
				)
		if view.has_signal("help_requested"):
			view.help_requested.connect(show_help)
		if view.has_signal("scale_auto_changed"):
			view.scale_auto_changed.connect(set_ui_scale_automatic)
		if view.has_signal("change_build"):
			view.change_build.connect(_open_build_pane)
		if view.has_signal("landscape_toggled"):
			view.landscape_toggled.connect(_toggle_landscape)
		if view.has_signal("colony_shown"):
			view.colony_shown.connect(_on_colony_shown)
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
		if view.has_signal("relocate_requested"):
			view.relocate_requested.connect(_start_relocation_pick)
		native_layer.add_child(view)
		_raise_chrome() # the bar, the pockets, the tabs and the frame stay over every view
		_views[scene_name] = view
		_attach_view_help(scene_name, view)
		if scene_name == "colony_pane":
			# It grows to fit its rows once its colony's data arrives, after a build pane opened
			# with it was placed (owner 2026-11-23: opened from the Colonies panel, the build pane
			# overlapped it): the build pane moves along.
			view.resized.connect(func():
				if _views.has("build_pane") and _views.build_pane.visible:
					_place_build_pane.call_deferred())
		if scene_name == "side_tabs": # their column limits the scale
			_apply_saved_scale.call_deferred()
		if scene_name == "options_menu":
			view.show_flight(galaxy.flight_time)
			view.show_todo_auto(todo_auto)
			view.show_native_battles(native_battles)
			view.show_themes(_themes, theme_id)
			view.show_scale(ui_scale, max_ui_scale(), MIN_UI_SCALE, is_ui_scale_automatic())
		if scene_name == "build_pane": # the colony's landscape moves aside for it, and back
			view.visibility_changed.connect(func(): _place_landscape.call_deferred())
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


## Turns passing by themselves (End Of Turn Wait off; owner 2026-11-23): each turn's summary
## messages came as their own list, and the lists opened one after another once the turns stopped.
## A turn's unread messages the player never had the list open for are carried into the next
## turn's list, marked with their stardate, so the turns since the list was last seen make one.
var _todo_backlog: Array = [] # carried report items (copies), oldest turns last
var _todo_shown_stardate := -1 # the stardate whose list the player last had open


func _carry_todo_reports() -> void:
	var stardate := int(_todo.get("stardate", -1))
	if _todo.is_empty() or stardate < 0 or stardate == _todo_shown_stardate:
		_todo_backlog = [] # that turn's list was seen
		return
	var carried: Array = []
	for item: Dictionary in _todo.get("items", []):
		if String(item.get("kind", "")) != "report" or item.get("done", false):
			continue
		var copy: Dictionary = item.duplicate()
		if not copy.get("carried", false):
			copy["carried"] = true
			copy["text"] = "%d.%d: %s" % [stardate / 10, stardate % 10, String(copy.get("text", ""))]
		carried.append(copy)
	_todo_backlog = carried


func _with_todo_backlog(result: Dictionary) -> Dictionary:
	var open_backlog: Array = _todo_backlog.filter(func(item: Dictionary) -> bool: return not item.get("done", false))
	if open_backlog.is_empty():
		return result
	var merged: Dictionary = result.duplicate()
	merged["items"] = (result.get("items", []) as Array) + open_backlog
	merged["unread"] = int(result.get("unread", 0)) + open_backlog.size()
	return merged


func _open_todo() -> void:
	var todo := _view("todo_panel")
	_todo_shown_stardate = int(_todo.get("stardate", -1))
	if not todo.item_chosen.is_connected(_on_todo_item):
		todo.item_chosen.connect(_on_todo_item)
		todo.end_turn_requested.connect(func():
			todo.close()
			_send({"verb": "end_turn"}))
	_todo_seen = _todo_total()
	_todo_pending = false
	_bring_up(todo)
	todo.show_todo(_todo)
	var bar := _view("galaxy_bar")
	todo.open_at(bar.get_node("Row/Turn").get_global_rect().get_center().x)


## AUTO ADDRESS TO-DOS (owner, 2026-11-20): the stock game's way. Each item not yet opened this
## turn opens in turn, the next once the last is dealt with: its panes closed and the galaxy free,
## or for a colony's build, its build list closed with the queue filled (on to the next colony at
## once, as the original goes from one build screen to the next). An item left as it was isn't
## opened again this turn; with all of them opened and some still open, the list comes up.
var todo_auto: bool = Themes.saved_value("todo", "auto", false)
# WP3.9: battles in the native scene (a preview, off by default; owner, 2026-11-21). The smoke's
# battle steps turn it on for their run.
var native_battles: bool = bool(Themes.saved_value("combat", "native", false)) or "--combat" in OS.get_cmdline_user_args() \
	or "--torpedo" in OS.get_cmdline_user_args() or "--combat-speed" in OS.get_cmdline_user_args() \
	or "--selfdestruct" in OS.get_cmdline_user_args()
var _todo_auto_opened := {} # this turn's items opened, by key
var _todo_auto_last := {} # the item opened last, and when: {item, at, open}
var _todo_auto_listed := false # this turn's leftovers shown in the list
const TODO_AUTO_SETTLE := 0.8 # seconds after an item opens before the next may


func _auto_todo_step() -> void:
	if not _todo_may_open():
		return
	var now := Time.get_ticks_msec() / 1000.0
	if _landing_followup_since >= 0.0:
		if now - _landing_followup_since < 15.0:
			return
		_landing_followup_since = -1.0 # never came: don't hold the to-dos for ever
	if not _todo_auto_last.is_empty():
		if now - float(_todo_auto_last.at) < TODO_AUTO_SETTLE:
			return
		var panes_free := not _view("system_pane").visible and not _view("colony_pane").visible and not _view("build_pane").visible
		# A build item, or a colony base whose landing opened the new colony's build list: on at once
		# when that list is closed with the queue filled.
		var build_done: bool = String(_todo_auto_last.item.get("kind", "")) in ["build", "colonize"] and not _view("build_pane").visible \
			and int(_todo.get("open", 0)) < int(_todo_auto_last.open)
		if not panes_free and not build_done:
			return
	for item: Dictionary in _todo.get("items", []):
		var key := "%s:%s:%s:%s" % [item.get("kind", ""), item.get("item", -1), item.get("colony", -1), item.get("star", -1)]
		if item.get("done", false) or _todo_auto_opened.has(key) or String(item.get("kind", "")) == "report":
			continue
		_todo_auto_opened[key] = true
		_todo_auto_last = {"item": item, "at": now, "open": int(_todo.get("open", 0))}
		_todo_seen = int(_todo.get("open", 0))
		_todo_pending = false
		_on_todo_item(item)
		return
	# All opened this turn; what is still open waits in the list, shown once.
	_todo_auto_last = {}
	if not _todo_auto_listed:
		_todo_auto_listed = true
		_open_todo()


## A discovery that gave the player something (a splinter colony joining, a pirate cache or space
## debris, a marooned leader joining; the item's `gain`) opens its window as soon as the galaxy is
## free, as the original's discovery popup came up (owner 2026-11-23: a splinter colony joined
## unnoticed in the list). Once per item a turn; with AUTO ADDRESS TO-DOS every item opens anyway.
func _gain_discovery_step() -> bool:
	if not _todo_may_open():
		return false
	for item: Dictionary in _todo.get("items", []):
		if String(item.get("kind", "")) != "new_system" or not item.get("gain", false) or item.get("done", false):
			continue
		var key := "%s:%s:%s:%s" % [item.get("kind", ""), item.get("item", -1), item.get("colony", -1), item.get("star", -1)]
		if _todo_auto_opened.has(key):
			continue
		_todo_auto_opened[key] = true
		_on_todo_item(item)
		return true
	return false


func _todo_may_open() -> bool:
	if native_screen != Screens.MAIN or overlay.visible or legacy.visible or not galaxy.visible:
		return false
	for scene_name in FLYOUTS:
		var other: Control = _views.get(scene_name)
		if other != null and other.visible:
			return false
	return _todo_total() > 0


var _orphan_system_time := 0.0


## A system left on the map with no system pane (owner, 2026-11-21: after a turn's combat the
## stars stayed dimmed around a system that still showed a colony just destroyed, with no way
## back): once it has lasted a second on the galaxy, with no game question, landing or map
## popup showing, the galaxy comes back.
func _check_orphan_system(delta: float) -> void:
	var pane: Control = _views.get("system_pane")
	var orphan: bool = native_screen == Screens.MAIN and galaxy.visible and pane != null and not pane.visible and \
		(galaxy.system_star >= 0 or galaxy.others_alpha < 0.99) and not overlay.visible and _popup_zoom < 0 and \
		_popup_map_ending < 0 and not (_scene_window != null and _scene_window.is_open()) and not legacy.visible
	if not orphan:
		_orphan_system_time = 0.0
		return
	_orphan_system_time += delta
	if _orphan_system_time < 1.0:
		return
	_orphan_system_time = 0.0
	print("frontend: a system (%d) was left on the map with no pane: back to the galaxy" % galaxy.system_star)
	_panel_star = -1
	galaxy.selected = -1
	galaxy.clear_system_view()
	galaxy.fly_back()
	galaxy.redraw()


## What the to-do list shows (owner, 2026-11-21): the choices that hold the turn and the news
## not yet read. TURN reads TO DO (n) while it isn't 0.
func _todo_total() -> int:
	return int(_todo.get("open", 0)) + int(_todo.get("unread", 0))


## The turn summary's notification area (owner, 2026-11-14): asked again each turn and when the
## options change.
func _refresh_summary() -> void:
	# Owner, 2026-11-21: the summary's messages are in the to-do list; the pane stays hidden.
	var pane: Control = _views.get("summary_panel")
	if pane != null:
		pane.hide()


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
		"report":
			if item.get("carried", false):
				# An earlier turn's message (the game's keys are this turn's): read here.
				for carried: Dictionary in _todo_backlog:
					if String(carried.get("text", "")) == String(item.get("text", "")):
						carried["done"] = true
				_send({"verb": "query", "what": "todo"}, func(r: Dictionary): _on_snapshot("todo", r))
				_on_summary_item(item)
				return
			# A turn summary message (owner, 2026-11-21: in this list): read, and its colony opens.
			_send({"verb": "todo_read", "report": String(item.get("report", ""))}, func(_r: Dictionary):
				_send({"verb": "query", "what": "todo"}, func(r: Dictionary): _on_snapshot("todo", r)))
			_on_summary_item(item)
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


## Newest pane on top (owner 2026-11-23: the design panel's parts list stayed behind the colony
## and build panes it was opened over). Whatever opens in the layer, by the player or by the game,
## rises over every other view; the turn notice, the film window and the chrome then go back over
## it. Only a view's own show counts: the layer coming back (after a battle or a legacy screen)
## keeps the order it had. The colony landscape places itself (over the panes when popped up, under
## the build pane beside them; _place_landscape).
var _layer_shown := {} # view -> its own `visible` as last seen


func _watch_layer_child(node: Node) -> void:
	if node is Control and not node.visibility_changed.is_connected(_on_layer_child_visibility):
		_layer_shown[node] = (node as Control).visible
		node.visibility_changed.connect(_on_layer_child_visibility.bind(node))
		node.tree_exiting.connect(func(): _layer_shown.erase(node), CONNECT_ONE_SHOT)


func _on_layer_child_visibility(view: Control) -> void:
	var was: bool = _layer_shown.get(view, false)
	_layer_shown[view] = view.visible
	if not view.visible or was or view.get_parent() != native_layer:
		return
	if view == _frame_view or view == _film_frame or view == _turn_notice or view == _measure_hint:
		return
	for own in ["galaxy_bar", "side_tabs", "colony_landscape"]:
		if _views.get(own) == view:
			return
	view.move_to_front()
	for top: Control in [_turn_notice, _film_frame]:
		if top != null and top.visible:
			top.move_to_front()
	_raise_chrome()


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
			_set_measuring(not galaxy.measuring)
		"fleet_next", "fleet_previous":
			_cycle_fleet(0 if action_name == "fleet_next" else 1)
		"turn":
			# WP3.4d: with items waiting the to-do list drops down (END TURN is in it); otherwise
			# the turn ends, as before.
			var todo := _view("todo_panel")
			_todo_seen = _todo_total() # opened or closed by hand: no reopening for it
			if todo.visible:
				todo.close()
			elif _todo_total() > 0:
				_open_todo() # END TURN is at its bottom
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
				_set_measuring(false)
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
		var landscape_view: Control = _views.get("colony_landscape")
		if landscape_view != null and landscape_view.visible:
			landscape_view.close()
		_landscape_hidden_for = -1
		if _panel_star >= 0 and _view("system_pane").visible:
			_frame_system(_panel_star) # the free area widened again
	elif scene_name == "colony_landscape":
		_landscape_hidden_for = int(_views.colony_landscape.colony_id) # hidden by hand: not opened again for it
	elif scene_name == "design_list" or scene_name == "design_panel":
		_view("side_tabs").show_open("designs", false)
	elif scene_name in SIDE_PANELS.values():
		_view("side_tabs").show_open(SIDE_PANELS.find_key(scene_name), false)
		galaxy.selected = -1
		if not _colonies_return and not _switching_panel:
			galaxy.unfit() # the camera where it was before the panel
		# A system shown on the map with no system pane (owner, 2026-11-21: every star stayed
		# dimmed, zoomed out, with no way back): the galaxy comes back.
		var map_question: bool = overlay.visible or _popup_zoom >= 0 or _popup_map_ending >= 0 or \
			(_scene_window != null and _scene_window.is_open()) # a game question or a landing on that system
		if not _switching_panel and not map_question and native_screen == Screens.MAIN and \
				(galaxy.system_star >= 0 or galaxy.others_alpha < 0.99) and not _view("system_pane").visible:
			galaxy.selected = -1
			galaxy.clear_system_view()
			galaxy.redraw()
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
		"combat":
			_combat_view().show_state(result)
		"combat_event":
			_combat_view().play_event(result)
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
				_carry_todo_reports()
				_todo_seen = -1 # a new turn's list
				_todo_auto_opened = {}
				_todo_auto_last = {}
				_todo_auto_listed = false
				_refresh_summary() # and its summary
			result = _with_todo_backlog(result)
			_todo = result
			var open := _todo_total()
			_view("galaxy_bar").show_todo(open)
			var todo_view: Control = _views.get("todo_panel")
			if todo_view != null and todo_view.visible:
				if open == 0:
					todo_view.close() # everything answered (owner, 2026-11-14)
				else:
					todo_view.show_todo(result)
					# Shown as it came (owner 2026-11-23: a new turn's list arriving while the list
					# was open made it open again as soon as it was closed).
					_todo_seen = open
					_todo_shown_stardate = int(result.get("stardate", -1))
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
	# WP3.9b: a decision in a native battle goes to the battle scene, not a dialog.
	if kind == "combat":
		_combat_view().on_request(id, request)
		return
	# WP3.9d: a battle effect the rules wait for; answered once it has played.
	if kind == "animate":
		_combat_view().on_animate(id, request)
		return
	var dialog: Control = _views.get("dialog")
	if dialog == null:
		dialog = load(_views_dir() + "/dialog.tscn").instantiate()
		dialog.art = art
		dialog.answered.connect(_on_answered)
		dialog.released.connect(_popup_closed)
		dialog.fleet_row_hovered.connect(func(owner: int): galaxy.lit_owner = owner)
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
	var in_battle: bool = request.has("scene_state") and String((request.scene_state as Dictionary).get("scene", "")) == "combat"
	if in_battle:
		# A message during a native battle (refusals, the battle's results): over the battle scene.
		_combat_view().show_state(request.scene_state)
	elif request.has("scene_state"):
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
	# At the owner's interface scale (130%), where the window's place follows the bar's right end.
	var scale_before := ui_scale
	var window_before := get_window().size
	get_window().size = Vector2i(1920, 1080)
	await get_tree().create_timer(0.5).timeout
	set_ui_scale(130.0, false)
	await get_tree().create_timer(0.5).timeout
	print("smoke: research at scale %.0f%%, window %s, view %s" % [ui_scale, get_window().size, get_viewport().get_visible_rect().size])
	var box := {}
	_send({"verb": "preview_popup", "popup": "research", "hyper": 1 if "--research-hyper" in OS.get_cmdline_user_args() else 0}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	var pages := 0
	var aligned := false
	var page_rect := Rect2() # the window on the first page; the picker must not move it (owner, 2026-11-20, Glass)
	var picker_window := Rect2()
	var bar_on_page := Rect2() # the bar, which must not narrow as the readout says "none" (owner)
	var bar_with_picker := Rect2()
	while not box.has("r") and waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		if _scene_window == null or not _scene_window.visible:
			continue
		if _picker != null and _picker.visible and not _picker._flashing:
			await get_tree().create_timer(1.0).timeout # slid down
			var scene_rect: Rect2 = _scene_window._frame.get_global_rect()
			picker_window = scene_rect
			bar_with_picker = _view("galaxy_bar").get_global_rect()
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
				page_rect = _scene_window._frame.get_global_rect()
				bar_on_page = _view("galaxy_bar").get_global_rect()
				if shots != "":
					await host.shot(shots + "/native-research-page-0.png")
			pages += 1
			_scene_window._frame.gui_input.emit(_smoke_left_click())
			await get_tree().create_timer(0.5).timeout
	get_window().size = window_before
	await get_tree().create_timer(0.3).timeout
	set_ui_scale(scale_before, false)
	await get_tree().create_timer(0.3).timeout
	var stayed := page_rect.position.distance_to(picker_window.position) < 1.5 and page_rect.size.distance_to(picker_window.size) < 1.5 and \
		bar_on_page.end.x <= bar_with_picker.end.x + 0.5
	print("smoke: research bar on the page %s, with the picker %s" % [bar_on_page, bar_with_picker])
	print("smoke: research pages %d, aligned %s, result %s, window %s; window on the page %s, with the picker %s (stayed %s)" % [pages, aligned,
		box.get("r", {}), _scene_window.visible if _scene_window else false, page_rect, picker_window, stayed])
	return stayed and box.has("r") and bool(box.r.get("ok", false)) and pages == (1 if "--research-hyper" in OS.get_cmdline_user_args() else 2) and aligned and _scene_window != null and not _scene_window.visible


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
	# Side by side once they have settled (owner 2026-11-23: the build pane overlapped the colony
	# pane a little when opened from here).
	await get_tree().create_timer(1.5).timeout
	var colony_rect: Rect2 = _view("colony_pane").get_global_rect()
	var build_rect: Rect2 = _view("build_pane").get_global_rect()
	print("smoke: build detour panes: colony %s, build %s, gap %.1f" % [colony_rect, build_rect, build_rect.position.x - colony_rect.end.x])
	if shots != "":
		await host.shot(shots + "/native-colonies-build-settled.png")
	if build_rect.position.x < colony_rect.end.x:
		print("smoke: FAILED: the build pane overlaps the colony pane")
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
	# A planet no free colony ship can reach (owner 2026-11-23: a black hole in the way): selected,
	# then SEND COLONY SHIP says why in a popup and sends nothing. Only where the save has one.
	if panel._result.get("colony_ships", false):
		for p: Dictionary in panel.planets:
			if int(p.get("best_colony_eta", -1)) < 0:
				var star := int(p.get("star", -1))
				panel._select(int(p.planet), star)
				var sent := [false]
				var watch := func(d: Dictionary) -> void:
					if String(d.get("verb", "")) == "planets_send":
						sent[0] = true
				panel.command.connect(watch)
				panel.colony_button.button_pressed = true
				await get_tree().process_frame
				panel.command.disconnect(watch)
				var popup: Control = preload("views/info_popup.gd")._current
				var shown := popup != null and is_instance_valid(popup) and popup.is_visible_in_tree()
				print("smoke: planets: unreachable %s (blocked %s): popup %s, sent %s, armed '%s'" % [String(p.name), p.get("blocked", false),
					shown, sent[0], panel.send_kind])
				if shots != "":
					await host.shot(shots + "/native-planets-unreachable.png")
				preload("views/info_popup.gd").close_current()
				if not shown or sent[0] or panel.send_kind != "":
					return false
				break
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
			print("smoke: sent a colony ship to %s: on its way %s, disarmed %s" % [target.name, after.get("colony_eta", -1), panel.send_kind == ""])
			if panel.send_kind != "":
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
	# The ship's card in the battle scan's look (owner 2026-11-23): drive, armour and computer named.
	var texts: Array = panel._detail.find_children("*", "Label", true, false).map(func(l: Label) -> String: return l.text)
	var named := ["DRIVE", "ARMOUR", "COMPUTER", "SHIELD"].all(func(t: String) -> bool: return t in texts)
	print("smoke: fleets detail card: systems named %s" % named)
	if not named:
		return false
	# A right click on a warship's tile: its card beside it.
	var warship := -1
	for s: Dictionary in panel.fleet.ships:
		if s.get("combat", false) and panel._tiles.has(int(s.id)):
			warship = int(s.id)
			break
	if warship >= 0:
		var click := InputEventMouseButton.new()
		click.button_index = MOUSE_BUTTON_RIGHT
		click.pressed = true
		(panel._tiles[warship] as Control).gui_input.emit(click)
		await get_tree().create_timer(0.6).timeout
		var popup: Control = preload("views/info_popup.gd")._current
		var shown := popup != null and is_instance_valid(popup) and popup.is_visible_in_tree() and not popup.find_children("*", "VBoxContainer", true, false).filter(func(n: Node) -> bool: return n.get_script() == preload("views/ship_card.gd")).is_empty()
		print("smoke: fleets right click on ship %d: card popup %s" % [warship, shown])
		if shots != "":
			await host.shot(shots + "/native-fleets-card.png")
		preload("views/info_popup.gd").close_current()
		if not shown:
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
		panel._act("IGNORE", panel.picked)
		await get_tree().create_timer(0.3).timeout
		var ignoring: bool = panel._race(panel.picked).get("ignoring", false)
		panel._act("IGNORE", panel.picked)
		await get_tree().create_timer(0.3).timeout
		print("smoke: races: ignore toggled to %s and back to %s" % [ignoring, panel._race(panel.picked).get("ignoring", false)])
		if ignoring == panel._race(panel.picked).get("ignoring", false):
			return false
	# Every action in two clicks (owner, 2026-11-20): armed, Esc cancels; armed again, a click on an
	# empire that can take it sends the action for that empire (caught here: the diplomacy smoke
	# opens the audience, the IGNORE check above toggles), and all are disarmed after.
	var audience: Button = panel._buttons["AUDIENCE"]
	audience.button_pressed = true
	await get_tree().process_frame
	var armed_text: String = audience.text
	var esc := InputEventKey.new()
	esc.keycode = KEY_ESCAPE
	esc.pressed = true
	Input.parse_input_event(esc)
	await get_tree().create_timer(0.2).timeout
	var esc_disarmed: bool = panel.armed_action == ""
	var connections: Array = panel.command.get_connections()
	for c: Dictionary in connections:
		panel.command.disconnect(c.callable)
	var sent := {}
	var catch := func(dict: Dictionary): sent.merge(dict, true)
	panel.command.connect(catch)
	var picked_before: int = panel.picked
	var results := {}
	for action: String in ["AUDIENCE", "DECLARE WAR", "IGNORE"]:
		var b: Button = panel._buttons[action]
		if b.disabled:
			results[action] = "disabled"
			continue
		sent.clear()
		b.button_pressed = true
		await get_tree().process_frame
		var others_off := true
		for k: String in panel._buttons:
			if k != action and (panel._buttons[k] as Button).button_pressed:
				others_off = false
		for slot: PanelContainer in panel._slots.get_children():
			var probe := InputEventMouseButton.new()
			probe.button_index = MOUSE_BUTTON_LEFT
			probe.pressed = true
			slot.gui_input.emit(probe)
			if sent.has("verb"):
				break
		results[action] = "%s for %s, others off %s, disarmed %s" % [sent.get("verb", "nothing"), sent.get("player", sent.get("screen", "?")),
			others_off, panel.armed_action == ""]
		var expected := {"AUDIENCE": "audience", "DECLARE WAR": "declare_war", "IGNORE": "toggle_ignore"}
		if String(sent.get("verb", "")) != expected[action] or panel.armed_action != "" or not others_off:
			print("smoke: races action %s: %s" % [action, results[action]])
			panel.command.disconnect(catch)
			for c: Dictionary in connections:
				panel.command.connect(c.callable)
			return false
	panel.command.disconnect(catch)
	for c: Dictionary in connections:
		panel.command.connect(c.callable)
	print("smoke: races actions: armed '%s', Esc disarmed %s; %s" % [armed_text, esc_disarmed, results])
	if armed_text != "PICK AN EMPIRE…" or not esc_disarmed:
		return false
	# The report flyout (owner, 2026-11-20): beside the panel, the empire's techs under the four
	# filters, the ruler, its spies and its relations.
	var report_target := -1
	for r: Dictionary in panel.races:
		if r.contact and not r.eliminated:
			report_target = int(r.player)
			break
	# A met empire's REPORT tab (owner, 2026-11-20: down each card's side): one click opens it, its
	# tab lit; all cards are one height, the portraits at it.
	var heights := {}
	var tabs: Array = []
	for slot: Control in panel._slots.get_children():
		heights[roundi(slot.size.y)] = true
		for t: Node in slot.find_children("*", "Button", true, false):
			if t.get_script() == preload("views/side_tab.gd"):
				tabs.append(t)
	print("smoke: races cards: heights %s, %d report tabs, panel width %.0f" % [heights.keys(), tabs.size(), panel.size.x])
	if heights.size() != 1 or tabs.is_empty():
		return false
	(tabs[0] as Button).pressed.emit()
	await get_tree().create_timer(1.5).timeout
	var report: Control = panel._report
	report_target = int(report.player) if report else -1
	var tab_lit := false
	for slot: Control in panel._slots.get_children():
		for t: Node in slot.find_children("*", "Button", true, false):
			if t.get_script() == preload("views/side_tab.gd") and (t as Button).button_pressed:
				tab_lit = true
	print("smoke: races report tab lit %s" % tab_lit)
	var tech_rows: int = report._rows.get_child_count() if report else 0
	var filters: int = report._filters.get_child_count() if report else 0
	var report_rect: Rect2 = report.get_global_rect() if report else Rect2()
	var beside: bool = report != null and report_rect.end.x <= panel.get_global_rect().position.x + 1.0 and report_rect.position.x >= 0.0
	print("smoke: races report on %d: shown %s, title '%s', %d filters, %d rows, side %d items, beside the panel %s (%s), app '%s'" % [report_target,
		report != null and report.visible, report._title.text if report else "", filters, tech_rows,
		report._side.get_child_count() if report else 0, beside, report_rect, report._app_title.text if report else ""])
	if shots != "":
		await host.shot(shots + "/native-race-report.png")
	if report == null or not report.visible or filters != 4 or tech_rows == 0 or not beside or report._side.get_child_count() < 4:
		return false
	(report._filters.get_child(2) as Button).pressed.emit() # WEAPONS
	await get_tree().create_timer(0.5).timeout
	if shots != "":
		await host.shot(shots + "/native-race-report-weapons.png")
	report.close()
	panel.picked = picked_before
	panel.show_races(panel.result)
	if not await _smoke_side_resize(panel, shots, "races"):
		return false
	panel.close()
	await get_tree().create_timer(0.6).timeout
	return not tab.button_pressed


## WP3.6 (`--design`): EDIT on a design opens the Ship Design panel; a weapon is added (the first
## beam, then one more), its arc changed, a special added, the size changed; CANCEL leaves the slot
## as it was; again, a weapon added and BUILD writes the slot and brings the list back.
## Newest pane on top (owner 2026-11-23): a colony with its build list open, then a ship design
## and its parts list: the design panel and the parts list over both colony panes, the frame over all.
func _smoke_layering(shots: String) -> bool:
	var star := -1
	var colony := -1
	for c in 64:
		var data: Dictionary = await _query_args({"what": "colony", "colony": c})
		if data.get("ok", false) and not data.get("outpost", false):
			star = int(data.star)
			colony = c
			break
	if colony < 0:
		print("smoke: layering: no colony")
		return false
	var listed: Dictionary = await _query_args({"what": "designs"})
	if (listed.get("designs", []) as Array).is_empty():
		print("smoke: layering: no designs")
		return false
	# A design with its parts list first, closed again: the list stays in the layer, below what
	# opens after it (the owner's case).
	_on_view_action("designs")
	await get_tree().create_timer(1.0).timeout
	_on_view_command({"verb": "open", "screen": Screens.DESIGN, "slot": int(listed.designs[0].slot)}, _view("design_list"))
	await get_tree().create_timer(1.2).timeout
	_view("design_panel")._open_parts("weapons")
	await get_tree().create_timer(0.5).timeout
	_view("design_panel").close()
	await get_tree().create_timer(0.5).timeout
	await _open_colony_at(star, colony, true)
	await get_tree().create_timer(1.5).timeout
	var colony_pane := _view("colony_pane")
	var build := _view("build_pane")
	if not colony_pane.visible or not build.visible:
		print("smoke: layering: colony pane %s, build pane %s" % [colony_pane.visible, build.visible])
		return false
	_on_view_action("designs")
	await get_tree().create_timer(1.0).timeout
	_on_view_command({"verb": "open", "screen": Screens.DESIGN, "slot": int(listed.designs[0].slot)}, _view("design_list"))
	await get_tree().create_timer(1.2).timeout
	var panel := _view("design_panel")
	panel._open_parts("weapons")
	await get_tree().create_timer(0.5).timeout
	var parts: Control = panel._parts
	if shots != "":
		await host.shot(shots + "/native-layering.png")
	var below := maxi(colony_pane.get_index(), build.get_index())
	print("smoke: layering: colony %d, build %d, design %d, parts %d, frame %d (of %d)" % [colony_pane.get_index(), build.get_index(),
		panel.get_index(), parts.get_index() if parts != null else -1, _frame_view.get_index(), native_layer.get_child_count()])
	if parts == null or not parts.visible or not panel.visible:
		return false
	if parts.get_index() < below or panel.get_index() < below or _frame_view.get_index() != native_layer.get_child_count() - 1:
		return false
	# A design saved with the build list open: the list asks for its options again (owner
	# 2026-11-23: REFIT offered a new design only once the build list was opened again).
	var refit_before: Dictionary = build.options.get("refit", {})
	build.options["refit"] = {}
	_on_design_built(int(listed.designs[0].slot))
	await get_tree().create_timer(0.8).timeout
	var refit_after: Dictionary = build.options.get("refit", {})
	print("smoke: layering: build list options again after a design: refit %s (before %s)" % [not refit_after.is_empty(), not refit_before.is_empty()])
	if refit_after.is_empty() and not refit_before.is_empty():
		return false
	_view("design_list").close()
	await get_tree().create_timer(0.3).timeout
	panel.close()
	await get_tree().create_timer(0.5).timeout
	return true


func _smoke_design_panel(shots: String) -> bool:
	_on_view_action("designs")
	await get_tree().create_timer(1.0).timeout
	var designs := _view("design_list")
	var before: Dictionary = await _query_args({"what": "designs"})
	if (before.get("designs", []) as Array).is_empty():
		print("smoke: design: no designs to edit")
		return false
	if shots != "":
		await host.shot(shots + "/native-design-list.png")
	print("smoke: design: list before %s" % [(before.get("designs", []) as Array).map(func(d: Dictionary) -> String:
		return "%d:%s%s" % [int(d.slot), d.hull_name, " (locked: %s)" % d.get("needs", "") if d.get("locked", false) else ""])])
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
	# Each slot keeps its hull (owner, 2026-11-20), or takes the largest researched one where its
	# hull isn't; none comes out locked.
	var largest := 3
	for d: Dictionary in auto.get("designs", []):
		if not d.get("locked", false):
			largest = maxi(largest, int(d.hull))
	var by_slot := {}
	for d: Dictionary in before.get("designs", []):
		by_slot[int(d.slot)] = d
	for d: Dictionary in auto.get("designs", []):
		var was: Dictionary = by_slot.get(int(d.slot), {})
		var expected: int = int(was.get("hull", -1)) if not was.get("locked", false) else largest
		if d.get("locked", false) or (not was.is_empty() and int(d.hull) != expected):
			print("smoke: design: auto design slot %d: hull %d (was %d%s), locked %s" % [int(d.slot), int(d.hull), int(was.get("hull", -1)),
				", locked" if was.get("locked", false) else "", d.get("locked", false)])
			return false
	before = auto
	var slot := int(before.designs[0].slot)
	var cost_before := int(before.designs[0].cost)
	_on_view_command({"verb": "open", "screen": Screens.DESIGN, "slot": slot}, designs)
	await get_tree().create_timer(1.2).timeout
	var panel := _view("design_panel")
	# Titans and Doom Stars only with their tech (owner, 2026-11-20): the size menu offers only the
	# available hulls, and the list marks a design on a hull that isn't as locked.
	var available: Array = (panel.design.get("sizes", []) as Array).filter(func(z: Dictionary) -> bool: return z.get("available", true)).map(
		func(z: Dictionary) -> int: return int(z.size))
	var listed_hulls: Array = (before.get("designs", []) as Array).filter(func(d: Dictionary) -> bool: return not d.get("locked", false)).map(
		func(d: Dictionary) -> int: return int(d.hull))
	for d: Dictionary in before.get("designs", []):
		if d.get("locked", false) == (int(d.hull) in available):
			print("smoke: design: slot %d hull %d locked %s against the hulls available" % [int(d.slot), int(d.hull), d.get("locked", false)])
			return false
	var menu_sizes: Array = []
	for i in panel._size.item_count:
		menu_sizes.append(panel._size.get_item_id(i))
	print("smoke: design: hulls available %s, size menu %s, listed designs' hulls %s" % [available, menu_sizes, listed_hulls])
	for hull: int in listed_hulls + menu_sizes:
		if not hull in available and hull != int(panel.design.get("size", -1)):
			print("smoke: design: hull %d shown without its tech" % hull)
			return false
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
	# AUTO (owner, 2026-11-20): this design redone for its hull, in the editor only; CANCEL
	# below must still leave the slot as it was.
	var hull_before := int(panel.design.size)
	panel._send({"verb": "design_auto"})
	await get_tree().create_timer(0.6).timeout
	print("smoke: design: auto: '%s' hull %d (was %d), space %d/%d, cost %d, %d weapons, %d specials" % [panel.design.get("name", ""),
		int(panel.design.size), hull_before, int(panel.design.space_used), int(panel.design.hull_space), int(panel.design.cost),
		(panel.design.get("weapons", []) as Array).size(), (panel.design.get("specials", []) as Array).size()])
	if shots != "":
		await host.shot(shots + "/native-design-auto.png")
	if int(panel.design.size) != hull_before or (panel.design.get("weapons", []) as Array).is_empty() \
			or int(panel.design.space_used) > int(panel.design.hull_space):
		return false
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
	# A name typed and not confirmed with Enter (owner, 2026-11-20): BUILD keeps it.
	panel._name.grab_focus()
	panel._name.text = "Typed Name"
	panel._build.pressed.emit()
	await get_tree().create_timer(1.2).timeout
	var after_build: Dictionary = await _query_args({"what": "designs"})
	var built_cost := -1
	var built_name := ""
	for d: Dictionary in after_build.designs:
		if int(d.slot) == slot:
			built_cost = int(d.cost)
			built_name = String(d.name)
	print("smoke: design: built: panel %s, list %s, slot cost %d (editor showed %d), name '%s'" % [panel.visible, _view("design_list").visible,
		built_cost, cost_built, built_name])
	if built_name != "Typed Name":
		return false
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


## WP3.9b: the battle scene (views/combat_view.gd), on its own layer over the galaxy's and under
## the dialogs, so the game's own messages during a battle show over it.
var _combat: Control


func _combat_view() -> Control:
	if _combat == null:
		var layer := CanvasLayer.new()
		layer.name = "Combat"
		layer.layer = 2
		add_child(layer)
		move_child(layer, $Overlay.get_index())
		_combat = preload("views/combat_view.gd").new()
		_combat.name = "CombatView"
		_combat.art = art
		_combat.game = game
		_combat.theme = native_layer.theme
		_combat.frame_style = _built.get("frame")
		_combat.help_requested.connect(show_help) # the scan's right clicks
		_combat.tee = _built.get("tee", {})
		_combat.dialog_open = func() -> bool: return overlay.visible
		# The legacy view hides under a battle: its pointer image (the hidden combat screen's, frozen)
		# would replace the system's, and its keys would reach the battle's legacy input (owner,
		# 2026-11-15: a "no" cursor, and Wait pressed repeatedly turned Auto on).
		_combat.opened.connect(func(): legacy.hide())
		_combat.closed.connect(func():
			# The legacy view comes back only with a new frame from the game, different from the
			# battle's last (owner 2026-11-22: the legacy screen showed for a frame on the way back
			# to the galaxy); the native galaxy, when it takes over first, cancels it.
			if native_screen != Screens.LEGACY or _question_over_galaxy:
				return
			if native and not galaxy.stars.is_empty():
				# The galaxy as after a film: it stays until the game is back on the galaxy or a
				# legacy screen waits for the player (_process's hand-back).
				if not _turn_over_galaxy:
					_turn_over_galaxy = true
					var held_galaxy: Image = game.get_frame_image()
					_stale_frame = held_galaxy.get_data() if held_galaxy != null else PackedByteArray()
				legacy.hide()
				galaxy.show()
				_frame_view.show()
				for chrome in ["galaxy_bar", "side_tabs", "map_controls"]:
					_view(chrome).show()
				_turn_clearing = 0.0
				_turn_clearing_polls = int(game.get_stats().get("input_polls", 0))
				return
			var held: Image = game.get_frame_image()
			_stale_frame = held.get_data() if held != null else PackedByteArray()
			_legacy_after_frame = int(game.get_stats().frames)
			_legacy_waited = 0.0)
		layer.add_child(_combat)
	return _combat


func _in_battle() -> bool:
	return _combat != null and _combat.visible


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
		_landing_followup_since = Time.get_ticks_msec() / 1000.0 # until the new colony's panes are up
		_prepare_landed_colony(int(state.get("star", -1)), int(state.get("colony", -1)), state)
	_scene_window.show_page(id, request)


## After the colony landing (WP3.11a, owner 2026-11-19): the new colony and its build list, so the
## click that ends the landing arrives at the build decision.
func _open_landed_colony(star: int, colony: int, with_build := true) -> void:
	if star < 0 or colony < 0:
		_landing_followup_since = -1.0
		return
	var waited := 0.0
	while native_screen != Screens.MAIN and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	if native_screen != Screens.MAIN:
		_landing_followup_since = -1.0
		return
	await _open_colony_at(star, colony, with_build)
	_landing_followup_since = -1.0


## A landing's new colony and its build list on their way (since this time; -1 none): the next
## auto to-do waits for them (owner, 2026-11-20: a second colony base's question came up beside the
## first one's build list, which then stayed stuck behind it).
var _landing_followup_since := -1.0


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
		# Placed once per scene (owner, 2026-11-20, Glass: the window shifted left as the picker
		# came): the bar's research readout changes when the research is done ("none"), the bar
		# narrows, and its right end, which the window follows, moved.
		if _scene_window != null and _scene_window.is_open() and _scene_window.custom_rect.has_area():
			return
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
## How long an answered system-zoom popup's system stays for a follow-up question or its landing.
const POPUP_FOLLOW_UP_WAIT := 0.6


func _start_popup_map(dialog: Control, request: Dictionary) -> void:
	var zoomed_before := _popup_zoom
	_popup_zoom = int(request.get("zoom", -1))
	# A live window's next step (the bombing, sent again each bomb) keeps the system its first
	# step showed (owner, 2026-11-21: after the bombing the map stayed dimmed on that system, its
	# fly-back forgotten with the zoom).
	if _popup_zoom < 0 and zoomed_before >= 0 and bool(request.get("live", false)) and not request.get("pick_star", false):
		_popup_zoom = zoomed_before
	# The question after a system-zoom answer (the colony ship's "are you sure?" after its planet
	# pick; owner, 2026-11-20: it flew back out, then the landing flew in again) stays on that
	# system, its window beside it.
	if _popup_zoom < 0 and _popup_map_ending >= 0 and not request.get("pick_star", false):
		_popup_zoom = _popup_map_ending
	_popup_map_ending = -1
	if _scene_window != null and _scene_window.is_open():
		_popup_zoom = -1 # over a scene (the landing's occupation question): the map stays
	_popup_pick_star = bool(request.get("pick_star", false))
	var on_map: bool = (_popup_zoom >= 0 or _popup_pick_star) and galaxy.visible and not galaxy.stars.is_empty()
	dialog.placement = ("beside" if _popup_zoom >= 0 else "top") if on_map else ""
	dialog.bare_width = 0.0
	overlay.mouse_filter = Control.MOUSE_FILTER_IGNORE if on_map else Control.MOUSE_FILTER_STOP
	overlay.color.a = 0.0 if on_map else 0.45
	if not on_map:
		_popup_zoom = -1
		_popup_pick_star = false
		return
	if _popup_zoom >= 0:
		# The system in the middle of the map, the window just left of it (owner, 2026-11-20: it
		# sat at the screen's far left); the select-combat window's fleet on its right. A system
		# pane goes first (owner, 2026-11-14: a to-do item's window flew to its system with the old
		# system's pane still up); the camera flies on from where it is.
		var system_pane: Control = _views.get("system_pane")
		var framed := {}
		if galaxy.system_star == _popup_zoom and system_pane != null and system_pane.visible:
			# Its own system already open (COLONIZE in its pane, the confirm after a pick): the
			# pane and the framing stay; the window beside the system where it is.
			framed = _system_screen_frame(_popup_zoom)
		else:
			if system_pane != null and system_pane.visible:
				_keep_camera = true # no flight back to the galaxy on its closing
				system_pane.dismiss()
				_keep_camera = false
			galaxy.selected = _popup_zoom
			framed = _frame_system_centred(_popup_zoom)
		dialog.placement_at = Vector2(framed.centre.x - framed.radius - POPUP_BESIDE_GAP, framed.centre.y)
		_popup_system_frame = framed
		# The select-combat window: the player's fleet on the system's other side.
		if request.has("fleet"):
			var band := _pane_band()
			_combat_fleet_view().open(request.fleet, framed.centre.x + framed.radius + POPUP_BESIDE_GAP, band.x, band.y, _landscape_right_limit())
		if galaxy.system_star != _popup_zoom:
			galaxy.show_system(_popup_zoom, request.get("system", {}))
		galaxy.redraw()


func _end_popup_map() -> void:
	overlay.mouse_filter = Control.MOUSE_FILTER_STOP
	overlay.color.a = 0.45
	if _popup_zoom >= 0 and _panel_star != _popup_zoom:
		# Not at once: another question on the same system (or its landing) may follow.
		_popup_map_ending = _popup_zoom
		_popup_map_end_after(_popup_zoom)
	_popup_zoom = -1
	_popup_pick_star = false


var _popup_map_ending := -1 # the zoomed system, while its fly-back waits for a follow-up


## The fly-back from an answered system-zoom popup, unless a question on it came first (it keeps
## the system) or its landing began (that scene frames the colony itself).
func _popup_map_end_after(star: int) -> void:
	# At least a moment, and while the game is still busy, up to a limit: on a legacy screen, or
	# still working on a command sent from here (owner, 2026-11-20: the to-do's colonize runs as a
	# command from the galaxy, and over the share its landing took longer than the moment to
	# load, so the camera flew out and the landing flew it back in).
	var waited := 0.0
	while waited < POPUP_FOLLOW_UP_WAIT or (_game_busy() and waited < 10.0):
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
		if _popup_map_ending != star:
			return # a follow-up took it over
		if _scene_window != null and _scene_window.is_open():
			break
	if _popup_map_ending != star:
		return
	_popup_map_ending = -1
	if _scene_window != null and _scene_window.is_open() and String(_scene_window._state.get("scene", "")) == "landing":
		return
	if _popup_zoom < 0 and _panel_star != star:
		galaxy.selected = _panel_star
		galaxy.hide_system()
		galaxy.fly_back()


## The game is still at work on something the player started: on a legacy screen, handing over to
## one (the bridge holds the switch, input off, until the new screen has drawn: a landing loading
## over the share), or answering a command sent from here.
func _game_busy() -> bool:
	return native_screen != Screens.MAIN or get_viewport().is_input_disabled() or not _pending.is_empty()


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
	# A live window's step (the bombing's next bomb, or its STOP / CONTINUE): the window stays, the
	# game sends it again at once.
	if _views.has("dialog") and _views.dialog.stepping:
		game.answer_request(id, value)
		return
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
	_popup_closed()
	if text != "" or (_views.has("dialog") and _views.dialog._kind == "text"):
		game.answer_request_text(id, value, text)
	else:
		game.answer_request(id, value)


## The select-combat window's fleet side (views/combat_fleet.gd), in the overlay over the shade.
var _combat_fleet: Control


func _combat_fleet_view() -> Control:
	if _combat_fleet == null:
		_combat_fleet = preload("views/combat_fleet.gd").new()
		_combat_fleet.name = "CombatFleet"
		_combat_fleet.art = art
		_combat_fleet.theme = native_layer.theme
		_combat_fleet.hide()
		$Overlay.add_child(_combat_fleet)
	_combat_fleet.move_to_front()
	return _combat_fleet


## The question's window gone: the overlay down, the galaxy or the legacy view back.
func _popup_closed() -> void:
	if _combat_fleet != null and _combat_fleet.visible:
		_combat_fleet.close()
	galaxy.lit_owner = -1
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
		panel.built.connect(_on_design_built) # (help_requested: connected by _view)
	_view("design_list").close()
	_bring_up(panel)
	panel.open_panel(_side_panel_area(), slot)
	_view("side_tabs").show_open("designs", true)


func _on_design_built(_slot: int) -> void:
	# An open build list offers the new design (to build, and to refit to).
	var build: Control = _views.get("build_pane")
	if build != null and build.visible:
		build.refresh_options()
	var designs := _view("design_list")
	_bring_up(designs)
	designs.fit_bottom(_side_panel_area().end.y)
	designs.open()
	_view("side_tabs").show_open("designs", true)


## The fleet the next Fleets panel opens on (the Leaders panel's FLEETS), or -1.
var _fleet_for_panel := -1


func _open_side_panel(action_name: String) -> void:
	var side_view := _view(SIDE_PANELS[action_name])
	_side_panel_opens += 1
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
				side_view.item_chosen.connect(_on_info_summary_item) # (help_requested: connected by _view)
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
	if galaxy.measuring:
		galaxy.measure_from = star_id # measured from here; another click starts again there
		return
	if not galaxy.relocation_pick.is_empty():
		_relocation_pick_star(star_id)
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
	var opens := _side_panel_opens
	_send({"verb": "query", "what": "system", "star": star_id}, func(result: Dictionary):
		if not result.get("ok", false) or native_screen != Screens.MAIN:
			return
		if opens != _side_panel_opens:
			_send({"verb": "close_system"}, func(_r: Dictionary): pass) # the game's system box record too
			return # a side panel opened since (owner, 2026-11-21): the system doesn't open over it
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


## A system-zoom popup's system: centred in the map's free width (the screen less the side tabs),
## sized so a window fits on each side of it. Its centre and on-screen radius once the flight ends.
const POPUP_BESIDE_GAP := 18.0
const POPUP_SIDE_WIDTH := 470.0 # a window beside the system (the dialog's 460, the fleet's)
var _popup_system_frame := {}


func _frame_system_centred(star_id: int) -> Dictionary:
	var view := get_viewport().get_visible_rect().size
	var left := 8.0
	var right := view.x - 48.0
	var tabs: Control = _views.get("side_tabs")
	if tabs != null and tabs.visible:
		for b in tabs.find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				right = minf(right, (b as Control).get_global_rect().position.x - 8.0)
	# The box no bigger than the system view draws it (owner, 2026-11-14: larger came out muddy).
	var largest := view.y * SYSTEM_ROOM_PER_HEIGHT * SYSTEM_VIEW_SHARE * SYSTEM_ZOOM_CLOSER
	var room := clampf(right - left - 2.0 * (POPUP_SIDE_WIDTH + POPUP_BESIDE_GAP), minf(view.x * 0.22, largest), largest)
	var zoom: float = galaxy.zoom_for_scale(SYSTEM_VIEW_SCALE)
	galaxy.system_scale = room / (288.0 * zoom) # the system box fills the room between the windows
	var centre := Vector2((left + right) * 0.5, view.y * 0.5)
	galaxy.fly_to(galaxy.star_position(star_id), zoom, centre)
	return {"centre": centre, "radius": 0.5 * 288.0 * galaxy.system_scale * zoom}


## Where an open system is on the screen now: its centre and the radius of its box.
func _system_screen_frame(star_id: int) -> Dictionary:
	var view := get_viewport().get_visible_rect().size
	var zoom: float = galaxy.camera.zoom.x
	var centre: Vector2 = (galaxy.star_position(star_id) - galaxy.camera.position) * zoom + view * 0.5
	return {"centre": centre, "radius": 0.5 * 288.0 * galaxy.system_scale * zoom}


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


## The band the colony, build and landscape panes fill (owner, 2026-11-20: one height for all, at
## any zoom and theme): from under the top bar (the theme's content_top, its frame included) to
## above the map controls, less the gap.
func _pane_band() -> Vector2:
	var bar: Control = _view("galaxy_bar")
	var view_height := get_viewport().get_visible_rect().size.y
	var gap := maxf(float(bar.get_theme_constant("pane_gap", "Screen")), 6.0)
	var top := float(bar.get_theme_constant("content_top", "Screen"))
	var bottom := view_height - float(bar.get_theme_constant("inset", "Screen")) - gap
	var controls: Control = _views.get("map_controls")
	if controls != null and controls.is_visible_in_tree():
		# On the screen only (they slide away during the turn: a 7000 px band, 2026-11-21).
		bottom = minf(bottom, controls.get_global_rect().position.y - gap)
	return Vector2(top, maxf(bottom - top, 200.0))


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
## The colony's landscape (owner, 2026-11-20): a 4:3 frame as tall as the colony pane, after it,
## or after the build pane when that is out (pushed right); against the side tabs the build pane
## overlaps it by the difference. On screens with room for it beside the colony pane it opens with
## the pane; VIEW COLONY shows or hides it; hidden by hand it stays hidden for that colony.
var _landscape_hidden_for := -1


func _toggle_landscape(colony_id: int) -> void:
	var land := _view("colony_landscape")
	if land.shown() and int(land.colony_id) == colony_id:
		land.close()
		return
	_landscape_hidden_for = -1
	_show_landscape(colony_id)


func _on_colony_shown(colony_id: int) -> void:
	var land: Control = _views.get("colony_landscape")
	if land != null and land.shown():
		if int(land.colony_id) != colony_id:
			land.open(colony_id)
		_place_landscape.call_deferred()
	elif colony_id != _landscape_hidden_for and _landscape_fits_beside():
		_show_landscape(colony_id)
	else:
		_view("colony_landscape").prefetch(colony_id) # VIEW COLONY finds it ready


func _show_landscape(colony_id: int) -> void:
	var land := _view("colony_landscape")
	land.open(colony_id)
	_place_landscape.call_deferred()


## The colony pane's resting rect (not where it is mid-slide).
func _colony_pane_rest() -> Rect2:
	var pane := _view("colony_pane")
	return Rect2(Vector2(float(pane._shown_left), pane.get_global_rect().position.y), Vector2(maxf(pane.size.x, float(pane.WIDTH)), pane.size.y))


## Where the landscape may reach: the side tabs' left edge, less the gap.
func _landscape_right_limit() -> float:
	var land := _view("colony_landscape")
	var gap := maxf(get_theme_constant_from(land, "pane_gap"), 6.0)
	var right := get_viewport().get_visible_rect().size.x - gap
	var tabs: Control = _views.get("side_tabs")
	if tabs != null and tabs.visible:
		for b in tabs.find_children("*", "Button", true, false):
			if (b as Control).is_visible_in_tree():
				right = minf(right, (b as Control).get_global_rect().position.x - gap)
	return right


## The whole landscape fits between the colony pane and the side tabs (it then opens by itself).
func _landscape_fits_beside() -> bool:
	var land := _view("colony_landscape")
	var pane := _colony_pane_rest()
	var gap := maxf(get_theme_constant_from(land, "pane_gap"), 6.0)
	return pane.end.x + gap + land.frame_size_for(_pane_band().y).x <= _landscape_right_limit()


func _place_landscape() -> void:
	var land: Control = _views.get("colony_landscape")
	if land == null or not land.shown():
		return
	var pane := _colony_pane_rest()
	var gap := maxf(get_theme_constant_from(land, "pane_gap"), 6.0)
	var right := _landscape_right_limit()
	# The band's full height at any screen (owner, 2026-11-20). Without room beside the colony
	# pane (a 16:9 screen) it pops up centred over the panes, with its own close button (owner:
	# under the colony pane it hid the colony's controls).
	var band := _pane_band()
	var frame: Vector2 = land.frame_size_for(band.y)
	var view_width := get_viewport().get_visible_rect().size.x
	if frame.x > view_width - 16.0:
		frame = land.frame_size_for_width(view_width - 16.0)
	var left := pane.end.x + gap
	var build := _view("build_pane")
	if build.visible:
		left = float(build._shown_left) + build.size.x + gap
	var popped := not _landscape_fits_beside()
	var x := maxf(minf(left, right - frame.x), 8.0)
	if popped:
		x = floorf((view_width - frame.x) * 0.5)
	land.set_popped_up(popped)
	land.size = frame
	land.position.y = band.x
	land.move_to(x) # slides in when just opened, as the other panes
	if popped:
		land.move_to_front() # over the panes, the build pane too (owner, 2026-11-20)
	else:
		land.move_to_front()
		if build.visible:
			build.move_to_front() # over it where it overlaps
	_raise_chrome()


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
	_place_landscape.call_deferred() # pushed right by the build pane


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


## Measure distance (F9, Options > Distance; owner, 2026-11-20: over the native galaxy, not the
## legacy screen): a star click picks where it measures from, the hovered star shows the line and
## the parsecs (galaxy/measure.gd). A right click, Esc or the key again stops it.
var _measure_hint: Label


func _set_measuring(on: bool) -> void:
	if on == galaxy.measuring:
		return
	if on:
		_end_relocating()
	galaxy.measuring = on
	Input.set_default_cursor_shape(Input.CURSOR_CROSS if on else Input.CURSOR_ARROW)
	if on:
		_show_hint("MEASURE DISTANCE: click a star, then point at another. Right click or Esc to stop.")
	elif _measure_hint:
		_measure_hint.hide()


## A mode's line under the bar (measuring, picking a relocation).
func _show_hint(text: String) -> void:
	if _measure_hint == null:
		_measure_hint = Label.new()
		_measure_hint.theme_type_variation = &"Heading"
		_measure_hint.add_theme_font_size_override("font_size", 16)
		_measure_hint.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.9))
		_measure_hint.add_theme_constant_override("outline_size", 6)
		_measure_hint.mouse_filter = Control.MOUSE_FILTER_IGNORE
		native_layer.add_child(_measure_hint)
	_measure_hint.text = text
	_measure_hint.show()
	_measure_hint.reset_size()
	var bar: Control = _view("galaxy_bar")
	var top: float = bar.get_global_rect().end.y + 10.0 if bar.visible else 60.0
	_measure_hint.position = Vector2(floorf((get_viewport().get_visible_rect().size.x - _measure_hint.size.x) * 0.5), top)


## The system pane's RELOCATE NEW SHIPS (owner, 2026-11-20): the system closes, the map shows
## whole, and the hovered star gets the line with its distance and the ships' ETA (or why not);
## a click asks to confirm, OK sets it (`relocation_set`, the game's own checks) and the system
## comes back. A right click or Esc goes back to the system unchanged.
func _start_relocation_pick(from: int) -> void:
	_set_measuring(false)
	_end_relocating()
	_send({"verb": "query", "what": "relocation_targets", "from": from}, func(result: Dictionary):
		if not result.get("ok", false):
			return
		var by_star := {}
		for item: Dictionary in result.get("stars", []):
			by_star[int(item.star)] = item
		galaxy.relocation_pick = {"from": from, "stars": by_star, "to": int(result.get("to", -1))}
		var pane := _view("system_pane")
		if pane.visible:
			pane.close()
		await get_tree().create_timer(0.05).timeout
		galaxy._fly(Vector2(float(galaxy.data.width), float(galaxy.data.height)) * 0.5, galaxy.furthest_zoom())
		Input.set_default_cursor_shape(Input.CURSOR_CROSS)
		_show_hint("RELOCATE NEW SHIPS FROM %s: click the star they go to. Right click or Esc goes back." % _star_name(from).to_upper()))


func _star_name(star_id: int) -> String:
	if star_id < 0 or star_id >= galaxy.stars.size():
		return ""
	return String(galaxy.stars[star_id].get("label", "")).trim_prefix("(").trim_suffix(")")


var _relocation_question: ConfirmationDialog


func _relocation_pick_star(star_id: int) -> void:
	var pick: Dictionary = galaxy.relocation_pick
	var target: Dictionary = pick.stars.get(star_id, {})
	if star_id == int(pick.from) or not target.get("allowed", false) or is_instance_valid(_relocation_question):
		return # the line says why
	var eta := int(target.get("eta", 0))
	var parsecs := int(target.get("parsecs", 0))
	var text := "New ships built at %s will go to %s:\n%d parsec%s, about %d turn%s." % [_star_name(int(pick.from)), _star_name(star_id),
		parsecs, "" if parsecs == 1 else "s", eta, "" if eta == 1 else "s"]
	_relocation_question = Themes.confirm(native_layer, text, "Relocate", func():
		_send({"verb": "relocation_set", "from": int(pick.from), "to": star_id}, func(result: Dictionary):
			if result.get("set", false):
				_end_relocation_pick()))


## Out of the pick: back to the system (`back`), or not (a legacy screen took over).
func _end_relocation_pick(back := true) -> void:
	if galaxy.relocation_pick.is_empty():
		return
	var from := int(galaxy.relocation_pick.from)
	galaxy.relocation_pick = {}
	Input.set_default_cursor_shape(Input.CURSOR_ARROW)
	if _measure_hint:
		_measure_hint.hide()
	if is_instance_valid(_relocation_question):
		_relocation_question.queue_free()
	if back and native_screen == Screens.MAIN:
		_open_system(from)


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
		if arg == "--landscape":
			# VIEW COLONY natively (owner, 2026-11-20): on a screen with room, the landscape opens
			# with the colony pane, after it; CHANGE's build pane pushes it right; VIEW COLONY
			# hides and shows it.
			var window_before := get_window().size
			var wide := OS.get_environment("SMOKE_WINDOW").split("x")
			get_window().size = Vector2i(int(wide[0]), int(wide[1])) if wide.size() == 2 else Vector2i(1920, 700)
			if OS.get_environment("SMOKE_SCALE") != "":
				get_window().content_scale_factor = float(OS.get_environment("SMOKE_SCALE"))
			await get_tree().create_timer(0.6).timeout
			var source := {}
			var best := -1
			for c in 64:
				var data: Dictionary = await _query_args({"what": "colony", "colony": c})
				if data.get("ok", false) and not data.get("outpost", false) and (data.get("buildings", []) as Array).size() > best:
					best = (data.get("buildings", []) as Array).size()
					source = data
			art.clear() # cold: every picture asked for anew
			_open_colony_at(int(source.star), int(source.colony), false)
			# Never seen half built (owner, 2026-11-20): when it first shows, every layer is in.
			var t_open := Time.get_ticks_msec()
			var seen_partial := false
			var seen_after := -1
			while Time.get_ticks_msec() - t_open < 9000:
				await get_tree().process_frame
				var watching: Control = _views.get("colony_landscape")
				if watching != null and watching.visible and watching.modulate.a > 0.0:
					seen_partial = not watching.is_complete()
					seen_after = Time.get_ticks_msec() - t_open
					break
				if watching != null and not watching.visible and Time.get_ticks_msec() - t_open > 1500:
					break # a screen without room: VIEW COLONY below
			await get_tree().create_timer(1.0).timeout
			print("smoke: landscape first seen after %d ms, complete then %s" % [seen_after, not seen_partial])
			if seen_partial:
				await host.smoke_fail("the colony landscape showed before its pictures were in")
				return
			var land: Control = _views.get("colony_landscape")
			var pane := _view("colony_pane")
			var shown_by_itself: bool = land != null and land.visible
			var narrow := not shown_by_itself and not _landscape_fits_beside() # a screen without room: VIEW COLONY opens it
			if narrow:
				# Prefetched with the colony pane: VIEW COLONY shows it at once.
				var t_view := Time.get_ticks_msec()
				pane.landscape_toggled.emit(int(source.colony))
				while not land.is_complete() and Time.get_ticks_msec() - t_view < 9000:
					await get_tree().process_frame
				print("smoke: landscape by VIEW COLONY, prefetched: complete after %d ms" % (Time.get_ticks_msec() - t_view))
				await get_tree().create_timer(1.0).timeout
				land = _views.get("colony_landscape")
				shown_by_itself = land != null and land.visible
			var layers: int = (land.landscape.get("layers", []) as Array).size() if land else 0
			var view_w := get_viewport().get_visible_rect().size.x
			var centred: bool = land != null and absf(land.get_global_rect().get_center().x - view_w * 0.5) < 2.0 and land._close_button.visible
			var beside: bool = land != null and (centred if narrow else land.get_global_rect().position.x >= pane.get_global_rect().end.x - 1.0)
			print("smoke: landscape narrow %s: centred with a close button %s" % [narrow, centred])
			var rect_alone: Rect2 = land.get_global_rect() if land else Rect2()
			print("smoke: landscape for colony %d (%d buildings): shown by itself %s, %d layers, at %s beside the pane %s (pane ends %.0f), fits %s" % [
				int(source.colony), best, shown_by_itself, layers, rect_alone, beside, pane.get_global_rect().end.x, _landscape_fits_beside()])
			if shots != "":
				await host.shot(shots + "/native-landscape.png")
			_open_build_pane(int(source.colony), false)
			await get_tree().create_timer(1.5).timeout
			var build := _view("build_pane")
			var rect_build: Rect2 = land.get_global_rect()
			var pushed: bool = rect_build.position.x > rect_alone.position.x + 1.0
			var over_tabs: bool = rect_build.end.x > _landscape_right_limit() + 1.0
			# Popped up (narrow screens): over the build pane (owner, 2026-11-20).
			var over_build: bool = not narrow or land.get_index() > build.get_index()
			print("smoke: landscape over the build pane when popped up: %s" % over_build)
			# One band for the three (owner, 2026-11-20): the same top and height, at any zoom.
			var band := _pane_band()
			var in_band := true
			for r: Rect2 in [rect_build, build.get_global_rect(), pane.get_global_rect()]:
				if absf(r.position.y - band.x) > 1.0 or absf(r.size.y - band.y) > 1.0:
					in_band = false
			print("smoke: landscape band top %.1f height %.1f: landscape, build and colony panes in it %s" % [band.x, band.y, in_band])
			print("smoke: landscape with the build pane: at %s (pushed %s), build pane %s, colony pane %s, system pane %s, landscape over the side tabs %s" % [rect_build, pushed,
				build.get_global_rect(), pane.get_global_rect(), _view("system_pane").get_global_rect(), rect_build.end.x > _landscape_right_limit() + 1.0])
			if shots != "":
				await host.shot(shots + "/native-landscape-build.png")
			build.cancel()
			await get_tree().create_timer(1.0).timeout
			var back: bool = absf(land.get_global_rect().position.x - rect_alone.position.x) < 2.0
			pane.landscape_toggled.emit(int(source.colony)) # VIEW COLONY: hidden
			await get_tree().create_timer(0.3).timeout
			var hidden_now: bool = not land.shown()
			pane.refresh() # shown again: stays hidden for it
			await get_tree().create_timer(1.0).timeout
			var stays_hidden: bool = not land.visible
			pane.landscape_toggled.emit(int(source.colony)) # VIEW COLONY: shown
			await get_tree().create_timer(1.0).timeout
			var shown_again: bool = land.visible
			print("smoke: landscape back after the build pane %s; VIEW COLONY hid it %s, stayed hidden %s, showed it again %s" % [back, hidden_now,
				stays_hidden, shown_again])
			get_window().size = window_before
			await get_tree().create_timer(0.5).timeout
			if not shown_by_itself or layers < 3 or not beside or (not pushed and not narrow) or not back or not hidden_now or not stays_hidden or not shown_again \
					or over_tabs or not in_band or not over_build:
				await host.smoke_fail("the colony landscape did not open, move or toggle as it should")
				return
			game.request_game_quit()
			return
		if arg == "--officer-badge":
			# Owner, 2026-11-21: a ship with an officer aboard is marked in the select-combat
			# window's Your Fleet. A real fleet, the first ship given a real ship officer's card.
			var fleet_data := {}
			for st: Dictionary in galaxy.stars:
				var cf: Dictionary = await _query_args({"what": "combat_fleet", "star": int(st.id)})
				if cf.get("ok", false) and not (cf.get("ships", []) as Array).is_empty():
					fleet_data = cf
					break
			var officers: Dictionary = await _query_args({"what": "leaders", "type": "ship"})
			var card: Dictionary = {}
			for l: Dictionary in officers.get("leaders", []):
				if not l.get("for_hire", false):
					card = l.card
					break
			if fleet_data.is_empty() or card.is_empty():
				print("smoke: officer badge: fleet %s, leaders %s" % [JSON.stringify(fleet_data).left(120), JSON.stringify(officers).left(300)])
				await host.smoke_fail("no fleet or no ship officer for the badge")
				return
			fleet_data.ships[0]["officer"] = card
			var band := _pane_band()
			_combat_fleet_view().open(fleet_data, get_viewport().get_visible_rect().size.x * 0.55, band.x, band.y, _landscape_right_limit())
			await get_tree().create_timer(1.5).timeout
			var row: Control = _combat_fleet._rows.get_child(0)
			var badge := row.find_children("*", "PanelContainer", false, false)
			print("smoke: officer badge: %s on '%s', tooltip '%s'" % [not badge.is_empty(), fleet_data.ships[0].name, (row as Button).tooltip_text])
			if shots != "":
				await host.shot(shots + "/officer-badge.png")
			_combat_fleet.close()
			if badge.is_empty():
				await host.smoke_fail("the officer's ship was not marked")
				return
			game.request_game_quit()
			return
		if arg == "--attack-bomb":
			# Owner, 2026-11-21: a colony without defences goes straight from the attack window to
			# the bombing; after it the map stayed dimmed on that system with no window.
			var target_star := -1
			var target_colony := -1
			for star: Dictionary in galaxy.stars:
				var sys: Dictionary = await _query_args({"what": "system", "star": int(star.id)})
				for planet: Dictionary in sys.get("planets", []):
					var colony: Dictionary = planet.get("colony", {})
					if not colony.is_empty() and not colony.get("mine", false) and not colony.get("outpost", false):
						target_star = int(star.id)
						target_colony = int(colony.id)
						break
				if target_star >= 0:
					break
			var first := {}
			_send({"verb": "preview_popup", "popup": "attack_target", "star": target_star}, func(r: Dictionary): first["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.1).timeout
				if _views.dialog.visible and String(_views.dialog.popup_name) == "attack_target":
					break
			await get_tree().create_timer(1.5).timeout
			_views.dialog._answer(_views.dialog.OPTION_ANSWER) # the first target
			for i in 30:
				await get_tree().create_timer(0.1).timeout
				if first.has("r"):
					break
			var bomb := {}
			_send({"verb": "preview_popup", "popup": "colony_bombing", "colony": target_colony, "hits": 8}, func(r: Dictionary): bomb["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.1).timeout
				if _views.dialog.visible and String(_views.dialog.popup_name) == "colony_bombing":
					break
			await get_tree().create_timer(1.0).timeout
			var during := "system %d, popup zoom %d" % [galaxy.system_star, _popup_zoom]
			var dialog: Control = _views.dialog
			if dialog._bombing_buttons.get_child_count() > 1:
				(dialog._bombing_buttons.get_child(1) as Button).pressed.emit() # DROP ALL
			await get_tree().create_timer(1.0).timeout
			(dialog._bombing_buttons.get_child(dialog._bombing_buttons.get_child_count() - 1) as Button).pressed.emit() # CLOSE
			for i in 30:
				await get_tree().create_timer(0.1).timeout
				if bomb.has("r"):
					break
			await get_tree().create_timer(0.9).timeout # the fly-back's own wait, before the stale-system check
			var after := "system %d, others_alpha %.2f, overlay %s, popup zoom %d" % [galaxy.system_star, galaxy.others_alpha, overlay.visible, _popup_zoom]
			print("smoke: attack then bombing at %d (colony %d): during the bombing %s; 0.9 s after it closed %s" % [target_star, target_colony, during, after])
			if shots != "":
				await host.shot(shots + "/attack-bomb-after.png")
			if target_star < 0 or not bomb.has("r") or galaxy.system_star >= 0:
				await host.smoke_fail("after the attack's bombing the map stayed on the system")
				return
			game.request_game_quit()
			return
		if arg == "--system-endturn":
			# Owner, 2026-11-21: zoomed into a system, END TURN from the to-do list: after the turn
			# the stars stayed dimmed with no system pane.
			var home := -1
			for st: Dictionary in galaxy.stars:
				if int(st.get("label_owner", -1)) == int(galaxy.data.get("player", -2)):
					home = int(st.id)
					break
			_open_system(home)
			await get_tree().create_timer(2.0).timeout
			# Whatever holds the turn answered, so END TURN is enabled.
			if not await _smoke_clear_todo():
				return
			_open_system(home)
			await get_tree().create_timer(2.0).timeout
			# A discovery that gave something opens its window by itself, once (owner 2026-11-23:
			# a splinter colony joined unnoticed); here a made-up item, so only the opening counts.
			var keep_todo := _todo
			var fake := {"kind": "new_system", "item": 999, "star": home, "gain": true, "text": "smoke"}
			_todo = keep_todo.duplicate()
			_todo["items"] = [fake]
			_todo["unread"] = 1 # as a real discovery is
			print("smoke: system-endturn: may open: screen %s, overlay %s, legacy %s, galaxy %s" % [native_screen, overlay.visible, legacy.visible, galaxy.visible])
			var opened_before := _todo_auto_opened.size()
			var first := _gain_discovery_step()
			var again := _gain_discovery_step()
			_todo = keep_todo
			print("smoke: system-endturn: gain discovery opened %s, again %s (may open %s)" % [first, again, _todo_may_open()])
			_todo_auto_opened.erase("new_system:999:-1:%d" % home)
			await get_tree().create_timer(0.5).timeout
			_smoke_answer()
			if not first or _todo_auto_opened.size() != opened_before or again:
				print("smoke: FAILED: gain discovery")
				return
			# The list open when another screen takes over, a fuller list arriving meanwhile, the
			# galaxy back (owner 2026-11-23: it came back with its old 2 items, and the full list
			# opened only once that was closed): it comes back with the full list, nothing pending.
			preload("views/info_popup.gd").close_current()
			_open_todo()
			await get_tree().create_timer(0.6).timeout
			var list_view := _view("todo_panel")
			var keep_list := _todo
			_on_native_screen(Screens.LEGACY)
			var fuller: Dictionary = keep_list.duplicate()
			var more: Array = (keep_list.get("items", []) as Array).duplicate()
			more.append({"kind": "report", "item": -1, "report": "msg:smoke-late", "text": "smoke late message", "news": true, "done": false, "star": -1, "colony": -1})
			fuller["items"] = more
			fuller["unread"] = int(keep_list.get("unread", 0)) + 1
			_on_snapshot("todo", fuller)
			_on_native_screen(Screens.MAIN)
			await get_tree().create_timer(0.3).timeout
			var rows: int = (list_view.items_box.get_children() as Array).filter(func(r: Node) -> bool: return r is Button and String((r as Button).text).contains("smoke late message")).size()
			print("smoke: system-endturn: list back after a screen: visible %s, late message shown %s, pending %s" % [list_view.visible, rows > 0, _todo_pending])
			list_view.close()
			_on_snapshot("todo", keep_list)
			await get_tree().create_timer(0.5).timeout
			if not list_view.visible and rows == 0:
				pass
			if rows == 0 or _todo_pending:
				print("smoke: FAILED: the list came back without what arrived while it was hidden")
				return
			_on_view_action("turn")
			await get_tree().create_timer(1.0).timeout
			var todo_list := _view("todo_panel")
			print("smoke: system-endturn: system pane %s on %d, to-do list %s, END TURN %s" % [_view("system_pane").visible, galaxy.system_star,
				todo_list.visible, (not todo_list._end_turn.disabled) if todo_list._end_turn else "none"])
			# END TURN on the title's row (owner 2026-11-23), whatever the list's length.
			if todo_list._end_turn != null:
				var row: Node = todo_list._end_turn.get_parent()
				var title_beside: bool = row.name == "TitleRow" and row.get_node_or_null("Title") != null and row.get_index() == 0
				print("smoke: system-endturn: END TURN in %s, title beside %s" % [row.name, title_beside])
				if not title_beside:
					print("smoke: FAILED: END TURN not on the title's row")
					return
			if shots != "" and todo_list.visible:
				await host.shot(shots + "/todo-end-turn.png")
			# A turn's messages never seen are carried into the next turn's list (owner 2026-11-23):
			# a made-up unread one in this turn's list, as if the list had stayed closed.
			var carried_turn := int(_todo.get("stardate", -1))
			var fake_items: Array = (_todo.get("items", []) as Array).duplicate()
			fake_items.append({"kind": "report", "item": -1, "report": "msg:smoke", "text": "smoke carried message", "news": true, "done": false, "star": -1, "colony": -1})
			_todo["items"] = fake_items
			if todo_list.visible and todo_list._end_turn != null:
				todo_list._end_turn.pressed.emit()
			else:
				_send({"verb": "end_turn"})
			_todo_shown_stardate = -1
			var waited := 0.0
			while waited < 3.0 and native_screen == Screens.MAIN:
				await get_tree().create_timer(0.1).timeout
				waited += 0.1
			waited = 0.0
			while waited < 90.0:
				await get_tree().create_timer(0.5).timeout
				waited += 0.5
				_smoke_answer()
				if native_screen == Screens.MAIN and not overlay.visible and waited > 3.0:
					break
			for i in 8:
				await get_tree().create_timer(0.5).timeout
				_smoke_answer()
			var state := "system_star %d, system empty %s, others_alpha %.2f, system pane %s, panel star %d, to-do %s" % [galaxy.system_star,
				galaxy.system.is_empty(), galaxy.others_alpha, _view("system_pane").visible, _panel_star, _view("todo_panel").visible]
			print("smoke: system-endturn after the turn: %s" % state)
			var carried_text := "%d.%d: smoke carried message" % [carried_turn / 10, carried_turn % 10]
			var carried_now: Array = (_todo.get("items", []) as Array).filter(func(i: Dictionary) -> bool: return String(i.get("text", "")) == carried_text)
			print("smoke: system-endturn carried: stardate %s -> %s, '%s' in the list %s (carried flag %s)" % [carried_turn, _todo.get("stardate", -1),
				carried_text, not carried_now.is_empty(), carried_now[0].get("carried", false) if not carried_now.is_empty() else false])
			if carried_now.is_empty():
				await host.smoke_fail("an unseen turn's message was not carried into the next list")
				return
			_todo_backlog = [] # the made-up message goes
			if shots != "":
				await host.shot(shots + "/system-endturn.png")
			if (galaxy.others_alpha < 0.99 or galaxy.system_star >= 0) and not _view("system_pane").visible:
				await host.smoke_fail("after END TURN the map stayed dimmed for a system with no pane")
				return
			# The owner's state forced: a system on the map, its pane gone (a stale view): the
			# galaxy comes back by itself.
			_view("system_pane").close()
			await get_tree().create_timer(1.0).timeout
			var sq: Dictionary = await _query_args({"what": "system", "star": home})
			galaxy.show_system(home, sq)
			await get_tree().create_timer(2.5).timeout
			print("smoke: system-endturn orphan system: system_star %d, others_alpha %.2f" % [galaxy.system_star, galaxy.others_alpha])
			if galaxy.system_star >= 0 or galaxy.others_alpha < 0.99:
				await host.smoke_fail("a system left on the map with no pane stayed")
				return
			game.request_game_quit()
			return
		if arg == "--scouts-special":
			# Owner, 2026-11-21: the scouts' window didn't say which planet had the special, nor
			# list the planets. A system with a planet's special, previewed.
			var found_star := -1
			var found_planet := -1
			var found_special := 0
			for st: Dictionary in galaxy.stars:
				var sq: Dictionary = await _query_args({"what": "system", "star": int(st.id)})
				for pl: Dictionary in sq.get("planets", []):
					if int(pl.get("special", 0)) > 0 and String(pl.get("special_name", "")) != "":
						found_star = int(st.id)
						found_planet = int(pl.planet)
						found_special = int(pl.special)
						break
				if found_star >= 0:
					break
			print("smoke: scouts special: star %d, planet %d, special %d" % [found_star, found_planet, found_special])
			if found_star < 0:
				await host.smoke_fail("no planet with a special in this save")
				return
			var ns := {}
			_send({"verb": "preview_popup", "popup": "new_system", "star": found_star, "special": found_special}, func(r: Dictionary): ns["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.1).timeout
				if _views.dialog.visible and String(_views.dialog.popup_name) == "new_system":
					break
			await get_tree().create_timer(1.5).timeout
			var rows: Dictionary = _views.dialog._planet_rows
			var marked := false
			for label in _views.dialog.find_children("*", "Label", true, false):
				if (label as Label).text.contains("\u2605"):
					marked = true
			print("smoke: scouts window: %d planet rows, the special's planet listed %s, marked %s, size %s" % [rows.size(), rows.has(found_planet), marked, _views.dialog.size])
			if shots != "":
				await host.shot(shots + "/scouts-special.png")
			var row: Button = rows.get(found_planet)
			if row != null:
				_views.dialog.planet_row_hovered.emit(found_planet)
				await get_tree().create_timer(0.5).timeout
				if shots != "":
					await host.shot(shots + "/scouts-special-hover.png")
				_views.dialog.planet_row_hovered.emit(-1)
			_views.dialog._answer(0)
			for i in 20:
				await get_tree().create_timer(0.1).timeout
				if ns.has("r"):
					break
			if rows.is_empty() or not rows.has(found_planet) or not marked:
				await host.smoke_fail("the scouts' window did not list the planets with the special's marked")
				return
			game.request_game_quit()
			return
		if arg == "--system-side":
			# Owner, 2026-11-21: a system open, a right side panel opened and closed: the stars
			# stayed dimmed as in a system, zoomed out, with no way back to the galaxy.
			var home := -1
			for st: Dictionary in galaxy.stars:
				if int(st.get("label_owner", -1)) == int(galaxy.data.get("player", -2)):
					home = int(st.id)
					break
			var bad := []
			for run: String in ["colonies", "planets", "fleets", "leaders", "races", "info", "colonies:quick", "leaders:quick", "colonies:colony"]:
				var panel_name := run.get_slice(":", 0)
				var how := run.get_slice(":", 1) if ":" in run else ""
				if how == "colony":
					var cs: Dictionary = await _query_args({"what": "system", "star": home})
					var col := -1
					for pl: Dictionary in cs.get("planets", []):
						if pl.get("colony") is Dictionary and pl.colony.get("mine", false) and not pl.colony.get("outpost", false):
							col = int(pl.colony.id)
							break
					_open_colony_at(home, col, false)
				else:
					_open_system(home)
				await get_tree().create_timer(0.05 if how == "quick" else 1.5).timeout
				_on_view_action(panel_name)
				await get_tree().create_timer(1.5).timeout
				var side := _view(SIDE_PANELS[panel_name])
				side.close()
				await get_tree().create_timer(1.5).timeout
				var state := "system_star %d, system empty %s, others_alpha %.2f, system pane %s, zoom %.3f" % [galaxy.system_star,
					galaxy.system.is_empty(), galaxy.others_alpha, _view("system_pane").visible, galaxy.scale.x]
				print("smoke: system then %s, closed: %s" % [run, state])
				if (galaxy.others_alpha < 0.99 or galaxy.system_star >= 0) != _view("system_pane").visible:
					bad.append(panel_name)
				if shots != "":
					await host.shot(shots + "/system-side-%s.png" % panel_name)
			if not bad.is_empty():
				await host.smoke_fail("the galaxy stayed dimmed with no system pane after: %s" % ", ".join(bad))
				return
			game.request_game_quit()
			return
		if arg == "--assign-officer":
			# Owner, 2026-11-21: assigning a ship officer to a warship elsewhere (the transit
			# question answered yes) froze the game and crashed it 5-10 s later.
			_on_view_action("leaders")
			await get_tree().create_timer(0.8).timeout
			var lp := _view("leaders_panel")
			lp._set_tab("ship")
			await get_tree().create_timer(1.0).timeout
			var officer := -1
			var officer_ship := -2
			for l: Dictionary in lp.leaders:
				if int(l.get("status", -1)) != 4:
					officer = int(l.leader)
					officer_ship = int(l.get("ship", -1))
					break
			var target := -1
			for pl: Dictionary in lp.places:
				for s: Dictionary in pl.get("ships", []):
					if int(s.id) != officer_ship and target < 0 and (officer_ship < 0 or not (pl.get("ships", []) as Array).any(func(x: Dictionary) -> bool: return int(x.id) == officer_ship)):
						target = int(s.id)
			print("smoke: assign officer %d (on ship %d) to ship %d" % [officer, officer_ship, target])
			if officer < 0 or target < 0:
				await host.smoke_fail("no officer or ship to assign")
				return
			lp.picked = officer
			lp.picked_ship = target
			lp._act("assign")
			for i in 30:
				await get_tree().create_timer(0.2).timeout
				_smoke_answer()
			var pings := 0
			for i in 15:
				var got := {}
				_send({"verb": "query", "what": "leaders", "type": "ship"}, func(r: Dictionary): got["r"] = r)
				for k in 20:
					await get_tree().create_timer(0.05).timeout
					if got.has("r"):
						break
				if got.has("r"):
					pings += 1
				await get_tree().create_timer(1.0).timeout
				_smoke_answer()
			var after: Dictionary = lp._leader(officer)
			print("smoke: after the assignment the game answered %d of 15 queries over 15 s; officer status %d, '%s'" % [pings,
				int(after.get("status", -1)), after.get("state", "")])
			# The leader picked (owner, 2026-11-21): the panel shows the fleet with their ship, its tile
			# ringed, the officer's portrait on it.
			lp.picked = -1
			lp.place = 0
			lp._show_place()
			lp.refresh()
			await get_tree().create_timer(1.0).timeout
			var card_index := -1
			for i in lp.leaders.size():
				if int(lp.leaders[i].leader) == officer:
					card_index = i
			var click := InputEventMouseButton.new()
			click.button_index = MOUSE_BUTTON_LEFT
			click.pressed = true
			if card_index >= 0:
				(lp._cards.get_child(card_index) as Control).gui_input.emit(click)
			await get_tree().create_timer(0.8).timeout
			var shown_ships: Array = (lp.places[lp.place].get("ships", []) as Array).map(func(s: Dictionary) -> int: return int(s.id))
			var ringed := 0
			var badged := 0
			for tile in lp._ships.get_children():
				for child in tile.get_children():
					if child is Panel:
						ringed += 1
					elif child is TextureRect:
						badged += 1
			print("smoke: leader %d picked: place shows ship %d %s, tiles ringed %d, officer badges %d" % [officer, target, target in shown_ships, ringed, badged])
			if shots != "":
				await host.shot(shots + "/leader-ship-marked.png")
			if not target in shown_ships or ringed != 1 or badged < 1:
				await host.smoke_fail("picking the officer did not show and mark their ship")
				return
			if pings < 15 or int(after.get("status", -1)) != 1:
				await host.smoke_fail("the game stopped answering after the officer assignment")
				return
			game.request_game_quit()
			return
		if arg == "--select-combat":
			await _smoke_window_setting()
			# The select-combat window (WP, owner 2026-11-20): an attack_target preview at a star
			# with another empire's colony; can the game answer a query while it waits?
			var sc_star := -1
			for st: Dictionary in galaxy.stars:
				var sys: Dictionary = await _query_args({"what": "system", "star": int(st.id)})
				for planet: Dictionary in sys.get("planets", []):
					var colony: Dictionary = planet.get("colony", {})
					if not colony.is_empty() and not colony.get("mine", false) and sc_star < 0:
						sc_star = int(st.id)
				if sc_star >= 0:
					break
			# The query on its own, at every star with ships of ours and someone else's.
			for st2: Dictionary in galaxy.stars:
				var cf: Dictionary = await _query_args({"what": "combat_fleet", "star": int(st2.id)})
				if cf.get("ok", false) and not (cf.ships as Array).is_empty():
					print("smoke: combat_fleet at %d: %d ships of ours, first %s; enemies %s; scanner %s" % [int(st2.id), (cf.ships as Array).size(),
						JSON.stringify(cf.ships[0]).left(700), JSON.stringify(cf.enemies), cf.scanner])
					break
			var sc_box := {}
			_send({"verb": "preview_popup", "popup": "attack_target", "star": sc_star}, func(r: Dictionary): sc_box["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.1).timeout
				if _views.has("dialog") and _views.dialog.visible:
					break
			var t_ask := Time.get_ticks_msec()
			var sc_mine := -1
			for f: Dictionary in galaxy.fleets:
				if int(f.owner) == int(galaxy.data.get("player", -1)):
					sc_mine = int(f.ship)
					break
			var answered := {}
			_send({"verb": "query", "what": "ship_detail", "ship": sc_mine}, func(r: Dictionary): answered["r"] = r)
			while not answered.has("r") and Time.get_ticks_msec() - t_ask < 3000:
				await get_tree().process_frame
			print("smoke: select combat at star %d: popup up %s; a query during it answered %s after %d ms (%s)" % [sc_star, _views.dialog.visible,
				answered.has("r"), Time.get_ticks_msec() - t_ask, str(answered.get("r", {})).left(120)])
			if shots != "":
				await host.shot(shots + "/select-combat.png")
			# A side's ships row and its fleets on the map light each other (owner, 2026-11-20).
			_views.dialog.fleet_row_hovered.emit(1)
			var map_lit: bool = galaxy.lit_owner == 1
			var probe := Button.new()
			_views.dialog._fleet_rows[1] = probe
			galaxy.fleet_owner_hovered.emit(1)
			var row_lit: bool = probe.has_theme_color_override("font_color")
			_views.dialog._fleet_rows.erase(1)
			probe.free()
			print("smoke: select combat links: a ships row frames its fleets %s, a fleet lights its row %s" % [map_lit, row_lit])
			_views.dialog._answer(-1)
			await get_tree().create_timer(0.3).timeout
			if not map_lit or not row_lit or galaxy.lit_owner != -1:
				await host.smoke_fail("the select-combat window's ships rows and the map's fleets did not light each other")
				return
			# The message before an attack on the player (owner, 2026-11-20): the same layout.
			var home := -1
			for st3: Dictionary in galaxy.stars:
				var cf3: Dictionary = await _query_args({"what": "combat_fleet", "star": int(st3.id)})
				if cf3.get("ok", false) and not (cf3.ships as Array).is_empty():
					home = int(st3.id)
					break
			var pa := {}
			_send({"verb": "preview_popup", "popup": "player_attacked", "star": home}, func(r: Dictionary): pa["r"] = r)
			for i in 40:
				await get_tree().create_timer(0.1).timeout
				if _views.dialog.visible and String(_views.dialog.popup_name) == "player_attacked":
					break
			await get_tree().create_timer(1.5).timeout
			var pa_fleet: bool = _combat_fleet != null and _combat_fleet.visible
			var pa_beside: bool = _views.dialog.placement == "beside" and galaxy.system_star == home
			# Owner, 2026-11-21: the player's colonies there and their defences show too.
			var pa_colonies: Array = _combat_fleet.fleet.get("colonies", []) if _combat_fleet != null else []
			var pa_defences: bool = not pa_colonies.is_empty() and _combat_fleet._colonies.get_child_count() > 0
			print("smoke: attacked message at %d: shown %s, beside its system %s, fleet window %s, colonies %s, text '%s'" % [home, _views.dialog.visible, pa_beside,
				pa_fleet, JSON.stringify(pa_colonies).left(200), _views.dialog.get_node("Fit/Box/Text").text.left(80)])
			if shots != "":
				await host.shot(shots + "/player-attacked.png")
			_views.dialog._answer(0)
			for i in 20:
				await get_tree().create_timer(0.1).timeout
				if pa.has("r"):
					break
			if not pa_fleet or not pa_beside or not pa_defences or not pa.has("r"):
				await host.smoke_fail("the attacked message did not show beside its system with the fleet and the colonies' defences")
				return
			game.request_game_quit()
			return
		if arg == "--combat-turn":
			await _smoke_window_setting()
			# Owner, 2026-11-20: right clicks did not turn ships in a combat. Turns are played until
			# one of the player's ships has the turn in a tactical combat; a right click on empty
			# space beside it must turn it (combat1.cpp: -space_field, Turn_To_Face).
			var turns := 0
			var mine := false
			while turns < 6 and not mine:
				await _smoke_clear_todo()
				_send({"verb": "end_turn"})
				turns += 1
				var waited := 0.0
				while waited < 90.0:
					var st: Dictionary = game.get_stats()
					if bool(st.get("legacy_combat", false)) and st.has("combat_owner") and int(st.combat_owner) == int(st.player):
						# Its orders are being waited for: polls go on, frames stop.
						var polls := int(st.get("input_polls", 0))
						await get_tree().create_timer(1.0).timeout
						if int(game.get_stats().get("input_polls", 0)) > polls + 5 and int(game.get_stats().get("combat_owner", -1)) == int(st.player):
							mine = true
							break
					if not bool(st.get("legacy_combat", false)) and native_screen == Screens.MAIN and waited > 5.0 and not _turn_over_galaxy:
						break # the turn is over without a combat for us
					if overlay.visible and String(_views["dialog"].popup_name) == "attack_target":
						# The select-combat window (WP): the fleet side beside the system.
						await get_tree().create_timer(2.0).timeout
						var fv: Control = _combat_fleet
						var dlg: Control = _views["dialog"]
						print("smoke: select combat: dialog %s, fleet view %s at %s with %d ships, %d enemies; system centre %s radius %.0f" % [
							dlg.get_global_rect(), fv != null and fv.visible, fv.get_global_rect() if fv else Rect2(),
							(fv.fleet.get("ships", []) as Array).size() if fv else 0, (fv.fleet.get("enemies", []) as Array).size() if fv else 0,
							_popup_system_frame.get("centre", Vector2.ZERO), float(_popup_system_frame.get("radius", 0.0))])
						if shots != "":
							await host.shot(shots + "/select-combat-real.png")
						_views["dialog"]._answer(_views["dialog"].OPTION_ANSWER) # attack the first target (fight with our ships)
						await get_tree().create_timer(0.3).timeout
						print("smoke: select combat answered: fleet view hidden %s" % [fv == null or not fv.visible])
					else:
						_smoke_answer()
					if bool(st.get("legacy_combat", false)) and st.has("combat_owner") and int(st.combat_owner) != int(st.player):
						game.send_key(KEY_ENTER, false, false, false) # boxes in the AI's turns
					await get_tree().create_timer(0.25).timeout
					waited += 0.25
			print("smoke: combat turn: one of the player's ships has the turn %s after %d turns" % [mine, turns])
			if not mine:
				await host.smoke_fail("no combat with a ship of the player's came")
				return
			var rect := legacy.get_global_rect()
			print("smoke: combat turn: legacy rect %s, game rect %s, window %s, scale %.2f" % [rect, game.get_global_rect(), get_window().size,
				get_window().content_scale_factor])
			var before: Dictionary = game.get_stats()
			if shots != "":
				await host.shot(shots + "/combat-turn-before.png")
			var tried := []
			for spot: Vector2 in [Vector2(120, 60), Vector2(520, 60), Vector2(120, 300), Vector2(520, 300)]:
				var at := _smoke_legacy_point(spot)
				var rc := InputEventMouseButton.new()
				rc.button_index = MOUSE_BUTTON_RIGHT
				rc.position = at
				rc.pressed = true
				rc.button_mask = MOUSE_BUTTON_MASK_RIGHT
				Input.parse_input_event(rc)
				if OS.get_environment("RC_HOLD") != "":
					await get_tree().create_timer(float(OS.get_environment("RC_HOLD"))).timeout
				var rc_up := rc.duplicate()
				rc_up.pressed = false
				rc_up.button_mask = 0
				Input.parse_input_event(rc_up) # a quick click
				await get_tree().create_timer(1.2).timeout
				var now: Dictionary = game.get_stats()
				tried.append([spot, int(now.get("combat_ship", -1)), int(now.get("combat_facing", -1))])
			if shots != "":
				await host.shot(shots + "/combat-turn-after.png")
			print("smoke: combat turn: ship %d facing %d; after right clicks %s" % [int(before.combat_ship), int(before.combat_facing), tried])
			var turned := false
			for t: Array in tried:
				if int(t[1]) == int(before.combat_ship) and int(t[2]) != int(before.combat_facing):
					turned = true
			if not turned:
				await host.smoke_fail("right clicks did not turn the player's ship")
				return
			game.request_game_quit()
			return
		if arg == "--selfdestruct":
			# Owner, 2026-11-20: SELF DESTRUCT in a combat froze, then crashed. The turn's end into
			# the combat (questions answered), then the button (combat1.cpp: 283,348) and Enter.
			await _smoke_clear_todo()
			_send({"verb": "end_turn"})
			var waited := 0.0
			while not bool(game.get_stats().get("legacy_combat", false)) and not _in_battle() and waited < 120.0:
				_smoke_answer()
				await get_tree().create_timer(0.25).timeout
				waited += 0.25
			if _in_battle():
				# WP3.9: the battle came up native. The same order (the acting ship, in the owner's
				# save the battlestation) through the battle scene; the game must survive it.
				if await _smoke_selfdestruct_native():
					print("smoke: selfdestruct: native OK")
					game.request_game_quit()
					print("smoke: asked the game to quit")
				else:
					await host.smoke_fail("selfdestruct in the native battle")
				return
			print("smoke: selfdestruct: in combat %s after %.1f s" % [game.get_stats().get("legacy_combat", false), waited])
			if not bool(game.get_stats().get("legacy_combat", false)):
				await host.smoke_fail("no combat came")
				return
			await get_tree().create_timer(4.0).timeout
			var polls: int = int(game.get_stats().get("input_polls", 0))
			var t_wait := 0.0
			while int(game.get_stats().get("input_polls", 0)) == polls and t_wait < 30.0: # the combat waits for orders
				await get_tree().create_timer(0.25).timeout
				t_wait += 0.25
			# The combat's file calls while it waits for orders (owner, 2026-11-20: stutter from
			# KENTEXT read hundreds of times a frame).
			var io_before := int(game.get_stats().get("io_calls", 0))
			var frames_before := int(game.get_stats().frames)
			await get_tree().create_timer(3.0).timeout
			var io_rate := (int(game.get_stats().get("io_calls", 0)) - io_before) / 3.0
			var frame_rate := (int(game.get_stats().frames) - frames_before) / 3.0
			print("smoke: combat idle: %.0f file calls/s at %.0f frames/s" % [io_rate, frame_rate])
			if shots != "":
				await host.shot(shots + "/selfdestruct-combat.png")
			var rect := legacy.get_global_rect()
			# A right click reaches the combat (owner, 2026-11-20: ships could not be turned).
			var got := []
			var hear := func(e: InputEvent):
				var mb := e as InputEventMouseButton
				if mb and mb.button_index == MOUSE_BUTTON_RIGHT:
					got.append(mb.pressed)
			game.gui_input.connect(hear)
			var st0: Dictionary = game.get_stats()
			print("smoke: before the right click: ship %s owner %s (player %s) facing %s" % [st0.get("combat_ship"), st0.get("combat_owner"), st0.get("player"), st0.get("combat_facing")])
			var r_at := (rect.position + Vector2(OS.get_environment("RC_X").to_float() if OS.get_environment("RC_X") != "" else 300.0,
				OS.get_environment("RC_Y").to_float() if OS.get_environment("RC_Y") != "" else 120.0) * rect.size / Vector2(640.0, 480.0)) * get_window().content_scale_factor
			var rc := InputEventMouseButton.new()
			rc.button_index = MOUSE_BUTTON_RIGHT
			rc.position = r_at
			rc.pressed = true
			rc.button_mask = MOUSE_BUTTON_MASK_RIGHT
			Input.parse_input_event(rc)
			var rc_up := rc.duplicate() # released in the same frame: a quick click on a slow poll
			rc_up.pressed = false
			rc_up.button_mask = 0
			Input.parse_input_event(rc_up)
			await get_tree().create_timer(0.3).timeout
			game.gui_input.disconnect(hear)
			await get_tree().create_timer(1.0).timeout
			var st1: Dictionary = game.get_stats()
			print("smoke: after the right click: ship %s owner %s facing %s" % [st1.get("combat_ship"), st1.get("combat_owner"), st1.get("combat_facing")])
			if shots != "":
				await host.shot(shots + "/combat-right-click.png")
			print("smoke: right click in combat reached the game: %s; hovered %s; mouse filter of game %d, legacy %d" % [got,
				get_viewport().gui_get_hovered_control(), game.mouse_filter, legacy.mouse_filter])
			var at := (rect.position + Vector2(290.0, 352.0) * rect.size / Vector2(640.0, 480.0)) * get_window().content_scale_factor
			print("smoke: selfdestruct: clicking %s (legacy rect %s)" % [at, rect])
			_smoke_mouse(true, at)
			await get_tree().create_timer(0.15).timeout
			_smoke_mouse(false, at)
			for i in 20:
				await get_tree().create_timer(0.5).timeout
				print("smoke: selfdestruct: %.1f s: frames %d polls %d" % [i * 0.5, int(game.get_stats().frames), int(game.get_stats().get("input_polls", 0))])
			if shots != "":
				await host.shot(shots + "/selfdestruct-after.png")
			game.send_key(KEY_ENTER, false, false, false)
			await get_tree().create_timer(3.0).timeout
			print("smoke: selfdestruct: after Enter frames %d polls %d" % [int(game.get_stats().frames), int(game.get_stats().get("input_polls", 0))])
			if shots != "":
				await host.shot(shots + "/selfdestruct-enter.png")
			var sd_frames := 0
			# The battlestation's turn: OPTIONS, then SELF DESTRUCT, then YES (owner's save,
			# work/fixtures/owner-2026-11-20-selfdestruct, as SAVE10).
			var options_at := (rect.position + Vector2(332.0, 456.0) * rect.size / Vector2(640.0, 480.0)) * get_window().content_scale_factor
			_smoke_mouse(true, options_at)
			await get_tree().create_timer(0.15).timeout
			_smoke_mouse(false, options_at)
			await get_tree().create_timer(1.5).timeout
			if shots != "":
				await host.shot(shots + "/selfdestruct-options.png")
			var sd_at := (rect.position + Vector2(OS.get_environment("SD_X").to_float() if OS.get_environment("SD_X") != "" else 290.0,
				OS.get_environment("SD_Y").to_float() if OS.get_environment("SD_Y") != "" else 352.0) * rect.size / Vector2(640.0, 480.0)) * get_window().content_scale_factor
			_smoke_mouse(true, sd_at)
			await get_tree().create_timer(0.15).timeout
			_smoke_mouse(false, sd_at)
			for i in 30:
				await get_tree().create_timer(0.5).timeout
				print("smoke: selfdestruct 2: %.1f s: frames %d polls %d combat %s" % [i * 0.5, int(game.get_stats().frames), int(game.get_stats().get("input_polls", 0)),
					game.get_stats().get("legacy_combat", false)])
				if i == 4 and shots != "":
					await host.shot(shots + "/selfdestruct-2.png")
				if i == 5: # YES
					var yes_at := (rect.position + Vector2(260.0, 314.0) * rect.size / Vector2(640.0, 480.0)) * get_window().content_scale_factor
					_smoke_mouse(true, yes_at)
					await get_tree().create_timer(0.15).timeout
					_smoke_mouse(false, yes_at)
				if i == 12 and shots != "":
					await host.shot(shots + "/selfdestruct-yes.png")
				if i == 8:
					sd_frames = int(game.get_stats().frames)
			if int(game.get_stats().frames) <= sd_frames:
				await host.smoke_fail("the combat stopped after SELF DESTRUCT")
				return
			game.request_game_quit()
			return
		if arg == "--build-turns":
			# The build time on hover (owner, 2026-11-20): colonists moved into industry with the
			# build pane open shorten the hovered item's time.
			var c: Dictionary = await _query_args({"what": "colony", "colony": 0})
			_open_colony_at(int(c.star), 0, false)
			await get_tree().create_timer(1.5).timeout
			_open_build_pane(0, false)
			await get_tree().create_timer(1.5).timeout
			var build := _view("build_pane")
			var dearest := {}
			for item: Dictionary in build.options.buildings + build.options.units:
				if int(item.get("cost", 0)) > int(dearest.get("cost", 0)):
					dearest = item
			build._show_detail(dearest)
			await get_tree().create_timer(0.5).timeout
			var before: String = build._cost_label.text
			var industry_before: int = build._cost_label.get_meta("industry", -1)
			var moved := 0
			for job: Dictionary in c.jobs:
				if int(job.job) != 1:
					for p: Dictionary in job.colonists:
						var r: Dictionary = await _command({"verb": "move_colonist", "colony": 0, "pop": int(p.pop), "job": 1}, true)
						moved += int(r.get("count", 0))
			build._show_detail(dearest)
			await get_tree().create_timer(0.5).timeout
			var after: String = build._cost_label.text
			print("smoke: build turns: industry %d -> %d" % [industry_before, int(build._cost_label.get_meta("industry", -1))])
			print("smoke: build turns for '%s': before '%s', %d colonists to industry, after '%s'" % [dearest.get("name", ""), before, moved, after])
			if moved == 0 or before == after:
				await host.smoke_fail("the build time did not follow the colony's production")
				return
			game.request_game_quit()
			return
		if arg == "--colony-base":
			# Owner, 2026-11-20: Colony Base is offered while the system's free planets outnumber
			# the Colony Bases in the queue being edited.
			var found := {}
			for c in 64:
				var data: Dictionary = await _query_args({"what": "colony", "colony": c})
				if not data.get("ok", false) or data.get("outpost", false):
					continue
				var o: Dictionary = await _query_args({"what": "build_options", "colony": c})
				if int(o.get("colony_base_room", 0)) > 0 and (o.get("units", []) as Array).any(func(u: Dictionary) -> bool: return int(u.get("id", -1)) == 11):
					found = {"colony": c, "star": int(data.star), "room": int(o.colony_base_room)}
					break
			if found.is_empty():
				print("smoke: colony base: no colony with a free planet in its system (skipped)")
				game.request_game_quit()
				return
			_open_colony_at(int(found.star), int(found.colony), true)
			await get_tree().create_timer(2.5).timeout
			var build := _view("build_pane")
			var offered := func() -> bool:
				return build.units_box.get_children().any(func(n: Node) -> bool: return n is Button and (n as Button).text == String(build._names.get(11, {}).get("name", "")))
			var added := 0
			while bool(offered.call()) and added < 7:
				build._add(11)
				added += 1
				await get_tree().process_frame
			var gone_after: int = added
			var still_offered: bool = offered.call()
			# One taken off again: offered again.
			var slot := -1
			for i in build._queue.size():
				if int(build._queue[i].id) == 11:
					slot = i
			if slot >= 0:
				build._delete(slot)
			await get_tree().process_frame
			var back: bool = offered.call()
			print("smoke: colony base at colony %d: room %d; offered until %d queued (still offered %s); one taken off: offered %s" % [found.colony,
				found.room, gone_after, still_offered, back])
			build.cancel()
			await get_tree().create_timer(0.5).timeout
			if gone_after != int(found.room) or still_offered or not back:
				await host.smoke_fail("Colony Base not limited to the system's free planets")
				return
			game.request_game_quit()
			return
		if arg == "--todo-auto":
			# AUTO ADDRESS TO-DOS (owner, 2026-11-20): after the turns each item opens in turn; the
			# player here looks and closes (a build list cancelled, the system closed), and the next
			# opens; none twice; the list comes up once with what is left.
			var auto_before := todo_auto
			todo_auto = true
			if not await _smoke_turns():
				return
			# The summary's messages are listed, not stepped through (owner, 2026-11-21).
			var open_items: Array = (_todo.get("items", []) as Array).filter(func(i: Dictionary) -> bool: return not i.get("done", false) and String(i.get("kind", "")) != "report")
			var seen: Array = []
			var held := false
			var waited := 0.0
			while waited < 40.0 and not _todo_auto_listed:
				await get_tree().create_timer(0.3).timeout
				waited += 0.3
				if _todo_auto_last.is_empty():
					continue
				var item: Dictionary = _todo_auto_last.item
				var tag := "%s %s" % [item.get("kind", ""), item.get("text", "")]
				if seen.is_empty() or seen.back() != tag:
					seen.append(tag)
					await get_tree().create_timer(1.5).timeout # it opens
					waited += 1.5
					print("smoke: to-do auto opened '%s': system pane %s, colony pane %s, build %s" % [tag, _view("system_pane").visible,
						_view("colony_pane").visible, _view("build_pane").visible])
					if shots != "" and seen.size() == 1:
						await host.shot(shots + "/native-todo-auto.png")
					if _view("build_pane").visible:
						_view("build_pane").cancel()
						await get_tree().create_timer(0.5).timeout
					if _view("system_pane").visible:
						_view("system_pane").close()
					# A landing's new colony still opening holds the next item (owner, 2026-11-20:
					# a second colony base's question came up beside the first one's build list).
					if seen.size() == 1:
						var held_count: int = _todo_auto_opened.size()
						_landing_followup_since = Time.get_ticks_msec() / 1000.0
						await get_tree().create_timer(1.5).timeout
						waited += 1.5
						held = _todo_auto_opened.size() == held_count
						_landing_followup_since = -1.0
					# A window the item opened (a discovery): answered.
					for i in 10:
						await get_tree().create_timer(0.3).timeout
						if not _smoke_answer():
							break
			await get_tree().create_timer(1.0).timeout
			var listed: bool = _view("todo_panel").visible
			var unique := {}
			for t: String in seen:
				unique[t] = true
			print("smoke: to-do auto: %d open after the turns, opened in turn %s; list at the end %s (%d left); held during a landing's follow-up %s" % [
				open_items.size(), seen, listed, int(_todo.get("open", 0)), held])
			todo_auto = auto_before
			if not held or seen.size() != open_items.size() or unique.size() != seen.size() or (int(_todo.get("open", 0)) > 0 and not listed):
				await host.smoke_fail("the to-dos were not addressed one after another")
				return
			game.request_game_quit()
			return
		if arg == "--bombing":
			# Owner, 2026-11-20: the bombs fall one at a time and STOP holds the rest.
			var enemy_colony := -1
			var enemy_star := -1
			for star: Dictionary in galaxy.stars:
				var sys: Dictionary = await _query_args({"what": "system", "star": int(star.id)})
				for planet: Dictionary in sys.get("planets", []):
					var colony: Dictionary = planet.get("colony", {})
					if not colony.is_empty() and not colony.get("mine", false) and enemy_colony < 0 and int(planet.get("population", colony.get("population", 1))) > 0:
						enemy_colony = int(colony.id)
						enemy_star = int(star.id)
				if enemy_colony >= 0:
					break
			var box := {}
			# One window from the orbit's choice to the last bomb (owner, 2026-11-20): BOMB keeps it.
			_send({"verb": "preview_popup", "popup": "colony_combat", "colony": enemy_colony, "then_bomb": 60}, func(r: Dictionary): box["r"] = r)
			for i in 30:
				await get_tree().create_timer(0.1).timeout
				if _views.has("dialog") and _views.dialog.visible and String(_views.dialog.popup_name) == "colony_combat":
					break
			var choice_dialog: Control = _views["dialog"]
			var choice_buttons: Array = choice_dialog._bombing_buttons.get_children().map(func(b: Button) -> String: return b.text)
			if shots != "":
				await host.shot(shots + "/native-orbit-choice.png")
			var bomb_at: Vector2 = (choice_dialog._bombing_buttons.get_child(0) as Control).get_global_rect().position
			(choice_dialog._bombing_buttons.get_child(0) as Button).pressed.emit() # BOMB
			var stayed := true
			for i in 30:
				await get_tree().process_frame
				if not choice_dialog.visible:
					stayed = false
				if String(choice_dialog.popup_name) == "colony_bombing":
					break
			var same_place: bool = (choice_dialog._bombing_buttons.get_child(0) as Control).get_global_rect().position.distance_to(bomb_at) < 1.0
			print("smoke: orbit choice %s; BOMB kept the window %s, bombing in it %s, its first button in place %s" % [choice_buttons, stayed,
				String(choice_dialog.popup_name) == "colony_bombing", same_place])
			if not stayed or not same_place or String(choice_dialog.popup_name) != "colony_bombing" or choice_buttons.is_empty() or choice_buttons[0] != "BOMB":
				await host.smoke_fail("BOMB did not carry the orbit's window into the bombing")
				return
			var dialog: Control
			var left := func() -> int: # the window's "N bomb strikes" row
				for label: Label in _views["dialog"].find_children("*", "Label", true, false):
					if label.text.contains("bomb strike"):
						return int(label.text.get_slice(" ", 0))
				return 0
			var labels := func() -> Array:
				return _views["dialog"]._bombing_buttons.get_children().map(func(b: Button) -> String: return b.text)
			for i in 30:
				await get_tree().create_timer(0.1).timeout
				if _views.has("dialog") and _views.dialog.visible and String(_views.dialog.popup_name) == "colony_bombing":
					break
			dialog = _views["dialog"]
			var at_start: int = left.call()
			# Nothing moves from step to step (owner, 2026-11-20: the buttons jumped about).
			var window_at: Vector2 = dialog.get_global_rect().position
			var stop_at: Vector2 = (dialog._bombing_buttons.get_child(0) as Control).get_global_rect().position
			await get_tree().create_timer(1.5).timeout
			var after_second: int = left.call()
			var steady: bool = dialog.get_global_rect().position.distance_to(window_at) < 1.0 and \
				(dialog._bombing_buttons.get_child(0) as Control).get_global_rect().position.distance_to(stop_at) < 1.0
			if shots != "":
				await host.shot(shots + "/native-bombing.png")
			var running_buttons: Array = labels.call()
			(dialog._bombing_buttons.get_child(0) as Button).pressed.emit() # STOP
			await get_tree().create_timer(0.3).timeout
			var at_stop: int = left.call()
			await get_tree().create_timer(1.0).timeout
			var still: int = left.call()
			var stopped_buttons: Array = labels.call()
			# After STOP, INVADE is open (owner 2026-11-23: it stayed greyed after a partial run).
			var stopped_open: Array = dialog._bombing_buttons.get_children().filter(func(b: Button) -> bool: return not b.disabled).map(func(b: Button) -> String: return b.text)
			print("smoke: bombing stopped: open %s" % [stopped_open])
			if not "INVADE" in stopped_open:
				await host.smoke_fail("INVADE stayed greyed after the bombs were stopped")
				return
			(dialog._bombing_buttons.get_child(0) as Button).pressed.emit() # CONTINUE
			await get_tree().create_timer(0.7).timeout
			var after_continue: int = left.call()
			if dialog._bombing_buttons.get_child_count() > 1:
				(dialog._bombing_buttons.get_child(1) as Button).pressed.emit() # ALL AT ONCE
			await get_tree().create_timer(0.5).timeout
			var done_buttons: Array = labels.call()
			var done_enabled: Array = dialog._bombing_buttons.get_children().filter(func(b: Button) -> bool: return not b.disabled).map(func(b: Button) -> String: return b.text)
			var visible_while: bool = dialog.visible and overlay.visible
			if shots != "":
				await host.shot(shots + "/native-bombing-done.png")
			(dialog._bombing_buttons.get_child(dialog._bombing_buttons.get_child_count() - 1) as Button).pressed.emit() # CLOSE
			for i in 30:
				await get_tree().create_timer(0.1).timeout
				if box.has("r"):
					break
			print("smoke: bombing steady %s, explosions %d, open at the end %s" % [steady, dialog.blast_count, done_enabled])
			print("smoke: bombing colony %d: bombs left %d, after 1.5 s %d %s; STOP at %d, 1 s later %d %s; CONTINUE 0.7 s %d; ALL AT ONCE %s; window up %s; result %s" % [
				enemy_colony, at_start, after_second, running_buttons, at_stop, still, stopped_buttons, after_continue, done_buttons, visible_while, box.get("r", "none")])
			# One step every 5 frames of 55 ms; a step may take several hits, as in the original.
			var paced: bool = after_second < at_start and after_second > 0 and steady
			if enemy_colony < 0 or not paced or still != at_stop or after_continue >= still or not "CLOSE" in done_enabled or done_enabled.any(func(t: String) -> bool: return t in ["STOP", "BOMB", "DROP ALL"]) or not box.has("r") or overlay.visible \
					or dialog.blast_count == 0 or running_buttons.slice(0, 2) != ["STOP", "DROP ALL"] or stopped_buttons[0] != "BOMB":
				await host.smoke_fail("bombing was not paced, stoppable and closable")
				return
			game.request_game_quit()
			return
		if arg == "--relocate":
			# Owner, 2026-11-20: relocation from the system pane's menu.
			var from := -1
			for star: Dictionary in galaxy.stars:
				var sys: Dictionary = await _query_args({"what": "system", "star": int(star.id)})
				if sys.get("relocation", {}).get("can", false):
					from = int(star.id)
					break
			if from < 0:
				await host.smoke_fail("no system to relocate from")
				return
			_open_system(from)
			await get_tree().create_timer(2.5).timeout
			var pane := _view("system_pane")
			var menu_shown: bool = pane._menu_button.visible
			pane._menu_button.pressed.emit()
			await get_tree().process_frame
			var items: Array = pane._menu.find_children("*", "Button", true, false)
			var item_texts: Array = items.map(func(b: Button) -> String: return b.text)
			if shots != "":
				await host.shot(shots + "/native-relocate-menu.png")
			(items[0] as Button).pressed.emit()
			await get_tree().create_timer(1.5).timeout
			var pick: Dictionary = galaxy.relocation_pick
			var target := -1
			for star: Dictionary in galaxy.stars:
				var t: Dictionary = pick.get("stars", {}).get(int(star.id), {})
				if t.get("allowed", false) and not t.get("guarded", false) and int(star.id) != from and galaxy.view_rect().has_point(galaxy.star_position(int(star.id))):
					if target < 0 or int(t.parsecs) > int(pick.stars[target].parsecs):
						target = int(star.id)
			var motion := InputEventMouseMotion.new()
			motion.position = (galaxy.get_global_transform_with_canvas() * galaxy.star_position(target)) * get_window().content_scale_factor
			Input.parse_input_event(motion)
			await get_tree().create_timer(0.4).timeout
			print("smoke: relocate: menu %s %s; picking %s from %d, pane hidden %s, hint %s, zoom %.3f of furthest %.3f; target %d (%d parsecs, %d turns)" % [
				menu_shown, item_texts, not pick.is_empty(), from, not pane.visible, _measure_hint.visible, galaxy.camera.zoom.x,
				galaxy.furthest_zoom(), target, int(pick.stars[target].parsecs), int(pick.stars[target].eta)])
			if shots != "":
				await host.shot(shots + "/native-relocate-pick.png")
			_on_star_clicked(target)
			await get_tree().create_timer(0.3).timeout
			var asked: bool = is_instance_valid(_relocation_question) and _relocation_question.visible
			if shots != "":
				await host.shot(shots + "/native-relocate-confirm.png")
			if asked:
				_relocation_question.get_ok_button().pressed.emit()
			for i in 20:
				await get_tree().create_timer(0.25).timeout
				_smoke_answer()
				if pane.visible and int(pane.system.get("relocation", {}).get("to", -1)) == target:
					break
			await get_tree().create_timer(1.0).timeout
			var set_to := int(pane.system.get("relocation", {}).get("to", -1))
			print("smoke: relocate: asked %s; back in the system %s, relocation to %d, line '%s'" % [asked, pane.visible, set_to, pane._relocation_line.text])
			if shots != "":
				await host.shot(shots + "/native-relocate-set.png")
			pane._menu_button.pressed.emit()
			await get_tree().process_frame
			var stop: Array = pane._menu.find_children("*", "Button", true, false).filter(func(b: Button) -> bool: return b.text.begins_with("STOP"))
			if not stop.is_empty():
				(stop[0] as Button).pressed.emit()
			await get_tree().create_timer(1.0).timeout
			var cleared: bool = int(pane.system.get("relocation", {}).get("to", -1)) == -1 and not pane._relocation_line.visible
			# A right click while picking goes back to the system unchanged.
			_start_relocation_pick(from)
			await get_tree().create_timer(1.0).timeout
			var right := InputEventMouseButton.new()
			right.button_index = MOUSE_BUTTON_RIGHT
			right.pressed = true
			right.position = motion.position
			Input.parse_input_event(right)
			var right_up := right.duplicate()
			right_up.pressed = false
			Input.parse_input_event(right_up)
			await get_tree().create_timer(2.0).timeout
			var backed: bool = galaxy.relocation_pick.is_empty() and pane.visible and int(pane.star) == from
			print("smoke: relocate: cleared %s; right click back to the system %s" % [cleared, backed])
			if not menu_shown or target < 0 or not asked or set_to != target or not cleared or not backed:
				await host.smoke_fail("relocation from the system pane did not work")
				return
			game.request_game_quit()
			return
		if arg == "--measure":
			# Owner, 2026-11-20: measure distance over the native galaxy (F9).
			_on_view_action("distance")
			await get_tree().create_timer(0.3).timeout
			var hint_on: bool = galaxy.measuring and _measure_hint != null and _measure_hint.visible
			var home := -1
			for star: Dictionary in galaxy.stars:
				if int(star.get("label_owner", -1)) == int(galaxy.data.get("player", -2)) or (home < 0 and String(star.label) != ""):
					home = int(star.id)
					if int(star.get("label_owner", -1)) == int(galaxy.data.get("player", -2)):
						break
			var far := -1
			var view: Rect2 = galaxy.view_rect()
			for star: Dictionary in galaxy.stars:
				var at := Vector2(float(star.x), float(star.y))
				if int(star.id) != home and view.grow(-40.0 / galaxy.camera.zoom.x).has_point(at) and \
						(far < 0 or at.distance_to(galaxy.star_position(home)) > galaxy.star_position(far).distance_to(galaxy.star_position(home))):
					far = int(star.id)
			_on_star_clicked(home)
			var screen: Vector2 = galaxy.get_global_transform_with_canvas() * galaxy.star_position(far)
			var motion := InputEventMouseMotion.new()
			motion.position = screen * get_window().content_scale_factor
			Input.parse_input_event(motion)
			await get_tree().create_timer(0.4).timeout
			var mismatches := 0
			for star: Dictionary in galaxy.stars:
				var d2 := Vector2(float(star.x), float(star.y)).distance_squared_to(galaxy.star_position(home))
				if galaxy.parsecs_between(home, int(star.id)) != int(ceil(sqrt(d2 / 900.0))):
					mismatches += 1
			print("smoke: measure: mode %s with hint %s; from %d to %d (hovered %d): %d parsecs; formula mismatches %d of %d" % [galaxy.measuring,
				hint_on, home, far, galaxy.hovered_star(), galaxy.parsecs_between(home, far), mismatches, galaxy.stars.size()])
			if shots != "":
				await host.shot(shots + "/native-measure.png")
			# A right click on the map stops it (owner, 2026-11-20: it didn't), and Esc.
			var right := InputEventMouseButton.new()
			right.button_index = MOUSE_BUTTON_RIGHT
			right.pressed = true
			right.position = motion.position
			Input.parse_input_event(right)
			var right_up := right.duplicate()
			right_up.pressed = false
			Input.parse_input_event(right_up)
			await get_tree().create_timer(0.3).timeout
			var right_stopped: bool = not galaxy.measuring
			_on_view_action("distance")
			await get_tree().create_timer(0.2).timeout
			var esc := InputEventKey.new()
			esc.keycode = KEY_ESCAPE
			esc.pressed = true
			Input.parse_input_event(esc)
			await get_tree().create_timer(0.3).timeout
			print("smoke: measure: a right click stopped it %s; after Esc: mode %s, hint %s" % [right_stopped, galaxy.measuring, _measure_hint.visible])
			if not right_stopped or not hint_on or galaxy.measure_from != -1 or galaxy.measuring or _measure_hint.visible or mismatches > 0 or far < 0:
				await host.smoke_fail("measure distance did not work")
				return
			game.request_game_quit()
			return
		if arg == "--transport":
			# Owner, 2026-11-20: pick up colonists, TRANSPORT COLONISTS lists where they can go
			# (population, planet, ETA), a click sends them with the game's questions answered.
			var source := {}
			for c in 64:
				var data: Dictionary = await _query_args({"what": "colony", "colony": c})
				if data.get("ok", false) and not data.get("outpost", false) and int(data.get("population", 0)) >= 3:
					source = data
					break
			if source.is_empty():
				await host.smoke_fail("no colony with colonists to transport")
				return
			var pane := _view("colony_pane")
			_open_colony_at(int(source.star), int(source.colony), false)
			await get_tree().create_timer(2.0).timeout
			var person := {}
			var person_job := -1
			for job_info: Dictionary in pane.colony.jobs:
				for c: Dictionary in job_info.colonists:
					if String(c.get("state", "")) != "native" and person.is_empty():
						person = c
						person_job = int(job_info.get("job", pane.colony.jobs.find(job_info)))
			var pop_before := int(pane.colony.get("population", 0))
			pane._pick_up([int(person.pop)], person_job)
			await get_tree().process_frame
			var transport: Control = pane.get_node("Scroll/Box/Transport")
			pane._input(_smoke_click_at(transport.get_global_rect().get_center()))
			await get_tree().create_timer(1.0).timeout
			var flyout: Control = pane._transport_flyout
			var rows: Array = [] if flyout == null else flyout.find_children("*", "PanelContainer", true, false).filter(
				func(n: Node) -> bool: return (n as Control).theme_type_variation == &"Row")
			var sendable: Array = rows.filter(func(r: Control) -> bool: return r.mouse_filter == Control.MOUSE_FILTER_STOP)
			var inside := flyout != null and get_viewport().get_visible_rect().encloses(flyout.get_global_rect())
			print("smoke: transport from %s (pop %d): list %s, %d colonies, %d can take them, on screen %s, carry left %s" % [source.name,
				pop_before, flyout != null and flyout.visible, rows.size(), sendable.size(), inside, not pane._carry.is_empty()])
			if shots != "":
				await host.shot(shots + "/native-transport.png")
			if flyout == null or not flyout.visible or rows.is_empty() or not inside or pane._carry.is_empty():
				await host.smoke_fail("the transport list did not open")
				return
			if sendable.is_empty():
				print("smoke: no colony can take them (send skipped)")
				game.request_game_quit()
				return
			var sent := {}
			pane.transported.connect(func(n: int): sent["n"] = n, CONNECT_ONE_SHOT)
			# A real click: the list must get it past the colonists on the pointer.
			var row_at := (sendable[0] as Control).get_global_rect().get_center() * get_window().content_scale_factor
			var row_press := InputEventMouseButton.new()
			row_press.button_index = MOUSE_BUTTON_LEFT
			row_press.pressed = true
			row_press.position = row_at
			Input.parse_input_event(row_press)
			var row_release := row_press.duplicate()
			row_release.pressed = false
			Input.parse_input_event(row_release)
			for i in 40:
				await get_tree().create_timer(0.25).timeout
				_smoke_answer()
				if sent.has("n"):
					break
			await get_tree().create_timer(0.6).timeout
			print("smoke: transport sent %s; list closed %s, pointer clear %s; population %d -> %d" % [sent.get("n", "no answer"), not flyout.visible,
				pane._carry.is_empty(), pop_before, int(pane.colony.get("population", 0))])
			if shots != "":
				await host.shot(shots + "/native-transport-sent.png")
			if int(sent.get("n", 0)) < 1 or flyout.visible or not pane._carry.is_empty():
				await host.smoke_fail("the colonists were not sent")
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
	var panel_steps: Array = ["--colonies", "--planets", "--fleets", "--leaders", "--races", "--info", "--design", "--layering"].filter(func(f: String) -> bool: return f in OS.get_cmdline_user_args())
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
			elif step == "--layering":
				passed = await _smoke_layering(shots)
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
	if "--combat-speed" in OS.get_cmdline_user_args():
		await _smoke_combat_speed(shots)
		game.request_game_quit()
		return
	if "--ground-combat" in OS.get_cmdline_user_args():
		var ok := await _smoke_ground_combat(shots)
		print("smoke: ground combat %s" % ("OK" if ok else "FAILED"))
		if not ok:
			await host.smoke_fail("the invasion's screen did not play in its window")
			return
		game.request_game_quit()
		return
	if "--fleet-camera" in OS.get_cmdline_user_args():
		await _smoke_fleet_camera(shots)
		game.request_game_quit()
		return
	if "--torpedo" in OS.get_cmdline_user_args():
		if await _smoke_torpedo(shots):
			print("smoke: torpedo OK")
		else:
			print("smoke: FAILED: torpedo")
		game.request_game_quit()
		return
	if "--combat" in OS.get_cmdline_user_args():
		# WP3.9b: end the turn into a battle (SAVE10: Fortuna) and play it in the native scene.
		var start: Dictionary = await _query("empire")
		if await _smoke_combat(shots) and await _smoke_after_battle(int(start.get("stardate", 0)), shots):
			print("smoke: combat OK")
			game.request_game_quit()
			print("smoke: asked the game to quit")
		else:
			await host.smoke_fail("combat")
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
	# VIEW COLONY natively (owner, 2026-11-20): the landscape beside the pane, shown and hidden.
	var land_was: bool = _views.has("colony_landscape") and _views.colony_landscape.visible
	if not land_was:
		colony_pane.landscape_toggled.emit(int(c.colony))
	await get_tree().create_timer(1.5).timeout
	var land: Control = _view("colony_landscape")
	var land_layers: int = (land.landscape.get("layers", []) as Array).size()
	if shots != "":
		await host.shot(shots + "/native-landscape-view.png")
	colony_pane.landscape_toggled.emit(int(c.colony))
	await get_tree().create_timer(0.4).timeout
	print("smoke: landscape view: shown with %d layers, hidden again %s, still on the galaxy %s" % [land_layers, not land.visible,
		native_screen == Screens.MAIN])
	if land_layers < 3 or land.visible or native_screen != Screens.MAIN:
		await host.smoke_fail("the landscape view did not show and hide")
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
			if b is Button and not b.is_queued_for_deletion() and (b as Button).text == String(build._names.get(pick, {}).get("name", "")):
				return true
		return false
	# The lists are rebuilt (the old buttons freed at the frame's end): checked a few frames on,
	# and the item taken off by its own slot.
	for i in 3:
		await get_tree().process_frame
	var hidden_after_add: bool = pick in [build.HOUSING, build.TRADE_GOODS] or not listed.call()
	var queued_slot: int = build._queue.size() - 1
	for slot in build._queue.size():
		if int(build._queue[slot].id) == pick:
			queued_slot = slot
	build._delete(queued_slot)
	for i in 3:
		await get_tree().process_frame
	var back_after_delete: bool = listed.call()
	print("smoke: build list: building %d hidden once queued %s, back once removed %s" % [pick, hidden_after_add, back_after_delete])
	if not back_after_delete:
		print("smoke: build list debug: queue %s; listed %s; option ids %s" % [build._queue.map(func(q: Dictionary) -> int: return int(q.id)),
			build.items_box.get_children().filter(func(b: Node) -> bool: return b is Button and not b.is_queued_for_deletion()).map(func(b: Node) -> String: return (b as Button).text),
			build.options.buildings.map(func(b: Dictionary) -> int: return int(b.get("id", -1)))])
	if not hidden_after_add or not back_after_delete:
		await host.smoke_fail("a queued building stayed in the BUILDINGS list, or did not come back")
		return false
	build._add(pick)
	# OK, a click away and OK again before the game answers (owner 2026-11-23: queues with every
	# item twice, a refitting ship lost): the edits go to the game once.
	build._ok()
	build.close_keeping()
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
	# Owner, 2026-11-21: the summary's messages are in the to-do list, the SUMMARY pane gone.
	var todo: Dictionary = await _query("todo")
	var reports := (todo.get("items", []) as Array).filter(func(i: Dictionary) -> bool: return String(i.kind) == "report").size()
	print("smoke: turn summary: option %s, %d messages, %d listed in the to-do list, SUMMARY pane shown %s" % [summary.get("on", "?"),
		summary.get("items", []).size(), reports, summary_panel.visible])
	if summary_panel.visible or reports < summary.get("items", []).size():
		await host.smoke_fail("the summary's messages are not all in the to-do list, or the SUMMARY pane still shows")
		return false
	var kinds := {}
	for item: Dictionary in todo.get("items", []):
		kinds[String(item.kind)] = int(kinds.get(String(item.kind), 0)) + 1
	var bar_text := String(_view("galaxy_bar").get_node("Row/Turn").text)
	print("smoke: to-do after the turns: %d open, %d builds, kinds %s, bar '%s'" % [int(todo.get("open", 0)), int(todo.get("builds", 0)), kinds, bar_text])
	for item: Dictionary in todo.get("items", []):
		print("smoke: to-do item: %s '%s' done %s" % [item.kind, item.text, item.done])
	var total := int(todo.get("open", 0)) + int(todo.get("unread", 0))
	if total > 0 and not bar_text.begins_with("TO DO"):
		await host.smoke_fail("TURN did not read TO DO (n)")
		return false
	if total == 0:
		return true
	# The list opens by itself (owner, 2026-11-14); TO DO closes and reopens it.
	var panel := _view("todo_panel")
	var waited_open := 0.0
	while not panel.visible and waited_open < 4.0:
		await get_tree().create_timer(0.1).timeout
		waited_open += 0.1
	var opened_itself: bool = panel.visible
	# A count still changing reopens the list by design (new items): let it settle first.
	var settled_total := _todo_total()
	var steady := 0.0
	while steady < 1.0 and waited_open < 8.0:
		await get_tree().create_timer(0.1).timeout
		waited_open += 0.1
		steady = steady + 0.1 if _todo_total() == settled_total else 0.0
		settled_total = _todo_total()
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
	# END TURN under the list (owner, 2026-11-21): disabled while choices hold the turn.
	var end_button: Button = panel._end_turn
	print("smoke: to-do END TURN shown %s, disabled %s with %d holding the turn, %d unread news" % [end_button != null and end_button.visible,
		end_button.disabled if end_button else "?", int(todo.get("open", 0)), int(todo.get("unread", 0))])
	if end_button == null or end_button.disabled != (int(todo.get("open", 0)) > 0):
		await host.smoke_fail("END TURN under the list is not disabled exactly while choices hold the turn")
		return false
	if int(todo.get("open", 0)) == 0:
		return await _smoke_clear_todo() # only news: nothing to refuse
	# Nothing may be ignored: the game refuses the turn while choices are open.
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
		if bool(item.news) and not bool(item.done) and not String(item.kind) in ["summary", "report"]:
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
					# The second question stays on the system (owner, 2026-11-20: it flew back out).
					await get_tree().create_timer(1.0).timeout
					print("smoke: to-do colonize confirm: galaxy system %d (item's star %d), window placement '%s'" % [galaxy.system_star,
						int(item.get("star", -1)), dialog.placement])
					if galaxy.system_star != int(item.get("star", -1)):
						await host.smoke_fail("the colony ship's confirmation flew away from its system")
						return false
					if shots != "":
						await get_tree().create_timer(1.5).timeout # the flight into the system
						await host.shot(shots + "/native-todo-colony-confirm.png")
					dialog._answer(0) # YES
					# Until the landing comes, the map stays on the system (owner: it flew out and
					# back in when the landing took a while to load).
					var left_system := 0
					var pending_frames := 0
					var gap_frames := 0
					for f in 600:
						if _scene_window != null and _scene_window.visible:
							break
						gap_frames += 1
						if _game_busy():
							pending_frames += 1
						if galaxy.system_star != int(item.get("star", -1)):
							left_system += 1
						await get_tree().process_frame
					print("smoke: to-do colonize after YES: the map left the system in %d frames before the landing; busy in %d of %d frames" % [
						left_system, pending_frames, gap_frames])
					# And busy throughout, so a slower landing (over the share) waits too.
					if left_system > 0 or pending_frames < gap_frames:
						await host.smoke_fail("the camera left the system between the confirmation and the landing")
						return false
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
		if int(todo.get("open", 0)) == 0 and int(todo.get("unread", 0)) == 0:
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
			"report":
				_send({"verb": "todo_read", "report": String(item.report)})
			_:
				_send({"verb": "todo_read", "item": int(item.item)})
		await get_tree().create_timer(0.3).timeout
	await host.smoke_fail("the to-do list did not empty")
	return false


## The smoke test answers native message boxes with OK / Yes.
## A real mouse click at a screen point (the legacy screens take the mouse, not keys, to go on).
## WP3.9b: `-- --smoke --native --combat --shots=DIR` with SAVE10: the turn's battle opens as the
## native scene; one move to a legal square, the order ring, Always show health, then Auto to the end.
## Speeds changed on the enemy's turn (owner 2026-11-23: they took effect only next round): the
## player ends their turn, and after the enemy's first animation Fast moves and Double speed are
## set; each animation's kind and length logged.
func _smoke_combat_speed(_shots: String) -> void:
	_send({"verb": "end_turn"})
	var view: Control = null
	var waited := 0.0
	while waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		view = _combat
		if view != null and view.visible and view._my_turn():
			break
		var dialog: Control = _views.get("dialog")
		if overlay.visible and dialog != null and String(dialog.popup_name) == "attack_target":
			for i in dialog.options.size():
				var item: Dictionary = dialog.options[i]
				if int(item.get("planet", -1)) >= 0 and bool(item.get("enabled", true)):
					dialog._answer(dialog.OPTION_ANSWER + i)
					break
			continue
		_smoke_answer()
	if view == null:
		print("smoke: combat-speed: no battle")
		return
	view._move_speed = 1
	view._set_speed_x2(false)
	await get_tree().create_timer(1.0).timeout
	view._act({"action": "end_turn"})
	var last_id := -1
	var counts: Dictionary = view.animated.duplicate()
	var changed := false
	var seen := 0
	waited = 0.0
	while waited < 60.0 and seen < 40:
		await get_tree().process_frame
		waited += get_process_delta_time()
		if view._my_turn() and seen > 0:
			break
		if view._animating.is_empty() or int(view._animating.id) == last_id:
			continue
		last_id = int(view._animating.id)
		var kind := "?"
		for k in view.animated.keys():
			if int(view.animated[k]) != int(counts.get(k, 0)):
				kind = String(k)
		counts = view.animated.duplicate()
		seen += 1
		print("smoke: combat-speed: %s dur %.2f (moves %s, x2 %s, round %s)" % [kind, float(view._animating.dur), view.MOVE_SPEEDS[view._move_speed][0], view._speedx2(), view.state.get("round", -1)])
		if not changed and seen >= 2:
			changed = true
			view._move_speed = 2
			view._set_speed_x2(true)
			print("smoke: combat-speed: now Fast and Double")


## The invasion's battle screen in the films' window over the galaxy (owner 2026-11-23: it was
## full screen): a preview on an enemy colony, a click to speed it, a click to end it.
func _smoke_ground_combat(shots: String) -> bool:
	var enemy_colony := -1
	for star: Dictionary in galaxy.stars:
		var sys: Dictionary = await _query_args({"what": "system", "star": int(star.id)})
		for planet: Dictionary in sys.get("planets", []):
			var colony: Dictionary = planet.get("colony", {})
			if not colony.is_empty() and not colony.get("mine", false):
				enemy_colony = int(colony.id)
				break
		if enemy_colony >= 0:
			break
	var box := {}
	_send({"verb": "preview_popup", "popup": "ground_combat", "colony": enemy_colony}, func(r: Dictionary): box["r"] = r)
	var waited := 0.0
	while not _film_active and waited < 10.0:
		await get_tree().create_timer(0.1).timeout
		waited += 0.1
	await get_tree().create_timer(2.0).timeout
	var framed: bool = _film_active and legacy.visible and galaxy.visible and _film_frame != null and _film_frame.visible
	var rect: Rect2 = legacy.get_global_rect()
	print("smoke: ground combat on colony %d: in its window %s, legacy at %s (screen %s)" % [enemy_colony, framed, rect, get_viewport().get_visible_rect().size])
	if shots != "":
		await host.shot(shots + "/ground-combat.png")
	var click := func() -> void: # a real press and release on the window, as the film smoke's
		var press := InputEventMouseButton.new()
		press.button_index = MOUSE_BUTTON_LEFT
		press.pressed = true
		press.position = rect.get_center() * get_window().content_scale_factor
		Input.parse_input_event(press)
		await get_tree().create_timer(0.15).timeout
		var release := press.duplicate()
		release.pressed = false
		Input.parse_input_event(release)
	waited = 0.0
	while (_film_active or not box.has("r")) and waited < 20.0:
		if _film_active:
			await click.call()
		await get_tree().create_timer(1.0).timeout
		waited += 1.0
	print("smoke: ground combat over: window closed %s, legacy hidden %s, answer %s" % [not _film_active, not legacy.visible, box.get("r", {})])
	return framed and rect.size.x < get_viewport().get_visible_rect().size.x * 0.9 and not _film_active and box.has("r")


## A fleet clicked from a colony's view (owner 2026-11-23: an odd zoom out; after closing the fleet
## and the system, two flights back): the camera after each step, sampled through the flights.
func _smoke_fleet_camera(shots: String) -> void:
	var player := int(galaxy.data.get("player", -1))
	var star := -1
	var colony := -1
	var fleet := -1
	for c in 64:
		var data: Dictionary = await _query_args({"what": "colony", "colony": c})
		if not data.get("ok", false) or data.get("outpost", false):
			continue
		for f: Dictionary in galaxy.fleets:
			if int(f.get("star", -1)) == int(data.star) and int(f.owner) == player:
				star = int(data.star)
				colony = c
				fleet = int(f.ship)
				break
		if fleet >= 0:
			break
	print("smoke: fleet-camera: colony %d at star %d, fleet %d; camera %s zoom %.3f" % [colony, star, fleet, galaxy.camera.position, galaxy.camera.zoom.x])
	var sample := func(label: String, seconds: float) -> void:
		var t := 0.0
		var trail: Array = []
		while t < seconds:
			await get_tree().create_timer(0.25).timeout
			t += 0.25
			trail.append("(%.0f,%.0f z%.3f)" % [galaxy.camera.position.x, galaxy.camera.position.y, galaxy.camera.zoom.x])
		print("smoke: fleet-camera: %s: %s" % [label, " ".join(trail)])
	if "--via-colonies" in OS.get_cmdline_user_args():
		_on_view_action("colonies")
		await sample.call("colonies panel", 1.5)
		var row: Dictionary = await _query_args({"what": "colony", "colony": colony})
		_colonies_build(colony, star, int(row.get("planet", -1)), false, false)
	else:
		await _open_colony_at(star, colony, false)
	await sample.call("colony open", 2.0)
	if shots != "":
		await host.shot(shots + "/fc-1-colony.png")
	if "--pan" in OS.get_cmdline_user_args():
		# As a drag in the system (galaxy.gd's motion), then zoomed out to see another star.
		var k := get_window().content_scale_factor
		var start := Vector2(1100, 700)
		var ev := InputEventMouseButton.new()
		ev.button_index = MOUSE_BUTTON_LEFT
		ev.pressed = true
		ev.position = start * k
		Input.parse_input_event(ev)
		for i in 12:
			await get_tree().process_frame
			var m := InputEventMouseMotion.new()
			m.position = (start + Vector2(-25.0 * (i + 1), 15.0 * (i + 1))) * k
			m.relative = Vector2(-25, 15) * k
			m.button_mask = MOUSE_BUTTON_MASK_LEFT
			Input.parse_input_event(m)
		var up := ev.duplicate()
		up.pressed = false
		up.position = (start + Vector2(-300, 180)) * k
		Input.parse_input_event(up)
		await get_tree().create_timer(0.3).timeout
		for i in 6:
			var w := InputEventMouseButton.new()
			w.button_index = MOUSE_BUTTON_WHEEL_DOWN
			w.pressed = true
			w.position = Vector2(900, 600) * k
			Input.parse_input_event(w)
			await get_tree().create_timer(0.08).timeout
		await sample.call("panned and zoomed out (real input), panned %s" % galaxy.panned, 1.5)
	if "--map-click" in OS.get_cmdline_user_args():
		_open_fleet(fleet)
	else:
		_open_fleet_from_pane(fleet)
	await sample.call("fleet opened", 2.0)
	if shots != "":
		await host.shot(shots + "/fc-2-fleet.png")
	if "--send" in OS.get_cmdline_user_args():
		# A destination: the nearest other star, clicked as on the map (preview, then the order).
		var here: Vector2 = galaxy.star_position(star)
		var best := -1
		var best_d := INF
		for st: Dictionary in galaxy.stars:
			var d := here.distance_to(galaxy.star_position(int(st.id)))
			if int(st.id) != star and d < best_d:
				best_d = d
				best = int(st.id)
		_on_star_clicked(best)
		await get_tree().create_timer(0.6).timeout
		_on_star_clicked(best)
		await sample.call("sent to star %d" % best, 2.0)
	_view("fleet_panel").close()
	await sample.call("fleet closed", 2.0)
	if shots != "":
		await host.shot(shots + "/fc-3-back.png")
	_view("system_pane").close()
	await sample.call("system closed", 3.0)
	if shots != "":
		await host.shot(shots + "/fc-4-galaxy.png")


## The torpedo hint (owner 2026-11-23: no warning before a Guardian's plasma torpedo hit; the
## original's warning skips torpedoes): a battle with a torpedo flying at the player's ship (fixture
## work/fixtures/owner-2026-11-23-guardian); the player ends their turns until the state lists
## squares whose move meets it, then the pointer over one shows the TORPEDO WARNING toast.
func _smoke_torpedo(shots: String) -> bool:
	_send({"verb": "end_turn"})
	var view: Control = null
	var turns := 0
	var waited := 0.0
	while waited < 240.0 and turns < 12:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		view = _combat
		if view == null or not view.visible or not view._my_turn():
			_smoke_answer()
			continue
		await get_tree().create_timer(0.6).timeout
		var torps: Array = view.state.get("torpedo_squares", [])
		var flying: Array = (view.state.get("missiles", []) as Array).map(func(m: Dictionary) -> String: return "%s->%s" % [m.get("name", m.get("weapon", "?")), m.get("target", "?")])
		print("smoke: torpedo: turn %d, ship %d, %d legal, %d torpedo squares, flying %s" % [turns, int(view.state.ship),
			(view.state.legal as Array).size(), torps.size(), flying])
		if not torps.is_empty():
			var code := int(torps[0])
			view._hover_square = Vector2i(code / 1000, code % 1000)
			view._update_warning()
			await get_tree().create_timer(0.5).timeout
			var shown: bool = view._warn_panel.visible and String(view._warn_label.text).begins_with("TORPEDO") and not view._missile_button.visible
			print("smoke: torpedo: hint over %s: shown %s ('%s')" % [view._hover_square, shown, view._warn_label.text])
			if shots != "":
				await host.shot(shots + "/combat-torpedo-hint.png")
			view._hover_square = Vector2i(-1, -1) # checked in the same frame: the view takes the real pointer's square each frame
			view._update_warning()
			var gone: bool = view._last_missile_warning == 0
			print("smoke: torpedo: pointer off: toast gone %s" % gone)
			return shown and gone
		turns += 1
		view._act({"action": "end_turn"})
		await get_tree().create_timer(1.0).timeout
	print("smoke: torpedo: no torpedo squares in %d turns" % turns)
	return false


func _smoke_combat(shots: String) -> bool:
	_send({"verb": "end_turn"})
	var waited := 0.0
	var view: Control = null
	while waited < 60.0:
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
		view = _combat
		if view != null and view.visible and view._my_turn():
			break
		# The battle's target (SAVE10: Fortuna): the first planet offered, as the replay clicks one.
		var dialog: Control = _views.get("dialog")
		if overlay.visible and dialog != null and String(dialog.popup_name) == "attack_target":
			var picked := false
			for i in dialog.options.size():
				var item: Dictionary = dialog.options[i]
				if int(item.get("planet", -1)) >= 0 and bool(item.get("enabled", true)):
					print("smoke: attacking planet %d" % int(item.planet))
					dialog._answer(dialog.OPTION_ANSWER + i)
					picked = true
					break
			if picked:
				continue
		_smoke_answer()
	if view == null or not view._my_turn():
		print("smoke: no native battle (combat view %s)" % [view != null and view.visible])
		return false
	await get_tree().create_timer(1.5).timeout # art arrives, ships ease in
	print("smoke: battle open, round %d, %d ships, acting %d, %d legal squares" % [int(view.state.round), (view.state.ships as Array).size(),
		int(view.state.ship), (view.state.legal as Array).size()])
	if shots != "":
		await host.shot(shots + "/combat-start.png")
	# The planet under the pointer: its defences still standing and its people (owner 2026-11-23).
	var planet_scan: Dictionary = view._ship(0).get("scan", {})
	if planet_scan.has("colony"):
		view._hover_ship = 0
		view._place_card()
		var card_text: String = view._card_body.text
		print("smoke: combat planet card: %s" % card_text.replace("\n", " | "))
		if shots != "":
			await host.shot(shots + "/combat-planet-card.png")
		view._hover_ship = -1
		view._place_card()
		if not card_text.contains("Defences:"):
			print("smoke: FAILED: the planet's card has no defences line")
			return false
	else:
		print("smoke: combat planet card: no colony in this battle")
	await get_tree().create_timer(0.3).timeout
	view._health_toggle.button_pressed = true
	await get_tree().create_timer(0.4).timeout
	if shots != "":
		await host.shot(shots + "/combat-health.png")
	# Close up on the acting ship and its nearest neighbours: the shield glow and the crescents.
	var zoom_before: float = view._zoom
	var cam_before: Vector2 = view._cam
	view._zoom = 3.0
	view._cam = view._center(view._ship(int(view.state.ship)))
	await get_tree().create_timer(0.4).timeout
	if shots != "":
		await host.shot(shots + "/combat-close.png")
	await get_tree().create_timer(0.3).timeout
	# The biggest enemy (SAVE10: a Doom Star) at an everyday zoom: no box round the sprites.
	view._zoom = 1.3
	for s2: Dictionary in view._ships_list():
		if not view._mine(s2) and not bool(s2.get("planet", false)) and int(s2.get("size", 0)) == 5:
			view._cam = view._center(s2)
	await get_tree().create_timer(0.4).timeout
	if shots != "":
		await host.shot(shots + "/combat-doomstar.png")
	view._zoom = zoom_before
	view._cam = cam_before
	await get_tree().create_timer(0.4).timeout
	if shots != "":
		await host.shot(shots + "/combat-health-again.png")
	view._zoom = zoom_before
	view._cam = cam_before
	view._health_toggle.button_pressed = false
	view._open_menu(int(view.state.ship))
	await get_tree().create_timer(0.4).timeout
	if shots != "":
		await host.shot(shots + "/combat-ring.png")
	view._close_menu()
	# One move: the legal square furthest towards the enemy (the field's right half).
	var legal: Array = view.state.legal
	var acting := int(view.state.ship)
	var ship: Dictionary = view._ship(acting)
	var best := -1
	for code in legal:
		if best < 0 or abs(int(code) / 1000 - 40) < abs(best / 1000 - 40):
			best = int(code)
	if best >= 0:
		var before := Vector2i(int(ship.x), int(ship.y))
		# The pointer on the nearest enemy: its card, and the weapons that can reach it lit.
		var enemy := -1
		for s6: Dictionary in view._ships_list():
			if view._alive(s6) and not view._mine(s6) and not bool(s6.get("planet", false)):
				if enemy < 0 or view._center(s6).distance_to(view._center(ship)) < view._center(view._ship(enemy)).distance_to(view._center(ship)):
					enemy = int(s6.i)
		if enemy >= 0:
			var on_enemy: Vector2 = view._to_screen(view._ships[enemy].shown)
			Input.warp_mouse(on_enemy)
			var hover := InputEventMouseMotion.new()
			hover.position = on_enemy
			hover.global_position = on_enemy
			Input.parse_input_event(hover)
			await get_tree().create_timer(0.4).timeout
			if shots != "":
				await host.shot(shots + "/combat-target.png")
			# Scan (owner 2026-11-22: from the ring): armed, then a click on an enemy opens its scan.
			view._arm_scan()
			for pressed in [true, false]:
				var right := InputEventMouseButton.new()
				right.button_index = MOUSE_BUTTON_LEFT
				right.pressed = pressed
				right.position = on_enemy
				right.global_position = on_enemy
				Input.parse_input_event(right)
				await get_tree().process_frame
			await get_tree().create_timer(0.3).timeout
			print("smoke: scan of ship %d open: %s" % [enemy, view._scan_ship == enemy and view._scan_panel.visible])
			if shots != "":
				await host.shot(shots + "/combat-scan.png")
			# A right click on the scan's first weapon: its help (served while the battle waits).
			if view._scan_body.get_child_count() > 0:
				var row_at: Vector2 = (view._scan_body.get_child(0) as Control).get_global_rect().get_center()
				for pressed in [true, false]:
					var rc := InputEventMouseButton.new()
					rc.button_index = MOUSE_BUTTON_RIGHT
					rc.pressed = pressed
					rc.position = row_at
					rc.global_position = row_at
					Input.parse_input_event(rc)
					await get_tree().process_frame
				await get_tree().create_timer(0.8).timeout
				if shots != "":
					await host.shot(shots + "/combat-scan-help.png")
				# A click elsewhere closes the help popup, as a player's would.
				var away := Vector2(view.size.x * 0.5, view.size.y * 0.55)
				for pressed in [true, false]:
					var lc := InputEventMouseButton.new()
					lc.button_index = MOUSE_BUTTON_LEFT
					lc.pressed = pressed
					lc.position = away
					lc.global_position = away
					Input.parse_input_event(lc)
					await get_tree().process_frame
				await get_tree().create_timer(0.2).timeout
			view._hide_scan()
			# The initiative bar scrolls (owner 2026-11-22): a wheel notch over it, as a player does it.
			var bar_at: Vector2 = view._tl_scroll.get_global_rect().get_center()
			var scroll_before: int = view._tl_scroll.scroll_horizontal
			for pressed in [true, false]:
				var wheel := InputEventMouseButton.new()
				wheel.button_index = MOUSE_BUTTON_WHEEL_DOWN
				wheel.pressed = pressed
				wheel.position = bar_at
				wheel.global_position = bar_at
				Input.parse_input_event(wheel)
				await get_tree().process_frame
			print("smoke: initiative bar scrolled by the wheel: %s (%d -> %d)" % [view._tl_scroll.scroll_horizontal > scroll_before, scroll_before, view._tl_scroll.scroll_horizontal])
			view._tl_scroll.scroll_horizontal = 0
			view._scroll_goal.erase(view._tl_scroll)
			await get_tree().process_frame
			# Its right arrow, clicked as a player clicks it.
			var arrow_button: Control = view._tl_scroll.get_parent().get_child(view._tl_scroll.get_index() + 1)
			var arrow_at: Vector2 = arrow_button.get_global_rect().get_center()
			for pressed in [true, false]:
				var click := InputEventMouseButton.new()
				click.button_index = MOUSE_BUTTON_LEFT
				click.pressed = pressed
				click.position = arrow_at
				click.global_position = arrow_at
				Input.parse_input_event(click)
				await get_tree().process_frame
			await get_tree().create_timer(0.5).timeout
			print("smoke: initiative bar scrolled by its arrow: %s (0 -> %d)" % [view._tl_scroll.scroll_horizontal > 0, view._tl_scroll.scroll_horizontal])
			view._tl_scroll.scroll_horizontal = 0
			view._scroll_goal.erase(view._tl_scroll)
			# The camera frames an enemy's beam (owner 2026-11-22): zoomed in on our ship, a beam from
			# the enemy must bring both on screen before it plays.
			var keep_zoom: float = view._zoom
			var keep_cam: Vector2 = view._cam
			view._zoom = 2.6
			view._cam = view._ships[int(ship.i)].shown
			await get_tree().process_frame
			view._frame_enemy_fire({"kind": "beam", "from": enemy, "to": int(ship.i)})
			var held: bool = view._cam_hold > 0.0
			await get_tree().create_timer(1.3).timeout
			var area: Rect2 = view._clear_area()
			var both: bool = area.has_point(view._to_screen(view._ships[enemy].shown)) and area.has_point(view._to_screen(view._ships[int(ship.i)].shown))
			print("smoke: camera framed an enemy beam: held %s, both on screen %s, hold released %s (zoom %.2f)" % [held, both, view._cam_hold == 0.0, view._zoom])
			if shots != "":
				await host.shot(shots + "/combat-framed.png")
			view._zoom = keep_zoom
			view._cam = keep_cam
			view._cam_goal = Vector2.INF
			await get_tree().process_frame
			await get_tree().process_frame
			# Combat graphics gallery (owner 2026-11-22): each special between our ship and the nearest
			# enemy, frozen at a telling moment; then the lasting looks on the enemy.
			if "--gallery" in OS.get_cmdline_user_args() and shots != "":
				await _smoke_effects_gallery(view, int(ship.i), enemy, shots)
		# As a player does it: the pointer onto the square, then a left click (real input events).
		var square := Vector2i(best / 1000, best % 1000)
		var at: Vector2 = view._to_screen(view._square_center(square, ship))
		Input.warp_mouse(at)
		var motion := InputEventMouseMotion.new()
		motion.position = at
		motion.global_position = at
		Input.parse_input_event(motion)
		await get_tree().create_timer(0.3).timeout
		print("smoke: pointer at %s, hover ship %d, hover square %s, legal %s, my turn %s" % [at, view._hover_ship, view._hover_square,
			view._square_legal(view._hover_square), view._my_turn()])
		if shots != "":
			await host.shot(shots + "/combat-preview.png")
		await get_tree().create_timer(0.3).timeout
		await _smoke_click(at)
		var moved := 0.0
		while moved < 20.0 and not view._my_turn():
			await get_tree().create_timer(0.25).timeout
			moved += 0.25
		print("smoke: targets after the move %s" % [view.state.get("targets", [])])
		var after: Dictionary = view._ship(acting)
		print("smoke: moved ship %d from %s to %d,%d (now %d,%d)" % [acting, before, best / 1000, best % 1000, int(after.get("x", -1)), int(after.get("y", -1))])
		# The click must move the ship (2026-11-21: after the merge of main a click in the battle was
		# taken as "stop the turns", and the smoke went on in auto unnoticed).
		if int(after.get("x", -1)) != best / 1000 or int(after.get("y", -1)) != best % 1000:
			print("smoke: the player's move did not land")
			return false
		await get_tree().create_timer(0.8).timeout
		if shots != "":
			await host.shot(shots + "/combat-moved.png")
	# The combat speed option (owner 2026-11-22: Double seemed to do nothing): volleys after it
	# must come at x2.
	# Boarding (owner 2026-11-22): the game's checks answer for the nearest enemy ship.
	if view._my_turn():
		var acting_ship: Dictionary = view._ship(int(view.state.get("ship", -1)))
		var near := -1
		var near_d := INF
		for other: Dictionary in view._ships_list():
			if view._alive(other) and not view._mine(other) and not bool(other.get("planet", false)):
				var d: float = view._center(other).distance_to(view._center(acting_ship))
				if d < near_d:
					near_d = d
					near = int(other.i)
		if near >= 0:
			view._board_target = near
			view._act({"action": "board_check", "target": near})
			for i in 100:
				await get_tree().create_timer(0.05).timeout
				if view._my_turn():
					break
			print("smoke: board_check on ship %d: '%s' (popup %s)" % [near, view.last_error, view._board_panel != null])
			if view._board_panel == null:
				view._show_board_popup(near, 7) # the popup itself (no ship in boarding range here)
				await get_tree().process_frame
				if shots != "":
					await host.shot(shots + "/combat-board-popup.png")
			if view._board_panel != null:
				view._board_panel.queue_free()
				view._board_panel = null
	view._set_speed_x2(true)
	if view._my_turn():
		view._act({"action": "auto"})
	waited = 0.0
	var shot_kinds := {}
	var x2_volleys := [0, 0] # volleys at x2, at normal speed
	var counts := {}
	while view.visible and waited < 180.0:
		await get_tree().create_timer(0.05).timeout
		waited += 0.05
		# Combat graphics: the first missile stack and the first fighter stack in flight, close up.
		for flyer: Dictionary in view.state.get("missiles", []):
			var flyer_kind := "fighters" if bool(flyer.get("fighter", false)) else "missiles"
			# An enemy stack can be picked by the pointer (owner 2026-11-22: only Auto could shoot them).
			if not shot_kinds.has("pick") and int(flyer.get("owner", -1)) != int(view.state.get("player", -2)) and not (flyer.get("pictures", []) as Array).is_empty():
				shot_kinds["pick"] = true
				var p0: Array = flyer.pictures[0]
				var where := Vector2(float(flyer.x) + float(p0[0]) + 11.0, float(flyer.y) + float(p0[1]) + 11.0)
				var found: int = view._missile_at(where)
				print("smoke: enemy %s stack %d under the pointer: picked %d" % [flyer_kind, int(flyer.get("i", -1)), found])
				if found != int(flyer.get("i", -1)):
					print("smoke: an enemy stack could not be picked")
					return false
			if shots != "" and not shot_kinds.has(flyer_kind):
				shot_kinds[flyer_kind] = true
				var keep_zoom: float = view._zoom
				view._cam = Vector2(float(flyer.get("x", 0)), float(flyer.get("y", 0)))
				view._zoom = 3.0
				await get_tree().create_timer(0.3).timeout
				await host.shot(shots + "/combat-%s.png" % flyer_kind)
				view._zoom = keep_zoom
		# WP3.9c: the first volley and the first explosion on screen, the camera on them.
		for effect: Dictionary in view._effects:
			var kind := String(effect.kind)
			counts[kind] = int(counts.get(kind, 0)) + (1 if effect.t <= 0.06 and effect.t > 0.0 else 0)
			# The big ships' death (owner 2026-11-22: Doom Stars vanished), mid-explosion.
			if shots != "" and kind == "sfx" and not shot_kinds.has("big_death") and float(effect.t) > 0.12:
				for part: Dictionary in effect.parts:
					if String(part.type) == "anim" and String(part.get("name", "")) == "cmbtsfx.lbx" and int(part.get("entry", -1)) == 5:
						shot_kinds["big_death"] = true
						view._cam = part.at
						view.smoke_hold = true
						await get_tree().process_frame
						await get_tree().process_frame
						await host.shot(shots + "/combat-death-big.png")
						print("smoke: big ship death at t %.2f, art %s" % [float(effect.t), not view.art.sprite("cmbtsfx.lbx", 5).is_empty()])
						view.smoke_hold = false
						break
			if kind == "volley" and not effect.get("_x2_counted", false):
				effect["_x2_counted"] = true
				x2_volleys[0 if bool(effect.get("speedx2", false)) else 1] += 1
			if shots != "" and not shot_kinds.has(kind) and kind in ["shot", "explosion", "capture", "volley"] and float(effect.t) > (0.3 if kind == "volley" else 0.12):
				shot_kinds[kind] = true
				var where: Vector2 = effect.get("at", view._effect_point(int(effect.get("to", -1)), view._cam))
				view._cam = where
				if kind == "volley":
					# WP3.9 stage 1: the first native volley held still and shot close up on chosen
					# ticks: its first segment, the middle, its first hit flare.
					await _smoke_volley_ticks(view, effect, shots)
					continue
				await host.shot(shots + "/combat-%s.png" % ("fire" if kind == "shot" else kind))
				if kind == "capture":
					# Spike 3.9: the same volley a few frames on, at the camera's zoom.
					await get_tree().create_timer(0.18).timeout
					await host.shot(shots + "/combat-capture-later.png")
		if int(waited * 20.0) % 10 == 0 and not _smoke_answer() and view._my_turn():
			view._act({"action": "auto"})
	print("smoke: effects seen %s" % [counts])
	print("smoke: effects played for the engine %s" % [view.animated]) # WP3.9d
	print("smoke: enemy fire framed by the camera: %d" % view.framed)
	print("smoke: volley art late on its first tick: %d of %d %s" % [view.art_late.size(), view.art_late.size() + view.art_drawn.size(), view.art_late.keys()])
	print("smoke: battle over after %.0f s (scene shown: %s)" % [waited, view.visible])
	print("smoke: volleys at x2 %d, at normal speed %d" % x2_volleys)
	view._set_speed_x2(false)
	if x2_volleys[0] == 0 or x2_volleys[0] < x2_volleys[1]:
		print("smoke: Double combat speed did not reach the volleys")
		return false
	return not view.visible


## WP3.9: SELF DESTRUCT in a native battle (owner, 2026-11-20: on a battlestation it crashed the
## game; ec6244a). The acting ship is ordered to self destruct; it must be gone and the battle must
## go on to its end.
func _smoke_selfdestruct_native() -> bool:
	var view: Control = _combat_view()
	var waited := 0.0
	while view.visible and not view._my_turn() and waited < 60.0:
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	if not view._my_turn():
		print("smoke: selfdestruct: no order asked for")
		return false
	var acting := int(view.state.get("ship", -1))
	var ship: Dictionary = view._ship(acting)
	print("smoke: selfdestruct: ship %d '%s' size %s planet %s" % [acting, ship.get("name", "?"), ship.get("size", "?"), ship.get("planet", false)])
	view._act({"action": "self_destruct"})
	waited = 0.0
	while view.visible and int(view._ship(acting).get("status", 0)) == 0 and waited < 30.0:
		_smoke_answer()
		await get_tree().create_timer(0.25).timeout
		waited += 0.25
	var status := int(view._ship(acting).get("status", 0))
	print("smoke: selfdestruct: ship %d status %d after %.1f s (battle shown %s)" % [acting, status, waited, view.visible])
	if view.visible and status == 0:
		return false
	# The rest of the battle on Auto: it must end.
	waited = 0.0
	while view.visible and waited < 240.0:
		if not _smoke_answer() and view._my_turn():
			view._act({"action": "auto"})
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
	print("smoke: selfdestruct: battle over after %.0f s (shown %s)" % [waited, view.visible])
	return not view.visible


## After the battle (2026-11-21, main's rewritten aftermath: the orbit and bombing window, the
## emptied colony's message, the to-do list): the turn's questions answered until the native galaxy
## is back on the next stardate.
func _smoke_after_battle(stardate_before: int, shots: String) -> bool:
	var waited := 0.0
	var stardate := stardate_before
	while waited < 120.0:
		_smoke_answer()
		await get_tree().create_timer(0.5).timeout
		waited += 0.5
		if native_screen == Screens.MAIN and not overlay.visible and not _in_battle():
			var empire: Dictionary = await _query("empire")
			stardate = int(empire.get("stardate", stardate_before))
			if stardate > stardate_before:
				break
	print("smoke: after the battle: galaxy %s, stardate %d (was %d) after %.0f s" % [native_screen == Screens.MAIN, stardate, stardate_before, waited])
	if shots != "":
		await host.shot(shots + "/combat-after.png")
	return native_screen == Screens.MAIN and stardate > stardate_before


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
	# A combat's question: the map's fleets at that star are this turn's (owner, 2026-11-20: a
	# blockade's question came with the arriving fleet still on its way).
	var asked_text: String = _views["dialog"].get_node("Fit/Box/Text").text
	if asked_text.contains("select combat at "):
		var star_name := asked_text.get_slice("select combat at ", 1).strip_edges()
		for star: Dictionary in galaxy.stars:
			if String(star.label).trim_prefix("(").trim_suffix(")") == star_name:
				var there: Array = galaxy.fleets.filter(func(f: Dictionary) -> bool: return int(f.get("star", -1)) == int(star.id))
				var coming: Array = galaxy.fleets.filter(func(f: Dictionary) -> bool: return int(f.get("destination", -1)) == int(star.id))
				print("smoke: combat question at %s: the map shows %d fleets there, %d still on their way" % [star_name, there.size(), coming.size()])
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
	_scene()
	_research_picker() # the overlay's windows follow the theme (owner, 2026-11-20: the discovery window stayed Glass)
	var saved_scale := Themes.saved_ui_scale()
	if OS.get_environment("SMOKE_SCALE") != "": # a player's scale near the limit (owner: 130% on Glass, then Chrome)
		var wide := OS.get_environment("SMOKE_WINDOW").split("x")
		if wide.size() == 2:
			get_window().size = Vector2i(int(wide[0]), int(wide[1]))
			await get_tree().create_timer(0.5).timeout
		Themes.save_ui_scale(float(OS.get_environment("SMOKE_SCALE")) * 100.0)
		set_ui_scale(_wanted_ui_scale(), false)
		await get_tree().create_timer(0.3).timeout
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
		await get_tree().create_timer(0.2).timeout
		print("smoke: theme %s scale %.0f%%, largest %.0f%%" % [entry.id, get_window().content_scale_factor * 100.0, max_ui_scale()])
		if get_window().content_scale_factor * 100.0 > max_ui_scale() + 0.5:
			overlaps.append("%s: the scale stayed over the theme's largest" % entry.id)
		if _scene_window.theme != native_layer.theme or _picker.theme != native_layer.theme:
			overlaps.append("%s: the discovery window or research picker kept the old theme" % entry.id)
	print("smoke: theme back to %s (started on %s), scale %.0f%%" % [theme_id, first_theme, get_window().content_scale_factor * 100.0])
	Themes.save_ui_scale(saved_scale)
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
	var scale_before: float = galaxy.system_scale
	button.pressed.emit()
	var waited := 0.0
	var asked: Array = []
	var landing_seen := false
	var kept_checked := false
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
			if not kept_checked:
				# Its own system open (owner, 2026-11-21 review): the question leaves the pane and
				# the system's scale as they were, the window beside the system.
				kept_checked = true
				await get_tree().create_timer(0.8).timeout
				var kept: bool = _view("system_pane").visible and absf(galaxy.system_scale - scale_before) < 0.0001
				print("smoke: settle question over its open system: pane and scale kept %s, placement '%s' at %s" % [kept, dialog.placement,
					dialog.get_global_rect()])
				if shots != "":
					await host.shot(shots + "/native-settle-question.png")
				if not kept:
					await host.smoke_fail("COLONIZE's question closed the open system's pane or rescaled the system")
					return
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
	# A click on the film skips it (owner, 2026-11-20: the Antarans' attack couldn't be clicked
	# through): a real press, held as a person holds it, then released.
	var film_at: Vector2 = _film_frame.get_global_rect().get_center() * get_window().content_scale_factor
	var press := InputEventMouseButton.new()
	press.button_index = MOUSE_BUTTON_LEFT
	press.pressed = true
	press.position = film_at
	Input.parse_input_event(press)
	await get_tree().create_timer(0.15).timeout
	var release := press.duplicate()
	release.pressed = false
	Input.parse_input_event(release)
	var skip_wait := 0.0
	var legacy_frames := 0 # frames the legacy view showed after the click (owner: a flash of it)
	while _film_active and skip_wait < 2.0:
		await get_tree().process_frame
		skip_wait += get_process_delta_time()
	for i in 60:
		await get_tree().process_frame
		if legacy.visible:
			legacy_frames += 1
	print("smoke: film clicked: skipped %s after %.1f s; legacy view shown in %d of 60 frames after" % [not _film_active, skip_wait, legacy_frames])
	if _film_active or legacy_frames > 0:
		await host.smoke_fail("a click didn't skip the film")
		return
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


## WP3.9 stage 1 (smoke --combat): a native volley frozen on a few ticks, shot at 3x zoom between
## its shooter and target.
func _smoke_volley_ticks(view: Control, effect: Dictionary, shots: String) -> void:
	var ticks: Array = effect.ticks
	var first_seg := -1
	var first_flare := -1
	for t in ticks.size():
		for op: Dictionary in ticks[t]:
			if String(op.op) == "seg" and first_seg < 0:
				first_seg = t
			if String(op.op) == "explosion" and first_flare < 0:
				first_flare = t
	var first_shield := -1
	for t in ticks.size():
		for op: Dictionary in ticks[t]:
			if String(op.op) == "shield" and int(op.get("status", 0)) == 2 and first_shield < 0:
				first_shield = t
	var picks := {"start": maxi(first_seg, 0), "middle": ticks.size() / 2, "flare": first_flare, "shield": first_shield,
		"flare-later": first_flare + 2 if first_flare >= 0 and first_flare + 2 < ticks.size() else -1}
	view.smoke_hold = true
	var keep_zoom: float = view._zoom
	view._zoom = 3.0
	for name: String in picks:
		var t: int = picks[name]
		if t < 0:
			continue
		var ops: Array = ticks[t]
		var seg: Dictionary = {}
		for op: Dictionary in ops:
			if String(op.op) == "seg":
				seg = op
				break
		if not seg.is_empty():
			# The flare: the target up and left of centre, clear of the hover card.
			view._cam = (Vector2(seg.from) + Vector2(seg.to)) / 2.0 if not name.begins_with("flare") else Vector2(seg.end) + Vector2(110, 60)
		elif name == "shield":
			view._cam = effect.at + Vector2(110, 60)
		effect.t = (float(t) + 0.5) * 0.055
		for i in 3:
			await get_tree().process_frame
		await host.shot(shots + "/combat-volley-%s.png" % name)
		print("smoke: volley tick %d (%s): %s" % [t, name, ", ".join(ops.map(func(op): return String(op.op)))])
	view._zoom = keep_zoom
	view.smoke_hold = false


func _smoke_effects_gallery(view: Control, mine: int, enemy: int, shots: String) -> void:
	var keep_zoom: float = view._zoom
	var keep_cam: Vector2 = view._cam
	var a: Vector2 = view._ships[mine].shown
	var b: Vector2 = view._ships[enemy].shown
	view._cam = (a + b) / 2.0
	view._zoom = clampf(minf(view.size.x * 0.6 / maxf(absf(b.x - a.x), 1.0), 2.4), 0.6, 2.4)
	# Picking an enemy missile stack by the pointer (owner 2026-11-22), on a stack set into the state.
	var fake := {"i": 77, "owner": int(view._ship(enemy).get("owner", -1)), "x": int(b.x), "y": int(b.y) - 60, "pictures": [[-12, -12], [4, -12]],
		"lbx": "CMBTMISL.LBX", "entry": 0, "count": 10, "facing": 0}
	if not view.state.has("missiles"):
		view.state["missiles"] = []
	(view.state["missiles"] as Array).append(fake)
	var picked: int = view._missile_at(Vector2(fake.x, fake.y) + Vector2(-1, -1))
	var own := fake.duplicate()
	own["owner"] = int(view.state.get("player", -1))
	own["i"] = 78
	(view.state["missiles"] as Array).append(own)
	var picked_own: int = view._missile_at(Vector2(own.x, own.y) + Vector2(-1, -1) + Vector2(0, 0))
	print("smoke: gallery: enemy stack picked %d (77 wanted), own stack picked %d" % [picked, picked_own])
	(view.state["missiles"] as Array).erase(fake)
	(view.state["missiles"] as Array).erase(own)
	# The gyro destabilizer spins its target a facing at a time (owner 2026-11-23: it jumped to the
	# final facing): start facing held, 8 slow steps, then the rest fast, ending at start + steps.
	view._effects.clear()
	view._play_special({"kind": "gyro", "from": mine, "to": enemy, "facing": 3, "steps": 20})
	var spin_fx: Dictionary = view._effects.back()
	var spun := []
	for t in [0.0, 8 * 0.055 + 0.001, 24 * 0.055 + 0.001, float(spin_fx.life) + 0.01]:
		spin_fx["t"] = t
		spun.append(view._spin_facing(enemy))
	view._effects.clear()
	print("smoke: gallery: gyro spin facings %s" % [spun])
	if spun != [3, 3, 11, 7]:
		print("smoke: FAILED: the gyro spin went %s, not [3, 3, 11, 7]" % [spun])
	var cases := [
		[{"kind": "stellar", "from": mine, "to": enemy}, 1.4],
		[{"kind": "stasis", "from": mine, "to": enemy}, 0.4],
		[{"kind": "stasis", "from": mine, "to": enemy}, 1.25],
		[{"kind": "tractor", "from": mine, "to": enemy}, 0.3],
		[{"kind": "plasma_web", "from": mine, "to": enemy}, 0.5],
		[{"kind": "black_hole", "from": mine, "to": enemy}, 0.3],
		[{"kind": "gyro", "from": mine, "to": enemy}, 0.3],
		[{"kind": "blast", "ship": enemy, "type": 1}, 0.3],
		[{"kind": "blast", "ship": enemy, "type": 2}, 0.3],
		[{"kind": "blast", "ship": enemy, "type": 3}, 0.3],
		[{"kind": "blast", "ship": enemy, "type": 4}, 0.5],
		[{"kind": "lightning", "ship": enemy}, 0.3],
		[{"kind": "retreat", "ship": enemy}, 0.6],
		[{"kind": "teleport", "ship": enemy, "x0": int(view._ship(enemy).get("x", 0)) - 3, "y0": int(view._ship(enemy).get("y", 0))}, 0.35],
		[{"kind": "cloak", "ship": enemy, "kind_": 1}, 0.9],
		[{"kind": "cloak", "ship": enemy, "kind_": 4}, 2.0],
		[{"kind": "blast", "ship": enemy, "type": 3}, 0.12],
		[{"kind": "destroyed", "ship": enemy, "death": 1, "size": 2}, 0.3],
		[{"kind": "destroyed", "ship": enemy, "monster_entry": 0x45, "monster_sound": -1, "facing": 2}, 0.3],
		[{"kind": "destroyed", "ship": enemy, "monster_entry": 0x45, "monster_sound": -1, "facing": 2}, 0.8],
	]
	view.smoke_hold = true
	var n := 0
	for c: Array in cases:
		var event: Dictionary = c[0].duplicate()
		if event.has("kind_"):
			event["kind"] = "cloak"
			event["cloak_type"] = int(event.kind_)
		view._effects.clear()
		var life: float
		view._gone.clear()
		if String(event.kind) == "destroyed":
			view._death_played.clear()
			life = view._play_death(event)
			view._death_played.clear()
		else:
			life = view._play_special(event)
		var effect: Dictionary = view._effects.back() if not view._effects.is_empty() else {}
		# The art first (each sprite a new export).
		for i in 40:
			var ready := true
			for part: Dictionary in effect.get("parts", []):
				if part.has("name") and view.art.sprite(String(part.name), int(part.entry)).is_empty():
					ready = false
			if ready:
				break
			await get_tree().create_timer(0.1).timeout
		if not effect.is_empty():
			effect.t = minf(float(c[1]), life * 0.95)
		for i in 3:
			await get_tree().process_frame
		await host.shot(shots + "/gallery-%02d-%s%s.png" % [n, String(event.kind), ("-%d" % int(event.get("type", 0))) if event.has("type") else ""])
		n += 1
	# The officer's cry (sound 0x21, owner 2026-11-23): with an explosion, and with the drive blast
	# that follows a drive-core death.
	for pair: Array in [[{"kind": "destroyed", "ship": enemy, "death": 1, "size": 2, "officer_sound": true}, {}],
			[{"kind": "destroyed", "ship": enemy, "death": 3, "size": 2, "officer_sound": true}, {"kind": "blast", "ship": enemy, "type": 3}]]:
		view._effects.clear()
		view._death_played.clear()
		view._gone.clear()
		view._officer_cry.clear()
		view._play_death(pair[0])
		if not (pair[1] as Dictionary).is_empty():
			view._play_special(pair[1])
		var cry := false
		for effect: Dictionary in view._effects:
			for part: Dictionary in effect.get("parts", []):
				if String(part.type) == "sound" and int(part.get("id", -1)) == 0x21:
					cry = true
		if cry:
			print("smoke: officer cry with death %d ok" % int(pair[0].death))
		else:
			print("smoke: FAILED: no officer cry with death %d" % int(pair[0].death))
	view._death_played.clear()
	view._officer_cry.clear()
	view._effects.clear()
	view._gone.clear()
	# The lasting looks, set on the enemy in the shown state.
	for look in ["stasis", "webbed", "black_hole", "absorbing", "tractor"]:
		var st: Dictionary = view.state
		var ship: Dictionary = view._ship(enemy)
		var keep := ship.duplicate()
		if look == "tractor":
			view._ship(mine)["tractor"] = enemy
		else:
			ship[look] = true
		for part in [["stasis", "cmbtsfx.lbx!shield"], ["webbed", "cmbtsfx.lbx!glass=web"], ["black_hole", "cmbtsfx.lbx!glass=bhg"]]:
			pass
		for i in 20:
			await get_tree().create_timer(0.1).timeout
		await host.shot(shots + "/gallery-%02d-lasting-%s.png" % [n, look])
		n += 1
		for k in ship.keys():
			if not keep.has(k):
				ship.erase(k)
		for k in keep.keys():
			ship[k] = keep[k]
		view._ship(mine).erase("tractor")
	view.smoke_hold = false
	view._zoom = keep_zoom
	view._zoom_goal = -1.0
	view._cam = keep_cam
	view._cam_goal = Vector2.INF
	view._frame = {}
	for i in 4:
		await get_tree().process_frame
	view._place_world()
	print("smoke: effects gallery: %d shots" % n)

