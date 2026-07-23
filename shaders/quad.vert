#version 450

layout(push_constant) uniform Push {
    vec2 offset;
    vec2 scale;
    vec4 color;
} pc;

vec2 positions[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0)
);

void main() {
    vec2 p = positions[gl_VertexIndex] * pc.scale + pc.offset;
    gl_Position = vec4(p, 0.0, 1.0);
}
