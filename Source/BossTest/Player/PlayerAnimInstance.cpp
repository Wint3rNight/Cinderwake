#include "Player/PlayerAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Player/OrientationWarp.h"

void FPlayerAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UPlayerAnimInstance* Instance = Cast<UPlayerAnimInstance>(InAnimInstance))
	{
		WarpAngle = Instance->WarpAngle;
		ChestCounter = Instance->ChestCounter;
	}
}

bool FPlayerAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	EvaluateAnimationNode_WithRoot(Output, InRootNode); // the blueprint's own graph, slots and all
	// Only the main graph's pose: a linked layer evaluates its own root through here too, and must not be warped twice.
	if (InRootNode != GetRootNode() || FMath::Abs(WarpAngle) < 0.01f)
	{
		return true;
	}

	// Root, pelvis, then the three spine bones: the chain from the origin to the chest (probed in Feng Mao's skeleton).
	static const FName Names[] = { TEXT("root"), TEXT("pelvis"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03") };
	constexpr int32 Count = UE_ARRAY_COUNT(Names);
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FCompactPoseBoneIndex Index[Count] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE),
		FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE) };
	FTransform Chain[Count];
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(Names[i]);
		if (MeshIndex == INDEX_NONE)
		{
			return true;
		}
		Index[i] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Index[i] == INDEX_NONE)
		{
			return true; // not in this level of detail
		}
		Chain[i] = Output.Pose[Index[i]];
	}
	OrientationWarp::Apply(Chain, 2, WarpAngle, ChestCounter);
	for (int32 i = 0; i < Count; ++i)
	{
		Output.Pose[Index[i]] = Chain[i];
	}
	return true;
}
