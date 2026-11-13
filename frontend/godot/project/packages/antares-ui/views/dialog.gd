extends PanelContainer
## Native answer to the game's dialogs (requests, WP3.1b and WP3.2b): "message" (with an optional
## title) and "warning" have OK, "confirm" has Yes and No, "text" (the game's input box: a star's
## name) has a line to type in with OK and Cancel. The game thread waits until answered.

signal answered(id: int, value: int, text: String)

var request_id := 0


var _kind := ""


func _ready() -> void:
	$Fit/Box/Buttons/Yes.pressed.connect(func(): _answer(1))
	$Fit/Box/Buttons/No.pressed.connect(func(): _answer(0))
	$Fit/Box/Entry.text_submitted.connect(func(_t: String): _answer(1))


func ask(id: int, kind: String, request: Dictionary) -> void:
	request_id = id
	$Fit/Box/Text.text = request.get("text", "")
	var title: String = request.get("title", "")
	if title == "":
		title = "Warning" if kind == "warning" else ("Confirm" if kind == "confirm" else "")
	$Fit/Box/Title.text = title
	$Fit/Box/Title.visible = $Fit/Box/Title.text != ""
	_kind = kind
	var entry: LineEdit = $Fit/Box/Entry
	entry.visible = kind == "text"
	$Fit/Box/Buttons/Yes.text = "YES" if kind == "confirm" else "OK"
	$Fit/Box/Buttons/No.text = "CANCEL" if kind == "text" else "NO"
	$Fit/Box/Buttons/No.visible = kind == "confirm" or kind == "text"
	if kind == "text":
		entry.max_length = int(request.get("max_chars", 15))
		entry.text = String(request.get("value", ""))
	show()
	if kind == "text":
		entry.grab_focus()
		entry.select_all()
	reset_size() # as tall as its text: OK as far from the bottom as the text from the top (owner)


func _answer(value: int) -> void:
	hide()
	answered.emit(request_id, value, $Fit/Box/Entry.text if _kind == "text" else "")
