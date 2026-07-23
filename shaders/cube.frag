#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

const vec3 kLightDir = normalize(vec3(0.35, 0.9, 0.5));

void main() {
    float diffuse = max(dot(normalize(inNormal), kLightDir), 0.0);
    vec3 lit = pc.color.rgb * (0.35 + 0.65 * diffuse);
    outColor = vec4(lit, pc.color.a);
}
