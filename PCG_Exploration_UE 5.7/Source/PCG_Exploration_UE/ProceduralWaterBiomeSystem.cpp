#include "ProceduralWaterBiomeSystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralLandmass.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyOceanComponent.h"
#include "WaterBodyTypes.h"
#include "WaterWaves.h"
#include "WaterSplineComponent.h"
#include "WaterZoneActor.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProceduralWaterBiome, Log, All);

namespace
{
struct FInlandLakeRegion
{
    bool bFound = false;
    int32 CellCount = 0;
    FVector2D CenterLocal = FVector2D::ZeroVector;
    FVector2D ExtentLocal = FVector2D::ZeroVector;
    TArray<int32> Cells;
};

int32 GetEffectiveWaveProbeSteps(
    const AProceduralLandmass* LandmassActor,
    float ProbeDistance,
    int32 RequestedSteps,
    int32 MinRequestedSteps,
    int32 MaxRequestedSteps,
    int32 MaxEffectiveSteps)
{
    const int32 ClampedRequestedSteps = FMath::Clamp(RequestedSteps, MinRequestedSteps, MaxRequestedSteps);
    if (!LandmassActor)
    {
        return ClampedRequestedSteps;
    }

    // Sample more densely than the user-facing setting when terrain cells are
    // small, so beaches, coves, and small sheltering islands actually affect
    // the sea-state field instead of being skipped over.
    const float TargetStepLength = FMath::Clamp(LandmassActor->GridSize * 0.75f, 150.0f, 1000.0f);
    const int32 TerrainDrivenSteps = FMath::CeilToInt(ProbeDistance / FMath::Max(TargetStepLength, 1.0f));
    return FMath::Clamp(FMath::Max(ClampedRequestedSteps, TerrainDrivenSteps), ClampedRequestedSteps, MaxEffectiveSteps);
}

struct FSeaStateTextureWorldBounds
{
    float MinX = 0.0f;
    float MinY = 0.0f;
    float SizeX = 1.0f;
    float SizeY = 1.0f;
};

const FName ProceduralGeneratedLakeTag(TEXT("ProceduralGeneratedLake"));

FSeaStateTextureWorldBounds GetSeaStateTextureWorldBounds(const AProceduralWaterBiomeSystem* System)
{
    FSeaStateTextureWorldBounds Result;
    if (!System || !System->LandmassActor)
    {
        return Result;
    }

    if (const AActor* ZoneActor = System->WaterZoneActor)
    {
        const FBox ZoneBounds = ZoneActor->GetComponentsBoundingBox(true);
        if (ZoneBounds.IsValid)
        {
            Result.MinX = ZoneBounds.Min.X;
            Result.MinY = ZoneBounds.Min.Y;
            Result.SizeX = FMath::Max(ZoneBounds.Max.X - ZoneBounds.Min.X, 1.0f);
            Result.SizeY = FMath::Max(ZoneBounds.Max.Y - ZoneBounds.Min.Y, 1.0f);
            return Result;
        }
    }

    const FVector Center = System->LandmassActor->GetLandmassCenter();
    const float LandmassWidth = FMath::Max(0, System->LandmassActor->MapWidth - 1) * System->LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, System->LandmassActor->MapHeight - 1) * System->LandmassActor->GridSize;
    const float HalfExtentX = LandmassWidth * 0.5f + System->SeaStateTexturePadding;
    const float HalfExtentY = LandmassHeight * 0.5f + System->SeaStateTexturePadding;

    Result.MinX = Center.X - HalfExtentX;
    Result.MinY = Center.Y - HalfExtentY;
    Result.SizeX = FMath::Max(HalfExtentX * 2.0f, 1.0f);
    Result.SizeY = FMath::Max(HalfExtentY * 2.0f, 1.0f);
    return Result;
}
}

AProceduralWaterBiomeSystem::AProceduralWaterBiomeSystem()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AProceduralWaterBiomeSystem::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    DrawDebugBiomeGrid();
}

bool AProceduralWaterBiomeSystem::ShouldTickIfViewportsOnly() const
{
    return true;
}

#if WITH_EDITOR
void AProceduralWaterBiomeSystem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.Property
        ? PropertyChangedEvent.Property->GetFName()
        : NAME_None;

    if (bAutoSyncWaterHeightOnOffsetChange &&
        (PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaterSurfaceZOffset) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, LakeSurfaceZOffset) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanSurfaceZOffset) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, bFitWaterActorsToLandmassWaterHeight)))
    {
        SyncWaterActorsToLandmass();
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, bOverrideOceanWaves) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, bDisableOceanWaves) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanWavesAssetOverride) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanWaveAttenuationWaterDepth) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanMaxWaveHeightOffset))
    {
        RefreshWaterRendering(WaterBodyOceanActor);
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, bOverrideWaterMaterialWaveVisuals) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanShoreWaveVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanOpenWaveVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanFarNormalVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, LakeWaveVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, LakeEdgeVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, LakeRoughVisualIntensity) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, SeaStateTextureResolution) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, SeaStateTexturePadding) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, LakeWaveEnergy) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OceanBoundaryWaveEnergy) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OpenOceanWaveEnergy) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, OpenOceanWaveRampDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveDepthInfluence) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveExposureInfluence) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveExposureProbeDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveExposureProbeCount) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveExposureProbeSteps) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, WaveShoreInfluenceDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, PrimaryWaveDirectionDegrees) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, PrimaryWaveDirectionSpreadDegrees) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, PrimaryWaveDirectionInfluence) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(AProceduralWaterBiomeSystem, PrimaryWaveDirectionProbeCount))
    {
        SeaStateTexture = nullptr;
        bSeaStateTextureDirty = true;
        RefreshWaterRendering(WaterBodyLakeActor);
        for (AActor* AdditionalLakeActor : AdditionalWaterBodyLakeActors)
        {
            RefreshWaterRendering(AdditionalLakeActor);
        }
        RefreshWaterRendering(WaterBodyOceanActor);
    }
}
#endif

void AProceduralWaterBiomeSystem::FitLakeSplineToLandmass()
{
    InvalidateWaterQueryCache();
    FitLakeSplineToInlandBasin();
    if (bFitWaterZoneToLandmass)
    {
        FitWaterZoneToLandmass();
    }
}

void AProceduralWaterBiomeSystem::FitOceanSplineToLandmass()
{
    InvalidateWaterQueryCache();
    if (!FitOceanSplineToOuterCoastline(OceanBoundsPadding))
    {
        FitWaterActorSplineToLandmass(WaterBodyOceanActor, OceanBoundsPadding);
    }
    if (bFitWaterZoneToLandmass)
    {
        FitWaterZoneToLandmass();
    }
}

void AProceduralWaterBiomeSystem::SyncWaterActorsToLandmass()
{
    InvalidateWaterQueryCache();
    EnsureWaterBodyRenderable(WaterBodyLakeActor);
    for (AActor* AdditionalLakeActor : AdditionalWaterBodyLakeActors)
    {
        EnsureWaterBodyRenderable(AdditionalLakeActor);
    }
    EnsureWaterBodyRenderable(WaterBodyOceanActor);
    SyncWaterActorHeight(WaterBodyLakeActor);
    for (AActor* AdditionalLakeActor : AdditionalWaterBodyLakeActors)
    {
        SyncWaterActorHeight(AdditionalLakeActor);
    }
    SyncWaterActorHeight(WaterBodyOceanActor);
}

void AProceduralWaterBiomeSystem::FitWaterZoneToLandmass()
{
    if (!FitActorBoundsToLandmass(WaterZoneActor, WaterZoneBoundsPadding, false))
    {
        return;
    }

    AWaterBody* OceanBody = Cast<AWaterBody>(WaterBodyOceanActor);
    UWaterBodyOceanComponent* OceanComponent = OceanBody
        ? Cast<UWaterBodyOceanComponent>(OceanBody->GetWaterBodyComponent())
        : nullptr;

    if (OceanComponent)
    {
#if WITH_EDITOR
        OceanComponent->Modify();
#endif
        OceanComponent->FillWaterZoneWithOcean();
        RefreshWaterRendering(WaterBodyOceanActor);
    }
}

void AProceduralWaterBiomeSystem::FitAllWaterToLandmass()
{
    InvalidateWaterQueryCache();
    SyncWaterActorsToLandmass();
    FitLakeSplineToInlandBasin();
    if (!FitOceanSplineToOuterCoastline(OceanBoundsPadding))
    {
        FitWaterActorSplineToLandmass(WaterBodyOceanActor, OceanBoundsPadding);
    }

    if (bFitWaterZoneToLandmass)
    {
        FitWaterZoneToLandmass();
    }
}

void AProceduralWaterBiomeSystem::DiagnoseWaterBodies() const
{
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("========== Procedural Water Diagnostics =========="));
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("ControlActor=%s Landmass=%s WaterZoneRef=%s SurfaceZ=%.2f Offset=%.2f"),
        *GetNameSafe(this),
        *GetNameSafe(LandmassActor),
        *GetNameSafe(WaterZoneActor),
        GetWaterSurfaceZ(),
        WaterSurfaceZOffset);
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("TargetZ: Lake=%.2f Ocean=%.2f LakeSplinePoints=%d OceanSplinePoints=%d"),
        GetWaterActorSurfaceZ(WaterBodyLakeActor),
        GetWaterActorSurfaceZ(WaterBodyOceanActor),
        GetWaterActorFitSplinePointCount(WaterBodyLakeActor),
        GetWaterActorFitSplinePointCount(WaterBodyOceanActor));
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("OverlapPriority: Lake=%d Ocean=%d MatchOceanToLake=%s"),
        LakeOverlapMaterialPriority,
        OceanOverlapMaterialPriority,
        bMatchOceanSplineToLakeSpline ? TEXT("true") : TEXT("false"));
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("FitShape: Lake=%s Ocean=%s"),
        *StaticEnum<EWaterSplineFitShape>()->GetNameStringByValue(static_cast<int64>(GetWaterActorFitShape(WaterBodyLakeActor))),
        *StaticEnum<EWaterSplineFitShape>()->GetNameStringByValue(static_cast<int64>(GetWaterActorFitShape(WaterBodyOceanActor))));

    LogWaterBodyDiagnostics(TEXT("Lake"), WaterBodyLakeActor);
    for (int32 AdditionalLakeIndex = 0; AdditionalLakeIndex < AdditionalWaterBodyLakeActors.Num(); ++AdditionalLakeIndex)
    {
        LogWaterBodyDiagnostics(*FString::Printf(TEXT("AdditionalLake%d"), AdditionalLakeIndex + 2), AdditionalWaterBodyLakeActors[AdditionalLakeIndex]);
    }
    LogWaterBodyDiagnostics(TEXT("Ocean"), WaterBodyOceanActor);

    if (const AWaterZone* WaterZone = Cast<AWaterZone>(WaterZoneActor))
    {
        UE_LOG(LogProceduralWaterBiome, Display, TEXT("WaterZone: Name=%s Location=%s Extent=%s Bounds=%s"),
            *GetNameSafe(WaterZone),
            *WaterZone->GetActorLocation().ToCompactString(),
            *WaterZone->GetZoneExtent().ToString(),
            *WaterZone->GetZoneBounds().ToString());
    }
    else
    {
        UE_LOG(LogProceduralWaterBiome, Warning, TEXT("WaterZone: reference is not an AWaterZone."));
    }

    UE_LOG(LogProceduralWaterBiome, Display, TEXT("=================================================="));
}

