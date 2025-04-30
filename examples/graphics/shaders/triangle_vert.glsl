#version 460

const vec2 positions[3] = vec2[](
    vec2(-0.5, -0.5),
    vec2( 0.5, -0.5),
    vec2( 0.0,  0.5)
);

const vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout (location = 0) out vec4 out_color;

void main() {
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
    out_color = vec4(colors[gl_VertexID], 1.0);
}
