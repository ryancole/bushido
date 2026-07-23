#version 450

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 color;
} pc;

layout(location = 0) out vec3 outNormal;

// Unit cube centered at the origin, 6 faces * 2 triangles. Models are
// axis-aligned (translate + scale only), so object-space normals are valid
// world-space normals.
const vec3 kPositions[36] = vec3[](
    // -Z
    vec3(-0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5),
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    // +Z
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3(-0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    // -X
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5),
    // +X
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    // -Y
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    // +Y
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5)
);

const vec3 kNormals[6] = vec3[](
    vec3(0.0, 0.0, -1.0), vec3(0.0, 0.0, 1.0),
    vec3(-1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0),
    vec3(0.0, -1.0, 0.0), vec3(0.0, 1.0, 0.0)
);

void main() {
    gl_Position = pc.mvp * vec4(kPositions[gl_VertexIndex], 1.0);
    outNormal = kNormals[gl_VertexIndex / 6];
}