bool AProceduralWaterBiomeSystem::FitWaterActorSplineToLandmass(AActor* WaterActor, float Padding)
{
    if (!LandmassActor || !WaterActor)
    {
        return false;
    }

    EnsureWaterBodyRenderable(WaterActor);

    USplineComponent* SplineComponent = FindEditableWaterSpline(WaterActor);
    if (!SplineComponent)
    {
        return false;
    }

    const FVector Center = LandmassActor->GetLandmassCenter();
    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;
    const float RadiusX = LandmassWidth * 0.5f + Padding;
    const float RadiusY = LandmassHeight * 0.5f + Padding;
    const EWaterSplineFitShape FitShape = GetWaterActorFitShape(WaterActor);
    const float WaterZ = bFitWaterActorsToLandmassWaterHeight
        ? GetWaterActorSurfaceZ(WaterActor)
        : WaterActor->GetActorLocation().Z;
    const int32 NumPoints = (FitShape == EWaterSplineFitShape::Rectangle)
        ? FMath::Max(GetWaterActorFitSplinePointCount(WaterActor), 8)
        : GetWaterActorFitSplinePointCount(WaterActor);
    TArray<FVector> SplinePoints;
    SplinePoints.Reserve(NumPoints);

#if WITH_EDITOR
    WaterActor->Modify();
    SplineComponent->Modify();
#endif

    if (bFitWaterActorsToLandmassWaterHeight)
    {
        FVector ActorLocation = WaterActor->GetActorLocation();
        ActorLocation.Z = WaterZ;
        WaterActor->SetActorLocation(ActorLocation);
    }

    for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
    {
        FVector Point = Center;

        if (FitShape == EWaterSplineFitShape::Rectangle)
        {
            const float Perimeter = 4.0f * (RadiusX + RadiusY);
            const float DistanceAlongPerimeter = (static_cast<float>(PointIndex) / static_cast<float>(NumPoints)) * Perimeter;
            const float TopEdgeLength = RadiusX * 2.0f;
            const float RightEdgeLength = RadiusY * 2.0f;
            const float BottomEdgeLength = RadiusX * 2.0f;

            if (DistanceAlongPerimeter < TopEdgeLength)
            {
                Point.X += -RadiusX + DistanceAlongPerimeter;
                Point.Y += -RadiusY;
            }
            else if (DistanceAlongPerimeter < TopEdgeLength + RightEdgeLength)
            {
                const float EdgeDistance = DistanceAlongPerimeter - TopEdgeLength;
                Point.X += RadiusX;
                Point.Y += -RadiusY + EdgeDistance;
            }
            else if (DistanceAlongPerimeter < TopEdgeLength + RightEdgeLength + BottomEdgeLength)
            {
                const float EdgeDistance = DistanceAlongPerimeter - TopEdgeLength - RightEdgeLength;
                Point.X += RadiusX - EdgeDistance;
                Point.Y += RadiusY;
            }
            else
            {
                const float EdgeDistance = DistanceAlongPerimeter - TopEdgeLength - RightEdgeLength - BottomEdgeLength;
                Point.X += -RadiusX;
                Point.Y += RadiusY - EdgeDistance;
            }
        }
        else
        {
            const float Angle = (static_cast<float>(PointIndex) / static_cast<float>(NumPoints)) * UE_TWO_PI;
            Point.X += FMath::Cos(Angle) * RadiusX;
            Point.Y += FMath::Sin(Angle) * RadiusY;
        }

        Point.Z = WaterZ;
        SplinePoints.Add(Point);
    }

#if WITH_EDITOR
    if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
    {
        TArray<FVector> LocalSplinePoints;
        LocalSplinePoints.Reserve(SplinePoints.Num());
        const FTransform SplineTransform = WaterSplineComponent->GetComponentTransform();
        for (const FVector& WorldPoint : SplinePoints)
        {
            LocalSplinePoints.Add(SplineTransform.InverseTransformPosition(WorldPoint));
        }

        WaterSplineComponent->ResetSpline(LocalSplinePoints);
    }
    else
#endif
    {
        SplineComponent->ClearSplinePoints(false);
        for (const FVector& Point : SplinePoints)
        {
            SplineComponent->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
        }
    }

    SplineComponent->SetClosedLoop(true, false);

    for (int32 PointIndex = 0; PointIndex < SplineComponent->GetNumberOfSplinePoints(); ++PointIndex)
    {
        const ESplinePointType::Type PointType = (FitShape == EWaterSplineFitShape::Rectangle)
            ? ESplinePointType::Linear
            : ESplinePointType::Curve;
        SplineComponent->SetSplinePointType(PointIndex, PointType, false);
    }

    SplineComponent->UpdateSpline();
    if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
    {
        WaterSplineComponent->K2_SynchronizeAndBroadcastDataChange();
    }
    RefreshWaterRendering(WaterActor);
    WaterActor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    WaterActor->MarkPackageDirty();
    SplineComponent->MarkPackageDirty();
#endif

    return true;
}

bool AProceduralWaterBiomeSystem::FitLakeSplineToInlandBasin()
{
    if (!LandmassActor || !WaterBodyLakeActor)
    {
        return false;
    }

    const int32 SampleWidth = FMath::Max(LandmassActor->MapWidth, 2);
    const int32 SampleHeight = FMath::Max(LandmassActor->MapHeight, 2);
    const float CellSize = FMath::Max(LandmassActor->GridSize, 1.0f);
    const float WaterZ = bFitWaterActorsToLandmassWaterHeight
        ? GetWaterActorSurfaceZ(WaterBodyLakeActor)
        : WaterBodyLakeActor->GetActorLocation().Z;

    auto CellIndex = [SampleWidth](int32 X, int32 Y)
    {
        return Y * SampleWidth + X;
    };

    TArray<uint8> bBelowWater;
    bBelowWater.Init(0, SampleWidth * SampleHeight);

    const FTransform LandmassTransform = LandmassActor->GetActorTransform();
    for (int32 Y = 0; Y < SampleHeight; ++Y)
    {
        for (int32 X = 0; X < SampleWidth; ++X)
        {
            const FVector LocalSample(X * CellSize, Y * CellSize, 0.0f);
            const FVector WorldSample = LandmassTransform.TransformPosition(LocalSample);
            const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldSample);
            if ((WaterZ - TerrainZ) > 1.0f)
            {
                bBelowWater[CellIndex(X, Y)] = 1;
            }
        }
    }

    TArray<uint8> bConnectedToOuterWater;
    bConnectedToOuterWater.Init(0, bBelowWater.Num());

    TArray<int32> PendingCells;
    PendingCells.Reserve(bBelowWater.Num());
    int32 QueueIndex = 0;

    auto EnqueueIfBorderWater = [&](int32 X, int32 Y)
    {
        const int32 Index = CellIndex(X, Y);
        if (bBelowWater[Index] && !bConnectedToOuterWater[Index])
        {
            bConnectedToOuterWater[Index] = 1;
            PendingCells.Add(Index);
        }
    };

    for (int32 X = 0; X < SampleWidth; ++X)
    {
        EnqueueIfBorderWater(X, 0);
        EnqueueIfBorderWater(X, SampleHeight - 1);
    }

    for (int32 Y = 1; Y < SampleHeight - 1; ++Y)
    {
        EnqueueIfBorderWater(0, Y);
        EnqueueIfBorderWater(SampleWidth - 1, Y);
    }

    while (QueueIndex < PendingCells.Num())
    {
        const int32 Current = PendingCells[QueueIndex++];
        const int32 X = Current % SampleWidth;
        const int32 Y = Current / SampleWidth;

        const int32 NeighborXs[] = { X - 1, X + 1, X, X };
        const int32 NeighborYs[] = { Y, Y, Y - 1, Y + 1 };
        for (int32 NeighborIndex = 0; NeighborIndex < 4; ++NeighborIndex)
        {
            const int32 NeighborX = NeighborXs[NeighborIndex];
            const int32 NeighborY = NeighborYs[NeighborIndex];
            if (NeighborX < 0 || NeighborX >= SampleWidth || NeighborY < 0 || NeighborY >= SampleHeight)
            {
                continue;
            }

            const int32 FlatNeighborIndex = CellIndex(NeighborX, NeighborY);
            if (bBelowWater[FlatNeighborIndex] && !bConnectedToOuterWater[FlatNeighborIndex])
            {
                bConnectedToOuterWater[FlatNeighborIndex] = 1;
                PendingCells.Add(FlatNeighborIndex);
            }
        }
    }

    TArray<uint8> bVisited;
    bVisited.Init(0, bBelowWater.Num());
    TArray<FInlandLakeRegion> LakeRegions;
    TArray<int32> RegionCells;
    const int32 MinimumRegionCells = FMath::Max(MinimumAdditionalLakeRegionCells, 4);

    for (int32 Y = 1; Y < SampleHeight - 1; ++Y)
    {
        for (int32 X = 1; X < SampleWidth - 1; ++X)
        {
            const int32 StartIndex = CellIndex(X, Y);
            if (!bBelowWater[StartIndex] || bConnectedToOuterWater[StartIndex] || bVisited[StartIndex])
            {
                continue;
            }

            PendingCells.Reset();
            QueueIndex = 0;
            RegionCells.Reset();
            PendingCells.Add(StartIndex);
            bVisited[StartIndex] = 1;

            int32 RegionCellCount = 0;
            int32 MinX = X;
            int32 MaxX = X;
            int32 MinY = Y;
            int32 MaxY = Y;
            FVector2D PositionSum = FVector2D::ZeroVector;

            while (QueueIndex < PendingCells.Num())
            {
                const int32 Current = PendingCells[QueueIndex++];
                const int32 RegionX = Current % SampleWidth;
                const int32 RegionY = Current / SampleWidth;

                ++RegionCellCount;
                MinX = FMath::Min(MinX, RegionX);
                MaxX = FMath::Max(MaxX, RegionX);
                MinY = FMath::Min(MinY, RegionY);
                MaxY = FMath::Max(MaxY, RegionY);
                PositionSum += FVector2D(RegionX * CellSize, RegionY * CellSize);
                RegionCells.Add(Current);

                const int32 NeighborXs[] = { RegionX - 1, RegionX + 1, RegionX, RegionX };
                const int32 NeighborYs[] = { RegionY, RegionY, RegionY - 1, RegionY + 1 };
                for (int32 NeighborIndex = 0; NeighborIndex < 4; ++NeighborIndex)
                {
                    const int32 NeighborX = NeighborXs[NeighborIndex];
                    const int32 NeighborY = NeighborYs[NeighborIndex];
                    if (NeighborX <= 0 || NeighborX >= SampleWidth - 1 || NeighborY <= 0 || NeighborY >= SampleHeight - 1)
                    {
                        continue;
                    }

                    const int32 FlatNeighborIndex = CellIndex(NeighborX, NeighborY);
                    if (!bBelowWater[FlatNeighborIndex] || bConnectedToOuterWater[FlatNeighborIndex] || bVisited[FlatNeighborIndex])
                    {
                        continue;
                    }

                    bVisited[FlatNeighborIndex] = 1;
                    PendingCells.Add(FlatNeighborIndex);
                }
            }

            if (RegionCellCount >= MinimumRegionCells)
            {
                FInlandLakeRegion& Region = LakeRegions.AddDefaulted_GetRef();
                Region.bFound = true;
                Region.CellCount = RegionCellCount;
                Region.CenterLocal = PositionSum / static_cast<float>(RegionCellCount);
                Region.ExtentLocal = FVector2D(
                    FMath::Max((MaxX - MinX + 1) * CellSize * 0.5f, CellSize * 1.5f),
                    FMath::Max((MaxY - MinY + 1) * CellSize * 0.5f, CellSize * 1.5f));
                Region.Cells = RegionCells;
            }
        }
    }

    LakeRegions.Sort([](const FInlandLakeRegion& A, const FInlandLakeRegion& B)
    {
        return A.CellCount > B.CellCount;
    });

    if (LakeRegions.IsEmpty())
    {
        UE_LOG(LogProceduralWaterBiome, Warning, TEXT("FitLakeSplineToInlandBasin: no enclosed inland basin found below water level."));
        return false;
    }

    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;

    auto FitLakeActorToRegion = [&](AActor* LakeActor, FInlandLakeRegion& Region) -> bool
    {
        if (!LakeActor)
        {
            return false;
        }

        EnsureWaterBodyRenderable(LakeActor);

        USplineComponent* SplineComponent = FindEditableWaterSpline(LakeActor);
        if (!SplineComponent)
        {
            return false;
        }

        Region.CenterLocal.X = FMath::Clamp(Region.CenterLocal.X, 0.0f, LandmassWidth);
        Region.CenterLocal.Y = FMath::Clamp(Region.CenterLocal.Y, 0.0f, LandmassHeight);

        TArray<uint8> bRegionMask;
        bRegionMask.Init(0, bBelowWater.Num());
        for (const int32 RegionCell : Region.Cells)
        {
            if (bRegionMask.IsValidIndex(RegionCell))
            {
                bRegionMask[RegionCell] = 1;
            }
        }

        auto IsInsideRegion = [&](const FVector2D& LocalPoint) -> bool
        {
            const int32 CellX = FMath::Clamp(FMath::RoundToInt(LocalPoint.X / CellSize), 0, SampleWidth - 1);
            const int32 CellY = FMath::Clamp(FMath::RoundToInt(LocalPoint.Y / CellSize), 0, SampleHeight - 1);
            const int32 Index = CellIndex(CellX, CellY);
            if (!bRegionMask.IsValidIndex(Index) || !bRegionMask[Index])
            {
                return false;
            }

            const FVector WorldPoint = LandmassTransform.TransformPosition(FVector(LocalPoint.X, LocalPoint.Y, 0.0f));
            const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldPoint);
            return (WaterZ - TerrainZ) > 1.0f;
        };

        const float MaxBoundingRadius = FVector2D(Region.ExtentLocal.X, Region.ExtentLocal.Y).Length();
        const float MarchStep = FMath::Max(CellSize * 0.4f, 50.0f);
        const float SafetyInset = FMath::Clamp(CellSize * 0.85f, 50.0f, CellSize * 2.0f);
        const float ShoreOverlapDistance = FMath::Max(0.0f, LakeShoreOverlapDistance);

        const int32 NumPoints = GetWaterActorFitSplinePointCount(LakeActor);
        TArray<FVector> SplinePoints;
        SplinePoints.Reserve(NumPoints);

#if WITH_EDITOR
        LakeActor->Modify();
        SplineComponent->Modify();
