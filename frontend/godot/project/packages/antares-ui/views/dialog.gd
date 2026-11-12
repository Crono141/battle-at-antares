extends PanelContainer
## Native answer to the game's dialogs (requests, WP3.1b and WP3.2b): "message" (with an optional
## title) and "warning" have OK, "confirm" has Yes and No. The game thread waits until answered.

signal answered(id: int, value: int)

var request_id := 0


func _ready() -> void:
	$Box/Buttons/Yes.pressed.connect(func(): _answer(1))
	$Box/Buttons/No.pressed.connect(func(): _answer(0))


func ask(id: int, kind: String, request: Dictionary) -> void:
	request_id = id
	$Box/Text.text = request.get("text", "")
	var title: String = request.get("title", "")
	if title == "":
		title = "Warning" if kind == "warning" else ("Confirm" if kind == "confirm" else "")
	$Box/Title.text = title
	$Box/Title.visible = $Box/Title.text != ""
	$Box/Buttons/Yes.text = "YES" if kind == "confirm" else "OK"
	$Box/Buttons/No.visible = kind == "confirm"
	show()
	reset_size() # as tall as its text: OK as far from the bottom as the text from the top (owner)


func _answer(value: int) -> void:
	hide()
	answered.emit(request_id, value)
