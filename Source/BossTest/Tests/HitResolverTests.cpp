// Step 1, Task 4: hit registration and damage resolution (spec section 12).
#include "Misc/AutomationTest.h"
#include "Combat/HitResolver.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags ResolveTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
	constexpr float ResolveTol = 1e-3f;

	FCombatHitContext MakeHit(uint32 Instance, double Time = 0.0)
	{
		FCombatHitContext Hit;
		Hit.AttackInstanceId = Instance;
		Hit.TargetKey = 7;
		Hit.Damage = 35.f;
		Hit.PoiseDamage = 35.f;
		Hit.Time = Time;
		return Hit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveSingleHitOnce, "BossTest.Combat.Resolve.SingleHitOnce", ResolveTestFlags)
bool FResolveSingleHitOnce::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	const FHitOutcome First = ResolveCombatHit(MakeHit(1), Target, Registry);
	TestTrue(TEXT("first contact hits"), First.Result == EHitResult::Hit);
	TestEqual(TEXT("damage dealt"), First.DamageDealt, 35.f, ResolveTol);
	const FHitOutcome Second = ResolveCombatHit(MakeHit(1, 0.05), Target, Registry);
	TestTrue(TEXT("same instance again is invalid"), Second.Result == EHitResult::Invalid);
	TestEqual(TEXT("no double damage"), Target.Health.Current, 265.f, ResolveTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveNewInstanceHitsAgain, "BossTest.Combat.Resolve.NewInstanceHitsAgain", ResolveTestFlags)
bool FResolveNewInstanceHitsAgain::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	ResolveCombatHit(MakeHit(1), Target, Registry);
	TestTrue(TEXT("a new attack instance can hit again"), ResolveCombatHit(MakeHit(2), Target, Registry).Result == EHitResult::Hit);
	TestEqual(TEXT("two hits"), Target.Health.Current, 230.f, ResolveTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveDodgeDoesNotConsume, "BossTest.Combat.Resolve.DodgeDoesNotConsume", ResolveTestFlags)
bool FResolveDodgeDoesNotConsume::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	Target.bInvulnerable = true;
	TestTrue(TEXT("dodged while invulnerable"), ResolveCombatHit(MakeHit(1), Target, Registry).Result == EHitResult::Dodged);
	TestEqual(TEXT("no damage while dodging"), Target.Health.Current, 300.f, ResolveTol);
	TestEqual(TEXT("no poise either"), Target.Poise.Current(), 0.f, ResolveTol);
	Target.bInvulnerable = false;
	TestTrue(TEXT("the same sweep can still land after invulnerability ends"), ResolveCombatHit(MakeHit(1, 0.1), Target, Registry).Result == EHitResult::Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveMultiHitIntervalAndMax, "BossTest.Combat.Resolve.MultiHitIntervalAndMax", ResolveTestFlags)
bool FResolveMultiHitIntervalAndMax::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	auto Multi = [](double Time)
	{
		FCombatHitContext Hit = MakeHit(5, Time);
		Hit.Policy = EHitPolicy::MultiHit;
		Hit.MultiHitInterval = 0.2f;
		Hit.MultiHitMax = 3;
		return Hit;
	};
	TestTrue(TEXT("t=0.0 hits"), ResolveCombatHit(Multi(0.0), Target, Registry).Result == EHitResult::Hit);
	TestTrue(TEXT("t=0.1 is inside the interval"), ResolveCombatHit(Multi(0.1), Target, Registry).Result == EHitResult::Invalid);
	TestTrue(TEXT("t=0.2 hits"), ResolveCombatHit(Multi(0.2), Target, Registry).Result == EHitResult::Hit);
	TestTrue(TEXT("t=0.4 hits"), ResolveCombatHit(Multi(0.4), Target, Registry).Result == EHitResult::Hit);
	TestTrue(TEXT("t=0.6 is past the max of 3"), ResolveCombatHit(Multi(0.6), Target, Registry).Result == EHitResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveImmuneAndDead, "BossTest.Combat.Resolve.ImmuneAndDead", ResolveTestFlags)
bool FResolveImmuneAndDead::RunTest(const FString& Parameters)
{
	FHitRegistry Registry;
	FCombatantState Immune;
	Immune.bImmune = true;
	TestTrue(TEXT("immune"), ResolveCombatHit(MakeHit(1), Immune, Registry).Result == EHitResult::Immune);
	TestEqual(TEXT("immune takes nothing"), Immune.Health.Current, 300.f, ResolveTol);

	FCombatantState Dead;
	Dead.Health.Current = 0.f;
	TestTrue(TEXT("a dead target is invalid"), ResolveCombatHit(MakeHit(2), Dead, Registry).Result == EHitResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveKillBeatsStagger, "BossTest.Combat.Resolve.KillBeatsStagger", ResolveTestFlags)
bool FResolveKillBeatsStagger::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	Target.Health.Current = 30.f;
	Target.Poise.Apply(640.f, false, EArmor::None);
	const FHitOutcome Out = ResolveCombatHit(MakeHit(1), Target, Registry);
	TestTrue(TEXT("killed"), Out.bKilled);
	TestTrue(TEXT("a lethal hit doesn't also stagger"), Out.Poise == EPoiseOutcome::None);
	TestFalse(TEXT("not staggered"), Target.Poise.IsStaggered());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveStaggerBeatsCastBreak, "BossTest.Combat.Resolve.StaggerBeatsCastBreak", ResolveTestFlags)
