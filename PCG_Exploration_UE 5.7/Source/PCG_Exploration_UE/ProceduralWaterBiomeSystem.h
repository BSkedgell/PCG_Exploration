#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralWaterBiomeSystem.generated.h"

class AProceduralLandmass;
class UMaterialInstanceDynamic;
class USplineComponent;
class UTexture2D;
class UWaterBodyOceanComponent;
class UWaterWavesBase;

UENUM(BlueprintType)
enum class EProceduralWaterBiome : uint8
{
    DryLand UMETA(DisplayName = "Dry Land"),
    WetShore UMETA(DisplayName = "Wet Shore"),
    ShallowWater UMETA(DisplayName = "Shallow Water"),
    LittoralShelf UMETA(DisplayName = "Littoral Shelf"),
    DeepWater UMETA(DisplayName = "Deep Water")
};

UENUM(BlueprintType)
enum class EWaterSplineFitShape : uint8
{
    Ellipse UMETA(DisplayName = "Ellipse"),
    Rectangle UMETA(DisplayName = "Rectangle")
};

UENUM(BlueprintType)
enum class EProceduralWaterDebugView : uint8
{
    Biome UMETA(DisplayName = "Biome"),
    WaveEnergy UMETA(DisplayName = "Wave Energy")
};

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralWaterBiomeSystem : public AActor
{
    GENERATED_BODY()

public:
    AProceduralWaterBiomeSystem();

    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

    // Terrain source used for height sampling. Keep this explicit so water,
    // biome, fauna, and PCG systems can evolve independently of terrain gen.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AProceduralLandmass* LandmassActor = nullptr;

    // Assign the placed WaterBodyLake actor here to use its Z as the live water
    // surface. This intentionally avoids a hard dependency on the Water module.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AActor* WaterBodyLakeActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AActor* WaterBodyOceanActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|References")
    AActor* WaterZoneActor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", AdvancedDisplay)
    EWaterSplineFitShape WaterSplineFitShape = EWaterSplineFitShape::Ellipse;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", AdvancedDisplay)
    EWaterSplineFitShape LakeSplineFitShape = EWaterSplineFitShape::Ellipse;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", AdvancedDisplay)
    EWaterSplineFitShape OceanSplineFitShape = EWaterSplineFitShape::Rectangle;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "4", ClampMax = "64"), AdvancedDisplay)
    int32 FitSplinePointCount = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "4", ClampMax = "128"), AdvancedDisplay)
    int32 LakeFitSplinePointCount = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "4", ClampMax = "128"), AdvancedDisplay)
    int32 OceanFitSplinePointCount = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"), AdvancedDisplay)
    float LakeBoundsPadding = 1500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"))
    float OceanBoundsPadding = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"))
    float LakeShoreOverlapDistance = 120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"))
    float OceanShoreOverlapDistance = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", AdvancedDisplay)
    bool bFitWaterActorsToLandmassWaterHeight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting")
    bool bFitWaterZoneToLandmass = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"))
    float WaterZoneBoundsPadding = 15000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", AdvancedDisplay)
    bool bMatchOceanSplineToLakeSpline = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Spline Fitting", meta = (ClampMin = "0.0"), AdvancedDisplay)
    float OceanLakeBlendOverlapDistance = 600.0f;

    // Internal helper; the main editor workflow should use FitAllWaterToLandmass.
    void FitLakeSplineToLandmass();

    // Internal helper; the main editor workflow should use FitAllWaterToLandmass.
    void FitOceanSplineToLandmass();

    // Internal helper; the main editor workflow should use FitAllWaterToLandmass.
    void SyncWaterActorsToLandmass();

    // Internal helper; the main editor workflow should use FitAllWaterToLandmass.
    void FitWaterZoneToLandmass();

    UFUNCTION(CallInEditor, Category = "Water Biome|Spline Fitting")
    void FitAllWaterToLandmass();

    UFUNCTION(CallInEditor, Category = "Water Biome|Diagnostics")
    void DiagnoseWaterBodies() const;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface", AdvancedDisplay)
    bool bUseWaterBodyLakeActorHeight = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface")
    float WaterSurfaceZOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface", AdvancedDisplay)
    float LakeSurfaceZOffset = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface", AdvancedDisplay)
    float OceanSurfaceZOffset = -120.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Surface", AdvancedDisplay)
    bool bAutoSyncWaterHeightOnOffsetChange = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Rendering", meta = (ClampMin = "-8192", ClampMax = "8191"), AdvancedDisplay)
    int32 LakeOverlapMaterialPriority = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Rendering", meta = (ClampMin = "-8192", ClampMax = "8191"), AdvancedDisplay)
    int32 OceanOverlapMaterialPriority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Waves")
    bool bOverrideOceanWaves = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Waves", meta = (EditCondition = "bOverrideOceanWaves", EditConditionHides))
    bool bDisableOceanWaves = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Waves", meta = (EditCondition = "bOverrideOceanWaves && !bDisableOceanWaves", EditConditionHides), AdvancedDisplay)
    UWaterWavesBase* OceanWavesAssetOverride = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideOceanWaves", EditConditionHides), AdvancedDisplay)
    float OceanWaveAttenuationWaterDepth = 256.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Waves", meta = (ClampMin = "-4096.0", ClampMax = "4096.0", EditCondition = "bOverrideOceanWaves", EditConditionHides), AdvancedDisplay)
    float OceanMaxWaveHeightOffset = -48.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", AdvancedDisplay)
    bool bOverrideWaterMaterialWaveVisuals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float OceanShoreWaveVisualIntensity = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float OceanOpenWaveVisualIntensity = 1.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float OceanFarNormalVisualIntensity = 1.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float LakeWaveVisualIntensity = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float LakeEdgeVisualIntensity = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float LakeRoughVisualIntensity = 0.04f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "32", ClampMax = "1024", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    int32 SeaStateTextureResolution = 256;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Visual Waves", meta = (ClampMin = "0.0", EditCondition = "bOverrideWaterMaterialWaveVisuals", EditConditionHides), AdvancedDisplay)
    float SeaStateTexturePadding = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float LakeWaveEnergy = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float OceanBoundaryWaveEnergy = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float OpenOceanWaveEnergy = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "100.0"), AdvancedDisplay)
    float OpenOceanWaveRampDistance = 12000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float WaveDepthInfluence = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float WaveExposureInfluence = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "500.0"), AdvancedDisplay)
    float WaveExposureProbeDistance = 14000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "4", ClampMax = "32"), AdvancedDisplay)
    int32 WaveExposureProbeCount = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "2", ClampMax = "16"), AdvancedDisplay)
    int32 WaveExposureProbeSteps = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "250.0"), AdvancedDisplay)
    float WaveShoreInfluenceDistance = 4500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "360.0"), AdvancedDisplay)
    float PrimaryWaveDirectionDegrees = 315.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "180.0"), AdvancedDisplay)
    float PrimaryWaveDirectionSpreadDegrees = 35.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "0.0", ClampMax = "1.0"), AdvancedDisplay)
    float PrimaryWaveDirectionInfluence = 0.75f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Sea State", meta = (ClampMin = "1", ClampMax = "9"), AdvancedDisplay)
    int32 PrimaryWaveDirectionProbeCount = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"), AdvancedDisplay)
    float ShorelineBandHeight = 140.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"), AdvancedDisplay)
    float ShallowWaterDepth = 180.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Thresholds", meta = (ClampMin = "1.0"), AdvancedDisplay)
    float LittoralShelfDepth = 650.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    bool bDrawDebugBiomeGrid = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Biome|Debug")
    EProceduralWaterDebugView DebugViewMode = EProceduralWaterDebugView::Biome;

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

    UFUNCTION(BlueprintPure, Category = "Water Biome|Queries")
    float GetWaveEnergyAtWorldLocation(const FVector& WorldLocation) const;

