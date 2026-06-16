# ProceduralLandmass Technical Overview

This document explains the current `AProceduralLandmass` implementation for programmers joining the project. It focuses on how the actor is structured, how the terrain is generated, how it behaves in Unreal Editor, and where future contributors should be careful.

## Purpose

`AProceduralLandmass` is a C++ actor that generates the project's main procedural island/landmass using Unreal's `UProceduralMeshComponent`.

The actor currently owns two procedural mesh components:

- `ProceduralMesh`: the main terrain heightfield. It is one height value per XY grid point, so it is good for hills, mountains, beaches, plateaus, ridges, and heightmap-safe cliffs.
- `OverhangMesh`: separate closed geometry for true overhang shapes. These cannot be represented by the main heightfield because a heightmap cannot fold back underneath itself.

The landmass is not an Unreal Landscape actor. That keeps the system lightweight and code-driven, but it also means Landscape-specific tooling, sculpting, foliage workflows, and assumptions do not automatically apply.

## Unreal Editor Behavior

The actor is designed to be tuned directly in the Details panel.

When a generation-related property changes in the editor, `PostEditChangeProperty` calls `GenerateTerrain()`, which rebuilds both the main terrain mesh and the overhang mesh. Material-only changes refresh material assignment without forcing a full geometry rebuild.

At runtime, `BeginPlay()` ensures the dynamic material instance exists and is synced. There is no per-frame terrain regeneration and no `Tick()` dependency.

The generated mesh sections create collision, so the player can walk on the procedural terrain and overhang geometry. Rebuilding high-resolution meshes can be expensive, so large map sizes should be tuned carefully.

## Generation Flow

The main flow is:

```text
GenerateTerrain()
  -> CreateMesh()
      -> BuildHeightMap()
      -> Build grid vertices, UVs, triangles, normals, tangents, vertex colors
      -> Create ProceduralMesh section with collision
      -> BuildOverhangMesh()
  -> EnsureTerrainMaterialInstance()
```

`BuildHeightMap()` owns the main terrain look. It produces normalized `0..1` heights, then `CreateMesh()` multiplies those values by `HeightMultiplier` when creating vertices.

The current heightmap stages run in this order:

1. World-aligned fBm and ridged noise create the base island and mountain shapes.
2. Coast shaping keeps the map edges lower and reduces hard cut-off mountains at landmass boundaries.
3. Sparse plateaus and access ramps add larger readable landforms.
4. Heightfield cliff shelves add stepped rocky areas without requiring extra geometry.
5. Beach flattening and beach erosion create flatter shoreline bands near sea level.
6. Mountain wear softens and breaks up high areas.
7. Thermal erosion post-processes steep high slopes so mountain faces feel less raw.

This order matters. For example, beaches are flattened after the larger terrain forms are created, and thermal erosion runs late so it can smooth the combined result.

## Coordinate And Height Model

The terrain is a regular grid:

- `MapWidth` and `MapHeight` control how many vertices exist in X/Y.
- `GridSize` controls world-space spacing between vertices.
- `TileOffset` allows procedural variation without changing actor placement.
- Internal heights are normalized `0..1`.
- `HeightMultiplier` turns normalized heights into Unreal world Z values.

`GetLandmassCenter()` returns the approximate center in local/world terms for systems that need to reason about the generated island.

## Water Integration

`WaterHeight01` is the procedural sea-level reference used by terrain generation and material masks. It does not spawn, move, or configure Unreal Water plugin actors.

The current project uses Unreal Water actors separately in the level, such as `WaterBodyLake`, `WaterBodyOcean`, and `WaterZone`. To make the visuals line up, tune `WaterHeight01` and the Water actor placement together in the editor.

## Materials And Vertex Colors

`BaseTerrainMaterial` is wrapped in a dynamic material instance so code can keep scalar parameters, especially `WaterHeight`, in sync with generator settings.

The terrain material depends on this vertex color contract:

```text
R = beach mask
G = slope mask
B = normalized height
A = dry-land mask
```

The overhang mesh can use `OverhangMaterial`. If no overhang material is assigned, it falls back to the terrain material instance. This is useful for quick visual consistency, but it also means material graph changes can strongly affect overhang appearance.

## Editor Categories

