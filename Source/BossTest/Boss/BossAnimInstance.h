#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BossAnimInstance.generated.h"

// Sevarog's anim instance proxy: runs his Paragon animation blueprint's graph as usual, then adds the hit flinch
// (HitFlinch.h) and the held swing's strain to the finished pose. The tilt comes from the game thread once a frame, in PreUpdate.
struct FBossAnimInstanceProxy : public FAnimInstanceProxy
{
	FBossAnimInstanceProxy() = default;
	explicit FBossAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

private:
	FVector FlinchAxis = FVector::ZeroVector;
	float FlinchAngle = 0.f;
	FVector StrainAxis = FVector::ZeroVector;
	float StrainAngle = 0.f;
};

// The parent class of Sevarog's Paragon animation blueprint (Docs/tools/setup_feel.py reparents it), so the blueprint keeps
// its own graph and the flinch runs on top. ABossCharacter sets the tilt every tick.
UCLASS(Transient, Blueprintable, BlueprintType)
class BOSSTEST_API UBossAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	FVector FlinchAxis = FVector::ZeroVector; // component space
	float FlinchAngle = 0.f;                  // degrees
	FVector StrainAxis = FVector::ZeroVector; // the held swing's lean and tremble (HitFlinch::HoldStrain)
	float StrainAngle = 0.f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FBossAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FBossAnimInstanceProxy*>(InProxy); }
};