private:
    bool FitLakeSplineToInlandBasin();
    bool FitOceanSplineToOuterCoastline(float OceanExtentPadding);
    bool FitWaterActorSplineToLandmass(AActor* WaterActor, float Padding);
    bool FitOceanToLakeSpline(float OceanExtentPadding);
    void SyncWaterActorHeight(AActor* WaterActor) const;
    void SetWaterActorSplineHeight(AActor* WaterActor, float TargetZ) const;
    void RefreshWaterRendering(AActor* WaterActor) const;
    void RefreshWaterZoneRendering() const;
    void EnsureWaterBodyRenderable(AActor* WaterActor) const;
    void ApplyWaterBodyOverlapPriority(AActor* WaterActor) const;
    void ApplyOceanWaveSettings(AActor* WaterActor) const;
    void ApplyWaterMaterialVisualSettings(AActor* WaterActor) const;
    void UpdateSeaStateTexture() const;
    void ApplySeaStateTextureParameters(UMaterialInstanceDynamic* MaterialInstance) const;
    UMaterialInstanceDynamic* GetOrCreateWaterMaterialInstance(AActor* WaterActor) const;
    UMaterialInterface* GetFallbackWaterMaterial() const;
    void LogWaterBodyDiagnostics(const TCHAR* Label, AActor* WaterActor) const;
    bool FitActorBoundsToLandmass(AActor* Actor, float Padding, bool bSyncHeight) const;
    float GetWaterActorSurfaceZ(AActor* WaterActor) const;
    int32 GetWaterActorFitSplinePointCount(AActor* WaterActor) const;
    EWaterSplineFitShape GetWaterActorFitShape(AActor* WaterActor) const;
    USplineComponent* FindEditableWaterSpline(AActor* WaterActor) const;
    void DrawDebugBiomeGrid() const;
    bool IsInsideDebugWaterBounds(const FVector& WorldLocation) const;
    bool IsInsideAnyWaterSpline(const FVector& WorldLocation, bool& bFoundSpline) const;
    float GetDistanceToNearestLakeSplineEdge(const FVector& WorldLocation, bool& bFoundSpline, bool& bInsideLake) const;
    float GetDistanceToNearestShoreline(const FVector& WorldLocation) const;
    float GetWaveExposureAtWorldLocation(const FVector& WorldLocation) const;
    float GetDirectionalWaveFetchAtWorldLocation(const FVector& WorldLocation) const;
    FColor GetDebugColorForBiome(EProceduralWaterBiome Biome) const;
    FColor GetDebugColorForWaveEnergy(float WaveEnergy) const;

    UPROPERTY(Transient)
    mutable TObjectPtr<UMaterialInstanceDynamic> LakeWaterMaterialInstance = nullptr;

    UPROPERTY(Transient)
    mutable TObjectPtr<UMaterialInstanceDynamic> OceanWaterMaterialInstance = nullptr;

    UPROPERTY(Transient)
    mutable TObjectPtr<UTexture2D> SeaStateTexture = nullptr;
};
