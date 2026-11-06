extends PanelContainer
## Native stand-in for SCREEN_SHOW_COMMAND_POINTS (WP3.1b): the same summary the legacy text box
## shows, from the "command_points" query. OK sends "close" (back to the screen that opened it).

signal command(dict: Dictionary)


func _ready() -> void:
	$Box/Ok.pressed.connect(func(): command.emit({"verb": "close"}))


func show_result(result: Dictionary) -> void:
	$Box/Title.text = result.get("title", "Command points")
	$Box/Text.text = result.get("text", "")
	$Box/Total.text = "%d of %d command points used" % [
		int(result.get("command_points_used", 0)), int(result.get("command_points", 0))]
