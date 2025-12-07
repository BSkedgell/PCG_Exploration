//-----------------------------------------------------------------------------
// ProceduralLandmass.h
//
// A simple runtime-generated terrain actor.
//
// High-level:
// - Uses multi-octave Perlin noise to generate a 2D height map.
// - Converts that height map into a grid mesh using UProceduralMeshComponent.
// - Can be regenerated in-editor (OnConstruction + CallInEditor button)
//   and at runtime (BeginPlay).
//-----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralLandmass.generated.h"

// Forward declaration to avoid extra includes in the header
class UProceduralMeshComponent;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralLandmass : public AActor
{
    GENERATED_BODY()

public:
    // Constructor: sets default values and creates the procedural mesh component
    AProceduralLandmass();

protected:
    // Called when the game starts (Play in editor / standalone)
    virtual void BeginPlay() override;

#if WITH_EDITOR
    // Called when the actor is constructed or its properties change in the editor
    virtual void OnConstruction(const FTransform& Transform) override;
#endif

    // The mesh that will hold our generated terrain geometry
    UPROPERTY(VisibleAnywhere, Category = "Terrain")
    UProceduralMeshComponent* ProceduralMesh;

public:
    // =========================
    //   Exposed noise settings
    // =========================

    // Number of vertices in X (width) direction of the height map / mesh
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2"))
    int32 MapWidth = 100;

    // Number of vertices in Y (height) direction of the height map / mesh
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "2"))
    int32 MapHeight = 100;

    // World-space spacing between vertices (controls overall size of the mesh)
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1.0"))
    float GridSize = 100.0f;

    // How zoomed in the noise is (smaller value = more stretched hills, larger = more detail)
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.001"))
    float NoiseScale = 25.0f;

    // Seed for the random stream so we can get reproducible noise
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Seed = 1337;

    // Number of noise octaves for fractal noise (higher = more detail, more cost)
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1"))
    int32 Octaves = 4;

    // How quickly amplitude decreases each octave (0–1). Seb’s “persistance”.
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Persistance = 0.5f;

    // How quickly frequency increases each octave (>1). Seb’s “lacunarity”.
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise", meta = (ClampMin = "1.0"))
    float Lacunarity = 2.0f;

    // Global offset applied to all octave samples (lets you pan around the noise field)
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    FVector2D NoiseOffset = FVector2D::ZeroVector;

    // Vertical scale applied to the normalized noise values
    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float HeightMultiplier = 200.0f;

    // =========================
    //   Editor utility
    // =========================

    // Button exposed in the Details panel to regenerate terrain on demand
    UFUNCTION(CallInEditor, Category = "Terrain")
    void GenerateTerrain();

private:
    // Fills OutHeightMap with normalized [0,1] noise values for the entire grid
    void GenerateNoiseMap(TArray<float>& OutHeightMap);

    // Converts the given height map into a procedural mesh section
    void CreateMeshFromHeightMap(const TArray<float>& HeightMap);
};
