// The feel pass after Step 7: the flinch on Sevarog when your hit lands.
#include "Misc/AutomationTest.h"
#include "Boss/HitFlinch.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags FlinchTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// A chain like his: root at the origin, pelvis up 100, each spine bone up 40, all unrotated, plus a turn at the root so the
	// test doesn't only pass for an identity skeleton.
	void MakeChain(FTransform (&Chain)[5])
	{
		Chain[0] = FTransform(FRotator(0.f, 90.f, 0.f), FVector::ZeroVector);
		Chain[1] = FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 100.f));
		for (int32 i = 2; i < 5; ++i)
		{
			Chain[i] = FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 40.f));
		}
	}

	FTransform CompOf(const FTransform (&Chain)[5], int32 Bone)
	{
		FTransform T = Chain[0];
		for (int32 i = 1; i <= Bone; ++i)
		{
			T = Chain[i] * T;
		}
		return T;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlinchEnvelope, "BossTest.Boss.Flinch.EnvelopeRisesFastAndSettles", FlinchTestFlags)
bool FFlinchEnvelope::RunTest(const FString& Parameters)
{
	// Up over the rise, back to rest over the fall, nothing before or after; a new hit carries on from the current tilt.
	TestEqual(TEXT("at the hit: 0"), HitFlinch::Envelope(0.f, 10.f, 0.05f, 0.3f), 0.f, 0.001f);
	TestEqual(TEXT("at the end of the rise: the full tilt"), HitFlinch::Envelope(0.05f, 10.f, 0.05f, 0.3f), 10.f, 0.001f);
	TestTrue(TEXT("halfway down: between"), FMath::IsNearlyEqual(HitFlinch::Envelope(0.2f, 10.f, 0.05f, 0.3f), 5.f, 0.01f));
	TestEqual(TEXT("after the fall: rest"), HitFlinch::Envelope(0.36f, 10.f, 0.05f, 0.3f), 0.f);
	TestEqual(TEXT("before the hit: rest"), HitFlinch::Envelope(-0.1f, 10.f, 0.05f, 0.3f), 0.f);
	const float From = HitFlinch::RiseTimeFor(6.f, 12.f, 0.05f);
	TestEqual(TEXT("a bigger hit mid-flinch carries on from the current tilt"), HitFlinch::Envelope(From, 12.f, 0.05f, 0.3f), 6.f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlinchTilt, "BossTest.Boss.Flinch.TiltsTheChestAwayFromTheHit", FlinchTestFlags)
bool FFlinchTilt::RunTest(const FString& Parameters)
{
	// A hit travelling along +X tilts the top of the spine toward +X by the angle; the pelvis and spine_01 stay put, and spine_02
	// takes the lower share.
	FTransform Chain[5];
	MakeChain(Chain);
	const FVector Pelvis = CompOf(Chain, 1).GetLocation();
	const FVector Spine1 = CompOf(Chain, 2).GetLocation();
	const FVector Axis = HitFlinch::AxisFor(FVector(1.f, 0.f, 0.f));
	HitFlinch::Apply(Chain, Axis, 10.f, 0.4f);
	TestTrue(TEXT("the pelvis doesn't move"), CompOf(Chain, 1).GetLocation().Equals(Pelvis, 0.01f));
	TestTrue(TEXT("nor spine_01"), CompOf(Chain, 2).GetLocation().Equals(Spine1, 0.01f));
	const FVector Up3 = CompOf(Chain, 4).GetRotation().RotateVector(FVector::UpVector);
	const float Tilt = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Up3.X, Up3.Z)));
	TestEqual(TEXT("spine_03 leans 10 degrees along the hit"), Tilt, 10.f, 0.05f);
	TestEqual(TEXT("and not sideways"), static_cast<float>(Up3.Y), 0.f, 0.001f);
	const FVector Up2 = CompOf(Chain, 3).GetRotation().RotateVector(FVector::UpVector);
	TestEqual(TEXT("spine_02 leans the lower 40%"), static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Up2.X, Up2.Z))), 4.f, 0.05f);
	TestTrue(TEXT("spine_03's position rides on spine_02's lean"), CompOf(Chain, 4).GetLocation().X > 0.1f);
	FTransform Still[5];
	MakeChain(Still);
	HitFlinch::Apply(Still, HitFlinch::AxisFor(FVector::UpVector), 10.f);
	TestTrue(TEXT("a vertical hit has no axis and moves nothing"), CompOf(Still, 4).GetRotation().Equals(FQuat(FRotator(0.f, 90.f, 0.f)), 0.0001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFlinchHoldStrain, "BossTest.Boss.Flinch.HoldStrainLeansAndTrembles", FlinchTestFlags)
bool FFlinchHoldStrain::RunTest(const FString& Parameters)
{
	// A held swing leans back to its full lean over the lean time and trembles around it, never by more than the tremble;
	// nothing at the hold's start, so there's no pop as the hold begins.
	TestEqual(TEXT("at the hold's start: nothing"), HitFlinch::HoldStrain(0.f, 3.f, 0.6f, 0.8f), 0.f);
	TestTrue(TEXT("a moment in: barely anything"), FMath::Abs(HitFlinch::HoldStrain(0.02f, 3.f, 0.6f, 0.8f)) < 0.15f);
	float Sum = 0.f;
	float Most = 0.f;
	float Least = 100.f;
	int32 Count = 0;
	for (float T = 0.6f; T < 1.6f; T += 0.005f, ++Count)
	{
		const float A = HitFlinch::HoldStrain(T, 3.f, 0.6f, 0.8f);
		Sum += A;
		Most = FMath::Max(Most, A);
		Least = FMath::Min(Least, A);
	}
	TestTrue(FString::Printf(TEXT("after the lean time it sits around the full lean (%.2f)"), Sum / Count), FMath::Abs(Sum / Count - 3.f) < 0.15f);
	TestTrue(FString::Printf(TEXT("and trembles within 0.8 of it (%.2f to %.2f)"), Least, Most), Most <= 3.8f + 0.001f && Least >= 2.2f - 0.001f);
	TestTrue(TEXT("it does tremble"), Most - Least > 0.5f);
	return true;
}

#endif