bool FResolveStaggerBeatsCastBreak::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	Target.Poise.Apply(640.f, false, EArmor::None);
	Target.Cast.Open(70.f);
	Target.Cast.Apply(35.f);
	const FHitOutcome Out = ResolveCombatHit(MakeHit(1), Target, Registry);
	TestTrue(TEXT("the hit staggers"), Out.Poise == EPoiseOutcome::Stagger);
	TestFalse(TEXT("so it isn't reported as a cast break"), Out.bCastBroken);
	TestFalse(TEXT("the cast is closed either way"), Target.Cast.IsOpen());
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveRegistryClear, "BossTest.Combat.Resolve.RegistryClear", ResolveTestFlags)
bool FResolveRegistryClear::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	ResolveCombatHit(MakeHit(1), Target, Registry);
	Registry.Clear(); // encounter reset; instance IDs may be reused afterwards
	TestTrue(TEXT("a reused instance ID hits after a clear"), ResolveCombatHit(MakeHit(1), Target, Registry).Result == EHitResult::Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveKnockdownCarried, "BossTest.Combat.Resolve.KnockdownCarried", ResolveTestFlags)
bool FResolveKnockdownCarried::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	FHitRegistry Registry;
	FCombatHitContext Hit = MakeHit(1);
	Hit.bKnockdown = true;
	TestTrue(TEXT("a landed knockdown hit says so"), ResolveCombatHit(Hit, Target, Registry).bKnockdown);
	FCombatantState Dodging;
	Dodging.bInvulnerable = true;
	FHitRegistry Other;
	TestFalse(TEXT("a dodged one doesn't"), ResolveCombatHit(Hit, Dodging, Other).bKnockdown);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveGuardBlocksFront, "BossTest.Combat.Resolve.GuardBlocksFrontSweeps", ResolveTestFlags)
bool FResolveGuardBlocksFront::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.bGuarding = true;
	FHitRegistry Registry;
	FCombatHitContext Hit = MakeHit(1);
	Hit.bBlockable = true;
	Hit.AngleFromTargetFront = 30.f;
	const FHitOutcome Out = ResolveCombatHit(Hit, Target, Registry);
	TestTrue(TEXT("a sweep from the front is blocked"), Out.Result == EHitResult::Blocked);
	TestEqual(TEXT("30% gets through"), Out.DamageDealt, 10.5f, ResolveTol);
	TestEqual(TEXT("the raw damage is reported for the stamina cost"), Out.BlockedDamage, 35.f, ResolveTol);
	TestEqual(TEXT("poise untouched"), Target.Poise.Current(), 0.f, ResolveTol);
	TestTrue(TEXT("the hit is used up: the same sweep can't land again"), ResolveCombatHit(MakeHit(1, 0.05), Target, Registry).Result == EHitResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveGuardIgnoresBack, "BossTest.Combat.Resolve.GuardIgnoresBackAndUnblockable", ResolveTestFlags)
bool FResolveGuardIgnoresBack::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.bGuarding = true;
	FHitRegistry Registry;
	FCombatHitContext Side = MakeHit(1);
	Side.bBlockable = true;
	Side.AngleFromTargetFront = 70.f;
	TestTrue(TEXT("outside the 60 degree arc: a full hit"), ResolveCombatHit(Side, Target, Registry).Result == EHitResult::Hit);
	FCombatHitContext Projectile = MakeHit(2);
	Projectile.AngleFromTargetFront = 0.f; // bBlockable false
	TestTrue(TEXT("unblockable from the front: a full hit"), ResolveCombatHit(Projectile, Target, Registry).Result == EHitResult::Hit);
	TestEqual(TEXT("both full"), Target.Health.Current, 230.f, ResolveTol);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveChipCanKill, "BossTest.Combat.Resolve.ChipCanKill", ResolveTestFlags)
bool FResolveChipCanKill::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.bGuarding = true;
	Target.Health.Current = 5.f;
	FHitRegistry Registry;
	FCombatHitContext Hit = MakeHit(1);
	Hit.bBlockable = true;
	const FHitOutcome Out = ResolveCombatHit(Hit, Target, Registry);
	TestTrue(TEXT("blocked, and the chip kills"), Out.Result == EHitResult::Blocked && Out.bKilled);
	return true;
}

// ---- Step 4c: the perfect guard ----