The exposed settings are grouped to make the generation stack easier to reason about.

- `Terrain|Dimensions`: grid resolution and spacing.
- `Terrain|Heights`: height multiplier and sea-level reference.
- `Terrain|Noise`: base fBm noise inputs.
- `Terrain|Coast & Beaches`: coastline falloff and beach flattening controls.
- `Terrain|Erosion`: beach erosion, thermal erosion, and mountain wear controls.
- `Terrain|Mountains`: ridged mountain shaping.
- `Terrain|Detail`: small high-frequency detail.
- `Terrain|Landforms`: plateaus and plateau access ramps.
- `Terrain|Cliffs`: heightfield-safe cliff shelf controls.
- `Terrain|Overhangs`: separate overhang mesh generation controls.
- `Terrain|Material`: base and overhang material assignments.

The `Cliffs` section is still used. It affects cliff-like shapes inside the main heightfield mesh. This is separate from `Overhangs`, which builds additional mesh geometry.

## Overhang System

Overhangs are intentionally isolated from the heightmap because they are experimental geometry and have different constraints.

`BuildOverhangMesh()` scans the terrain heightfield for steep cliff-like runs. When a valid run is found, it builds a closed ledge-like mesh from generated point strips. The mesh is closed so collision and rendering work from normal viewing angles.

Current overhang goals:

- Avoid overlapping nearby overhangs.
- Keep top surfaces flatter and more walkable.
- Use terrain height samples so placement fits the mountain face better than a simple floating prism.
- Keep the system separate enough that it can be replaced or heavily revised later without destabilizing the main terrain.

Current known limitation: overhangs are still prototype-quality. They are not boolean-merged into the terrain mesh, so visual blending at the attachment seam is approximate rather than physically continuous.

## Important Extension Rules

When adding new generator settings:

- Add the `UPROPERTY` to the most relevant editor category in `ProceduralLandmass.h`.
- If the property changes geometry, masks, collision, or generated heights, add it to `IsGenerationProperty()`.
- If the property only changes material assignment or material sync, add it to `IsMaterialProperty()`.
- Keep new generation logic in the stage where it belongs rather than scattering it throughout `CreateMesh()`.
- Prefer deriving mesh masks from the same height data used for geometry so visuals and collision stay consistent.

When adding new material behavior:

- Preserve the existing vertex color contract unless the material and code are updated together.
- Update this document if the vertex color channels change.
- Keep `WaterHeight` material sync in mind when changing water or shoreline visuals.

## Build And Verification

On this project setup, the editor build command is:

```zsh
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" PCG_Exploration_UEEditor Mac Development -Project="$HOME/Documents/PCG_Exploration/PCG_Exploration_UE 5.7/PCG_Exploration_UE.uproject" -architecture=arm64
```

If Unreal reports a `ConflictingInstance` or UnrealBuildTool mutex error, another compile is already running. Wait for it to finish, then build again.

Recommended manual checks after terrain changes:

- Compile the project.
- Open `Procedural_World`.
- Select the `ProceduralLandmass` actor.
- Change a visible generation value, such as `Seed`, and confirm the mesh rebuilds.
- Confirm collision by walking the player on terrain and overhangs.
- Check beaches with Water actors visible and hidden so shoreline height is understandable.

## Source Control Notes

This project contains a current Unreal project folder named `PCG_Exploration_UE 5.7`. There is also historical/deleted content under `PCG_Exploration_UE` without `5.7` visible in Git status. Be careful not to accidentally stage those legacy deletions unless the team intentionally decides to remove that old folder from the repository.

For C++ changes, stage the source files directly. For map/material changes, stage the intended files under `PCG_Exploration_UE 5.7/Content`.

Avoid `git add .` for now. The Unreal asset and World Partition external actor files are numerous, and broad staging can easily include unrelated map or legacy-folder changes.

## Current Mental Model

Think of `ProceduralLandmass` as a staged terrain factory:

1. Build a normalized terrain heightfield.
2. Convert the heightfield into a renderable and collidable procedural mesh.
3. Generate material masks from the same terrain data.
4. Add separate overhang geometry where the heightfield is not expressive enough.
5. Sync the assigned materials so Unreal Editor tuning remains immediate.

That separation is the safest way to keep the system understandable as more programmers begin contributing.
