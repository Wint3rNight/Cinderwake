// The fighters' voices (the feel pass after Step 7, 2026-11-29): each hero's own effort, pain and death lines, played on what
// the fight does. The component listens to its owner's attack (an effort as a swing goes live) and combatant (pain, death,
// a block); FVoiceGate's plain rules keep lines from stacking into noise.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/AttackTimeline.h"
#include "FighterVoice.generated.h"

class UAudioComponent;
class USoundBase;
struct FHitOutcome;

enum class EVoiceEvent : uint8 { Effort, HeavyEffort, Pain, HeavyPain, Death, Block, Laugh };

// Which line may play now. A line still playing is only cut by a more important one (death over pain over the rest); between
// lines there's a short gap, and pains have their own longer one so a flurry of hits doesn't become a stutter.
struct BOSSTEST_API FVoiceGate
{
	float MinGap = 0.35f;
	float PainGap = 0.6f;

	static int32 Priority(EVoiceEvent E);
	// May E play at Now? bInterrupt says it cuts the line still playing.
	bool Allow(EVoiceEvent E, double Now, bool& bInterrupt) const;
	void Note(EVoiceEvent E, double Now, float Length);
	void Reset() { *this = FVoiceGate{ MinGap, PainGap }; }

	double LastAt = -100.0;
	double LastPainAt = -100.0;
	double PlayingUntil = -100.0;
	int32 PlayingPriority = 0;
};

// One hero's lines (Paragon's cues each pick among their own takes) and how talkative he is.
USTRUCT()
struct FFighterVoices
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "As a light or ordinary swing goes live"))
	TObjectPtr<USoundBase> Effort;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "As a heavy swing goes live"))
	TObjectPtr<USoundBase> HeavyEffort;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "A hit that hurts"))
	TObjectPtr<USoundBase> Pain;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "A knockdown or a stagger"))
	TObjectPtr<USoundBase> HeavyPain;

	UPROPERTY(EditAnywhere, Category = "Voice")
	TObjectPtr<USoundBase> Death;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "A hit stopped by the guard"))
	TObjectPtr<USoundBase> Block;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Sevarog's aura circle"))
	TObjectPtr<USoundBase> Laugh;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Chance an ordinary swing gets an effort (heavies always do): every swing grunting gets old fast"))
	float EffortChance = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Only staggers and hits of at least Big Hit hurt audibly (Sevarog: armour, and a boss who grunts at every scratch sounds weak)"))
	bool bPainOnlyOnBigHits = false;

	UPROPERTY(EditAnywhere, Category = "Voice")
	float BigHit = 100.f;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Seconds before the blade goes live that the effort starts, so the grunt leads the swing instead of landing on the impact"))
	float EffortLead = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Seconds between any two lines (2026-11-29: they came too often at 0.35)"))
	float MinGap = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Voice", meta = (ToolTip = "Seconds between two pains"))
	float PainGap = 2.0f;
};

UCLASS(ClassGroup = (BossTest), meta = (BlueprintSpawnableComponent))
class BOSSTEST_API UFighterVoiceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFighterVoiceComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	void SetVoices(const FFighterVoices& InVoices) { Voices = InVoices; Gate.MinGap = InVoices.MinGap; Gate.PainGap = InVoices.PainGap; }
	// Plays the event's line if the gate allows it. True when it was allowed (even with no audio device, as in tests).
	bool Play(EVoiceEvent Event);
	void ResetVoice();
	// Fades out the line still playing if it's this event's (the laugh when the aura circle ends).
	void FadeOut(EVoiceEvent Event, float Seconds);

	// Tests and the debug view: what was last allowed, and how many lines in all.
	int32 GetPlayedCount() const { return Played; }
	int32 GetPlayedCount(EVoiceEvent Event) const { return PlayedByEvent[static_cast<int32>(Event)]; }

private:
	void OnHitResolved(const FHitOutcome& Outcome, AActor* Attacker);
	USoundBase* SoundFor(EVoiceEvent Event) const;

	FFighterVoices Voices;
	FVoiceGate Gate;
	EVoiceEvent CurrentEvent = EVoiceEvent::Effort;
	const void* LastAttack = nullptr;
	bool bEffortTried = false;     // this step's effort chance was rolled
	int32 Played = 0;
	int32 PlayedByEvent[7] = {};

	UPROPERTY()
	TObjectPtr<UAudioComponent> Current;
};
