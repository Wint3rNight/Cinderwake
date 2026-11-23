// Step 3, Task 2: the input buffer.
#include "Misc/AutomationTest.h"
#include "Player/InputBuffer.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags BufferTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBufferLatestPressWins, "BossTest.Player.Buffer.LatestPressWins", BufferTestFlags)
bool FBufferLatestPressWins::RunTest(const FString& Parameters)
{
	FInputBuffer Buffer;
	TestTrue(TEXT("empty to start"), Buffer.Peek() == EPlayerInput::None);
	Buffer.Press(EPlayerInput::Light);
	Buffer.Tick(0.1f);
	Buffer.Press(EPlayerInput::Dodge);
	TestTrue(TEXT("the later press replaces the earlier one"), Buffer.Peek() == EPlayerInput::Dodge);
	Buffer.Tick(0.17f);
	TestTrue(TEXT("and its age starts again"), Buffer.Peek() == EPlayerInput::Dodge);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBufferExpires, "BossTest.Player.Buffer.Expires", BufferTestFlags)
bool FBufferExpires::RunTest(const FString& Parameters)
{
	FInputBuffer Buffer;
	Buffer.Press(EPlayerInput::Heavy);
	Buffer.Tick(0.17f);
	TestTrue(TEXT("still there at 0.17 s"), Buffer.Peek() == EPlayerInput::Heavy);
	Buffer.Tick(0.02f);
	TestTrue(TEXT("gone at 0.19 s"), Buffer.Peek() == EPlayerInput::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBufferConsume, "BossTest.Player.Buffer.Consume", BufferTestFlags)
bool FBufferConsume::RunTest(const FString& Parameters)
{
	FInputBuffer Buffer;
	Buffer.Press(EPlayerInput::Heal);
	Buffer.Consume();
	TestTrue(TEXT("a consumed press is gone"), Buffer.Peek() == EPlayerInput::None);
	return true;
}

#endif
