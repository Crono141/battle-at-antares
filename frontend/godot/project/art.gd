extends Node
## The game's own art as textures (WP3.3a). Views ask for an LBX entry by name and number; the
## first ask sends a request to the game (it exports the entry's frames from the HD pack, or the
## original pixels when the pack has none) and returns nothing yet; `loaded` follows, and later asks
## get the cached sprite. A sprite is a Dictionary:
##   frames:  Array of ImageTexture (with mipmaps, so they shrink cleanly)
##   origins: Array of Vector2, each frame's top-left in original (1x) pixels
##   size:    Vector2, the original 1x frame size
##   scale:   texture pixels per 1x pixel (4 with HD art, 1 without)

signal loaded(key: String)

var game: AntaresGame
var _sprites := {} # "LBX/entry" -> sprite Dictionary, or {} while it is on its way
var _pending := {} # request id -> key
var _failures := {} # key -> failed exports so far (asked again up to RETRIES times)
const RETRIES := 2
var _hd_retries := {} # key -> asks for HD art that was still decoding
var _hd_waiting := {} # key -> true while its 1x stands in for HD art still decoding
const HD_RETRIES := 4


func setup(antares_game: AntaresGame) -> void:
	game = antares_game
	game.sprite_ready.connect(_on_sprite_ready)


## The sprite, or an empty Dictionary until it has arrived.
func sprite(lbx: String, entry: int) -> Dictionary:
	if entry < 0 or game == null:
		return {}
	var key := "%s/%d" % [lbx, entry]
	if not _sprites.has(key):
		_sprites[key] = {}
		_pending[game.request_sprite(lbx, entry)] = key
	return _sprites[key]


## The sprite is as it will stay: arrived (in HD when that was decoding), or given up on.
func settled(lbx: String, entry: int) -> bool:
	if entry < 0:
		return true
	var key := "%s/%d" % [lbx, entry]
	if _hd_waiting.has(key):
		return false
	if not (_sprites.get(key, {}) as Dictionary).is_empty():
		return true
	return int(_failures.get(key, 0)) > RETRIES


## One frame's texture (frames past the last repeat it), or null until loaded.
func texture(lbx: String, entry: int, frame := 0) -> Texture2D:
	var s := sprite(lbx, entry)
	if s.is_empty():
		return null
	return s.frames[clampi(frame, 0, s.frames.size() - 1)]


## Draws a frame on `item` with the 1x frame's top-left at `top_left` and `scale` world units
## per 1x pixel. Returns false while the art is not here yet.
func draw(item: CanvasItem, lbx: String, entry: int, frame: int, top_left: Vector2, scale: float, modulate := Color.WHITE) -> bool:
	var s := sprite(lbx, entry)
	if s.is_empty():
		return false
	var i := posmod(frame, s.frames.size()) # animations loop (black holes: 16 frames)
	var tex: Texture2D = s.frames[i]
	var at: Vector2 = top_left + s.origins[i] * scale
	item.draw_texture_rect(tex, Rect2(at, Vector2(tex.get_width(), tex.get_height()) / s.scale * scale), false, modulate)
	return true


## Art changed (packages loaded): ask again.
## Drops one sprite (the scene window's large animations when it closes); asked again, it loads anew.
func forget(lbx: String, entry: int) -> void:
	var key := "%s/%d" % [lbx, entry]
	_sprites.erase(key)
	_failures.erase(key)
	for id in _pending.keys():
		if _pending[id] == key:
			_pending.erase(id)


## Asks for a sprite again (its HD art may be decoded by now); the 1x stays shown until it comes.
func _ask_again(key: String) -> void:
	if game == null or _pending.values().has(key):
		return
	var at := key.rfind("/")
	_pending[game.request_sprite(key.substr(0, at), int(key.substr(at + 1)))] = key


func clear() -> void:
	_hd_retries.clear()
	_hd_waiting.clear()
	_sprites.clear()
	_pending.clear()
	_failures.clear()


func _on_sprite_ready(id: int, data: Dictionary) -> void:
	if not _pending.has(id):
		return
	var key: String = _pending[id]
	_pending.erase(id)
	if data.frames.is_empty():
		push_warning("art: %s: %s" % [key, data.error])
		# A failed export is asked for again on the next ask, a few times (owner, 2026-11-14: the
		# build list's freighter picture came once and was missing later); then it stays empty and
		# views keep their stand-in.
		_failures[key] = int(_failures.get(key, 0)) + 1
		if int(_failures[key]) <= RETRIES:
			_sprites.erase(key)
		_hd_waiting.erase(key)
		loaded.emit(key) # settled for whoever waits on it (as given up on)
		return
	var frames: Array = []
	var origins: Array = []
	for frame: Dictionary in data.frames:
		var image: Image = frame.image
		# Full-screen scene frames (about 1280x960) show near their own size: no mipmaps, which
		# would add a third to tens of MB.
		if image.get_width() * image.get_height() < 1_000_000:
			image.generate_mipmaps()
		frames.append(ImageTexture.create_from_image(image))
		origins.append(Vector2(frame.origin))
	if data.get("hd_pending", false) and int(_hd_retries.get(key, 0)) < HD_RETRIES:
		# The 1x pixels for now; the HD art was still decoding (a slow disk): ask again shortly and
		# swap it in (owner, 2026-11-15: the Info panel's portrait came out muddy).
		_hd_retries[key] = int(_hd_retries.get(key, 0)) + 1
		_hd_waiting[key] = true
		get_tree().create_timer(1.5).timeout.connect(func(): _ask_again(key), CONNECT_ONE_SHOT)
	else:
		_hd_waiting.erase(key)
	_sprites[key] = {
		"frames": frames,
		"origins": origins,
		"size": Vector2(float(data.width), float(data.height)),
		"scale": float(data.scale),
	}
	loaded.emit(key)
