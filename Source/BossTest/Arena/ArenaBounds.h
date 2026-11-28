// Step 6b: the arena as gameplay sees it (spec section 14). A ring of invisible boxes at WallRadius stops both fighters and a
// ragdoll, never the camera or a line of sight, and its centre and AIRadius are where the boss's movement stays (the wall at
// 14 m, movement targets clamped to 13 m). The look is separate: Docs/tools/build_arena.py places Paragon pieces around it,
// all without collision, so only our floor and this wall can catch anyone.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaBounds.generated.h"

class UBoxComponent;

namespace ArenaRing
{
	struct FSegment
	{
		FVector Centre = FVector::ZeroVector; // relative to the arena's centre, at half the wall's height
		float Yaw = 0.f;                      // degrees; the box's X axis points out from the centre
		FVector Extent = FVector::ZeroVector; // half sizes: X across the wall, Y along it, Z up
	};

	// The Index-th of Count flat boxes making a closed ring: inner faces at InnerRadius, each long enough that neighbours meet
	// even at the outer face, plus Overlap cm at both ends so no capsule finds a seam.
	BOSSTEST_API FSegment Segment(int32 Index, int32 Count, float InnerRadius, float Thickness, float Height, float Overlap = 10.f);
}

UCLASS()
class BOSSTEST_API AArenaBounds : public AActor
{
	GENERATED_BODY()

public:
	AArenaBounds();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ToolTip = "cm from the centre to the wall's inner face (section 14: 14 m)"))
	float WallRadius = 1400.f;

	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ToolTip = "cm from the centre: the boss's movement targets stay inside (section 14: 13 m)"))
	float AIRadius = 1300.f;

	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ToolTip = "cm; tall enough that no leap or knock clears it"))
	float WallHeight = 1200.f;

	UPROPERTY(EditAnywhere, Category = "Arena", meta = (ToolTip = "cm"))
	float WallThickness = 100.f;

	static constexpr int32 SegmentCount = 48;

	// The floor-level centre of the arena (the actor sits on the floor).
	FVector GetCentre() const { return GetActorLocation(); }

	// The arena in this world, if one is placed.
	static AArenaBounds* Find(const UWorld* World);

private:
	void LayoutWall();

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TArray<TObjectPtr<UBoxComponent>> Wall;
};
