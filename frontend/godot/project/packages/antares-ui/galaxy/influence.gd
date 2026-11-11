extends Node2D
## Empires' influence (the legacy map's V key: off, filled, borders only). The `galaxy` query
## gives one disc per known colony star, its radius from the owner's ship range; each empire's
## discs go into their own CanvasGroup, whose shader draws the union as a fill and a border.

const SHADER := preload("influence.gdshader")
const FILL := 0.12

@onready var galaxy: Node2D = get_parent()

var _built_for := {} # the galaxy data the groups were built from
var _groups := {} # player colour -> CanvasGroup


func _process(_delta: float) -> void:
	visible = galaxy.influence_mode != 0
	if not visible or is_same(_built_for, galaxy.data):
		return
	_built_for = galaxy.data
	_rebuild()


func _rebuild() -> void:
	for group in _groups.values():
		group.queue_free()
	_groups.clear()
	var discs := {} # colour -> Array of [centre, radius]
	for source: Dictionary in galaxy.data.get("influence", []):
		var color := int(source.color)
		if not discs.has(color):
			discs[color] = []
		discs[color].append([galaxy.star_position(int(source.star)), float(source.radius)])
	for color: int in discs:
		var group := CanvasGroup.new()
		group.name = "Empire%d" % color
		var material := ShaderMaterial.new()
		material.shader = SHADER
		material.set_shader_parameter("tint", galaxy.player_color(color))
		group.material = material
		var drawer := Node2D.new()
		drawer.name = "Discs"
		drawer.draw.connect(func():
			for disc: Array in discs[color]:
				drawer.draw_circle(disc[0], disc[1], Color.WHITE))
		group.add_child(drawer)
		add_child(group)
		_groups[color] = group
	_apply_mode()


func _apply_mode() -> void:
	for group: CanvasGroup in _groups.values():
		(group.material as ShaderMaterial).set_shader_parameter("fill", FILL if galaxy.influence_mode == 1 else 0.0)


## The V key: off, filled, borders only.
func cycle() -> void:
	galaxy.influence_mode = (galaxy.influence_mode + 1) % 3
	_apply_mode()