#endif

        FVector LakeLocation = LakeActor->GetActorLocation();
        if (bFitWaterActorsToLandmassWaterHeight)
        {
            LakeLocation.Z = WaterZ;
        }
        const FVector RegionWorldCenter = LandmassTransform.TransformPosition(FVector(Region.CenterLocal.X, Region.CenterLocal.Y, 0.0f));
        LakeLocation.X = RegionWorldCenter.X;
        LakeLocation.Y = RegionWorldCenter.Y;
        LakeActor->SetActorLocation(LakeLocation);

        for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
        {
            const float Angle = (static_cast<float>(PointIndex) / static_cast<float>(NumPoints)) * UE_TWO_PI;
            const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
            FVector2D LastValidPoint = Region.CenterLocal;

            for (float Distance = MarchStep; Distance <= MaxBoundingRadius; Distance += MarchStep)
            {
                const FVector2D CandidatePoint = Region.CenterLocal + Direction * Distance;
                if (!IsInsideRegion(CandidatePoint))
                {
                    break;
                }

                LastValidPoint = CandidatePoint;
            }

            const FVector2D ToBoundary = LastValidPoint - Region.CenterLocal;
            const float BoundaryDistance = ToBoundary.Length();
            FVector2D LocalPoint2D = LastValidPoint;
            if (BoundaryDistance > KINDA_SMALL_NUMBER)
            {
                const float InsetDistance = FMath::Min(SafetyInset, BoundaryDistance * 0.35f);
                LocalPoint2D -= (ToBoundary / BoundaryDistance) * InsetDistance;
                LocalPoint2D += (ToBoundary / BoundaryDistance) * FMath::Min(ShoreOverlapDistance, CellSize * 2.5f);
            }

            FVector WorldPoint = LandmassTransform.TransformPosition(FVector(LocalPoint2D.X, LocalPoint2D.Y, 0.0f));
            WorldPoint.Z = WaterZ;
            SplinePoints.Add(WorldPoint);
        }

#if WITH_EDITOR
        if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
        {
            TArray<FVector> LocalSplinePoints;
            LocalSplinePoints.Reserve(SplinePoints.Num());
            const FTransform SplineTransform = WaterSplineComponent->GetComponentTransform();
            for (const FVector& WorldPoint : SplinePoints)
            {
                LocalSplinePoints.Add(SplineTransform.InverseTransformPosition(WorldPoint));
            }

            WaterSplineComponent->ResetSpline(LocalSplinePoints);
        }
        else
#endif
        {
            SplineComponent->ClearSplinePoints(false);
            for (const FVector& Point : SplinePoints)
            {
                SplineComponent->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
            }
        }

        SplineComponent->SetClosedLoop(true, false);
        for (int32 PointIndex = 0; PointIndex < SplineComponent->GetNumberOfSplinePoints(); ++PointIndex)
        {
            SplineComponent->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
        }

        SplineComponent->UpdateSpline();
        if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
        {
            WaterSplineComponent->K2_SynchronizeAndBroadcastDataChange();
        }

        RefreshWaterRendering(LakeActor);
        LakeActor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
        LakeActor->MarkPackageDirty();
        SplineComponent->MarkPackageDirty();
#endif

        return true;
    };

    bool bFittedAnyLake = FitLakeActorToRegion(WaterBodyLakeActor, LakeRegions[0]);
    const int32 DesiredAdditionalLakeCount = bAutoCreateAdditionalLakeActors
        ? FMath::Clamp(LakeRegions.Num() - 1, 0, FMath::Max(MaxAutoLakeActors, 0))
        : FMath::Min(AdditionalWaterBodyLakeActors.Num(), LakeRegions.Num() - 1);

    for (int32 AdditionalLakeIndex = 0; AdditionalLakeIndex < DesiredAdditionalLakeCount; ++AdditionalLakeIndex)
    {
        FInlandLakeRegion& Region = LakeRegions[AdditionalLakeIndex + 1];
        const FVector SpawnLocation = LandmassTransform.TransformPosition(FVector(Region.CenterLocal.X, Region.CenterLocal.Y, WaterZ));
        AActor* AdditionalLakeActor = bAutoCreateAdditionalLakeActors
            ? GetOrCreateAdditionalLakeActor(AdditionalLakeIndex, SpawnLocation)
            : AdditionalWaterBodyLakeActors[AdditionalLakeIndex];

        bFittedAnyLake |= FitLakeActorToRegion(AdditionalLakeActor, Region);
    }

    for (int32 ExtraLakeIndex = DesiredAdditionalLakeCount; ExtraLakeIndex < AdditionalWaterBodyLakeActors.Num(); ++ExtraLakeIndex)
    {
        AActor* ExtraLakeActor = AdditionalWaterBodyLakeActors[ExtraLakeIndex];
        if (ExtraLakeActor && ExtraLakeActor->Tags.Contains(ProceduralGeneratedLakeTag))
        {
#if WITH_EDITOR
            ExtraLakeActor->Modify();
#endif
            ExtraLakeActor->Destroy();
        }
    }
    AdditionalWaterBodyLakeActors.SetNum(DesiredAdditionalLakeCount);

    UE_LOG(LogProceduralWaterBiome, Display, TEXT("FitLakeSplineToInlandBasin: fitted %d inland lake region(s)."), bFittedAnyLake ? DesiredAdditionalLakeCount + 1 : 0);
    return bFittedAnyLake;
}

AActor* AProceduralWaterBiomeSystem::GetOrCreateAdditionalLakeActor(int32 AdditionalLakeIndex, const FVector& SpawnLocation)
{
    if (AdditionalLakeIndex < 0 || !GetWorld())
    {
        return nullptr;
    }

    if (AdditionalWaterBodyLakeActors.IsValidIndex(AdditionalLakeIndex) &&
        AdditionalWaterBodyLakeActors[AdditionalLakeIndex])
    {
        return AdditionalWaterBodyLakeActors[AdditionalLakeIndex];
    }

    AdditionalWaterBodyLakeActors.SetNum(FMath::Max(AdditionalWaterBodyLakeActors.Num(), AdditionalLakeIndex + 1));

    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(GetWorld()->GetCurrentLevel(), AWaterBodyLake::StaticClass(), TEXT("GeneratedWaterBodyLake"));
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
#if WITH_EDITOR
    SpawnParams.bHideFromSceneOutliner = false;
#endif

    AWaterBodyLake* LakeActor = GetWorld()->SpawnActor<AWaterBodyLake>(
        AWaterBodyLake::StaticClass(),
        SpawnLocation,
        FRotator::ZeroRotator,
        SpawnParams);
    if (!LakeActor)
    {
        return nullptr;
    }

    LakeActor->Tags.AddUnique(ProceduralGeneratedLakeTag);

#if WITH_EDITOR
    LakeActor->SetActorLabel(FString::Printf(TEXT("WaterBodyLake_Generated_%02d"), AdditionalLakeIndex + 2));
    if (WaterBodyLakeActor)
    {
        LakeActor->SetFolderPath(WaterBodyLakeActor->GetFolderPath());
    }
    LakeActor->MarkPackageDirty();
#endif

    AdditionalWaterBodyLakeActors[AdditionalLakeIndex] = LakeActor;
    return LakeActor;
}

bool AProceduralWaterBiomeSystem::IsLakeWaterActor(const AActor* WaterActor) const
{
    return WaterActor &&
        (WaterActor == WaterBodyLakeActor || AdditionalWaterBodyLakeActors.ContainsByPredicate([WaterActor](const AActor* LakeActor)
        {
            return LakeActor == WaterActor;
        }));
}

bool AProceduralWaterBiomeSystem::FitOceanSplineToOuterCoastline(float OceanExtentPadding)
{
    if (!LandmassActor || !WaterBodyOceanActor)
    {
        return false;
    }

    EnsureWaterBodyRenderable(WaterBodyOceanActor);

    USplineComponent* SplineComponent = FindEditableWaterSpline(WaterBodyOceanActor);
    AWaterBody* OceanBody = Cast<AWaterBody>(WaterBodyOceanActor);
    UWaterBodyOceanComponent* OceanComponent = OceanBody
        ? Cast<UWaterBodyOceanComponent>(OceanBody->GetWaterBodyComponent())
        : nullptr;
    if (!SplineComponent || !OceanComponent)
    {
        return false;
    }

    const int32 SampleWidth = FMath::Max(LandmassActor->MapWidth, 2);
    const int32 SampleHeight = FMath::Max(LandmassActor->MapHeight, 2);
    const float CellSize = FMath::Max(LandmassActor->GridSize, 1.0f);
    const float WaterZ = bFitWaterActorsToLandmassWaterHeight
        ? GetWaterActorSurfaceZ(WaterBodyOceanActor)
        : WaterBodyOceanActor->GetActorLocation().Z;

    auto CellIndex = [SampleWidth](int32 X, int32 Y)
    {
        return Y * SampleWidth + X;
    };

    TArray<uint8> bAboveWater;
    bAboveWater.Init(0, SampleWidth * SampleHeight);

    const FTransform LandmassTransform = LandmassActor->GetActorTransform();
    for (int32 Y = 0; Y < SampleHeight; ++Y)
    {
        for (int32 X = 0; X < SampleWidth; ++X)
        {
            const FVector LocalSample(X * CellSize, Y * CellSize, 0.0f);
            const FVector WorldSample = LandmassTransform.TransformPosition(LocalSample);
            const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldSample);
            if ((TerrainZ - WaterZ) > 1.0f)
            {
                bAboveWater[CellIndex(X, Y)] = 1;
            }
        }
    }

    TArray<uint8> bVisited;
    bVisited.Init(0, bAboveWater.Num());
    TArray<int32> PendingCells;
    PendingCells.Reserve(bAboveWater.Num());
    int32 QueueIndex = 0;

    struct FOuterCoastRegion
    {
        bool bFound = false;
        int32 CellCount = 0;
        FVector2D CenterLocal = FVector2D::ZeroVector;
        FVector2D ExtentLocal = FVector2D::ZeroVector;
        TArray<int32> Cells;
    };

    FOuterCoastRegion BestRegion;
    TArray<int32> RegionCells;

    for (int32 Y = 0; Y < SampleHeight; ++Y)
    {
        for (int32 X = 0; X < SampleWidth; ++X)
        {
            const int32 StartIndex = CellIndex(X, Y);
            if (!bAboveWater[StartIndex] || bVisited[StartIndex])
            {
                continue;
            }

            PendingCells.Reset();
            QueueIndex = 0;
            RegionCells.Reset();
            PendingCells.Add(StartIndex);
            bVisited[StartIndex] = 1;

            int32 RegionCellCount = 0;
            int32 MinX = X;
            int32 MaxX = X;
            int32 MinY = Y;
            int32 MaxY = Y;
            FVector2D PositionSum = FVector2D::ZeroVector;

            while (QueueIndex < PendingCells.Num())
            {
                const int32 Current = PendingCells[QueueIndex++];
                const int32 RegionX = Current % SampleWidth;
                const int32 RegionY = Current / SampleWidth;

                ++RegionCellCount;
                MinX = FMath::Min(MinX, RegionX);
                MaxX = FMath::Max(MaxX, RegionX);
                MinY = FMath::Min(MinY, RegionY);
                MaxY = FMath::Max(MaxY, RegionY);
                PositionSum += FVector2D(RegionX * CellSize, RegionY * CellSize);
                RegionCells.Add(Current);

                const int32 NeighborXs[] = { RegionX - 1, RegionX + 1, RegionX, RegionX };
                const int32 NeighborYs[] = { RegionY, RegionY, RegionY - 1, RegionY + 1 };
                for (int32 NeighborIndex = 0; NeighborIndex < 4; ++NeighborIndex)
                {
                    const int32 NeighborX = NeighborXs[NeighborIndex];
                    const int32 NeighborY = NeighborYs[NeighborIndex];
                    if (NeighborX < 0 || NeighborX >= SampleWidth || NeighborY < 0 || NeighborY >= SampleHeight)
                    {
                        continue;
                    }

                    const int32 FlatNeighborIndex = CellIndex(NeighborX, NeighborY);
                    if (!bAboveWater[FlatNeighborIndex] || bVisited[FlatNeighborIndex])
                    {
                        continue;
                    }

                    bVisited[FlatNeighborIndex] = 1;
                    PendingCells.Add(FlatNeighborIndex);
                }
            }

            if (RegionCellCount > BestRegion.CellCount)
            {
                BestRegion.bFound = true;
                BestRegion.CellCount = RegionCellCount;
                BestRegion.CenterLocal = PositionSum / static_cast<float>(RegionCellCount);
                BestRegion.ExtentLocal = FVector2D(
                    FMath::Max((MaxX - MinX + 1) * CellSize * 0.5f, CellSize * 1.5f),
                    FMath::Max((MaxY - MinY + 1) * CellSize * 0.5f, CellSize * 1.5f));
                BestRegion.Cells = RegionCells;
            }
        }
    }

    if (!BestRegion.bFound || BestRegion.CellCount < 8)
    {
        UE_LOG(LogProceduralWaterBiome, Warning, TEXT("FitOceanSplineToOuterCoastline: no valid dry coastline region found."));
        return false;
    }

    TArray<uint8> bBestRegionMask;
    bBestRegionMask.Init(0, bAboveWater.Num());
    for (const int32 RegionCell : BestRegion.Cells)
    {
        if (bBestRegionMask.IsValidIndex(RegionCell))
        {
            bBestRegionMask[RegionCell] = 1;
        }
    }

    auto IsInsideBestDryRegion = [&](const FVector2D& LocalPoint) -> bool
    {
        const int32 CellX = FMath::Clamp(FMath::FloorToInt(LocalPoint.X / CellSize), 0, SampleWidth - 1);
        const int32 CellY = FMath::Clamp(FMath::FloorToInt(LocalPoint.Y / CellSize), 0, SampleHeight - 1);
        const int32 Index = CellIndex(CellX, CellY);
        if (!bBestRegionMask.IsValidIndex(Index) || !bBestRegionMask[Index])
        {
            return false;
        }

        const FVector WorldPoint = LandmassTransform.TransformPosition(FVector(LocalPoint.X, LocalPoint.Y, 0.0f));
        const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldPoint);
        return (TerrainZ - WaterZ) > 1.0f;
    };

    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;
    BestRegion.CenterLocal.X = FMath::Clamp(BestRegion.CenterLocal.X, 0.0f, LandmassWidth);
    BestRegion.CenterLocal.Y = FMath::Clamp(BestRegion.CenterLocal.Y, 0.0f, LandmassHeight);

    const float MaxBoundingRadius = FVector2D(BestRegion.ExtentLocal.X, BestRegion.ExtentLocal.Y).Length();
    const float MarchStep = FMath::Max(CellSize * 0.35f, 40.0f);
    const float CoastInset = FMath::Clamp(CellSize * 0.4f, 25.0f, CellSize);
    const float ShoreOverlapDistance = FMath::Max(0.0f, OceanShoreOverlapDistance);

    const int32 NumPoints = GetWaterActorFitSplinePointCount(WaterBodyOceanActor);
    TArray<FVector> SplinePoints;
    SplinePoints.Reserve(NumPoints);

