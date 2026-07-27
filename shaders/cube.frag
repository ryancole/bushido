#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 n0; // .w = 1 means "unlit": emit pc.color as authored
    vec4 n1;
    vec4 n2;
    vec4 color;
} pc;

layout(location = 0) in vec3 inNormal;
layout(location = 0) out vec4 outColor;

const vec3 kLightDir = normalize(vec3(0.35, 0.9, 0.5));

void main() {
    float diffuse = max(dot(normalize(inNormal), kLightDir), 0.0);
    vec3 lit = pc.color.rgb * (0.35 + 0.65 * diffuse);
    // The sky is a light source, not a surface: shading its shell would make
    // the four walls of one gradient band four different colors.
    outColor = vec4(mix(lit, pc.color.rgb, pc.n0.w), pc.color.a);
}
