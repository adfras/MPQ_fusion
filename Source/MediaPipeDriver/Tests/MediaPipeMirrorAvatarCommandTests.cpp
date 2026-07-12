#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"

// mp.MirrorAvatar (2026-07-12): console-side mirror-avatar switch that writes the
// placed pawn's MetaHumanProfileId (the real selector) instead of the play-start-
// stomped mp.MetaHumanActiveProfile CVar. The automation world has no placed pawn,
// so these tests cover registration and argument safety, not the pawn write - that
// path was verified live (PIE spawned MP_LiveMetaHumanEmory, active=1 valid=1).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMirrorAvatarCommandRegisteredTest,
	"TestingKit5.MediaPipe.Commands.MirrorAvatar.Registered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMirrorAvatarCommandRegisteredTest::RunTest(const FString& Parameters)
{
	IConsoleObject* CommandObject = IConsoleManager::Get().FindConsoleObject(TEXT("mp.MirrorAvatar"));
	TestNotNull(TEXT("mp.MirrorAvatar is registered"), CommandObject);
	if (CommandObject)
	{
		TestNotNull(TEXT("mp.MirrorAvatar is a command, not a variable"), CommandObject->AsCommand());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMirrorAvatarCommandArgSafetyTest,
	"TestingKit5.MediaPipe.Commands.MirrorAvatar.ArgSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMirrorAvatarCommandArgSafetyTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* ActiveProfileVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MetaHumanActiveProfile"));
	if (!ActiveProfileVar)
	{
		AddError(TEXT("mp.MetaHumanActiveProfile is not registered"));
		return false;
	}
	const FString ProfileBefore = ActiveProfileVar->GetString();

	FOutputDeviceNull NullOutput;

	// No-argument call: prints current state, must not crash or write anything.
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.MirrorAvatar"), NullOutput, nullptr);
	TestEqual(TEXT("no-arg call leaves the active profile untouched"),
		ActiveProfileVar->GetString(), ProfileBefore);

	// Unknown profile id: warns with the valid list, must not write the CVar.
	AddExpectedError(TEXT("mp.MirrorAvatar: unknown profile"), EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.MirrorAvatar DefinitelyNotACastMember"), NullOutput, nullptr);
	TestEqual(TEXT("unknown-id call leaves the active profile untouched"),
		ActiveProfileVar->GetString(), ProfileBefore);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