#if WITH_EDITOR
    WaterBodyOceanActor->Modify();
    SplineComponent->Modify();
    OceanComponent->Modify();
#endif

    if (bFitWaterActorsToLandmassWaterHeight)
    {
        FVector OceanLocation = WaterBodyOceanActor->GetActorLocation();
        OceanLocation.Z = WaterZ;
        WaterBodyOceanActor->SetActorLocation(OceanLocation);
    }

    const FVector2D OceanExtent(
        LandmassWidth + OceanExtentPadding * 2.0f,
        LandmassHeight + OceanExtentPadding * 2.0f);
    OceanComponent->SetOceanExtent(OceanExtent);

    for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
    {
        const float Angle = (static_cast<float>(PointIndex) / static_cast<float>(NumPoints)) * UE_TWO_PI;
        const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
        FVector2D LastDryPoint = BestRegion.CenterLocal;

        for (float Distance = MarchStep; Distance <= MaxBoundingRadius + CellSize * 4.0f; Distance += MarchStep)
        {
            const FVector2D CandidatePoint = BestRegion.CenterLocal + Direction * Distance;
            if (!IsInsideBestDryRegion(CandidatePoint))
            {
                break;
            }

            LastDryPoint = CandidatePoint;
        }

        FVector2D LocalPoint2D = LastDryPoint;
        const FVector2D ToBoundary = LastDryPoint - BestRegion.CenterLocal;
        const float BoundaryDistance = ToBoundary.Length();
        if (BoundaryDistance > KINDA_SMALL_NUMBER)
        {
            const FVector2D InsetDirection = ToBoundary / BoundaryDistance;
            const float TotalInset = FMath::Min(CoastInset + ShoreOverlapDistance, BoundaryDistance * 0.35f);
            LocalPoint2D -= InsetDirection * TotalInset;
        }

        FVector WorldPoint = LandmassTransform.TransformPosition(FVector(LocalPoint2D.X, LocalPoint2D.Y, 0.0f));
        WorldPoint.Z = WaterZ;
        SplinePoints.Add(WorldPoint);
    }

#if WITH_EDITOR
    if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
    {
        TArray<FVector> LocalSplinePoints;
        LocalSplinePoints.Reserve(SplinePoints.Num());
        const FTransform SplineTransform = WaterSplineComponent->GetComponentTransform();
        for (const FVector& WorldPoint : SplinePoints)
        {
            LocalSplinePoints.Add(SplineTransform.InverseTransformPosition(WorldPoint));
        }

        WaterSplineComponent->ResetSpline(LocalSplinePoints);
    }
    else
#endif
    {
        SplineComponent->ClearSplinePoints(false);
        for (const FVector& Point : SplinePoints)
        {
            SplineComponent->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
        }
    }

    SplineComponent->SetClosedLoop(true, false);
    for (int32 PointIndex = 0; PointIndex < SplineComponent->GetNumberOfSplinePoints(); ++PointIndex)
    {
        SplineComponent->SetSplinePointType(PointIndex, ESplinePointType::Curve, false);
    }

    SplineComponent->UpdateSpline();
    if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
    {
        WaterSplineComponent->K2_SynchronizeAndBroadcastDataChange();
    }

    RefreshWaterRendering(WaterBodyOceanActor);
    WaterBodyOceanActor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    WaterBodyOceanActor->MarkPackageDirty();
    SplineComponent->MarkPackageDirty();
#endif

    return true;
}

bool AProceduralWaterBiomeSystem::FitOceanToLakeSpline(float OceanExtentPadding)
{
    if (!bMatchOceanSplineToLakeSpline || !LandmassActor || !WaterBodyOceanActor || !WaterBodyLakeActor)
    {
        return false;
    }

    EnsureWaterBodyRenderable(WaterBodyLakeActor);
    EnsureWaterBodyRenderable(WaterBodyOceanActor);

    USplineComponent* SourceSpline = FindEditableWaterSpline(WaterBodyLakeActor);
    USplineComponent* TargetSpline = FindEditableWaterSpline(WaterBodyOceanActor);
    AWaterBody* OceanBody = Cast<AWaterBody>(WaterBodyOceanActor);
    if (!SourceSpline || !TargetSpline || !OceanBody)
    {
        return false;
    }

    UWaterBodyOceanComponent* OceanComponent = Cast<UWaterBodyOceanComponent>(OceanBody->GetWaterBodyComponent());
    if (!OceanComponent)
    {
        return false;
    }

    const FVector Center = LandmassActor->GetLandmassCenter();
    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;
    const float WaterZ = bFitWaterActorsToLandmassWaterHeight
        ? GetWaterActorSurfaceZ(WaterBodyOceanActor)
        : WaterBodyOceanActor->GetActorLocation().Z;

#if WITH_EDITOR
    WaterBodyOceanActor->Modify();
    TargetSpline->Modify();
    OceanComponent->Modify();
#endif

    if (bFitWaterActorsToLandmassWaterHeight)
    {
        FVector OceanLocation = WaterBodyOceanActor->GetActorLocation();
        OceanLocation.Z = WaterZ;
        WaterBodyOceanActor->SetActorLocation(OceanLocation);
    }

    const FVector2D OceanExtent(
        LandmassWidth + OceanExtentPadding * 2.0f,
        LandmassHeight + OceanExtentPadding * 2.0f);
    OceanComponent->SetOceanExtent(OceanExtent);

    TArray<FVector> WorldPoints;
    const int32 NumPoints = SourceSpline->GetNumberOfSplinePoints();
    WorldPoints.Reserve(NumPoints);
    FVector2D SplineCenter2D(0.0f, 0.0f);
    for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
    {
        FVector PointLocation = SourceSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
        PointLocation.Z = WaterZ;
        WorldPoints.Add(PointLocation);
        SplineCenter2D += FVector2D(PointLocation.X, PointLocation.Y);
    }

    if (NumPoints > 0)
    {
        SplineCenter2D /= static_cast<float>(NumPoints);
    }

    const float OverlapDistance = FMath::Max(0.0f, OceanLakeBlendOverlapDistance);
    if (OverlapDistance > 0.0f)
    {
        for (FVector& PointLocation : WorldPoints)
        {
            const FVector2D Point2D(PointLocation.X, PointLocation.Y);
            const FVector2D ToCenter = SplineCenter2D - Point2D;
            const float DistanceToCenter = ToCenter.Length();
            if (DistanceToCenter > KINDA_SMALL_NUMBER)
            {
                const FVector2D InsetDirection = ToCenter / DistanceToCenter;
                const float InsetAmount = FMath::Min(OverlapDistance, DistanceToCenter * 0.45f);
                const FVector2D AdjustedPoint2D = Point2D + InsetDirection * InsetAmount;
                PointLocation.X = AdjustedPoint2D.X;
                PointLocation.Y = AdjustedPoint2D.Y;
            }
        }
    }

#if WITH_EDITOR
    if (UWaterSplineComponent* TargetWaterSpline = Cast<UWaterSplineComponent>(TargetSpline))
    {
        TArray<FVector> LocalSplinePoints;
        LocalSplinePoints.Reserve(WorldPoints.Num());
        const FTransform SplineTransform = TargetWaterSpline->GetComponentTransform();
        for (const FVector& WorldPoint : WorldPoints)
        {
            LocalSplinePoints.Add(SplineTransform.InverseTransformPosition(WorldPoint));
        }

        TargetWaterSpline->ResetSpline(LocalSplinePoints);
    }
    else
#endif
    {
        TargetSpline->ClearSplinePoints(false);
        for (const FVector& Point : WorldPoints)
        {
            TargetSpline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
        }
    }

    TargetSpline->SetClosedLoop(true, false);
    for (int32 PointIndex = 0; PointIndex < TargetSpline->GetNumberOfSplinePoints(); ++PointIndex)
    {
        const ESplinePointType::Type PointType = SourceSpline->GetSplinePointType(PointIndex);
        TargetSpline->SetSplinePointType(PointIndex, PointType, false);
    }

    TargetSpline->UpdateSpline();
    if (UWaterSplineComponent* TargetWaterSpline = Cast<UWaterSplineComponent>(TargetSpline))
    {
        TargetWaterSpline->K2_SynchronizeAndBroadcastDataChange();
    }

    RefreshWaterRendering(WaterBodyOceanActor);
    WaterBodyOceanActor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    WaterBodyOceanActor->MarkPackageDirty();
    TargetSpline->MarkPackageDirty();
#endif

    return true;
}

void AProceduralWaterBiomeSystem::SyncWaterActorHeight(AActor* WaterActor) const
{
    if (!LandmassActor || !WaterActor)
    {
        return;
    }

    const float TargetZ = GetWaterActorSurfaceZ(WaterActor);

#if WITH_EDITOR
    WaterActor->Modify();
#endif

    FVector ActorLocation = WaterActor->GetActorLocation();
    ActorLocation.Z = TargetZ;
    WaterActor->SetActorLocation(ActorLocation);
    SetWaterActorSplineHeight(WaterActor, TargetZ);
    RefreshWaterRendering(WaterActor);
    WaterActor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    WaterActor->MarkPackageDirty();
#endif
}

void AProceduralWaterBiomeSystem::SetWaterActorSplineHeight(AActor* WaterActor, float TargetZ) const
{
    if (!WaterActor)
    {
        return;
    }

    TArray<USplineComponent*> SplineComponents;
    WaterActor->GetComponents<USplineComponent>(SplineComponents);

    for (USplineComponent* SplineComponent : SplineComponents)
    {
        if (!SplineComponent)
        {
            continue;
        }

#if WITH_EDITOR
        SplineComponent->Modify();
#endif

        const int32 NumSplinePoints = SplineComponent->GetNumberOfSplinePoints();
        for (int32 PointIndex = 0; PointIndex < NumSplinePoints; ++PointIndex)
        {
            FVector PointLocation = SplineComponent->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::World);
            PointLocation.Z = TargetZ;
            SplineComponent->SetLocationAtSplinePoint(PointIndex, PointLocation, ESplineCoordinateSpace::World, false);
        }

        SplineComponent->UpdateSpline();
        if (UWaterSplineComponent* WaterSplineComponent = Cast<UWaterSplineComponent>(SplineComponent))
        {
            WaterSplineComponent->K2_SynchronizeAndBroadcastDataChange();
        }

#if WITH_EDITOR
        SplineComponent->MarkPackageDirty();
#endif
    }
}

void AProceduralWaterBiomeSystem::RefreshWaterRendering(AActor* WaterActor) const
{
    EnsureWaterBodyRenderable(WaterActor);

    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        RefreshWaterZoneRendering();
        return;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        RefreshWaterZoneRendering();
        return;
    }

    if (AWaterZone* WaterZone = Cast<AWaterZone>(WaterZoneActor))
    {
        WaterBodyComponent->SetWaterZoneOverride(WaterZone);
        WaterBodyComponent->UpdateWaterZones();
        WaterZone->AddWaterBodyComponent(WaterBodyComponent);
    }

    ApplyWaterBodyOverlapPriority(WaterActor);
    ApplyOceanWaveSettings(WaterActor);
    ApplyWaterMaterialVisualSettings(WaterActor);

    FOnWaterBodyChangedParams Params;
    Params.bShapeOrPositionChanged = true;
    Params.bWeightmapSettingsChanged = false;
    Params.bUserTriggered = true;
    WaterBodyComponent->OnWaterBodyChanged(Params);
    WaterBodyComponent->UpdateWaterBodyRenderData();

    RefreshWaterZoneRendering();
}

