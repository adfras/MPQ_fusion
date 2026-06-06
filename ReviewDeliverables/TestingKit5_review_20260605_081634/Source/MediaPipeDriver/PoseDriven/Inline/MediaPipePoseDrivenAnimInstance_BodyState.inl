void FAnimNode_MediaPipePoseDriven::ResetFootPlantState()
{
	LeftLegState.ResetFootPlant();
	RightLegState.ResetFootPlant();
	BodyState.ResetTorsoStability();
}
