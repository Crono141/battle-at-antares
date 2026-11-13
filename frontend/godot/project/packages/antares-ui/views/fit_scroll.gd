extends ScrollContainer
## A pane's contents that scroll when the window can't hold them (owner, 2026-11-13: at a large
## interface scale the Options flyout ran off the bottom of the screen). The pane keeps its
## natural height while there is room, and stops at the window's bottom edge otherwise.

## Room kept below the pane (logical pixels).
@export var bottom_margin := 8.0
## The pane is centred vertically (dialogs): room is kept above and below.
@export var centred := false

var _fitting := false


func _ready() -> void:
	horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	get_viewport().size_changed.connect(_queue_fit)
	for child in get_children():
		if child is Control:
			(child as Control).minimum_size_changed.connect(_queue_fit)
	var pane := get_parent() as Control
	if pane:
		pane.item_rect_changed.connect(_queue_fit)
		pane.visibility_changed.connect(_queue_fit)
	_queue_fit()


func _queue_fit() -> void:
	if not _fitting:
		_fitting = true
		_fit.call_deferred()


func _fit() -> void:
	_fitting = false
	var content := _content()
	var pane := get_parent() as Control
	if content == null or pane == null or not is_inside_tree():
		return
	var wanted := content.get_combined_minimum_size()
	var chrome := maxf(pane.size.y - size.y, 0.0) # the pane's own borders and padding
	var height := get_viewport_rect().size.y
	var room := height - 2.0 * bottom_margin - chrome if centred \
		else height - pane.global_position.y - bottom_margin - chrome
	room = maxf(room, 80.0)
	var fitted := Vector2(0.0, minf(wanted.y, room))
	if fitted != custom_minimum_size:
		custom_minimum_size = fitted
		# A pane sized by its contents shrinks back to them (its width and edges as they are).
		pane.size = Vector2(pane.size.x, 0.0)


func _content() -> Control:
	for child in get_children():
		if child is Control and not child is ScrollBar:
			return child
	return null
