#version 460

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_color;

layout (location = 0) out vec4 out_color;

layout (std140, binding = 0) uniform Uniforms {
    mat4 matrix;
} uniforms;

void main() {
    gl_Position = uniforms.matrix * vec4(in_position, 1.0);
    out_color = vec4(in_color, 1.0);
}
