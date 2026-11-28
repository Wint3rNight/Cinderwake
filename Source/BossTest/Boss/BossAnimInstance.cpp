#include "Boss/BossAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "Boss/HitFlinch.h"

void FBossAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UBossAnimInstance* Instance = Cast<UBossAnimInstance>(InAnimInstance))
	{
		FlinchAxis = Instance->FlinchAxis;
		FlinchAngle = Instance->FlinchAngle;
		StrainAxis = Instance->StrainAxis;
		StrainAngle = Instance->StrainAngle;
	}
}

bool FBossAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	EvaluateAnimationNode_WithRoot(Output, InRootNode); // the blueprint's own graph, slots and all
	// Only the main graph's pose (a linked layer evaluates its own root through here too), and only while flinching.
	if (InRootNode != GetRootNode() || (FMath::Abs(FlinchAngle) < 0.01f && FMath::Abs(StrainAngle) < 0.01f))
	{
		return true;
	}
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
	HitFlinch::Apply(Chain, FlinchAxis, FlinchAngle);
	HitFlinch::Apply(Chain, StrainAxis, StrainAngle); // the second tilt composes on the first
	Output.Pose[Index[3]] = Chain[3];
	Output.Pose[Index[4]] = Chain[4];
	return true;
}
