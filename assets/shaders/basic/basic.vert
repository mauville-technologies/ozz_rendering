#version 450

vec2 positions[3] = vec2[](
vec2(0.0, -0.5),
vec2(0.5, 0.5),
vec2(-0.5, 0.5)
);

vec3 colors[3] = vec3[](
vec3(1.0, 0.0, 0.0),
vec3(0.0, 1.0, 0.0),
vec3(0.0, 0.0, 1.0)
);

layout(location = 0) out vec3 fragColor;

void main() {
    vec2 position;
    vec3 color;

    switch (gl_VertexIndex) {
        case 0:
        position = positions[0];
        color = colors[0];
        break;
        case 1:
        position = positions[1];
        color = colors[1];
        break;
        case 2:
        position = positions[2];
        color = colors[2];
        break;
    }
    gl_Position = vec4(position, 0.0, 1.0);
    fragColor = color;
}