void AProceduralWaterBiomeSystem::EnsureWaterBodyRenderable(AActor* WaterActor) const
{
    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        return;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        return;
    }

    if (AWaterZone* WaterZone = Cast<AWaterZone>(WaterZoneActor))
    {
        WaterBodyComponent->SetWaterZoneOverride(WaterZone);
        WaterZone->AddWaterBodyComponent(WaterBodyComponent);
    }

    ApplyWaterBodyOverlapPriority(WaterActor);
    ApplyOceanWaveSettings(WaterActor);
    ApplyWaterMaterialVisualSettings(WaterActor);

    if (!WaterBodyComponent->GetWaterMaterial())
    {
        if (UMaterialInterface* FallbackMaterial = GetFallbackWaterMaterial())
        {
            WaterBodyComponent->SetWaterMaterial(FallbackMaterial);
        }
    }

#if WITH_EDITOR
    WaterBodyComponent->SetWaterBodyStaticMeshEnabled(false);
#endif
}

void AProceduralWaterBiomeSystem::ApplyWaterBodyOverlapPriority(AActor* WaterActor) const
{
    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        return;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        return;
    }

    const int32 DesiredPriority = IsLakeWaterActor(WaterActor)
        ? LakeOverlapMaterialPriority
        : (WaterActor == WaterBodyOceanActor ? OceanOverlapMaterialPriority : 0);

    if (FIntProperty* PriorityProperty = FindFProperty<FIntProperty>(UWaterBodyComponent::StaticClass(), TEXT("OverlapMaterialPriority")))
    {
#if WITH_EDITOR
        WaterBodyComponent->Modify();
#endif
        PriorityProperty->SetPropertyValue_InContainer(WaterBodyComponent, FMath::Clamp(DesiredPriority, -8192, 8191));
    }
}

void AProceduralWaterBiomeSystem::ApplyOceanWaveSettings(AActor* WaterActor) const
{
    if (!bOverrideOceanWaves || WaterActor != WaterBodyOceanActor)
    {
        return;
    }

    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        return;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        return;
    }

#if WITH_EDITOR
    WaterBody->Modify();
    WaterBodyComponent->Modify();
#endif

    if (bDisableOceanWaves)
    {
        WaterBody->SetWaterWaves(nullptr);
    }
    else if (OceanWavesAssetOverride)
    {
        WaterBody->SetWaterWaves(OceanWavesAssetOverride);
    }

    if (FFloatProperty* WaveMaskDepthProperty = FindFProperty<FFloatProperty>(UWaterBodyComponent::StaticClass(), TEXT("TargetWaveMaskDepth")))
    {
        WaveMaskDepthProperty->SetPropertyValue_InContainer(WaterBodyComponent, FMath::Max(0.0f, OceanWaveAttenuationWaterDepth));
    }

    if (FFloatProperty* WaveHeightOffsetProperty = FindFProperty<FFloatProperty>(UWaterBodyComponent::StaticClass(), TEXT("MaxWaveHeightOffset")))
    {
        WaveHeightOffsetProperty->SetPropertyValue_InContainer(WaterBodyComponent, OceanMaxWaveHeightOffset);
    }
}

void AProceduralWaterBiomeSystem::ApplyWaterMaterialVisualSettings(AActor* WaterActor) const
{
    if (!bOverrideWaterMaterialWaveVisuals)
    {
        return;
    }

    UMaterialInstanceDynamic* MaterialInstance = GetOrCreateWaterMaterialInstance(WaterActor);
    if (!MaterialInstance)
    {
        return;
    }

    ApplySeaStateTextureParameters(MaterialInstance);

    if (WaterActor == WaterBodyOceanActor)
    {
        MaterialInstance->SetScalarParameterValue(TEXT("OceanShore_Intensity"), OceanShoreWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("OceanShore_NormalIntensity"), OceanShoreWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("Ocean1_NormalIntensity"), OceanOpenWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("Ocean2_NormalIntensity"), OceanOpenWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("NearNormal_Flatten"), FMath::Clamp(1.0f - OceanShoreWaveVisualIntensity, 0.0f, 1.0f));
        MaterialInstance->SetScalarParameterValue(TEXT("FarNormal_Flatten"), FMath::Clamp(1.0f - OceanFarNormalVisualIntensity, 0.0f, 1.0f));
    }
    else if (IsLakeWaterActor(WaterActor))
    {
        MaterialInstance->SetScalarParameterValue(TEXT("Lake1_NormalIntensity"), LakeWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("Lake2_NormalIntensity"), LakeWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("WaterNormal_Intensity"), LakeWaveVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("EdgeNormal_Intensity"), LakeEdgeVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("RoughNormal_Intensity"), LakeRoughVisualIntensity);
        MaterialInstance->SetScalarParameterValue(TEXT("SubtleNormal_Intensity"), FMath::Max(LakeWaveVisualIntensity * 0.75f, 0.01f));
    }
}

void AProceduralWaterBiomeSystem::UpdateSeaStateTexture() const
{
    if (!bOverrideWaterMaterialWaveVisuals || !LandmassActor)
    {
        return;
    }

    const int32 Resolution = FMath::Clamp(SeaStateTextureResolution, 32, 1024);
    const bool bNeedsNewTexture =
        !SeaStateTexture ||
        !SeaStateTexture->GetPlatformData() ||
        SeaStateTexture->GetSizeX() != Resolution ||
        SeaStateTexture->GetSizeY() != Resolution;

    if (!bSeaStateTextureDirty && !bNeedsNewTexture)
    {
        return;
    }

    if (bNeedsNewTexture)
    {
        SeaStateTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_B8G8R8A8);
        if (!SeaStateTexture)
        {
            return;
        }

        SeaStateTexture->SRGB = false;
        SeaStateTexture->NeverStream = true;
        SeaStateTexture->Filter = TF_Bilinear;
    }

    FTexturePlatformData* PlatformData = SeaStateTexture->GetPlatformData();
    if (!PlatformData || PlatformData->Mips.IsEmpty())
    {
        return;
    }

    const FSeaStateTextureWorldBounds TextureBounds = GetSeaStateTextureWorldBounds(this);
    FTexture2DMipMap& Mip = PlatformData->Mips[0];
    FColor* TexturePixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    if (!TexturePixels)
    {
        Mip.BulkData.Unlock();
        return;
    }

    for (int32 PixelY = 0; PixelY < Resolution; ++PixelY)
    {
        const float V = (static_cast<float>(PixelY) + 0.5f) / static_cast<float>(Resolution);
        const float WorldY = TextureBounds.MinY + V * TextureBounds.SizeY;

        for (int32 PixelX = 0; PixelX < Resolution; ++PixelX)
        {
            const float U = (static_cast<float>(PixelX) + 0.5f) / static_cast<float>(Resolution);
            const float WorldX = TextureBounds.MinX + U * TextureBounds.SizeX;
            const FVector SampleLocation(WorldX, WorldY, GetWaterSurfaceZ());
            const float WaveEnergy = GetFastWaveEnergyAtWorldLocation(SampleLocation);
            const uint8 EncodedEnergy = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(WaveEnergy, 0.0f, 1.0f) * 255.0f));

            TexturePixels[PixelY * Resolution + PixelX] = FColor(EncodedEnergy, EncodedEnergy, EncodedEnergy, 255);
        }
    }

    Mip.BulkData.Unlock();
    SeaStateTexture->UpdateResource();
    bSeaStateTextureDirty = false;
}

void AProceduralWaterBiomeSystem::ApplySeaStateTextureParameters(UMaterialInstanceDynamic* MaterialInstance) const
{
    if (!MaterialInstance)
    {
        return;
    }

    UpdateSeaStateTexture();
    if (!SeaStateTexture)
    {
        return;
    }

    const FSeaStateTextureWorldBounds TextureBounds = GetSeaStateTextureWorldBounds(this);
    MaterialInstance->SetTextureParameterValue(TEXT("SeaStateTexture"), SeaStateTexture);
    MaterialInstance->SetScalarParameterValue(TEXT("SeaStateWorldMinX"), TextureBounds.MinX);
    MaterialInstance->SetScalarParameterValue(TEXT("SeaStateWorldMinY"), TextureBounds.MinY);
    MaterialInstance->SetScalarParameterValue(TEXT("SeaStateWorldSizeX"), TextureBounds.SizeX);
    MaterialInstance->SetScalarParameterValue(TEXT("SeaStateWorldSizeY"), TextureBounds.SizeY);
    MaterialInstance->SetScalarParameterValue(TEXT("SeaStateTextureEnabled"), 1.0f);
    MaterialInstance->SetVectorParameterValue(
        TEXT("SeaStateWorldMin"),
        FLinearColor(TextureBounds.MinX, TextureBounds.MinY, 0.0f, 0.0f));
    MaterialInstance->SetVectorParameterValue(
        TEXT("SeaStateWorldSize"),
        FLinearColor(TextureBounds.SizeX, TextureBounds.SizeY, 0.0f, 0.0f));
}

UMaterialInstanceDynamic* AProceduralWaterBiomeSystem::GetOrCreateWaterMaterialInstance(AActor* WaterActor) const
{
    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        return nullptr;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        return nullptr;
    }

    TObjectPtr<UMaterialInstanceDynamic>& CachedInstance =
        IsLakeWaterActor(WaterActor) ? LakeWaterMaterialInstance : OceanWaterMaterialInstance;

    if (CachedInstance)
    {
        return CachedInstance;
    }

    UMaterialInterface* BaseMaterial = WaterBodyComponent->GetWaterMaterial();
    if (!BaseMaterial)
    {
        BaseMaterial = GetFallbackWaterMaterial();
        if (!BaseMaterial)
        {
            return nullptr;
        }
        WaterBodyComponent->SetWaterMaterial(BaseMaterial);
    }

    UMaterialInstanceDynamic* MaterialInstance = Cast<UMaterialInstanceDynamic>(BaseMaterial);
    if (!MaterialInstance)
    {
        MaterialInstance = UMaterialInstanceDynamic::Create(BaseMaterial, const_cast<AProceduralWaterBiomeSystem*>(this));
        if (!MaterialInstance)
        {
            return nullptr;
        }
        WaterBodyComponent->SetWaterMaterial(MaterialInstance);
    }

    CachedInstance = MaterialInstance;
    return CachedInstance;
}

UMaterialInterface* AProceduralWaterBiomeSystem::GetFallbackWaterMaterial() const
{
    const AActor* FallbackActors[] = { WaterBodyOceanActor, WaterBodyLakeActor };
    for (const AActor* FallbackActor : FallbackActors)
    {
        const AWaterBody* WaterBody = Cast<AWaterBody>(FallbackActor);
        if (!WaterBody)
        {
            continue;
        }

        const UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
        if (WaterBodyComponent && WaterBodyComponent->GetWaterMaterial())
        {
            return WaterBodyComponent->GetWaterMaterial();
        }
    }

    return nullptr;
}

void AProceduralWaterBiomeSystem::LogWaterBodyDiagnostics(const TCHAR* Label, AActor* WaterActor) const
{
    UE_LOG(LogProceduralWaterBiome, Display, TEXT("%s: Actor=%s Class=%s Hidden=%s Location=%s"),
        Label,
        *GetNameSafe(WaterActor),
        WaterActor ? *GetNameSafe(WaterActor->GetClass()) : TEXT("None"),
        WaterActor && WaterActor->IsHiddenEd() ? TEXT("true") : TEXT("false"),
        WaterActor ? *WaterActor->GetActorLocation().ToCompactString() : TEXT("None"));

    AWaterBody* WaterBody = Cast<AWaterBody>(WaterActor);
    if (!WaterBody)
    {
        UE_LOG(LogProceduralWaterBiome, Warning, TEXT("%s: Actor is not an AWaterBody."), Label);
        return;
    }

    UWaterBodyComponent* WaterBodyComponent = WaterBody->GetWaterBodyComponent();
    if (!WaterBodyComponent)
    {
        UE_LOG(LogProceduralWaterBiome, Warning, TEXT("%s: Missing WaterBodyComponent."), Label);
        return;
    }

    const UWaterSplineComponent* WaterSpline = WaterBody->GetWaterSpline();
    const AWaterZone* ComponentWaterZone = WaterBodyComponent->GetWaterZone();
    TArray<UPrimitiveComponent*> RenderableComponents = WaterBodyComponent->GetStandardRenderableComponents();
    TArray<UPrimitiveComponent*> CollisionComponents = WaterBodyComponent->GetCollisionComponents(false);

    const UEnum* WaterBodyTypeEnum = StaticEnum<EWaterBodyType>();
    const FString WaterBodyTypeName = WaterBodyTypeEnum
        ? WaterBodyTypeEnum->GetNameStringByValue(static_cast<int64>(WaterBodyComponent->GetWaterBodyType()))
        : FString::FromInt(static_cast<int32>(WaterBodyComponent->GetWaterBodyType()));

    UE_LOG(LogProceduralWaterBiome, Display,
        TEXT("%s: Type=%s Component=%s AffectsWaterMesh=%s AffectsWaterInfo=%s OverlapPriority=%d Material=%s Zone=%s MatchesRefZone=%s Bounds=%s"),
        Label,
        *WaterBodyTypeName,
        *GetNameSafe(WaterBodyComponent),
        WaterBodyComponent->AffectsWaterMesh() ? TEXT("true") : TEXT("false"),
        WaterBodyComponent->AffectsWaterInfo() ? TEXT("true") : TEXT("false"),
        WaterBodyComponent->GetOverlapMaterialPriority(),
        *GetNameSafe(WaterBodyComponent->GetWaterMaterial()),
        *GetNameSafe(ComponentWaterZone),
        ComponentWaterZone == Cast<AWaterZone>(WaterZoneActor) ? TEXT("true") : TEXT("false"),
        *WaterBodyComponent->Bounds.GetBox().ToString());

    UE_LOG(LogProceduralWaterBiome, Display,
        TEXT("%s: Spline=%s Points=%d Segments=%d Closed=%s SplineBounds=%s"),
        Label,
        *GetNameSafe(WaterSpline),
        WaterSpline ? WaterSpline->GetNumberOfSplinePoints() : 0,
        WaterSpline ? WaterSpline->GetNumberOfSplineSegments() : 0,
        WaterSpline && WaterSpline->IsClosedLoop() ? TEXT("true") : TEXT("false"),
        WaterSpline ? *WaterSpline->Bounds.GetBox().ToString() : TEXT("None"));

    UE_LOG(LogProceduralWaterBiome, Display,
        TEXT("%s: RenderableComponents=%d CollisionComponents=%d"),
        Label,
        RenderableComponents.Num(),
        CollisionComponents.Num());

    for (int32 ComponentIndex = 0; ComponentIndex < RenderableComponents.Num(); ++ComponentIndex)
    {
        const UPrimitiveComponent* Component = RenderableComponents[ComponentIndex];
        UE_LOG(LogProceduralWaterBiome, Display,
            TEXT("%s: Renderable[%d]=%s Class=%s Visible=%s HiddenInGame=%s Bounds=%s"),
            Label,
            ComponentIndex,
            *GetNameSafe(Component),
            Component ? *GetNameSafe(Component->GetClass()) : TEXT("None"),
            Component && Component->IsVisible() ? TEXT("true") : TEXT("false"),
            Component && Component->bHiddenInGame ? TEXT("true") : TEXT("false"),
            Component ? *Component->Bounds.GetBox().ToString() : TEXT("None"));
    }
}

