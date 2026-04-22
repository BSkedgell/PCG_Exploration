#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralWaterBiomeSystem.generated.h"

class AProceduralLandmass;

UENUM(BlueprintType)
enum class EProceduralWaterBiome : uint8
{
    DryLand UMETA(DisplayName = "Dry Land"),
    WetShore UMETA(DisplayName = "Wet Shore"),
    ShallowWater UMETA(DisplayName = "Shallow Water"),
    LittoralShelf UMETA(DisplayName = "Littoral Shelf"),
    DeepWater UMETA(DisplayName = "Deep Water")
};

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralWaterBiomeSystem : public AActor
{
    GENERATED_BODY()

public:
    AProceduralWaterBiomeSystem();

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;

    // Terrain source used for height sampling. Keep this explicit so water,
    // biome, fauna, and PCG systems can evolve independently of terrain gen.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AProceduralLandmass* LandmassActor = nullptr;

    // Assign the placed WaterBodyLake actor here to use its Z as the live water
    // surface. This intentionally avoids a hard dependency on the Water module.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AActor* WaterBodyLakeActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface")
    bool bUseWaterBodyLakeActorHeight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface")
    float WaterSurfaceZOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"))
    float ShorelineBandHeight = 140.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"))
    float ShallowWaterDepth = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"))
    float LittoralShelfDepth = 650.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    bool bDrawDebugBiomeGrid = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    bool bLimitDebugToWaterBodyBounds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    bool bPreferWaterBodySplineBounds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug", meta = (ClampMin = "0.0"))
    float DebugWaterBodyBoundsPadding = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug", meta = (ClampMin = "100.0"))
    float DebugGridSpacing = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    bool bAutoSizeDebugGridToLandmass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug", meta = (ClampMin = "100.0"))
    float DebugGridExtent = 8000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug", meta = (ClampMin = "1.0"))
    float DebugPointSize = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    float DebugDrawZOffset = 35.0f;

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    float GetWaterSurfaceZ() const;

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    float GetTerrainHeightAtWorldLocation(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    float GetWaterDepthAtWorldLocation(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    float GetShorelineProximityAtWorldLocation(const FVector& WorldLocation) const;

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    EProceduralWaterBiome GetWaterBiomeAtWorldLocation(const FVector& WorldLocation) const;

private:
    void DrawDebugBiomeGrid() const;
    bool IsInsideDebugWaterBounds(const FVector& WorldLocation) const;
    bool IsInsideAnyWaterSpline(const FVector& WorldLocation, bool& bFoundSpline) const;
    FColor GetDebugColorForBiome(EProceduralWaterBiome Biome) const;
};
