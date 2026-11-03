# BrickTrace

Open-source hardware ray tracing for **Minecraft Java Edition 26.3**.

**Static brick BVH + dynamic voxel payload** — the RT cores traverse a BLAS of 4³ brick AABBs, an intersection shader runs voxel DDA inside each brick, and block changes only rewrite voxel data. No acceleration structure is rebuilt when the world changes.

> Status: **Pre-M1**. Design docs only, no renderer code yet. See [docs/roadmap.md](docs/roadmap.md).

## Why

Existing Minecraft RT mods (Radiance, Radiante) trace triangle BLAS built from block geometry, so editing a block means rebuilding or refitting the affected acceleration structures. BrickTrace keeps the acceleration structure static: a 16³ section is 64 bricks of 4³ voxels, each brick is one AABB primitive with a 64-bit occupancy mask, and the voxel payload lives in a plain buffer that can be updated in microseconds.

## Architecture

```
Minecraft 26.3 JVM
├─ Fabric Mixin (LevelRenderer / GameRenderer / Window)
├─ WorldSnapshot (chunk / block / light / entity / camera)
├─ double-buffered snapshot + dirty-section queue
└─ JNI
    └─ Native C++ renderer (Vulkan 1.3, KHR ray tracing)
        ├─ WorldDB      brick pool / material table
        ├─ ASManager    static BLAS + per-frame TLAS
        ├─ RT passes    shadow / reflection / GI
        ├─ Denoise      NRD, DLSS RR
        ├─ Upscale      DLSS SR / FSR / XeSS
        └─ Composite / Present
```

| Layer | Representation |
|---|---|
| Section | 16³ blocks, 64 bricks |
| Brick | 4³ voxels, `uint64` occupancy mask |
| Voxel | `uint32`: material 16b · block light 4b · sky light 4b · flags 8b |
| BLAS | 64 AABB primitives per section, built once |
| TLAS | visible sections + entities, rebuilt per frame |

Voxel data per section is 16 KB; an empty brick has `occupancy == 0` and is skipped by DDA.

## Roadmap

| Phase | Deliverable | Status |
|---|---|---|
| P0 | 256³ world export + CPU/GPU brick DDA reference images | planned |
| P1 | Fabric mod, official `VkDevice`, RT triangle on screen | in progress |
| P2 | Terrain shadows, 1080p / 8 chunks / ≥ 30 fps | planned |
| P3 | GI + reflections + NRD | planned |
| P4 | Dynamic blocks and entities, 0 BLAS rebuilds | planned |
| P5 | DLSS / FSR / XeSS, HDR, UI compositing | planned |
| P6 | Multi-GPU validation, docs, GPL-3.0 release | planned |

Target hardware: 1080p, 8–12 chunks, 8 GB VRAM (currently validated on an RTX 5060 Laptop).

## Build

Nothing to build yet. The toolchain is documented in [docs/environment.md](docs/environment.md); run the environment check with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-env.ps1
```

## Docs

- [HANDOFF.md](HANDOFF.md) — project handoff and scope
- [docs/architecture.md](docs/architecture.md) — technical design (single source of truth)
- [docs/roadmap.md](docs/roadmap.md) — phases and the 14-day M1 plan
- [docs/environment.md](docs/environment.md) — Windows toolchain setup and verification
- [docs/prior-art.md](docs/prior-art.md) — prior art and novelty claim
- [docs/report-01-template.md](docs/report-01-template.md) — milestone report template

## License

[GPL-3.0](LICENSE). Third-party components keep their own licenses.

This project is clean-room: it is not derived from any closed-source Minecraft ray tracing mod, and no such code or assets are included.
