//-----------------------------------------------------------------------------
// ProceduralWaterPlane.h
//
// Simple flat water surface generated via ProceduralMeshComponent.
// Can auto-sync size & height from a linked ProceduralLandmass.
//-----------------------------------------------------------------------------

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralWaterPlane.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class AProceduralLandmass;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralWaterPlane : public AActor
{
    GENERATED_BODY()

public:
    AProceduralWaterPlane();

protected:
    virtual void BeginPlay() override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // The procedural mesh used to render the water surface
    UPROPERTY(VisibleAnywhere, Category = "Water")
    UProceduralMeshComponent* WaterMesh;

public:
    // =========================
    //   Shape / size settings
    // =========================

    // When not auto-syncing, these control the water grid size directly.
    // When auto-syncing, they will be overwritten from the linked landmass.
    UPROPERTY(EditAnywhere, Category = "Water|Shape", meta = (ClampMin = "2"))
    int32 MapWidth = 100;

    UPROPERTY(EditAnywhere, Category = "Water|Shape", meta = (ClampMin = "2"))
    int32 MapHeight = 100;

    UPROPERTY(EditAnywhere, Category = "Water|Shape", meta = (ClampMin = "1.0"))
    float GridSize = 100.0f;

    // World-space surface height (Z in cm).
    // When auto-syncing, this is computed from landmass HeightMultiplier and water biome height.
    UPROPERTY(EditAnywhere, Category = "Water|Shape")
    float WaterHeightWorld = 60.0f;

    // Whether to generate the water mesh at all
    UPROPERTY(EditAnywhere, Category = "Water|Shape")
    bool bGenerateWaterMesh = true;

    // =========================
    //   Material
    // =========================

    UPROPERTY(EditAnywhere, Category = "Water|Material")
    UMaterialInterface* WaterMaterial = nullptr;

    // =========================
    //   Landmass link (pro mode)
    // =========================

    // If true and LinkedLandmass is set, this water plane will auto-sync
    // its MapWidth/MapHeight/GridSize and WaterHeightWorld from the landmass.
    UPROPERTY(EditAnywhere, Category = "Water|Link")
    bool bAutoSyncWithLandmass = true;

    // Reference to the procedural landmass this water plane should follow.
    UPROPERTY(EditAnywhere, Category = "Water|Link")
    AProceduralLandmass* LinkedLandmass = nullptr;

    // Editor button to force regeneration
    UFUNCTION(CallInEditor, Category = "Water")
    void GenerateWater();

private:
    void CreateWaterMesh();

    // Pulls parameters from LinkedLandmass if auto-sync is enabled.
    void SyncFromLinkedLandmass();
};
