#version 460
#extension GL_EXT_ray_tracing : require

struct HitPayload {
    vec3 color;
    vec3 normal;
    float t;
    int material;
    int hit;
};

layout(location = 0) rayPayloadInEXT HitPayload primary;

void main() {
    primary.hit = 0;
}
