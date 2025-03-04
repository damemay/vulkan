#version 450
#extension GL_EXT_buffer_reference: require

layout(location = 0) out vec3 fragColor;

struct Vertex {
    vec4 position;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{
    Vertex vertices[];
};

layout(push_constant) uniform constants {
    VertexBuffer vertex_buffer;
} PushConstants;

void main() {
    Vertex vert = PushConstants.vertex_buffer.vertices[gl_VertexIndex];
    gl_Position = vec4(vert.position);
    fragColor = vec3(vert.color);
}
