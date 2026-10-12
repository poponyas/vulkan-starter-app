#version 450 core

layout(location = 0) in vec3 local_position;
layout(location = 0) out vec4 out_color;

void main() {

    vec3 normal = normalize(cross(dFdx(local_position), dFdy(local_position)));
    float shade = dot(abs(normal), vec3(0.55, 0.75, 1.0));
    out_color = vec4(vec3(shade), 1.0);
}
