#include "ProceduralWaterBiomeSystem.h"

#include "Components/SplineComponent.h"
#include "DrawDebugHelpers.h"
#include "ProceduralLandmass.h"

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

float AProceduralWaterBiomeSystem::GetWaterSurfaceZ() const
{
    if (bUseWaterBodyLakeActorHeight && WaterBodyLakeActor)
    {
        return WaterBodyLakeActor->GetActorLocation().Z + WaterSurfaceZOffset;
    }

    if (LandmassActor)
    {
        return LandmassActor->GetDefaultWaterSurfaceZ() + WaterSurfaceZOffset;
    }

    return GetActorLocation().Z + WaterSurfaceZOffset;
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

    if (WaterDepth <= 0.0f)
    {
        return (FMath::Abs(WaterDepth) <= ShorelineBandHeight)
            ? EProceduralWaterBiome::WetShore
            : EProceduralWaterBiome::DryLand;
    }

    if (WaterDepth <= ShallowWaterDepth)
    {
        return EProceduralWaterBiome::ShallowWater;
    }

    if (WaterDepth <= LittoralShelfDepth)
    {
        return EProceduralWaterBiome::LittoralShelf;
    }

    return EProceduralWaterBiome::DeepWater;
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

            DrawDebugPoint(
                GetWorld(),
                DrawLocation,
                DebugPointSize,
                GetDebugColorForBiome(Biome),
                false,
                0.0f);
        }
    }
}

bool AProceduralWaterBiomeSystem::IsInsideDebugWaterBounds(const FVector& WorldLocation) const
{
    if (!bLimitDebugToWaterBodyBounds || !WaterBodyLakeActor)
    {
        return true;
    }

    bool bFoundSpline = false;
    if (bPreferWaterBodySplineBounds)
    {
        const bool bInsideSpline = IsInsideAnyWaterSpline(WorldLocation, bFoundSpline);
        if (bFoundSpline)
        {
            return bInsideSpline;
        }
    }

    FBox Bounds = WaterBodyLakeActor->GetComponentsBoundingBox(true);
    if (!Bounds.IsValid)
    {
        return true;
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

    if (!WaterBodyLakeActor)
    {
        return false;
    }

    TArray<USplineComponent*> SplineComponents;
    WaterBodyLakeActor->GetComponents<USplineComponent>(SplineComponents);

    for (const USplineComponent* SplineComponent : SplineComponents)
    {
        if (!SplineComponent || SplineComponent->GetNumberOfSplinePoints() < 3)
        {
            continue;
        }

        bFoundSpline = true;

        const int32 NumSamples = FMath::Max(SplineComponent->GetNumberOfSplinePoints() * 8, 32);
        TArray<FVector2D> Points;
        Points.Reserve(NumSamples);

        const float SplineLength = SplineComponent->GetSplineLength();
        if (SplineLength <= KINDA_SMALL_NUMBER)
        {
            continue;
        }

        for (int32 Index = 0; Index < NumSamples; ++Index)
        {
            const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples);
            const FVector Point = SplineComponent->GetLocationAtDistanceAlongSpline(SplineLength * Alpha, ESplineCoordinateSpace::World);
            Points.Add(FVector2D(Point.X, Point.Y));
        }

        bool bInside = false;
        int32 PreviousIndex = Points.Num() - 1;
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const FVector2D& A = Points[Index];
            const FVector2D& B = Points[PreviousIndex];

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

FColor AProceduralWaterBiomeSystem::GetDebugColorForBiome(EProceduralWaterBiome Biome) const
{
    switch (Biome)
    {
    case EProceduralWaterBiome::WetShore:
        return FColor(255, 180, 0);
    case EProceduralWaterBiome::ShallowWater:
        return FColor::Cyan;
    case EProceduralWaterBiome::LittoralShelf:
        return FColor::Magenta;
    case EProceduralWaterBiome::DeepWater:
        return FColor(25, 25, 255);
    case EProceduralWaterBiome::DryLand:
    default:
        return FColor::Transparent;
    }
}
