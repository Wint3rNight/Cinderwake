// Orientation warping (after the 6d play, 2026-11-25): which of four clips a locked-on movement shows, and the skeleton warp that
// makes its steps follow the real direction while the chest stays toward the target.
#include "Misc/AutomationTest.h"
#include "Player/OrientationWarp.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags WarpTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	using OrientationWarp::EDir;

	// Component-space transforms of one parent chain (index 0 the root).
	TArray<FTransform> ToComponent(const TArray<FTransform>& Local)
	{
		TArray<FTransform> Out;
		for (int32 i = 0; i < Local.Num(); ++i)
		{
			Out.Add(i == 0 ? Local[0] : Local[i] * Out[i - 1]);
		}
		return Out;
	}

	// Feng Mao's reference chain: root, pelvis, spine_01, spine_02, spine_03 (Docs/tools/probe_feng_spine.py).
	TArray<FTransform> FengMaoChain()
	{
		return {
			FTransform::Identity,
			FTransform(FRotator(89.79f, 90.f, 90.f), FVector(0.f, -1.2f, 118.8f)),
			FTransform(FRotator(0.f, -10.71f, 0.f), FVector(8.6f, 1.f, 0.f)),
			FTransform(FRotator(0.f, 0.94f, 0.f), FVector(17.8f, 0.f, 0.f)),
			FTransform(FRotator(0.f, 0.08f, 0.f), FVector(15.6f, 0.f, 0.f)),
		};
	}

	float DegreesBetween(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(A.AngularDistance(B));
	}

	FQuat Yaw(float Degrees)
	{
		return FQuat(FVector::UpVector, FMath::DegreesToRadians(Degrees));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpPickNearest, "BossTest.Player.OrientationWarp.PickNearest", WarpTestFlags)
bool FWarpPickNearest::RunTest(const FString& Parameters)
{
	// From a standstill the nearest clip wins (ties at the diagonals go forward, then right, then left), and the residual is
	// what the warp turns so its steps follow the real direction.
	struct FCase { float Angle; EDir Dir; float Residual; };
	const FCase Cases[] = {
		{ 0.f, EDir::Forward, 0.f }, { 45.f, EDir::Forward, 45.f }, { -45.f, EDir::Forward, -45.f }, { 46.f, EDir::Right, -44.f },
		{ 135.f, EDir::Right, 45.f }, { -135.f, EDir::Left, -45.f }, { 136.f, EDir::Back, -44.f }, { 180.f, EDir::Back, 0.f },
		{ -90.f, EDir::Left, 0.f },
	};
	for (const FCase& C : Cases)
	{
		const OrientationWarp::FPick P = OrientationWarp::Pick(C.Angle, {});
		TestTrue(FString::Printf(TEXT("%.0f deg shows clip %d"), C.Angle, int32(C.Dir)), P.Dir == C.Dir);
		TestEqual(FString::Printf(TEXT("%.0f deg leaves %.0f to warp"), C.Angle, C.Residual), P.Residual, C.Residual, 1e-3f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpPickKeepsWithinBand, "BossTest.Player.OrientationWarp.PickKeepsWithinBand", WarpTestFlags)
bool FWarpPickKeepsWithinBand::RunTest(const FString& Parameters)
{
	// The clip on now stays to 60 degrees off its direction (15 past the diagonal), so a stick held near one doesn't flicker.
	using OrientationWarp::Pick;
	TestTrue(TEXT("forward holds at 60"), Pick(60.f, EDir::Forward).Dir == EDir::Forward);
	TestEqual(TEXT("warping the full 60"), Pick(60.f, EDir::Forward).Residual, 60.f, 1e-3f);
	TestTrue(TEXT("and gives way to the right at 61"), Pick(61.f, EDir::Forward).Dir == EDir::Right);
	TestEqual(TEXT("which warps back 29"), Pick(61.f, EDir::Forward).Residual, -29.f, 1e-3f);
	TestTrue(TEXT("back holds at -125 (55 off, across the wrap)"), Pick(-125.f, EDir::Back).Dir == EDir::Back);
	TestEqual(TEXT("warping 55"), Pick(-125.f, EDir::Back).Residual, 55.f, 1e-3f);
	TestTrue(TEXT("right holds at 30"), Pick(30.f, EDir::Right).Dir == EDir::Right);
	TestTrue(TEXT("but not at 29: forward is nearer"), Pick(29.f, EDir::Right).Dir == EDir::Forward);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpPositiveTurnsRight, "BossTest.Player.OrientationWarp.PositiveTurnsRight", WarpTestFlags)
bool FWarpPositiveTurnsRight::RunTest(const FString& Parameters)
{
	// + is right, as in the movement angle: a clip stepping ahead, warped +90, steps right (Unreal's +Y).
	TArray<FTransform> Root = { FTransform::Identity };
	OrientationWarp::Apply(Root, 1, 90.f, 0.f);
	TestTrue(TEXT("ahead becomes right"), Root[0].GetRotation().RotateVector(FVector::ForwardVector).Equals(FVector::RightVector, 1e-4f));

	// No warp leaves the pose alone.
	TArray<FTransform> Same = FengMaoChain();
	OrientationWarp::Apply(Same, 2, 0.f, 0.5f);
	const TArray<FTransform> Original = FengMaoChain();
	for (int32 i = 0; i < Same.Num(); ++i)
	{
		TestTrue(FString::Printf(TEXT("bone %d untouched at 0"), i), Same[i].Equals(Original[i], 1e-5f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarpTurnsLegsAndHalfTheChest, "BossTest.Player.OrientationWarp.TurnsLegsAndHalfTheChest", WarpTestFlags)
bool FWarpTurnsLegsAndHalfTheChest::RunTest(const FString& Parameters)
{
	const TArray<FTransform> Before = FengMaoChain();
	TArray<FTransform> After = Before;
	OrientationWarp::Apply(After, 2, 40.f, 0.5f);
	const TArray<FTransform> B = ToComponent(Before);
	const TArray<FTransform> A = ToComponent(After);

	// The pelvis, and with it both legs, turns the full 40 degrees about the vertical under his feet.
	TestTrue(FString::Printf(TEXT("pelvis turned 40 (off by %.3f deg)"), DegreesBetween(A[1].GetRotation(), Yaw(40.f) * B[1].GetRotation())),
		DegreesBetween(A[1].GetRotation(), Yaw(40.f) * B[1].GetRotation()) < 0.01f);
	TestTrue(TEXT("pelvis swung round the origin"), A[1].GetTranslation().Equals(Yaw(40.f).RotateVector(B[1].GetTranslation()), 0.01f));

	// The spine turns back half: the chest ends 20 degrees round, upright and as high as before.
	TestTrue(FString::Printf(TEXT("chest turned 20 (off by %.3f deg)"), DegreesBetween(A[4].GetRotation(), Yaw(20.f) * B[4].GetRotation())),
		DegreesBetween(A[4].GetRotation(), Yaw(20.f) * B[4].GetRotation()) < 0.01f);
	TestEqual(TEXT("chest height unchanged"), float(A[4].GetTranslation().Z), float(B[4].GetTranslation().Z), 0.01f);

	// Shared equally: the middle spine bone is between them.
	TestTrue(TEXT("spine_02 a third of the way back"), DegreesBetween(A[3].GetRotation(), Yaw(40.f - 20.f * 2.f / 3.f) * B[3].GetRotation()) < 0.01f);
	return true;
}

#endif
