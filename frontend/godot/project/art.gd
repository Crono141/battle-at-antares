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
func clear() -> void:
	_sprites.clear()
	_pending.clear()


func _on_sprite_ready(id: int, data: Dictionary) -> void:
	if not _pending.has(id):
		return
	var key: String = _pending[id]
	_pending.erase(id)
	if data.frames.is_empty():
		push_warning("art: %s: %s" % [key, data.error])
		return # stays empty: views keep their stand-in
	var frames: Array = []
	var origins: Array = []
	for frame: Dictionary in data.frames:
		var image: Image = frame.image
		image.generate_mipmaps()
		frames.append(ImageTexture.create_from_image(image))
		origins.append(Vector2(frame.origin))
	_sprites[key] = {
		"frames": frames,
		"origins": origins,
		"size": Vector2(float(data.width), float(data.height)),
		"scale": float(data.scale),
	}
	loaded.emit(key)