void AProceduralWaterBiomeSystem::RefreshWaterZoneRendering() const
{
    AWaterZone* WaterZone = Cast<AWaterZone>(WaterZoneActor);
    if (!WaterZone)
    {
        return;
    }

    WaterZone->MarkForRebuild(EWaterZoneRebuildFlags::All, this);
    WaterZone->Update();
    WaterZone->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    WaterZone->MarkPackageDirty();
#endif
}

bool AProceduralWaterBiomeSystem::FitActorBoundsToLandmass(AActor* Actor, float Padding, bool bSyncHeight) const
{
    if (!LandmassActor || !Actor)
    {
        return false;
    }

    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;
    const FVector Center = LandmassActor->GetLandmassCenter();
    const float TargetZ = LandmassActor->GetDefaultWaterSurfaceZ() + WaterSurfaceZOffset;

#if WITH_EDITOR
    Actor->Modify();
#endif

    if (AWaterZone* WaterZone = Cast<AWaterZone>(Actor))
    {
        WaterZone->SetActorScale3D(FVector::OneVector);
        WaterZone->SetActorLocation(FVector(Center.X, Center.Y, Actor->GetActorLocation().Z));
        WaterZone->SetZoneExtent(FVector2D(
            LandmassWidth + Padding * 2.0f,
            LandmassHeight + Padding * 2.0f));
        WaterZone->MarkForRebuild(EWaterZoneRebuildFlags::All, this);
        WaterZone->Update();
        WaterZone->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
        WaterZone->MarkPackageDirty();
#endif

        return true;
    }

    FVector ActorLocation = Actor->GetActorLocation();
    ActorLocation.X = Center.X;
    ActorLocation.Y = Center.Y;
    if (bSyncHeight)
    {
        ActorLocation.Z = TargetZ;
    }
    Actor->SetActorLocation(ActorLocation);

    const FVector CurrentScale = Actor->GetActorScale3D();
    const FBox LocalBounds = Actor->CalculateComponentsBoundingBoxInLocalSpace();
    if (LocalBounds.IsValid)
    {
        const FVector LocalSize = LocalBounds.GetSize();
        FVector NewScale = CurrentScale;
        if (LocalSize.X > KINDA_SMALL_NUMBER)
        {
            NewScale.X = (LandmassWidth + Padding * 2.0f) / LocalSize.X;
        }
        if (LocalSize.Y > KINDA_SMALL_NUMBER)
        {
            NewScale.Y = (LandmassHeight + Padding * 2.0f) / LocalSize.Y;
        }
        Actor->SetActorScale3D(NewScale);
    }

    Actor->MarkComponentsRenderStateDirty();

#if WITH_EDITOR
    Actor->MarkPackageDirty();
#endif

    return true;
}

float AProceduralWaterBiomeSystem::GetWaterActorSurfaceZ(AActor* WaterActor) const
{
    const float BaseZ = LandmassActor
        ? LandmassActor->GetDefaultWaterSurfaceZ()
        : GetActorLocation().Z;

    const float SharedZ = BaseZ + WaterSurfaceZOffset;
    if (WaterActor == WaterBodyOceanActor)
    {
        return SharedZ + OceanSurfaceZOffset;
    }

    if (WaterActor == WaterBodyLakeActor)
    {
        return SharedZ + LakeSurfaceZOffset;
    }

    return SharedZ;
}

int32 AProceduralWaterBiomeSystem::GetWaterActorFitSplinePointCount(AActor* WaterActor) const
{
    if (WaterActor == WaterBodyLakeActor)
    {
        return FMath::Clamp(LakeFitSplinePointCount, 4, 128);
    }

    if (WaterActor == WaterBodyOceanActor)
    {
        return FMath::Clamp(OceanFitSplinePointCount, 4, 128);
    }

    return FMath::Clamp(FitSplinePointCount, 4, 128);
}

EWaterSplineFitShape AProceduralWaterBiomeSystem::GetWaterActorFitShape(AActor* WaterActor) const
{
    if (WaterActor == WaterBodyLakeActor)
    {
        return LakeSplineFitShape;
    }

    if (WaterActor == WaterBodyOceanActor)
    {
        return OceanSplineFitShape;
    }

    return WaterSplineFitShape;
}

USplineComponent* AProceduralWaterBiomeSystem::FindEditableWaterSpline(AActor* WaterActor) const
{
    if (!WaterActor)
    {
        return nullptr;
    }

    TArray<USplineComponent*> SplineComponents;
    WaterActor->GetComponents<USplineComponent>(SplineComponents);

    for (USplineComponent* SplineComponent : SplineComponents)
    {
        if (SplineComponent && SplineComponent->GetName().Contains(TEXT("Spline")))
        {
            return SplineComponent;
        }
    }

    return SplineComponents.Num() > 0 ? SplineComponents[0] : nullptr;
}

void AProceduralWaterBiomeSystem::InvalidateWaterQueryCache() const
{
    CachedLakeSplineComponent.Reset();
    CachedLakeSplinePoints.Reset();
    CachedLakeSplinePointStarts.Reset();
    CachedLakeSplinePointCount = INDEX_NONE;
    CachedLakeSplineLength = -1.0f;
    bSeaStateTextureDirty = true;
}

void AProceduralWaterBiomeSystem::RebuildLakeSplineQueryCache() const
{
    if (CachedLakeSplinePoints.Num() > 0 && CachedLakeSplinePointStarts.Num() > 0)
    {
        return;
    }

    CachedLakeSplineComponent.Reset();
    CachedLakeSplinePoints.Reset();
    CachedLakeSplinePointStarts.Reset();
    CachedLakeSplinePointCount = 0;
    CachedLakeSplineLength = 0.0f;

    TArray<AActor*> LakeActors;
    LakeActors.Reserve(AdditionalWaterBodyLakeActors.Num() + 1);
    if (WaterBodyLakeActor)
    {
        LakeActors.Add(WaterBodyLakeActor);
    }
    for (AActor* AdditionalLakeActor : AdditionalWaterBodyLakeActors)
    {
        if (AdditionalLakeActor)
        {
            LakeActors.Add(AdditionalLakeActor);
        }
    }

    for (const AActor* LakeActor : LakeActors)
    {
        const USplineComponent* SplineComponent = FindEditableWaterSpline(const_cast<AActor*>(LakeActor));
        if (!SplineComponent || SplineComponent->GetNumberOfSplinePoints() < 3)
        {
            continue;
        }

        const int32 SplinePointCount = SplineComponent->GetNumberOfSplinePoints();
        const float SplineLength = SplineComponent->GetSplineLength();
        if (SplineLength <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        CachedLakeSplinePointStarts.Add(CachedLakeSplinePoints.Num());
        CachedLakeSplinePointCount += SplinePointCount;
        CachedLakeSplineLength += SplineLength;

        const int32 NumSamples = FMath::Max(SplinePointCount * 8, 32);
        CachedLakeSplinePoints.Reserve(CachedLakeSplinePoints.Num() + NumSamples);
        for (int32 Index = 0; Index < NumSamples; ++Index)
        {
            const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples);
            const FVector Point = SplineComponent->GetLocationAtDistanceAlongSpline(SplineLength * Alpha, ESplineCoordinateSpace::World);
            CachedLakeSplinePoints.Add(FVector2D(Point.X, Point.Y));
        }
    }
}

float AProceduralWaterBiomeSystem::GetWaterSurfaceZ() const
{
    if (bUseWaterBodyLakeActorHeight && WaterBodyLakeActor)
    {
        return WaterBodyLakeActor->GetActorLocation().Z;
    }

    if (LandmassActor)
    {
        return GetWaterActorSurfaceZ(WaterBodyLakeActor);
    }

    return GetWaterActorSurfaceZ(WaterBodyLakeActor);
}

float AProceduralWaterBiomeSystem::GetTerrainHeightAtWorldLocation(const FVector& WorldLocation) const
{
    return LandmassActor
        ? LandmassActor->GetTerrainHeightAtWorldLocation(WorldLocation)
        : WorldLocation.Z;
}

float AProceduralWaterBiomeSystem::GetWaterDepthAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldLocation);
    return FMath::Max(0.0f, GetWaterSurfaceZ() - TerrainZ);
}

float AProceduralWaterBiomeSystem::GetShorelineProximityAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldLocation);
    const float DistanceFromWaterline = FMath::Abs(GetWaterSurfaceZ() - TerrainZ);
    return 1.0f - FMath::Clamp(DistanceFromWaterline / FMath::Max(ShorelineBandHeight, 1.0f), 0.0f, 1.0f);
}

EProceduralWaterBiome AProceduralWaterBiomeSystem::GetWaterBiomeAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return EProceduralWaterBiome::DryLand;
    }

    const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(WorldLocation);
    const float WaterDepth = GetWaterSurfaceZ() - TerrainZ;
    const bool bInsideLandmass = IsInsideLandmassXY(WorldLocation);

    bool bFoundLakeSpline = false;
    bool bInsideLakeSpline = false;
    const float DistanceToLakeSpline = GetDistanceToNearestLakeSplineEdge(WorldLocation, bFoundLakeSpline, bInsideLakeSpline);

    if (bFoundLakeSpline && !bInsideLakeSpline)
    {
        if (bInsideLandmass && WaterDepth <= 0.0f)
        {
            return (FMath::Abs(WaterDepth) <= ShorelineBandHeight)
                ? EProceduralWaterBiome::OceanWetShore
                : EProceduralWaterBiome::DryLand;
        }

        if (DistanceToLakeSpline <= OceanWetShoreDistance)
        {
            return EProceduralWaterBiome::OceanWetShore;
        }

        if (DistanceToLakeSpline <= OceanShallowWaterDistance)
        {
            return EProceduralWaterBiome::OceanShallowWater;
        }

        if (DistanceToLakeSpline <= OceanLittoralShelfDistance)
        {
            return EProceduralWaterBiome::OceanLittoralShelf;
        }

        return EProceduralWaterBiome::OceanDeepWater;
    }

    if (WaterDepth <= 0.0f)
    {
        return (FMath::Abs(WaterDepth) <= ShorelineBandHeight)
            ? (bInsideLakeSpline ? EProceduralWaterBiome::LakeWetShore : EProceduralWaterBiome::OceanWetShore)
            : EProceduralWaterBiome::DryLand;
    }

    if (WaterDepth <= ShallowWaterDepth)
    {
        return bInsideLakeSpline
            ? EProceduralWaterBiome::LakeShallowWater
            : EProceduralWaterBiome::OceanShallowWater;
    }

    if (WaterDepth <= LittoralShelfDepth)
    {
        return bInsideLakeSpline
            ? EProceduralWaterBiome::LakeLittoralShelf
            : EProceduralWaterBiome::OceanLittoralShelf;
    }

    return bInsideLakeSpline
        ? EProceduralWaterBiome::LakeDeepWater
        : EProceduralWaterBiome::OceanDeepWater;
}

