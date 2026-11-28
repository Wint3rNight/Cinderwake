// Step 6b: the arena wall's ring of boxes (spec section 14: a solid invisible wall at 14 m).
#include "Misc/AutomationTest.h"
#include "Arena/ArenaBounds.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ArenaTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// Is the point (on the floor plane, relative to the centre) inside segment S's box, seen from above?
	bool InsideFlat(const ArenaRing::FSegment& S, const FVector& Point)
	{
		const FVector Local = FRotator(0.f, S.Yaw, 0.f).UnrotateVector(FVector(Point.X - S.Centre.X, Point.Y - S.Centre.Y, 0.f));
		return FMath::Abs(Local.X) <= S.Extent.X + 0.01f && FMath::Abs(Local.Y) <= S.Extent.Y + 0.01f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaRingFacesTheCentre, "BossTest.Arena.RingFacesTheCentre", ArenaTestFlags)
bool FArenaRingFacesTheCentre::RunTest(const FString& Parameters)
{
	// Each box's inner face sits on the wall radius, straight across from the centre, and the wall stands on the floor.
	for (int32 i = 0; i < AArenaBounds::SegmentCount; ++i)
	{
		const ArenaRing::FSegment S = ArenaRing::Segment(i, AArenaBounds::SegmentCount, 1400.f, 100.f, 1200.f);
		const float Inner = S.Centre.Size2D() - S.Extent.X;
		TestEqual(FString::Printf(TEXT("box %d's inner face at 14 m"), i), Inner, 1400.f, 0.01f);
		TestEqual(FString::Printf(TEXT("box %d stands on the floor"), i), float(S.Centre.Z - S.Extent.Z), 0.f, 0.01f);
		const float Facing = FMath::RadiansToDegrees(FMath::Atan2(S.Centre.Y, S.Centre.X));
		TestEqual(FString::Printf(TEXT("box %d turned to face the centre"), i), FMath::FindDeltaAngleDegrees(S.Yaw, Facing), 0.f, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaRingHasNoGaps, "BossTest.Arena.RingHasNoGaps", ArenaTestFlags)
bool FArenaRingHasNoGaps::RunTest(const FString& Parameters)
{
	// Every point of the wall's band, all the way round, is inside some box, so no capsule can find a seam between two of them.
	// The flat boxes make a 48-sided polygon: its inner face is at 14 m mid-box and a little further out where two boxes meet
	// (14 m / cos(3.75 deg)), so the band is sampled from there to the outer face, every half degree.
	constexpr int32 Count = AArenaBounds::SegmentCount;
	const float Corner = 1400.f / FMath::Cos(PI / Count);
	TestTrue(FString::Printf(TEXT("the wall is never more than 3.5 cm beyond 14 m (%.1f at the joins)"), Corner), Corner <= 1403.5f);
	TArray<ArenaRing::FSegment> Ring;
	for (int32 i = 0; i < Count; ++i)
	{
		Ring.Add(ArenaRing::Segment(i, Count, 1400.f, 100.f, 1200.f));
	}
	int32 Holes = 0;
	for (int32 Step = 0; Step < 720; ++Step)
	{
		const float Angle = FMath::DegreesToRadians(Step * 0.5f);
		for (const float Radius : { Corner + 0.5f, 1450.f, 1499.5f })
		{
			const FVector P(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			bool bCovered = false;
			for (const ArenaRing::FSegment& S : Ring)
			{
				bCovered |= InsideFlat(S, P);
			}
			Holes += bCovered ? 0 : 1;
		}
	}
	TestEqual(TEXT("no point of the wall's band is uncovered"), Holes, 0);

	// And it doesn't bulge inward: the arena floor just inside the wall is free.
	int32 Intrusions = 0;
	for (int32 Step = 0; Step < 720; ++Step)
	{
		const float Angle = FMath::DegreesToRadians(Step * 0.5f);
		const FVector P(FMath::Cos(Angle) * 1399.f, FMath::Sin(Angle) * 1399.f, 0.f);
		for (const ArenaRing::FSegment& S : Ring)
		{
			Intrusions += InsideFlat(S, P) ? 1 : 0;
		}
	}
	TestEqual(TEXT("nothing reaches inside 14 m"), Intrusions, 0);
	return true;
}

#endif
