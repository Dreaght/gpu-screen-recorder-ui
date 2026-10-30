#version 120

uniform vec2 size;
uniform float radius;
uniform float border_size;

float squircle_edge_distance(vec2 position, vec2 half_size) {
    vec2 corner_offset = abs(position) - (half_size - radius);
    vec2 outside = max(corner_offset, 0.0);
    vec2 outside_squared = outside * outside;
    float corner_distance = sqrt(sqrt(dot(outside_squared, outside_squared)));
    return corner_distance + min(max(corner_offset.x, corner_offset.y), 0.0) - radius;
}

void main() {
    vec2 half_size = size * 0.5;
    float edge_distance = squircle_edge_distance(gl_TexCoord[0].xy - half_size, half_size);
    float coverage = clamp(0.5 - edge_distance, 0.0, 1.0);
    if(border_size > 0.0)
        coverage *= clamp(edge_distance + border_size + 0.5, 0.0, 1.0);
    gl_FragColor = vec4(gl_Color.rgb, gl_Color.a * coverage);
}
