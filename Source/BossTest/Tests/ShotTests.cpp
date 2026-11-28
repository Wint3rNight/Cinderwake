// Screenshots of the arena with real rendering (Step 6b), so its look can be checked without opening the editor. Not under
// "BossTest." so the normal headless runs (no GPU) skip it. Run with rendering:
//   UnrealEditor-Cmd BossTest.uproject -ExecCmds="Automation RunTests BossTestShots;Quit" -RenderOffscreen -unattended
// Writes Saved/Screenshots/arena_*.png.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Camera/CameraActor.h"
#include "ContentStreaming.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HighResScreenshot.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	constexpr EAutomationTestFlags ShotTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	UWorld* ShotWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				return Context.World();
			}
		}
		return nullptr;
	}
}

struct FShotState
{
	double Start = 0.0;
	double ShotAt = 0.0;
};

// Points the view from From at To (or keeps the player's own camera when bOwnCamera), lets shaders and textures finish, then
// takes a 1600x900 shot and waits a second for it to be written.
DEFINE_LATENT_AUTOMATION_COMMAND_FIVE_PARAMETER(FArenaShot, FString, Name, FVector, From, FVector, To, bool, bOwnCamera, TSharedRef<FShotState>, State);
bool FArenaShot::Update()
{
	UWorld* World = ShotWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !GEngine->GameViewport || !GEngine->GameViewport->Viewport)
	{
		return true;
	}
	FShotState& S = State.Get();
	const double Now = FPlatformTime::Seconds();
	if (S.Start <= 0.0)
	{
		S.Start = Now;
		if (!bOwnCamera)
		{
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(From, (To - From).Rotation());
			PC->SetViewTarget(Camera);
		}
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		IStreamingManager::Get().StreamAllResources(3.f);
		return false;
	}
	if (S.ShotAt <= 0.0)
	{
		if (Now - S.Start < 2.0)
		{
			return false;
		}
		FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
		Config.SetResolution(1600, 900);
		Config.SetFilename(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (TEXT("arena_") + Name + TEXT(".png")));
		GEngine->GameViewport->Viewport->TakeHighResScreenShot();
		S.ShotAt = Now;
		return false;
	}
	return Now - S.ShotAt > 1.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShotsArena, "BossTestShots.Arena", ShotTestFlags)
bool FShotsArena::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/BossTest/BossArena")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(6.f));
	ADD_LATENT_AUTOMATION_COMMAND(FArenaShot(TEXT("1_player"), FVector::ZeroVector, FVector::ZeroVector, true, MakeShared<FShotState>()));
	ADD_LATENT_AUTOMATION_COMMAND(FArenaShot(TEXT("2_overview"), FVector(-2600.f, -2200.f, 2000.f), FVector(0.f, 0.f, 0.f), false, MakeShared<FShotState>()));
	ADD_LATENT_AUTOMATION_COMMAND(FArenaShot(TEXT("3_toward_entrance"), FVector(1500.f, 500.f, 450.f), FVector(-1400.f, 0.f, 300.f), false, MakeShared<FShotState>()));
	ADD_LATENT_AUTOMATION_COMMAND(FArenaShot(TEXT("4_rim"), FVector(-300.f, -1100.f, 260.f), FVector(400.f, 1600.f, 300.f), false, MakeShared<FShotState>()));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
