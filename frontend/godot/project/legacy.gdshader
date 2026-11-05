shader_type canvas_item;

// The game's frame arrives as ARGB8888 words (B, G, R, A in memory) uploaded as RGBA8.
void fragment() {
	COLOR = texture(TEXTURE, UV).bgra;
}
