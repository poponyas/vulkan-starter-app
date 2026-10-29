#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;

layout(set = 0, binding = 0, std140) uniform GlobalUniforms {
    mat4 model;
    mat4 view_projection;
    mat4 normal_matrix;
    vec4 camera_position;
    vec4 light_direction;
    vec4 light_color;
    vec4 ambient_color;
    vec4 albedo;
    vec4 specular;
} globals;

void main() {
    vec4 position = globals.model * vec4(in_position, 1.0);
    world_position = position.xyz;
    world_normal = mat3(globals.normal_matrix) * in_normal;
    gl_Position = globals.view_projection * position;
}
