#include "Arena/ArenaBounds.h"

#include "Components/BoxComponent.h"
#include "EngineUtils.h"

ArenaRing::FSegment ArenaRing::Segment(int32 Index, int32 Count, float InnerRadius, float Thickness, float Height, float Overlap)
{
	const float Step = 2.f * PI / FMath::Max(Count, 3);
	const float Angle = Step * Index;
	FSegment S;
	S.Centre = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (InnerRadius + Thickness * 0.5f) + FVector(0.f, 0.f, Height * 0.5f);
	S.Yaw = FMath::RadiansToDegrees(Angle);
	// Neighbouring flat boxes meet at half the step; at the outer face that's furthest along each box.
	S.Extent = FVector(Thickness * 0.5f, (InnerRadius + Thickness) * FMath::Tan(Step * 0.5f) + Overlap, Height * 0.5f);
	return S;
}

AArenaBounds::AArenaBounds()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	for (int32 i = 0; i < SegmentCount; ++i)
	{
		UBoxComponent* Box = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("Wall%02d"), i));
		Box->SetupAttachment(Root);
		Box->SetHiddenInGame(true);
		Box->SetCanEverAffectNavigation(false);
		// Fighters (capsules are Pawn) and a ragdoll (PhysicsBody) stop at it; the camera, sight lines and attack traces don't see it.
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
		Wall.Add(Box);
	}
}

void AArenaBounds::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	LayoutWall(); // in the editor, so the boxes show where the wall is when you edit the radius
}

void AArenaBounds::BeginPlay()
{
	Super::BeginPlay();
	LayoutWall();
}

void AArenaBounds::LayoutWall()
{
	for (int32 i = 0; i < Wall.Num(); ++i)
	{
		const ArenaRing::FSegment S = ArenaRing::Segment(i, Wall.Num(), WallRadius, WallThickness, WallHeight);
		Wall[i]->SetRelativeLocationAndRotation(S.Centre, FRotator(0.f, S.Yaw, 0.f));
		Wall[i]->SetBoxExtent(S.Extent);
	}
}

AArenaBounds* AArenaBounds::Find(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AArenaBounds> It(World);
	return It ? *It : nullptr;
}
