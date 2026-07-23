#version 450

layout(push_constant) uniform Push {
    vec2 offset;
    vec2 scale;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
