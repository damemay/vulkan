#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in float inUvX;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in float inUvY;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 outWPos;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec4 outTangent;

layout(set = 1, binding = 0) uniform View {
    mat4 view;
    mat4 projection;
    vec4 position;
} view;

layout(push_constant) uniform constants {
    mat4 model;
} PushConstants;

void main() {
    outWPos = vec3(PushConstants.model * vec4(inPos, 1.0));
    outUV = vec2(inUvX, inUvY);
    outNormal = mat3(PushConstants.model) * inNormal;
    outTangent = inTangent;
    gl_Position = view.projection * view.view * vec4(outWPos, 1.0);
}
