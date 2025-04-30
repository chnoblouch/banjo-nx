#version 460

layout (location = 0) in vec2 in_position;
layout (location = 0) out vec4 out_color;

void main() {
    gl_Position = vec4(in_position, 0.0, 1.0);
    out_color = vec4(1.0, 0.0, 0.5, 1.0);
}
