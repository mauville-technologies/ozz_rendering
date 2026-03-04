#version 450

layout (location = 0) in vec3 positions;
layout (location = 1) in vec4 colors;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    gl_Position = ubo.proj * ubo.view * pc.model * vec4(positions, 1.0);
    fragColor = colors.xyz;
}