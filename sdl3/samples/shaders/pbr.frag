#version 450

layout(location = 0) in vec3 inWPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec4 inTangent;

layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D colorMap;
layout (set = 0, binding = 1) uniform sampler2D normalMap;
layout (set = 0, binding = 2) uniform sampler2D roughMetalMap;

layout(set = 1, binding = 0) uniform View {
    mat4 view;
    mat4 projection;
    vec4 position;
} view;

layout (set = 1, binding = 1) uniform Lights {
    vec4 position;
} lights;

const float PI = 3.14159265359;

vec3 mcolor() {
    return vec3(texture(colorMap, inUV).xyz);
}

float d_ggx(float nh, float r) {
    float a = nh*r;
    float k = r/(1.0 - nh*nh + a*a);
    return k*k * (1.0/PI);
}

float v_smithggx_hammon(float nv, float nl, float r) {
    float a = r;
    float l = mix(2*nl*nv, nl+nv, a);
    return 0.5 / l;
}

vec3 f_schlick(float u, vec3 f0) {
    float f = pow(1.0-u, 5.0);
    return f + f0*(1.0-f);
}

float fd_lambert() {
    return 1.0 / PI;
}

vec3 brdf(vec3 l, vec3 v, vec3 n, float m, float r) {
    vec3 h = normalize(v + l);
    float nv = abs(dot(n,v)) + 1e-5;
    float nl = clamp(dot(n,l), 0.0, 1.0);
    float nh = clamp(dot(n,h), 0.0, 1.0);
    float lh = clamp(dot(l,h), 0.0, 1.0);

    float rr = r*r;
    vec3 f0 = mix(vec3(0.04), mcolor(), m);

    float d = d_ggx(nh, rr);
    vec3 f = f_schlick(lh, f0);
    float g = v_smithggx_hammon(nv, nl, rr);

    vec3 con = mix(vec3(1.0)-f, vec3(0.0), m);
    vec3 diff = mcolor() * con * nl;
    vec3 spec = d*g*f;

    float dist = length(l - inWPos);
    float attn = 1.0/(dist*dist);
    vec3 rad = vec3(100.0) * attn;

    vec3 color = vec3(0.0);
    color += spec * diff * rad;
    return color;
}

void main() {
    vec4 color = texture(colorMap, inUV);
    if(color.a < 1.0) discard;

    float roughness = texture(roughMetalMap, inUV).g;
    float metallic = texture(roughMetalMap, inUV).b;

    vec3 n = normalize(inNormal);
    // vec3 t = normalize(inTangent.xyz);
    // vec3 b = cross(inNormal, inTangent.xyz) * inTangent.w;
    // mat3 tbn = mat3(t,b,n);
    // n = tbn * normalize(texture(normalMap, inUV).xyz * 2.0 - vec3(1.0));
    vec3 v = normalize(view.position.xyz - inWPos);

    vec3 lo = vec3(0.0);
    vec3 l = normalize(lights.position.xyz - inWPos);
    lo += brdf(l, v, n, metallic, roughness);

    float ambient = 0.02;
    vec3 ccolor = vec3(color);
    ccolor *= ambient;
    ccolor += lo;

    outColor = vec4(ccolor, 1.0);
}