float AProceduralWaterBiomeSystem::GetWaveEnergyAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    if (!IsWaterAtWorldLocation(WorldLocation))
    {
        return 0.0f;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);
    bool bFoundLakeSpline = false;
    bool bInsideLakeSpline = false;
    const float DistanceToLakeSpline = GetDistanceToNearestLakeSplineEdge(WorldLocation, bFoundLakeSpline, bInsideLakeSpline);

    if (bFoundLakeSpline && bInsideLakeSpline)
    {
        const float LakeEdgeAlpha = FMath::Clamp(
            DistanceToLakeSpline / FMath::Max(WaveShoreInfluenceDistance, 250.0f),
            0.0f,
            1.0f);
        const float LakeDepthAlpha = FMath::Clamp(
            WaterDepth / FMath::Max(ShallowWaterDepth, 1.0f),
            0.0f,
            1.0f);
        return FMath::Clamp(LakeWaveEnergy * FMath::Lerp(0.65f, 1.0f, FMath::Max(LakeEdgeAlpha, LakeDepthAlpha)), 0.0f, 1.0f);
    }

    if (WaterDepth <= 0.0f && IsInsideLandmassXY(WorldLocation))
    {
        return 0.0f;
    }

    const float DistanceToShoreline = (bFoundLakeSpline && !bInsideLakeSpline)
        ? DistanceToLakeSpline
        : GetDistanceToNearestShoreline(WorldLocation);
    const float WaveExposure = GetWaveExposureAtWorldLocation(WorldLocation);
    const float DirectionalFetch = GetDirectionalWaveFetchAtWorldLocation(WorldLocation);
    const float CombinedExposure = FMath::Clamp(
        FMath::Lerp(
            WaveExposure,
            FMath::Max(WaveExposure, DirectionalFetch),
            PrimaryWaveDirectionInfluence),
        0.0f,
        1.0f);

    const float ShoreDistanceAlpha = FMath::Clamp(
        DistanceToShoreline / FMath::Max(WaveShoreInfluenceDistance, 250.0f),
        0.0f,
        1.0f);
    const float SmoothedShoreDistanceAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, ShoreDistanceAlpha, 2.6f);

    const float OceanDistanceAlpha = FMath::Clamp(
        DistanceToShoreline / FMath::Max(OpenOceanWaveRampDistance, 500.0f),
        0.0f,
        1.0f);
    const float SmoothedOceanDistanceAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, OceanDistanceAlpha, 1.8f);

    const float ExposedCoastEnergy = FMath::Lerp(
        LakeWaveEnergy,
        OceanBoundaryWaveEnergy,
        CombinedExposure);
    const float CoastalEnergy = FMath::Lerp(
        LakeWaveEnergy,
        ExposedCoastEnergy,
        SmoothedShoreDistanceAlpha);
    const float OpenOceanEnergy = FMath::Lerp(
        OceanBoundaryWaveEnergy,
        OpenOceanWaveEnergy,
        FMath::Max(SmoothedOceanDistanceAlpha, CombinedExposure));

    const float OffshoreBlendAlpha = FMath::Clamp(
        SmoothedShoreDistanceAlpha * (0.15f + CombinedExposure * 0.35f) +
            SmoothedOceanDistanceAlpha * (0.35f + CombinedExposure * 0.50f),
        0.0f,
        1.0f);
    float BaseWaveEnergy = FMath::Lerp(CoastalEnergy, OpenOceanEnergy, OffshoreBlendAlpha);
    BaseWaveEnergy = FMath::Max(BaseWaveEnergy, CoastalEnergy);

    const float EffectiveWaterDepth = (WaterDepth > 0.0f)
        ? WaterDepth
        : FMath::Max(DistanceToShoreline, LittoralShelfDepth);
    const float DepthAlpha = FMath::Clamp(
        EffectiveWaterDepth / FMath::Max(LittoralShelfDepth, 1.0f),
        0.0f,
        1.0f);
    const float DepthMultiplier = FMath::Lerp(1.0f - WaveDepthInfluence, 1.0f, DepthAlpha);
    return FMath::Clamp(BaseWaveEnergy * DepthMultiplier, 0.0f, 1.0f);
}

float AProceduralWaterBiomeSystem::GetFastWaveEnergyAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor || !IsWaterAtWorldLocation(WorldLocation))
    {
        return 0.0f;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);

    bool bFoundLakeSpline = false;
    bool bInsideLakeSpline = false;
    const float DistanceToLakeSpline = GetDistanceToNearestLakeSplineEdge(WorldLocation, bFoundLakeSpline, bInsideLakeSpline);

    if (bFoundLakeSpline && bInsideLakeSpline)
    {
        const float LakeEdgeAlpha = FMath::Clamp(
            DistanceToLakeSpline / FMath::Max(WaveShoreInfluenceDistance, 250.0f),
            0.0f,
            1.0f);
        const float LakeDepthAlpha = FMath::Clamp(
            WaterDepth / FMath::Max(ShallowWaterDepth, 1.0f),
            0.0f,
            1.0f);
        return FMath::Clamp(LakeWaveEnergy * FMath::Lerp(0.65f, 1.0f, FMath::Max(LakeEdgeAlpha, LakeDepthAlpha)), 0.0f, 1.0f);
    }

    if (WaterDepth <= 0.0f && IsInsideLandmassXY(WorldLocation))
    {
        return 0.0f;
    }

    const float DistanceToShoreline = (bFoundLakeSpline && !bInsideLakeSpline)
        ? DistanceToLakeSpline
        : GetDistanceToNearestShoreline(WorldLocation);
    const float ShoreDistanceAlpha = FMath::Clamp(
        DistanceToShoreline / FMath::Max(WaveShoreInfluenceDistance, 250.0f),
        0.0f,
        1.0f);
    const float OceanDistanceAlpha = FMath::Clamp(
        DistanceToShoreline / FMath::Max(OpenOceanWaveRampDistance, 500.0f),
        0.0f,
        1.0f);

    const float SmoothedShoreDistanceAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, ShoreDistanceAlpha, 2.6f);
    const float SmoothedOceanDistanceAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, OceanDistanceAlpha, 1.8f);
    const float EffectiveWaterDepth = (WaterDepth > 0.0f)
        ? WaterDepth
        : FMath::Max(DistanceToShoreline, LittoralShelfDepth);
    const float DepthAlpha = FMath::Clamp(
        EffectiveWaterDepth / FMath::Max(LittoralShelfDepth, 1.0f),
        0.0f,
        1.0f);

    const float CoastalEnergy = FMath::Lerp(LakeWaveEnergy, OceanBoundaryWaveEnergy, SmoothedShoreDistanceAlpha);
    const float OpenOceanEnergy = FMath::Lerp(OceanBoundaryWaveEnergy, OpenOceanWaveEnergy, SmoothedOceanDistanceAlpha);
    const float BaseWaveEnergy = FMath::Lerp(CoastalEnergy, OpenOceanEnergy, SmoothedOceanDistanceAlpha);
    const float DepthMultiplier = FMath::Lerp(1.0f - WaveDepthInfluence, 1.0f, DepthAlpha);
    return FMath::Clamp(BaseWaveEnergy * DepthMultiplier, 0.0f, 1.0f);
}

void AProceduralWaterBiomeSystem::DrawDebugBiomeGrid() const
{
    if (!bDrawDebugBiomeGrid || !LandmassActor || !GetWorld())
    {
        return;
    }

    const FVector Center = LandmassActor->GetLandmassCenter();
    const float Spacing = FMath::Max(DebugGridSpacing, 100.0f);
    const float LandmassWidth = FMath::Max(0, LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float LandmassHeight = FMath::Max(0, LandmassActor->MapHeight - 1) * LandmassActor->GridSize;
    const float AutoHalfExtent = FMath::Max(LandmassWidth, LandmassHeight) * 0.5f + Spacing;
    const float HalfExtent = bAutoSizeDebugGridToLandmass
        ? FMath::Max(AutoHalfExtent, Spacing)
        : FMath::Max(DebugGridExtent, Spacing);

    for (float Y = -HalfExtent; Y <= HalfExtent; Y += Spacing)
    {
        for (float X = -HalfExtent; X <= HalfExtent; X += Spacing)
        {
            const FVector SampleLocation(Center.X + X, Center.Y + Y, Center.Z);
            if (!IsInsideDebugWaterBounds(SampleLocation))
            {
                continue;
            }

            const EProceduralWaterBiome Biome = GetWaterBiomeAtWorldLocation(SampleLocation);
            if (Biome == EProceduralWaterBiome::DryLand)
            {
                continue;
            }

            const float TerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(SampleLocation);
            const float DrawZ = FMath::Max(TerrainZ, GetWaterSurfaceZ()) + DebugDrawZOffset;
            const FVector DrawLocation(SampleLocation.X, SampleLocation.Y, DrawZ);
            const FColor DebugColor = (DebugViewMode == EProceduralWaterDebugView::WaveEnergy)
                ? GetDebugColorForWaveEnergy(GetFastWaveEnergyAtWorldLocation(SampleLocation))
                : GetDebugColorForBiome(Biome);

            DrawDebugPoint(
                GetWorld(),
                DrawLocation,
                DebugPointSize,
                DebugColor,
                false,
                0.0f);
        }
    }
}

bool AProceduralWaterBiomeSystem::IsInsideLandmassXY(const FVector& WorldLocation) const
{
    if (!LandmassActor || LandmassActor->GridSize <= KINDA_SMALL_NUMBER || LandmassActor->MapWidth < 2 || LandmassActor->MapHeight < 2)
    {
        return false;
    }

    const FVector LocalLocation = LandmassActor->GetActorTransform().InverseTransformPosition(WorldLocation);
    const float WidthWorld = static_cast<float>(LandmassActor->MapWidth - 1) * LandmassActor->GridSize;
    const float HeightWorld = static_cast<float>(LandmassActor->MapHeight - 1) * LandmassActor->GridSize;

    return
        LocalLocation.X >= 0.0f &&
        LocalLocation.Y >= 0.0f &&
        LocalLocation.X <= WidthWorld &&
        LocalLocation.Y <= HeightWorld;
}

bool AProceduralWaterBiomeSystem::IsWaterAtWorldLocation(const FVector& WorldLocation) const
{
    bool bFoundLakeSpline = false;
    bool bInsideLakeSpline = false;
    GetDistanceToNearestLakeSplineEdge(WorldLocation, bFoundLakeSpline, bInsideLakeSpline);

    if (bFoundLakeSpline && bInsideLakeSpline)
    {
        return true;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);
    if (IsInsideLandmassXY(WorldLocation))
    {
        return WaterDepth > 0.0f || (bFoundLakeSpline && !bInsideLakeSpline);
    }

    return bFoundLakeSpline || WaterDepth > 0.0f;
}

bool AProceduralWaterBiomeSystem::IsInsideDebugWaterBounds(const FVector& WorldLocation) const
{
    if (!bLimitDebugToWaterBodyBounds || (!WaterBodyLakeActor && !WaterBodyOceanActor))
    {
        return true;
    }

    bool bFoundSpline = false;
    if (bPreferWaterBodySplineBounds)
    {
        const bool bInsideSpline = IsInsideAnyWaterSpline(WorldLocation, bFoundSpline);
        if (bFoundSpline && bInsideSpline)
        {
            return true;
        }
    }

    return IsInsideWaterBodyActorBounds(WaterBodyLakeActor, WorldLocation) ||
        IsInsideWaterBodyActorBounds(WaterBodyOceanActor, WorldLocation);
}

bool AProceduralWaterBiomeSystem::IsInsideWaterBodyActorBounds(const AActor* WaterActor, const FVector& WorldLocation) const
{
    if (!WaterActor)
    {
        return false;
    }

    FBox Bounds = WaterActor->GetComponentsBoundingBox(true);
    if (!Bounds.IsValid)
    {
        return false;
    }

    Bounds = Bounds.ExpandBy(FVector(DebugWaterBodyBoundsPadding, DebugWaterBodyBoundsPadding, HALF_WORLD_MAX));
    return
        WorldLocation.X >= Bounds.Min.X &&
        WorldLocation.X <= Bounds.Max.X &&
        WorldLocation.Y >= Bounds.Min.Y &&
        WorldLocation.Y <= Bounds.Max.Y;
}

bool AProceduralWaterBiomeSystem::IsInsideAnyWaterSpline(const FVector& WorldLocation, bool& bFoundSpline) const
{
    bFoundSpline = false;

    RebuildLakeSplineQueryCache();
    if (CachedLakeSplinePoints.Num() < 3 || CachedLakeSplinePointStarts.IsEmpty())
    {
        return false;
    }

    bFoundSpline = true;

    for (int32 SplineIndex = 0; SplineIndex < CachedLakeSplinePointStarts.Num(); ++SplineIndex)
    {
        const int32 StartIndex = CachedLakeSplinePointStarts[SplineIndex];
        const int32 EndIndex = CachedLakeSplinePointStarts.IsValidIndex(SplineIndex + 1)
            ? CachedLakeSplinePointStarts[SplineIndex + 1]
            : CachedLakeSplinePoints.Num();
        if (EndIndex - StartIndex < 3)
        {
            continue;
        }

        bool bInside = false;
        int32 PreviousIndex = EndIndex - 1;
        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
        {
            const FVector2D& A = CachedLakeSplinePoints[Index];
            const FVector2D& B = CachedLakeSplinePoints[PreviousIndex];

            const bool bYStraddles = (A.Y > WorldLocation.Y) != (B.Y > WorldLocation.Y);
            const float Denominator = B.Y - A.Y;
            if (bYStraddles && FMath::Abs(Denominator) > KINDA_SMALL_NUMBER)
            {
                const float IntersectionX = ((B.X - A.X) * (WorldLocation.Y - A.Y) / Denominator) + A.X;
                if (WorldLocation.X < IntersectionX)
                {
                    bInside = !bInside;
                }
            }

            PreviousIndex = Index;
        }

        if (bInside)
        {
            return true;
        }
    }

    return false;
}

float AProceduralWaterBiomeSystem::GetWaveExposureAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);
    if (WaterDepth <= 0.0f)
    {
        return 0.0f;
    }

    const float ProbeDistance = FMath::Max(WaveExposureProbeDistance, 500.0f);
    const int32 ProbeCount = FMath::Clamp(WaveExposureProbeCount, 4, 32);
    const int32 ProbeSteps = GetEffectiveWaveProbeSteps(this->LandmassActor, ProbeDistance, WaveExposureProbeSteps, 2, 16, 96);
    const float WaterSurfaceZ = GetWaterSurfaceZ();
    const float StepLength = ProbeDistance / static_cast<float>(ProbeSteps);
    const float ShallowShelterDepth = FMath::Max(ShorelineBandHeight * 0.35f, 25.0f);

    float ExposureSum = 0.0f;
    float MinimumExposure = 1.0f;
    float NearestShelterDistance = ProbeDistance;
    int32 BlockedDirectionCount = 0;

    for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
    {
        const float Angle = (static_cast<float>(ProbeIndex) / static_cast<float>(ProbeCount)) * UE_TWO_PI;
        const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));

        float DirectionExposure = 1.0f;
        for (int32 StepIndex = 1; StepIndex <= ProbeSteps; ++StepIndex)
        {
            const float Distance = StepLength * static_cast<float>(StepIndex);
            const FVector SampleLocation(
                WorldLocation.X + Direction.X * Distance,
                WorldLocation.Y + Direction.Y * Distance,
                WorldLocation.Z);

            const float SampleTerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(SampleLocation);
            const float SampleWaterDepth = WaterSurfaceZ - SampleTerrainZ;
            if (SampleWaterDepth <= 0.0f)
            {
                DirectionExposure = FMath::Square(Distance / ProbeDistance);
                NearestShelterDistance = FMath::Min(NearestShelterDistance, Distance);
                ++BlockedDirectionCount;
                break;
            }

            if (SampleWaterDepth <= ShallowShelterDepth)
            {
                DirectionExposure = FMath::Min(DirectionExposure, FMath::Square(Distance / ProbeDistance));
                NearestShelterDistance = FMath::Min(NearestShelterDistance, Distance);
                ++BlockedDirectionCount;
                break;
            }
        }

        const float ClampedExposure = FMath::Clamp(DirectionExposure, 0.0f, 1.0f);
        ExposureSum += ClampedExposure;
        MinimumExposure = FMath::Min(MinimumExposure, ClampedExposure);
    }

    const float AverageExposure = ExposureSum / static_cast<float>(ProbeCount);
    const float BlockedDirectionRatio = static_cast<float>(BlockedDirectionCount) / static_cast<float>(ProbeCount);
    const float NearestShelterAlpha = FMath::Clamp(NearestShelterDistance / ProbeDistance, 0.0f, 1.0f);
    const float DirectionalOpenness = AverageExposure * 0.45f + MinimumExposure * 0.55f;
    const float ShelterPenalty = 1.0f - BlockedDirectionRatio * (1.0f - NearestShelterAlpha) * 0.85f;
    return FMath::Clamp(DirectionalOpenness * ShelterPenalty, 0.0f, 1.0f);
}

