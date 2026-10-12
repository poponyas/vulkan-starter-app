#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec3 local_position;

layout(set = 0, binding = 0, std140) uniform GlobalUniforms {
    mat4 mvp;
} globals;

void main() {
    gl_Position = globals.mvp * vec4(in_position, 1.0);
    local_position = in_position;
}
