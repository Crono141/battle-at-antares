// One empire's influence (galaxy/influence.gd): its discs are drawn opaque into a CanvasGroup,
// and this turns the group's union into a faint fill with a clear border, so overlapping discs
// never darken each other.
shader_type canvas_item;

uniform sampler2D screen_texture : hint_screen_texture, repeat_disable, filter_nearest;
uniform vec4 tint : source_color = vec4(1.0);
uniform float fill = 0.12; // the inside's opacity (0 for borders only)
uniform float border_pixels = 1.5;

float inside(vec2 uv) {
	// Past the screen's edge counts as inside, so no border is drawn along it.
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		return 1.0;
	}
	return textureLod(screen_texture, uv, 0.0).a;
}

void fragment() {
	if (inside(SCREEN_UV) < 0.5) {
		discard;
	}
	vec2 d = SCREEN_PIXEL_SIZE * border_pixels;
	float edge = min(min(inside(SCREEN_UV + vec2(d.x, 0.0)), inside(SCREEN_UV - vec2(d.x, 0.0))),
		min(inside(SCREEN_UV + vec2(0.0, d.y)), inside(SCREEN_UV - vec2(0.0, d.y))));
	COLOR = edge < 0.5 ? vec4(tint.rgb, 0.85) : vec4(tint.rgb, fill);
}
