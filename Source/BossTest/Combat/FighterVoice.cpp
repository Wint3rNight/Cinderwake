#include "Combat/FighterVoice.h"

#include "Combat/AttackComponent.h"
#include "Combat/AttackDefinition.h"
#include "Combat/CombatantComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

int32 FVoiceGate::Priority(EVoiceEvent E)
{
	switch (E)
	{
	case EVoiceEvent::Death: return 3;
	case EVoiceEvent::Pain:
	case EVoiceEvent::HeavyPain: return 2;
	default: return 1;
	}
}

bool FVoiceGate::Allow(EVoiceEvent E, double Now, bool& bInterrupt) const
{
	bInterrupt = false;
	const bool bPain = E == EVoiceEvent::Pain || E == EVoiceEvent::HeavyPain;
	if (bPain && Now - LastPainAt < PainGap)
	{
		return false;
	}
	if (Now < PlayingUntil)
	{
		bInterrupt = Priority(E) > PlayingPriority;
		return bInterrupt; // only something more important cuts a line short
	}
	return E == EVoiceEvent::Death || Now - LastAt >= MinGap;
}

void FVoiceGate::Note(EVoiceEvent E, double Now, float Length)
{
	LastAt = Now;
	if (E == EVoiceEvent::Pain || E == EVoiceEvent::HeavyPain)
	{
		LastPainAt = Now;
	}
	PlayingUntil = Now + FMath::Max(0.f, Length);
	PlayingPriority = Priority(E);
}

UFighterVoiceComponent::UFighterVoiceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFighterVoiceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UCombatantComponent* Combatant = GetOwner()->FindComponentByClass<UCombatantComponent>())
	{
		Combatant->OnHitResolved.AddUObject(this, &UFighterVoiceComponent::OnHitResolved);
	}
}

void UFighterVoiceComponent::ResetVoice()
{
	if (Current)
	{
		Current->Stop();
		Current = nullptr;
	}
	Gate.Reset();
	LastAttack = nullptr;
	bEffortTried = false;
}

void UFighterVoiceComponent::FadeOut(EVoiceEvent Event, float Seconds)
{
	if (Current && CurrentEvent == Event && Current->IsPlaying())
	{
		Current->FadeOut(Seconds, 0.f);
	}
}

USoundBase* UFighterVoiceComponent::SoundFor(EVoiceEvent Event) const
{
	switch (Event)
	{
	case EVoiceEvent::Effort: return Voices.Effort;
	case EVoiceEvent::HeavyEffort: return Voices.HeavyEffort ? Voices.HeavyEffort.Get() : Voices.Effort.Get();
	case EVoiceEvent::Pain: return Voices.Pain;
	case EVoiceEvent::HeavyPain: return Voices.HeavyPain ? Voices.HeavyPain.Get() : Voices.Pain.Get();
	case EVoiceEvent::Death: return Voices.Death;
	case EVoiceEvent::Block: return Voices.Block;
	case EVoiceEvent::Laugh: return Voices.Laugh;
	default: return nullptr;
	}
}

bool UFighterVoiceComponent::Play(EVoiceEvent Event)
{
	USoundBase* Sound = SoundFor(Event);
	const double Now = GetWorld()->GetTimeSeconds();
	bool bInterrupt = false;
	if (!Sound || !Gate.Allow(Event, Now, bInterrupt))
	{
		return false;
	}
	if (bInterrupt && Current)
	{
		Current->Stop();
	}
	// Long cues (and Paragon's random ones) report odd durations; 1.5 s is longer than any effort or pain line.
	const float Length = FMath::Min(Sound->GetDuration(), 1.5f);
	Gate.Note(Event, Now, Length);
	CurrentEvent = Event;
	++Played;
	++PlayedByEvent[static_cast<int32>(Event)];
	USceneComponent* At = GetOwner()->GetRootComponent();
	FName Socket = NAME_None;
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		At = Character->GetMesh();
		Socket = TEXT("head");
	}
	Current = UGameplayStatics::SpawnSoundAttached(Sound, At, Socket); // null without an audio device (headless tests)
	return true;
}

void UFighterVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// An effort just before a swing goes live (after any hold), once per step: the grunt leads the blade, as a real swing's
	// breath does, instead of landing on the impact.
	const UAttackComponent* Attacks = GetOwner()->FindComponentByClass<UAttackComponent>();
	const UAttackDefinition* Def = Attacks ? Attacks->GetCurrent() : nullptr;
	if (Def != LastAttack)
	{
		bEffortTried = false;
		LastAttack = Def;
	}
	if (Def && !bEffortTried && Def->SocketEnd != NAME_None)
	{
		const float LiveAt = Def->GetTiming().Windup;
		if (Attacks->GetElapsed() >= FMath::Max(0.f, LiveAt - Voices.EffortLead))
		{
			bEffortTried = true;
			if (Def->bHeavy)
			{
				Play(EVoiceEvent::HeavyEffort);
			}
			else if (FMath::FRand() < Voices.EffortChance)
			{
				Play(EVoiceEvent::Effort);
			}
		}
	}
}

void UFighterVoiceComponent::OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker)
{
	if (Outcome.Result == EHitResult::Hit)
	{
		if (Outcome.bKilled)
		{
			Play(EVoiceEvent::Death);
		}
		else if (Outcome.bKnockdown || Outcome.Poise == EPoiseOutcome::Stagger)
		{
			Play(EVoiceEvent::HeavyPain);
		}
		else if (!Voices.bPainOnlyOnBigHits || Outcome.DamageDealt >= Voices.BigHit)
		{
			Play(EVoiceEvent::Pain);
		}
	}
	else if (Outcome.Result == EHitResult::Blocked)
	{
		Play(EVoiceEvent::Block);
	}
}
