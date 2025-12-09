// ProceduralWaterPlane.h

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralWaterPlane.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AProceduralLandmass;

UCLASS()
class PCG_EXPLORATION_UE_API AProceduralWaterPlane : public AActor
{
    GENERATED_BODY()

public:
    AProceduralWaterPlane();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // Called by the landmass when its water height changes
    UFUNCTION(BlueprintCallable, Category = "Water")
    void RefreshFromLandmass();

    // Landmass this water plane should follow (now PUBLIC so other classes can read it)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water|Links")
    AProceduralLandmass* LinkedLandmass = nullptr;

protected:
    // Rebuilds the quad mesh
    void BuildWaterPlane();

    // Creates the MID if needed and assigns it to the mesh
    void EnsureMaterialInstance();

    // ---------- Components ----------
    UPROPERTY(VisibleAnywhere, Category = "Water")
    UProceduralMeshComponent* Mesh = nullptr;

    // ---------- Size ----------
    UPROPERTY(EditAnywhere, Category = "Water|Size", meta = (ClampMin = "0.0"))
    float PlaneSizeX = 10000.0f;

    UPROPERTY(EditAnywhere, Category = "Water|Size", meta = (ClampMin = "0.0"))
    float PlaneSizeY = 10000.0f;

    // ---------- Material ----------
    UPROPERTY(EditAnywhere, Category = "Water|Material")
    UMaterialInterface* WaterMaterial = nullptr;

    UPROPERTY(Transient)
    UMaterialInstanceDynamic* WaterMID = nullptr;

    // ---------- Wave motion ----------
    UPROPERTY(EditAnywhere, Category = "Water|Waves")
    float WaveSpeed1 = 0.15f;

    UPROPERTY(EditAnywhere, Category = "Water|Waves")
    float WaveSpeed2 = -0.12f;

    float InternalTime = 0.0f;
};
