extends Node2D
## Nebulae: the closest zoom level's picture (STARBG.LBX), one picture pixel per map unit, from
## the nebula's top-left, as the legacy map places them at its closest zoom.

@onready var galaxy: Node2D = get_parent()


func _draw() -> void:
	for nebula: Dictionary in galaxy.data.get("nebulae", []):
		galaxy.art.draw(self, "STARBG.LBX", int(nebula.sprite), 0, Vector2(float(nebula.x), float(nebula.y)), 1.0)