float AProceduralWaterBiomeSystem::GetDirectionalWaveFetchAtWorldLocation(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);
    if (WaterDepth <= 0.0f)
    {
        return 0.0f;
    }

    const float ProbeDistance = FMath::Max(WaveExposureProbeDistance, 1000.0f);
    const int32 ProbeCount = FMath::Clamp(PrimaryWaveDirectionProbeCount, 1, 9);
    const int32 ProbeSteps = FMath::Max(
        GetEffectiveWaveProbeSteps(this->LandmassActor, ProbeDistance, WaveExposureProbeSteps, 2, 16, 96),
        8);
    const float StepLength = ProbeDistance / static_cast<float>(ProbeSteps);
    const float ShallowShelterDepth = FMath::Max(ShorelineBandHeight * 0.5f, 35.0f);
    const float WaterSurfaceZ = GetWaterSurfaceZ();
    const float BaseAngleRadians = FMath::DegreesToRadians(PrimaryWaveDirectionDegrees);
    const float SpreadRadians = FMath::DegreesToRadians(PrimaryWaveDirectionSpreadDegrees);

    float WeightedFetchSum = 0.0f;
    float TotalWeight = 0.0f;
    float MinimumFetch = 1.0f;

    for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
    {
        const float ProbeAlpha = (ProbeCount == 1)
            ? 0.5f
            : static_cast<float>(ProbeIndex) / static_cast<float>(ProbeCount - 1);
        const float OffsetAngle = FMath::Lerp(-SpreadRadians, SpreadRadians, ProbeAlpha);
        const float SampleAngle = BaseAngleRadians + OffsetAngle;
        const FVector2D Direction(FMath::Cos(SampleAngle), FMath::Sin(SampleAngle));
        const float DirectionWeight = (ProbeCount == 1)
            ? 1.0f
            : FMath::Clamp(1.0f - FMath::Abs(ProbeAlpha - 0.5f) * 1.5f, 0.25f, 1.0f);

        float DirectionFetch = 1.0f;
        for (int32 StepIndex = 1; StepIndex <= ProbeSteps; ++StepIndex)
        {
            const float Distance = StepLength * static_cast<float>(StepIndex);
            const FVector SampleLocation(
                WorldLocation.X + Direction.X * Distance,
                WorldLocation.Y + Direction.Y * Distance,
                WorldLocation.Z);

            const float SampleTerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(SampleLocation);
            const float SampleWaterDepth = WaterSurfaceZ - SampleTerrainZ;
            if (SampleWaterDepth <= 0.0f)
            {
                DirectionFetch = FMath::Square(Distance / ProbeDistance);
                break;
            }

            if (SampleWaterDepth <= ShallowShelterDepth)
            {
                DirectionFetch = FMath::Min(DirectionFetch, FMath::Square(Distance / ProbeDistance));
                break;
            }
        }

        const float ClampedFetch = FMath::Clamp(DirectionFetch, 0.0f, 1.0f);
        WeightedFetchSum += ClampedFetch * DirectionWeight;
        TotalWeight += DirectionWeight;
        MinimumFetch = FMath::Min(MinimumFetch, ClampedFetch);
    }

    return (TotalWeight > KINDA_SMALL_NUMBER)
        ? FMath::Clamp((WeightedFetchSum / TotalWeight) * 0.55f + MinimumFetch * 0.45f, 0.0f, 1.0f)
        : 0.0f;
}

float AProceduralWaterBiomeSystem::GetDistanceToNearestShoreline(const FVector& WorldLocation) const
{
    if (!LandmassActor)
    {
        return 0.0f;
    }

    const float WaterDepth = GetWaterDepthAtWorldLocation(WorldLocation);
    if (WaterDepth <= 0.0f)
    {
        return 0.0f;
    }

    const float ProbeDistance = FMath::Max(FMath::Min(WaveExposureProbeDistance, WaveShoreInfluenceDistance * 1.5f), 500.0f);
    const int32 ProbeCount = FMath::Clamp(WaveExposureProbeCount, 8, 48);
    const int32 ProbeSteps = FMath::Max(
        GetEffectiveWaveProbeSteps(this->LandmassActor, ProbeDistance, WaveExposureProbeSteps, 2, 16, 128),
        12);
    const float StepLength = ProbeDistance / static_cast<float>(ProbeSteps);
    const float ShallowShoreDepth = FMath::Max(ShorelineBandHeight * 0.65f, 40.0f);
    const float WaterSurfaceZ = GetWaterSurfaceZ();

    float NearestDistance = ProbeDistance;

    for (int32 ProbeIndex = 0; ProbeIndex < ProbeCount; ++ProbeIndex)
    {
        const float Angle = (static_cast<float>(ProbeIndex) / static_cast<float>(ProbeCount)) * UE_TWO_PI;
        const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));

        for (int32 StepIndex = 1; StepIndex <= ProbeSteps; ++StepIndex)
        {
            const float Distance = StepLength * static_cast<float>(StepIndex);
            const FVector SampleLocation(
                WorldLocation.X + Direction.X * Distance,
                WorldLocation.Y + Direction.Y * Distance,
                WorldLocation.Z);

            const float SampleTerrainZ = LandmassActor->GetTerrainHeightAtWorldLocation(SampleLocation);
            const float SampleWaterDepth = WaterSurfaceZ - SampleTerrainZ;
            if (SampleWaterDepth <= 0.0f || SampleWaterDepth <= ShallowShoreDepth)
            {
                NearestDistance = FMath::Min(NearestDistance, Distance);
                break;
            }
        }
    }

    return NearestDistance;
}

float AProceduralWaterBiomeSystem::GetDistanceToNearestLakeSplineEdge(const FVector& WorldLocation, bool& bFoundSpline, bool& bInsideLake) const
{
    bFoundSpline = false;
    bInsideLake = false;

    RebuildLakeSplineQueryCache();
    if (CachedLakeSplinePoints.Num() < 3 || CachedLakeSplinePointStarts.IsEmpty())
    {
        return 0.0f;
    }

    float NearestDistance = TNumericLimits<float>::Max();

    for (int32 SplineIndex = 0; SplineIndex < CachedLakeSplinePointStarts.Num(); ++SplineIndex)
    {
        const int32 StartIndex = CachedLakeSplinePointStarts[SplineIndex];
        const int32 EndIndex = CachedLakeSplinePointStarts.IsValidIndex(SplineIndex + 1)
            ? CachedLakeSplinePointStarts[SplineIndex + 1]
            : CachedLakeSplinePoints.Num();
        if (EndIndex - StartIndex < 3)
        {
            continue;
        }

        bool bInsideThisSpline = false;
        int32 PreviousIndex = EndIndex - 1;
        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
        {
            const FVector2D& A = CachedLakeSplinePoints[Index];
            const FVector2D& B = CachedLakeSplinePoints[PreviousIndex];

            const bool bYStraddles = (A.Y > WorldLocation.Y) != (B.Y > WorldLocation.Y);
            const float Denominator = B.Y - A.Y;
            if (bYStraddles && FMath::Abs(Denominator) > KINDA_SMALL_NUMBER)
            {
                const float IntersectionX = ((B.X - A.X) * (WorldLocation.Y - A.Y) / Denominator) + A.X;
                if (WorldLocation.X < IntersectionX)
                {
                    bInsideThisSpline = !bInsideThisSpline;
                }
            }

            const FVector2D Segment = B - A;
            const float SegmentLengthSq = Segment.SizeSquared();
            if (SegmentLengthSq > KINDA_SMALL_NUMBER)
            {
                const FVector2D RelativePoint(WorldLocation.X - A.X, WorldLocation.Y - A.Y);
                const float ProjectionAlpha = FMath::Clamp(FVector2D::DotProduct(RelativePoint, Segment) / SegmentLengthSq, 0.0f, 1.0f);
                const FVector2D ClosestPoint = A + Segment * ProjectionAlpha;
                NearestDistance = FMath::Min(NearestDistance, FVector2D::Distance(FVector2D(WorldLocation.X, WorldLocation.Y), ClosestPoint));
            }

            PreviousIndex = Index;
        }

        bInsideLake |= bInsideThisSpline;
    }

    bFoundSpline = true;
    return (NearestDistance == TNumericLimits<float>::Max()) ? 0.0f : NearestDistance;
}

FColor AProceduralWaterBiomeSystem::GetDebugColorForBiome(EProceduralWaterBiome Biome) const
{
    switch (Biome)
    {
    case EProceduralWaterBiome::LakeWetShore:
        return FColor(105, 220, 170);
    case EProceduralWaterBiome::LakeShallowWater:
        return FColor::Cyan;
    case EProceduralWaterBiome::LakeLittoralShelf:
        return FColor(40, 135, 255);
    case EProceduralWaterBiome::LakeDeepWater:
        return FColor(25, 25, 255);
    case EProceduralWaterBiome::OceanWetShore:
        return FColor(255, 180, 0);
    case EProceduralWaterBiome::OceanShallowWater:
        return FColor(255, 85, 40);
    case EProceduralWaterBiome::OceanLittoralShelf:
        return FColor::Magenta;
    case EProceduralWaterBiome::OceanDeepWater:
        return FColor(110, 35, 190);
    case EProceduralWaterBiome::DryLand:
    default:
        return FColor::Transparent;
    }
}

FColor AProceduralWaterBiomeSystem::GetDebugColorForWaveEnergy(float WaveEnergy) const
{
    const float ClampedEnergy = FMath::Clamp(WaveEnergy, 0.0f, 1.0f);
    const uint8 Red = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(30.0f, 255.0f, ClampedEnergy)));
    const uint8 Green = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(220.0f, 40.0f, ClampedEnergy)));
    const uint8 Blue = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(255.0f, 30.0f, ClampedEnergy)));
    return FColor(Red, Green, Blue);
}
