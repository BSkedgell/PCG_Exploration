#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralLandmass.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralLandmass : public AActor
{
    GENERATED_BODY()

public:
    AProceduralLandmass();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // ------------ Runtime helpers ------------
    float   GetDefaultWaterHeight01() const;
    FVector GetLandmassCenter() const;

    // ------------ Components ------------
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terrain")
    UProceduralMeshComponent* ProceduralMesh = nullptr;

    // ------------ Terrain settings ------------
    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    int32 MapWidth = 128;

    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    int32 MapHeight = 128;

    UPROPERTY(EditAnywhere, Category = "Terrain|Dimensions")
    float GridSize = 100.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Heights")
    float HeightMultiplier = 2000.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float NoiseScale = 80.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Seed = 1337;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    int32 Octaves = 4;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float Persistence = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Noise")
    float Lacunarity = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Terrain|Heights", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WaterHeight01 = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain")
    FVector2D TileOffset = FVector2D(0, 0);

    // ------------ Material ------------
    // Base material asset you assign in the editor (e.g. M_ProceduralTerrain)
    UPROPERTY(EditAnywhere, Category = "Terrain|Material")
    UMaterialInterface* BaseTerrainMaterial = nullptr;

private:
    // ------------ Internal helpers ------------
    void GenerateTerrain();
    void CreateMesh();
    void BuildHeightMap(TArray<float>& OutHeights) const;
    void EnsureTerrainMaterialInstance();

    // Our dynamic material instance (never exposed to BP)
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* TerrainMID = nullptr;
};
