#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "PlayerAnimInstance.generated.h"

class UPlayerAnimInstance;

// The proxy is the anim instance's worker-thread half: it runs Feng Mao's animation blueprint graph as usual, then warps the
// finished pose (OrientationWarp.h). The angle comes from the game thread once a frame, in PreUpdate.
struct FPlayerAnimInstanceProxy : public FAnimInstanceProxy
{
	FPlayerAnimInstanceProxy() = default;
	explicit FPlayerAnimInstanceProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

protected:
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode) override;

private:
	float WarpAngle = 0.f;
	float ChestCounter = 0.5f;
};

// The parent class of Feng Mao's Paragon animation blueprint (Docs/tools/setup_player.py reparents it), so the blueprint keeps
// its own graph and the warp runs on top. The character sets the angle every tick.
UCLASS(Transient, Blueprintable, BlueprintType)
class BOSSTEST_API UPlayerAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Degrees the skeleton turns about the vertical: + turns it right (the way a +90 movement angle points).
	float WarpAngle = 0.f;

	// How much of that the spine turns back, so the chest stays toward the target (the engine node's default is 0.5).
	float ChestCounter = 0.5f;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FPlayerAnimInstanceProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete static_cast<FPlayerAnimInstanceProxy*>(InProxy); }
};
