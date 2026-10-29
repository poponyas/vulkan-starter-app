#version 450 core

layout(location = 0) in vec3 world_position;
layout(location = 1) in vec3 world_normal;
layout(location = 0) out vec4 out_color;

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
    vec3 N = normalize(world_normal);
    vec3 L = -globals.light_direction.xyz;
    vec3 view_direction = globals.camera_position.xyz - world_position;
    vec3 V = view_direction / max(length(view_direction), 0.0001);
    vec3 halfway = L + V;
    vec3 H = halfway / max(length(halfway), 0.0001);
    float diffuse = max(dot(N, L), 0.0);
    float specular = diffuse > 0.0
        ? pow(max(dot(N, H), 0.0), globals.specular.w) : 0.0;
    vec3 ambient = globals.ambient_color.rgb * globals.ambient_color.w * globals.albedo.rgb;
    vec3 light = globals.light_color.rgb * globals.light_color.w;
    vec3 color = ambient + light * (globals.albedo.rgb * diffuse + globals.specular.rgb * specular);
    out_color = vec4(color, 1.0);
}
