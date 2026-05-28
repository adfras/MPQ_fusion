#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeMetaHumanPoseAdapter.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanPoseAdapterDefaultHelpersAutomationTest,
	"MediaPipe.SkeletonAdapter.MetaHuman.DefaultHelperBoneBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanPoseAdapterDefaultHelpersAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeMetaHumanHelperBoneBinding Binding = FMediaPipeMetaHumanHelperBoneBinding::Default();

	TestEqual(TEXT("Left clavicle helper count is stable"), Binding.ClavicleL.Num(), MediaPipeMetaHumanClavicleHelperCount);
	TestEqual(TEXT("Left upper-arm helper count is stable"), Binding.UpperArmL.Num(), MediaPipeMetaHumanUpperArmHelperCount);
	TestEqual(TEXT("Left lower-arm helper count is stable"), Binding.LowerArmL.Num(), MediaPipeMetaHumanLowerArmHelperCount);
	TestEqual(TEXT("Left clavicle outward helper"), Binding.ClavicleL[0], FName(TEXT("clavicle_out_l")));
	TestEqual(TEXT("Right clavicle pectoral helper"), Binding.ClavicleR[2], FName(TEXT("clavicle_pec_r")));
	TestEqual(TEXT("Left upper-arm twist corrective helper"), Binding.UpperArmL[0], FName(TEXT("upperarm_twistCor_01_l")));
	TestEqual(TEXT("Right upper-arm outward helper"), Binding.UpperArmR[8], FName(TEXT("upperarm_out_r")));
	TestEqual(TEXT("Left lower-arm corrective root"), Binding.LowerArmL[0], FName(TEXT("lowerarm_correctiveRoot_l")));
	TestEqual(TEXT("Right wrist outer helper"), Binding.LowerArmR[6], FName(TEXT("wrist_outer_r")));
	return true;
}

#endif
