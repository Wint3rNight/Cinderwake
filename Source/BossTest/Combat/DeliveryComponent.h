// Runs an attacker's rings and projectiles (Step 5, spec section 12) on his own clock: a component's tick delta carries
// the owner's time dilation, so hit-stop pauses them too. Each resolves through the victim's combatant like a sweep,
// unblockable, beaten by a timed dodge. Everything it spawns it owns, and ClearAll removes it (death, reset).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/Delivery.h"
#include "Combat/HitResolver.h"
#include "DeliveryComponent.generated.h"

class UAttackDefinition;
class UCombatantComponent;
class UParticleSystem;
class UParticleSystemComponent;
class UStaticMeshComponent;

UCLASS(ClassGroup = (Combat), meta = (BlueprintSpawnableComponent))
class BOSSTEST_API UDeliveryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDeliveryComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// The ring grows along the floor under Center. It skips anyone the attack's own sweep already met (AttackInstance).
	void LaunchRing(const UAttackDefinition& Definition, uint32 AttackInstance, const FVector& Center);
	void LaunchProjectile(const UAttackDefinition& Definition, const FVector& Start, const FVector& Direction);

	// The attack's sweep met this target (hit, dodged, blocked or deflected): its ring will skip them.
	void NoteDirectResult(uint32 AttackInstance, int32 TargetKey);
	void ForgetInstance(uint32 AttackInstance);

	void ClearAll();
	int32 GetLiveCount() const { return Rings.Num() + Projectiles.Num(); }
	int32 GetEffectCount() const; // live particle and ring-edge components

private:
	struct FLiveRing
	{
		FShockwaveRing Ring;
		FVector Center = FVector::ZeroVector;
		uint32 AttackInstance = 0;
		uint32 Id = 0;
		float Damage = 0.f;
		float HitStop = 0.f;
		float Age = 0.f;
		FHitRegistry Registry;
		TWeakObjectPtr<UParticleSystemComponent> Effect;
		TWeakObjectPtr<UStaticMeshComponent> Edge; // the ring's look, rescaled to its radius every tick
		float EdgeScale = 0.02f;
	};
	struct FLiveProjectile
	{
		FProjectileFlight Flight;
		uint32 Id = 0;
		float Radius = 20.f;
		float Damage = 0.f;
		float HitStop = 0.f;
		bool bSoulFragment = false;
		float Age = 0.f;
		FHitRegistry Registry;
		TWeakObjectPtr<UParticleSystemComponent> Effect;
		TWeakObjectPtr<UParticleSystem> Impact;
	};

	TArray<UCombatantComponent*> Targets() const;
	FHitOutcome Resolve(UCombatantComponent* Victim, uint32 Id, float Damage, bool bSoulFragment, float Age, FHitRegistry& Registry, float HitStop);
	void DestroyEffect(TWeakObjectPtr<UParticleSystemComponent>& Effect);

	TArray<FLiveRing> Rings;
	TArray<FLiveProjectile> Projectiles;
	TMap<uint32, TSet<int32>> DirectResults; // per attack instance, until the attack ends

	// Every effect this component spawned and hasn't destroyed yet, so none outlives a reset.
	UPROPERTY()
	TArray<TObjectPtr<UParticleSystemComponent>> Owned;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> OwnedEdges;

	static constexpr float RingLinger = 1.0f; // seconds the ring's look stays after it stops hitting
};
