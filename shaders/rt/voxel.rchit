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
hitAttributeEXT uvec2 hitInfo;

const vec3 kNormals[6] = vec3[6](
    vec3(0.0, 0.0, -1.0),
    vec3(0.0, 0.0, 1.0),
    vec3(0.0, -1.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(-1.0, 0.0, 0.0),
    vec3(1.0, 0.0, 0.0)
);

vec3 materialColor(uint material) {
    if (material == 1u) return vec3(0.52, 0.52, 0.54);  // stone
    if (material == 2u) return vec3(0.47, 0.33, 0.23);  // dirt
    if (material == 3u) return vec3(0.32, 0.58, 0.26);  // grass
    if (material == 4u) return vec3(0.16, 0.38, 0.62);  // water
    return vec3(0.8, 0.2, 0.8);
}

void main() {
    const uint material = hitInfo.x & 0xffffu;
    const uint blockLight = (hitInfo.x >> 16) & 0xfu;
    const uint skyLight = (hitInfo.x >> 20) & 0xfu;

    primary.hit = 1;
    primary.t = gl_HitTEXT;
    primary.normal = kNormals[hitInfo.y];
    primary.material = int(material);
    primary.color = materialColor(material) * (0.55 + 0.03 * float(skyLight)) +
                    vec3(1.0, 0.75, 0.4) * (0.01 * float(blockLight));
}
