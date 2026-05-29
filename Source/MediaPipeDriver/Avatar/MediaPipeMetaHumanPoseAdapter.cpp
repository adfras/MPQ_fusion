#include "MediaPipeMetaHumanPoseAdapter.h"

namespace
{
	template <int32 Count>
	void SetNames(TStaticArray<FName, Count>& OutNames, const TCHAR* const (&Names)[Count])
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			OutNames[Index] = FName(Names[Index]);
		}
	}
}

FMediaPipeMetaHumanHelperBoneBinding FMediaPipeMetaHumanHelperBoneBinding::Default()
{
	static const TCHAR* const ClavicleHelperBoneNamesL[MediaPipeMetaHumanClavicleHelperCount] = {
		TEXT("clavicle_out_l"),
		TEXT("clavicle_scap_l"),
		TEXT("clavicle_pec_l")
	};
	static const TCHAR* const ClavicleHelperBoneNamesR[MediaPipeMetaHumanClavicleHelperCount] = {
		TEXT("clavicle_out_r"),
		TEXT("clavicle_scap_r"),
		TEXT("clavicle_pec_r")
	};
	static const TCHAR* const UpperArmHelperBoneNamesL[MediaPipeMetaHumanUpperArmHelperCount] = {
		TEXT("upperarm_twistCor_01_l"),
		TEXT("upperarm_twistCor_02_l"),
		TEXT("upperarm_bicep_l"),
		TEXT("upperarm_tricep_l"),
		TEXT("upperarm_correctiveRoot_l"),
		TEXT("upperarm_bck_l"),
		TEXT("upperarm_fwd_l"),
		TEXT("upperarm_in_l"),
		TEXT("upperarm_out_l")
	};
	static const TCHAR* const UpperArmHelperBoneNamesR[MediaPipeMetaHumanUpperArmHelperCount] = {
		TEXT("upperarm_twistCor_01_r"),
		TEXT("upperarm_twistCor_02_r"),
		TEXT("upperarm_bicep_r"),
		TEXT("upperarm_tricep_r"),
		TEXT("upperarm_correctiveRoot_r"),
		TEXT("upperarm_bck_r"),
		TEXT("upperarm_fwd_r"),
		TEXT("upperarm_in_r"),
		TEXT("upperarm_out_r")
	};
	static const TCHAR* const LowerArmHelperBoneNamesL[MediaPipeMetaHumanLowerArmHelperCount] = {
		TEXT("lowerarm_correctiveRoot_l"),
		TEXT("lowerarm_in_l"),
		TEXT("lowerarm_out_l"),
		TEXT("lowerarm_fwd_l"),
		TEXT("lowerarm_bck_l"),
		TEXT("wrist_inner_l"),
		TEXT("wrist_outer_l")
	};
	static const TCHAR* const LowerArmHelperBoneNamesR[MediaPipeMetaHumanLowerArmHelperCount] = {
		TEXT("lowerarm_correctiveRoot_r"),
		TEXT("lowerarm_in_r"),
		TEXT("lowerarm_out_r"),
		TEXT("lowerarm_fwd_r"),
		TEXT("lowerarm_bck_r"),
		TEXT("wrist_inner_r"),
		TEXT("wrist_outer_r")
	};

	FMediaPipeMetaHumanHelperBoneBinding Binding;
	SetNames(Binding.ClavicleL, ClavicleHelperBoneNamesL);
	SetNames(Binding.ClavicleR, ClavicleHelperBoneNamesR);
	SetNames(Binding.UpperArmL, UpperArmHelperBoneNamesL);
	SetNames(Binding.UpperArmR, UpperArmHelperBoneNamesR);
	SetNames(Binding.LowerArmL, LowerArmHelperBoneNamesL);
	SetNames(Binding.LowerArmR, LowerArmHelperBoneNamesR);
	return Binding;
}
