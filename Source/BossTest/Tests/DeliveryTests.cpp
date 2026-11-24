// Step 5, Task 2: the shockwave ring and the projectile (spec section 12, delivery types).
#include "Misc/AutomationTest.h"
#include "Combat/Delivery.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags DeliveryTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryRingPasses, "BossTest.Combat.Delivery.RingPassesOnce", DeliveryTestFlags)
bool FDeliveryRingPasses::RunTest(const FString& Parameters)
{
	// Judgment Slam's ring: 10 m/s from the floor contact out to 5 m. A target 3 m out (capsule radius 40 cm) is
	// touched only while the edge crosses its capsule, 2.6-3.4 m, so a dodge timed to the ring beats it.
	FShockwaveRing Ring;
	Ring.Speed = 1000.f;
	Ring.MaxRadius = 500.f;
	TArray<int32> Touched;
	for (int32 i = 0; i < 60 && !Ring.IsDone(); ++i)
	{
		const FRingBand Band = Ring.Advance(1.f / 60.f);
		if (Band.Touches(300.f, 40.f))
		{
			Touched.Add(i);
		}
	}
	TestTrue(TEXT("it reaches the target"), Touched.Num() > 0);
	TestTrue(FString::Printf(TEXT("only while its edge crosses the capsule (%d steps of 1/60 s, expect 5 or 6)"), Touched.Num()), Touched.Num() >= 5 && Touched.Num() <= 6);
	TestTrue(TEXT("the first touch is about 0.26 s in"), Touched.Num() > 0 && FMath::Abs((Touched[0] + 1) / 60.f - 0.26f) < 0.02f);
	TestTrue(TEXT("done at 5 m"), Ring.IsDone() && FMath::IsNearlyEqual(Ring.Radius, 500.f));
	FShockwaveRing Far;
	Far.Speed = 1000.f;
	Far.MaxRadius = 500.f;
	bool bFarTouched = false;
	while (!Far.IsDone())
	{
		bFarTouched |= Far.Advance(1.f / 60.f).Touches(600.f, 40.f);
	}
	TestFalse(TEXT("a target 6 m out is never reached"), bFarTouched);
	FShockwaveRing Big;
	Big.Speed = 1000.f;
	Big.MaxRadius = 500.f;
	TestTrue(TEXT("a long frame still covers the whole band it swept"), Big.Advance(0.5f).Touches(300.f, 40.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryRingSkips, "BossTest.Combat.Delivery.RingSkipsDirectHitAndDodge", DeliveryTestFlags)
bool FDeliveryRingSkips::RunTest(const FString& Parameters)
{
	// One slam, one dodge: the ring skips anyone the direct strike already hit or who dodged it.
	FShockwaveRing Ring;
	Ring.Skip.Add(7);
	TestFalse(TEXT("the direct strike's target is skipped"), Ring.CanHit(7));
	TestTrue(TEXT("anyone else can be hit"), Ring.CanHit(8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryProjectile, "BossTest.Combat.Delivery.ProjectileTravelsAndHits", DeliveryTestFlags)
bool FDeliveryProjectile::RunTest(const FString& Parameters)
{
	// Soul Mark: 14 m/s, aimed when it's released, never steered after.
	FProjectileFlight P;
	P.Position = FVector(0.f, 0.f, 150.f);
	P.Velocity = FVector(1400.f, 0.f, 0.f);
	P.MaxRange = 1500.f;
	const FVector From = P.Position;
	const FVector To = P.Advance(0.5f);
	TestEqual(TEXT("0.5 s: 7 m along"), To.X, 700.0, 0.1);
	TestEqual(TEXT("it remembers how far it's gone"), P.Travelled, 700.f, 0.1f);
	// A capsule (radius 40, half height 90) standing 9 m out: the 0.5-1.0 s step passes through it.
	const FVector Capsule(900.f, 0.f, 90.f);
	TestFalse(TEXT("the first step stops short of it"), SegmentHitsCapsule(From, To, 20.f, Capsule, 40.f, 90.f));
	const FVector Before = P.Position;
	const FVector After = P.Advance(0.5f);
	TestTrue(TEXT("the next step passes through it"), SegmentHitsCapsule(Before, After, 20.f, Capsule, 40.f, 90.f));
	TestFalse(TEXT("70 cm to the side: a miss"), SegmentHitsCapsule(Before, After, 20.f, Capsule + FVector(0.f, 70.f, 0.f), 40.f, 90.f));
	TestTrue(TEXT("59 cm to the side: a graze still hits"), SegmentHitsCapsule(Before, After, 20.f, Capsule + FVector(0.f, 59.f, 0.f), 40.f, 90.f));
	TestFalse(TEXT("over his head: a miss"), SegmentHitsCapsule(Before + FVector(0, 0, 150), After + FVector(0, 0, 150), 20.f, Capsule, 40.f, 90.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDeliveryProjectileRange, "BossTest.Combat.Delivery.ProjectileStopsAtRange", DeliveryTestFlags)
bool FDeliveryProjectileRange::RunTest(const FString& Parameters)
{
	FProjectileFlight P;
	P.Velocity = FVector(1400.f, 0.f, 0.f);
	P.MaxRange = 1500.f;
	int32 Steps = 0;
	while (!P.IsSpent() && Steps < 1000)
	{
		P.Advance(1.f / 60.f);
		++Steps;
	}
	TestTrue(TEXT("spent at its range"), P.IsSpent());
	TestEqual(TEXT("never past it"), P.Travelled, 1500.f, 0.1f);
	TestEqual(TEXT("about 1.07 s of flight"), Steps / 60.f, 1500.f / 1400.f, 0.02f);
	return true;
}

#endif