namespace
{
	// A sweep from 30 degrees off his front, resolving at a given world time.
	FCombatHitContext MakeGuardableHit(uint32 Instance, double WorldTime)
	{
		FCombatHitContext Hit = MakeHit(Instance);
		Hit.bBlockable = true;
		Hit.AngleFromTargetFront = 30.f;
		Hit.WorldTime = WorldTime;
		return Hit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolvePerfectGuardInside, "BossTest.Combat.Resolve.PerfectGuardInsideWindow", ResolveTestFlags)
bool FResolvePerfectGuardInside::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.GuardPressTime = 10.0; // pressed 0.10 s before the hit: the guard isn't up yet
	FHitRegistry Registry;
	const FHitOutcome Out = ResolveCombatHit(MakeGuardableHit(1, 10.10), Target, Registry);
	TestTrue(TEXT("a perfect guard"), Out.Result == EHitResult::PerfectGuard);
	TestEqual(TEXT("no damage dealt"), Out.DamageDealt, 0.f, ResolveTol);
	TestEqual(TEXT("health untouched"), Target.Health.Current, 300.f, ResolveTol);
	TestEqual(TEXT("the raw damage is reported (stamina cost, poise sent back)"), Out.BlockedDamage, 35.f, ResolveTol);
	TestEqual(TEXT("the defender's poise untouched"), Target.Poise.Current(), 0.f, ResolveTol);
	TestTrue(TEXT("the sweep is used up"), ResolveCombatHit(MakeGuardableHit(1, 10.12), Target, Registry).Result == EHitResult::Invalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolvePerfectGuardEdges, "BossTest.Combat.Resolve.PerfectGuardWindowEdges", ResolveTestFlags)
bool FResolvePerfectGuardEdges::RunTest(const FString& Parameters)
{
	struct FCase { double Age; bool bGuardUp; EHitResult Want; const TCHAR* What; };
	const FCase Cases[] = {
		{ 0.039, false, EHitResult::Hit, TEXT("0.039 s after the press: too late, a hit") },
		{ 0.040, false, EHitResult::PerfectGuard, TEXT("0.04 s: perfect") },
		{ 0.180, true, EHitResult::PerfectGuard, TEXT("0.18 s: perfect (it wins over the block)") },
		{ 0.181, false, EHitResult::Blocked, TEXT("0.181 s: too early, a normal block (even before the defender's own tick has raised bGuarding)") },
	};
	for (const FCase& Case : Cases)
	{
		FCombatantState Target;
		Target.GuardPressTime = 10.0;
		Target.bGuarding = Case.bGuardUp;
		FHitRegistry Registry;
		TestTrue(Case.What, ResolveCombatHit(MakeGuardableHit(1, 10.0 + Case.Age), Target, Registry).Result == Case.Want);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolvePerfectGuardArc, "BossTest.Combat.Resolve.PerfectGuardNeedsArcAndBlockable", ResolveTestFlags)
bool FResolvePerfectGuardArc::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.GuardPressTime = 10.0;
	FHitRegistry Registry;
	FCombatHitContext Side = MakeGuardableHit(1, 10.10);
	Side.AngleFromTargetFront = 70.f;
	TestTrue(TEXT("outside the 60 degree arc: a full hit"), ResolveCombatHit(Side, Target, Registry).Result == EHitResult::Hit);
	FCombatHitContext Unblockable = MakeGuardableHit(2, 10.10);
	Unblockable.bBlockable = false;
	TestTrue(TEXT("unblockable: a full hit"), ResolveCombatHit(Unblockable, Target, Registry).Result == EHitResult::Hit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveNoPressNoPerfect, "BossTest.Combat.Resolve.NoPressNoPerfect", ResolveTestFlags)
bool FResolveNoPressNoPerfect::RunTest(const FString& Parameters)
{
	FCombatantState Target;
	Target.bGuarding = true; // a held guard with no window
	FHitRegistry Registry;
	TestTrue(TEXT("a normal block"), ResolveCombatHit(MakeGuardableHit(1, 10.10), Target, Registry).Result == EHitResult::Blocked);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResolveReflectedPoise, "BossTest.Combat.Resolve.ReflectedPoiseIsADeferrableHit", ResolveTestFlags)
bool FResolveReflectedPoise::RunTest(const FString& Parameters)
{
	// The poise a perfect guard sends back: a 0-damage hit, so the boss's hit handler sees it and a break under
	// his active armour is deferred, never lost.
	FCombatantState Boss;
	Boss.Armor = EArmor::Full;
	Boss.Poise.Apply(640.f, false, EArmor::None); // 10 short of the 650 threshold
	FHitRegistry Registry;
	FCombatHitContext Back;
	Back.AttackInstanceId = 9;
	Back.PoiseDamage = 35.f;
	const FHitOutcome Out = ResolveCombatHit(Back, Boss, Registry);
	TestTrue(TEXT("it resolves as a Hit"), Out.Result == EHitResult::Hit);
	TestEqual(TEXT("with no damage"), Out.DamageDealt, 0.f, ResolveTol);
	TestTrue(TEXT("and his armour defers the break"), Out.Poise == EPoiseOutcome::DeferredStagger);
	return true;
}

#endif
