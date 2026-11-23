// Frame-time measurements with real rendering, on this machine's GPU. Not under "BossTest." so the
// normal headless runs (no GPU) skip it. Run with rendering:
//   UnrealEditor-Cmd BossTest.uproject -ExecCmds="Automation RunTests BossTestPerf;Quit" -RenderOffscreen -unattended
// Each map plays in the editor; after a warm-up, every quality preset is measured for a few seconds.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "Scalability.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UnrealClient.h"

namespace
{
	constexpr EAutomationTestFlags PerfTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	void SetPreset(int32 Level, bool bVirtualShadows)
	{
		Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
		Q.SetFromSingleQualityLevel(Level);
		Scalability::SetQualityLevels(Q);
		if (IConsoleVariable* VSM = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))
		{
			VSM->Set(bVirtualShadows ? 1 : 0, ECVF_SetByCode);
		}
	}
}

// Sets a preset, waits for it to settle, then averages frame, game, render and GPU time over Seconds.
DEFINE_LATENT_AUTOMATION_COMMAND_FIVE_PARAMETER(FMeasurePreset, FString, Map, int32, Level, bool, bVirtualShadows, float, Seconds, TSharedRef<TArray<double>>, Acc);
bool FMeasurePreset::Update()
{
	TArray<double>& A = Acc.Get(); // [start, settledStart, frames, frameMs, gameMs, renderMs, gpuMs]
	const double Now = FPlatformTime::Seconds();
	if (A.Num() == 0)
	{
		SetPreset(Level, bVirtualShadows);
		A = { Now, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
		return false;
	}
	if (A[1] == 0.0)
	{
		if (Now - A[0] < 2.0)
		{
			return false; // let shaders, streaming and the preset settle
		}
		A[1] = Now;
		return false;
	}
	A[2] += 1.0;
	A[3] += FApp::GetDeltaTime() * 1000.0;
	A[4] += FPlatformTime::ToMilliseconds(GGameThreadTime);
	A[5] += FPlatformTime::ToMilliseconds(GRenderThreadTime);
	A[6] += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
	if (Now - A[1] < Seconds)
	{
		return false;
	}
	FIntPoint Size(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Size = GEngine->GameViewport->Viewport->GetSizeXY();
	}
	const double N = FMath::Max(1.0, A[2]);
	static const TCHAR* Names[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	UE_LOG(LogTemp, Display, TEXT("PERF %s %s%s: frame %.1f ms (%.0f fps), game %.1f, render %.1f, GPU %.1f ms, %dx%d, %d frames"),
		*Map, Names[FMath::Clamp(Level, 0, 4)], bVirtualShadows ? TEXT("") : TEXT(" (no virtual shadow maps)"),
		A[3] / N, 1000.0 / FMath::Max(0.01, A[3] / N), A[4] / N, A[5] / N, A[6] / N, Size.X, Size.Y, static_cast<int32>(N));
	return true;
}

namespace
{
	void QueuePerfRun(const FString& Map, const FString& Label)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(8.f));
		const struct { int32 Level; bool bVSM; } Runs[] = { {3, true}, {2, true}, {2, false}, {1, true}, {1, false}, {0, false} };
		for (const auto& R : Runs)
		{
			ADD_LATENT_AUTOMATION_COMMAND(FMeasurePreset(Label, R.Level, R.bVSM, 5.f, MakeShared<TArray<double>>()));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfFlatArena, "BossTestPerf.FlatArena", PerfTestFlags)
bool FPerfFlatArena::RunTest(const FString& Parameters)
{
	QueuePerfRun(TEXT("/Game/BossTest/BossArena"), TEXT("flat arena"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPerfParagonArena, "BossTestPerf.ParagonArena", PerfTestFlags)
bool FPerfParagonArena::RunTest(const FString& Parameters)
{
	const FString Arena = TEXT("/Game/Developers/ArenaTest/Arena_ParagonSample");
	if (!FPackageName::DoesPackageExist(Arena))
	{
		AddWarning(TEXT("no arena test level; run Docs/tools/setup_arena_test.py"));
		return true;
	}
	QueuePerfRun(Arena, TEXT("Paragon arena"));
	return true;
}

#endif
