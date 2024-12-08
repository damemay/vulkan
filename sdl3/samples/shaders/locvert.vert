#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in float inUvX;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inUvY;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUV;

layout(push_constant) uniform constants {
    mat4 render_matrix;
} PushConstants;

void main() {
    gl_Position = PushConstants.render_matrix * vec4(inPos, 1.0);
    fragColor = vec3(0.0);
    fragUV = vec2(inUvX, inUvY);
}
