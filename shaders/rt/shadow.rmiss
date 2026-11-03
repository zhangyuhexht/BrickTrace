#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 1) rayPayloadInEXT float shadowLit;

void main() {
    // Miss means no occluder: lit stays 1.0.
}
