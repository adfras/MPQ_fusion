param(
    [string]$ProjectRoot = "."
)

$ErrorActionPreference = "Stop"

Write-Warning "CheckWallaceArmSourceGuards.ps1 is archived for the historical Wallace/profile-4 constrained-arm path. Use CheckMetaHumanGenericProfileGuards.ps1 for the current generic MetaHuman profile path."

function Read-Text {
    param([string]$Path)

    $resolved = Join-Path $ProjectRoot $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Required file not found: $resolved"
    }
    return Get-Content -LiteralPath $resolved -Raw
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Message
    )
    $Failures.Add($Message) | Out-Null
}

function Test-RequiredText {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Name,
        [string]$Text,
        [string]$Pattern
    )

    if ($Text -notmatch $Pattern) {
        Add-Failure -Failures $Failures -Message "$Name missing: $Pattern"
    }
}

$failures = New-Object System.Collections.Generic.List[string]

$runtime = Read-Text "Source\MediaPipeDriver\MediaPipeDriver.cpp"
$editor = Read-Text "Source\MediaPipeDriverEditor\MediaPipeLiveVideoCommands.cpp"
$animInstance = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance.cpp"
$drivenActor = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenSkeletalActor.cpp"
$poseFrameContinuity = Read-Text "Source\MediaPipeDriver\MediaPipePoseFrameContinuity.h"
$poseFrameContinuityTests = Read-Text "Source\MediaPipeDriver\MediaPipePoseFrameContinuityTests.cpp"
$armGuardPolicy = Read-Text "Source\MediaPipeDriver\MediaPipeArmGuardPolicy.h"
$armGuardPolicyTests = Read-Text "Source\MediaPipeDriver\MediaPipeArmGuardPolicyTests.cpp"
$armTwist = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_ArmTwist.inl"
$armTwistSolver = Read-Text "Source\MediaPipeDriver\MediaPipeArmTwistSolver.cpp"
$armTwistTests = Read-Text "Source\MediaPipeDriver\MediaPipeArmTwistSolverTests.cpp"
$metaHumanHelperCoverageTests = Read-Text "Source\MediaPipeDriver\MediaPipeMetaHumanArmHelperCoverageTests.cpp"
$bodySolverMathHeader = Read-Text "Source\MediaPipeDriver\MediaPipeBodySolverMath.h"
$bodySolverMath = Read-Text "Source\MediaPipeDriver\MediaPipeBodySolverMath.cpp"
$bodySolverMathTests = Read-Text "Source\MediaPipeDriver\MediaPipeBodySolverMathTests.cpp"
$questArmSolve = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl"
$questHandRotation = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl"
$questSpaceMapping = Read-Text "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl"
$questWristCalibrationState = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristCalibrationState.cpp"
$questWristCalibrationStateTests = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristCalibrationStateTests.cpp"
$questWristApplyPolicyHeader = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristApplyPolicy.h"
$questWristApplyPolicy = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristApplyPolicy.cpp"
$questWristApplyPolicyTests = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristApplyPolicyTests.cpp"
$runtimeCVarsHeader = Read-Text "Source\MediaPipeDriver\MediaPipeRuntimeCVars.h"
$runtimeCVarsSource = Read-Text "Source\MediaPipeDriver\MediaPipeRuntimeCVars.cpp"
$constrainedArmSolver = Read-Text "Source\MediaPipeDriver\MediaPipeQuestConstrainedArmSolver.cpp"
$constrainedArmSolverTests = Read-Text "Source\MediaPipeDriver\MediaPipeQuestConstrainedArmSolverTests.cpp"
$diagnosticFormatter = Read-Text "Source\MediaPipeDriver\MediaPipeQuestWristDiagnosticFormatter.cpp"
$questObjectiveGate = Read-Text "Tools\RunQuestWristObjectiveGate.ps1"
$embodimentLogGate = Read-Text "Tools\CheckWallaceQuestVrEmbodimentLog.ps1"

$profile4Match = [regex]::Match(
    $runtime,
    'else if \(ReachAssistProfile == 4\)\s*\{(?<block>[\s\S]*?)\n\t\}',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)
if (-not $profile4Match.Success) {
    Add-Failure -Failures $failures -Message "Could not find ApplyAutoQuestProfile ReachAssistProfile == 4 block."
}
else {
    $profile4 = $profile4Match.Groups["block"].Value
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestArmMode"\), 3\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestWristPositionBlend"\), 1\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristRequireTrackedForApply"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestHandRotationMaxStepDegrees"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmSolve"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMaxReachFraction"\), 0\.997f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmSolvedPlaneMinSin"\), 0\.08f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmCloseReachPoleBias"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMaxReachStepCm"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmBodyFallback"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMediaPipeElbowHint"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmDownStraighten"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenThresholdCm"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMaxCm"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenReachFloorFraction"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMaxReachFraction"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmReachScaleCalibration"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmReachScaleUniform"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestArmLengthCalibrationStartup"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestArmLengthCalibrationHud"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestArmLengthCalibrationDownMinCorrectedReachFraction"\), 0\.95f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestArmDownFrameCorrection"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestArmDropoutDownFallback"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackRecentTrackedSeconds"\), 4\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackMinDownDominance"\), 0\.55f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackBlendHalfLife"\), 0\.08f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmTargetHalfLife"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationHalfLife"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationMaxStepDegrees"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationMaxSpeedDegreesPerSecond"\), 0\.0f\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveArmTwistBones"\), 1\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveMetaHumanArmHelpers"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristTwistDrivesForearm"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristForearmRollDriveTwistHelpers"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristUpperArmRollDriveTwistHelpers"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristDriveTwistCorrection"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.MediaPipeUseArmIK"\), 0\)'
    Test-RequiredText $failures "profile4" $profile4 'SetConsoleInt\(TEXT\("mp\.QuestWristForceArmIK"\), 0\)'
}

Test-RequiredText $failures "Auto Quest eye-center camera default" $runtime 'SetConsoleFloat\(TEXT\("mp\.AutoQuestEmbodiedCameraForwardOffsetCm"\), 0\.0f\)'

$editorHandsMatch = [regex]::Match(
    $editor,
    'void ApplyQuestWebcamHandsProfile\(\)\s*\{(?<block>[\s\S]*?)\n\}',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)
if (-not $editorHandsMatch.Success) {
    Add-Failure -Failures $failures -Message "Could not find ApplyQuestWebcamHandsProfile block."
}
else {
    $editorHands = $editorHandsMatch.Groups["block"].Value
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveClavicles"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveSpine"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestArmMode"\), 3\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestWristPositionBlend"\), 1\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestWristMaxRelativeDeltaCm"\), 82\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristRequireTrackedForApply"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristReachAssist"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristDriftGuard"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestHandRotationMaxStepDegrees"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmSolve"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMaxReachFraction"\), 0\.997f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmSolvedPlaneMinSin"\), 0\.08f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmCloseReachPoleBias"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMaxReachStepCm"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmMediaPipeElbowHint"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmDownStraighten"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenThresholdCm"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMaxCm"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenReachFloorFraction"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestConstrainedArmDownStraightenMaxReachFraction"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmReachScaleCalibration"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestConstrainedArmReachScaleUniform"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestArmLengthCalibrationStartup"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestArmLengthCalibrationHud"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestArmDownFrameCorrection"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestArmDropoutDownFallback"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackRecentTrackedSeconds"\), 4\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackMinDownDominance"\), 0\.55f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.QuestArmDropoutDownFallbackBlendHalfLife"\), 0\.08f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristPositionAdaptiveFilter"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmTargetHalfLife"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationHalfLife"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationMaxStepDegrees"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleFloat\(TEXT\("mp\.MediaPipeArmRotationMaxSpeedDegreesPerSecond"\), 0\.0f\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveArmTwistBones"\), 1\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.MediaPipeDriveMetaHumanArmHelpers"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristForearmRollDriveTwistHelpers"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristUpperArmRollDriveTwistHelpers"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristDriveTwistCorrection"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.MediaPipeUseArmIK"\), 0\)'
    Test-RequiredText $failures "ApplyQuestWebcamHandsProfile" $editorHands 'SetConsoleInt\(TEXT\("mp\.QuestWristForceArmIK"\), 0\)'
}

$poseFiles = @(
    "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance.cpp",
    "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_ArmTwist.inl",
    "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl",
    "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl",
    "Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl"
)
foreach ($poseFile in $poseFiles) {
    $text = Read-Text $poseFile
    if ($text -match 'SetComponentSpaceTransform\s*\(') {
        Add-Failure -Failures $failures -Message "$poseFile contains raw SetComponentSpaceTransform; use SafeSetCSBoneTransforms for component-space writes."
    }
}

Test-RequiredText $failures "ArmTwist lowerarm wrist helpers" $armTwist 'constexpr int32 MetaHumanLowerArmWristHelperStartIndex = 5;'
Test-RequiredText $failures "ArmTwist lowerarm wrist helpers" $armTwist 'constexpr int32 MetaHumanLowerArmWristHelperCount = 2;'
Test-RequiredText $failures "ArmTwist lowerarm wrist helpers" $armTwist 'MetaHumanLowerArmHelperOffset \+ MetaHumanLowerArmWristHelperStartIndex'
Test-RequiredText $failures "ArmTwist upper-arm helpers" $armTwist 'ApplyMetaHumanUpperArmHelpers\('
Test-RequiredText $failures "ArmTwist clavicle helpers" $armTwist 'MetaHumanClaviclePecHelperIndex'
Test-RequiredText $failures "ArmTwist source-parent solver input" $armTwistSolver 'ReferenceSourceParentComponent'
Test-RequiredText $failures "ArmTwist source-parent runtime call" $armTwist 'ApplyInterpolatedTwistWithSourceParent'
Test-RequiredText $failures "ArmTwist source-parent helper chain" $armTwist 'ApplyMetaHumanHelperGroupWithSourceParent'
Test-RequiredText $failures "ArmTwist source-parent regression test" $armTwistTests 'Unmapped chain twist helper uses source-parent projection weight'
Test-RequiredText $failures "ArmTwist Oculus-style stateless runtime" $armTwist 'twist helpers are derived from the current frame pose only'
if ($armTwist -match 'UpdateSmoothedRotation\s*\(') {
    Add-Failure -Failures $failures -Message "ArmTwist runtime still applies temporal smoothing inside the Oculus-style helper interpolation pass."
}
Test-RequiredText $failures "Quest hand rotation continuous untracked policy input" $questHandRotation 'FMediaPipeQuestWristApplyPolicyInput HandRotationPolicyInput'
Test-RequiredText $failures "Quest hand rotation current wrist-frame policy input" $questHandRotation 'FMediaPipeQuestHandRotationFramePolicyInput HandRotationFramePolicyInput'
Test-RequiredText $failures "Quest hand rotation current wrist-frame policy gate" $questHandRotation 'CanUseQuestHandRotationForCurrentFrame\(HandRotationFramePolicyInput\)'
Test-RequiredText $failures "Quest hand rotation uses constrained continuity only" $questHandRotation 'bAllowUsableUntrackedForPositionApply\s*=[\s\S]*CVarQuestArmMode\.GetValueOnAnyThread\(\), 0, 3\) >= 2'
Test-RequiredText $failures "Quest hand rotation consumes wrist trace" $questArmSolve 'DriveQuestHandCS\(CSPose, bIsLeft, QuestForearmAxisComp, CurrentHandTargetCS, DeltaSeconds, &QuestHandRotationTrace, &QuestWristTrace\)'
Test-RequiredText $failures "Quest hand rotation wrist-frame regression" $questWristApplyPolicyTests 'Untracked Quest hand rotation cannot follow a body-fallback wrist target'
Test-RequiredText $failures "Quest hand rotation tracked authoritative direct runtime" $questHandRotation 'bUseDirectTrackedQuestHandRotation[\s\S]*SmoothedQuestHandRotLocal = TargetHandRotLocal'
Test-RequiredText $failures "Quest hand rotation tracked authoritative direct CS runtime" $questHandRotation 'bUseDirectTrackedQuestHandRotation[\s\S]*SmoothedQuestHandRotCS = TargetHandRotCS'
Test-RequiredText $failures "Wallace presentation mesh post-process enabled" $drivenActor 'SetDisablePostProcessBlueprint\(false\)'
Test-RequiredText $failures "Wallace presentation mesh post-process log" $drivenActor 'Auto Quest presentation mesh:'
Test-RequiredText $failures "Embodiment log gate presentation post-process" $embodimentLogGate 'postProcessClass=\.\*m_med_unw_animbp_Cinematic'
Test-RequiredText $failures "Embodiment log gate presentation post-process enabled" $embodimentLogGate 'postProcessDisabled=0'
Test-RequiredText $failures "Oculus-style standard twist exact scope" $metaHumanHelperCoverageTests 'StandardTwistCandidates == static_cast<int32>\(UE_ARRAY_COUNT\(StandardTwistBones\)\)'
Test-RequiredText $failures "Wallace standard twist sidecar-safe topology" $metaHumanHelperCoverageTests 'Wallace standard twist helpers are sidecar-safe for post-finger writes'
Test-RequiredText $failures "Evaluate_AnyThread arm solve order" $animInstance 'DriveArmCS\(CSPose, true, DeltaSeconds\);[\s\S]*DriveArmCS\(CSPose, false, DeltaSeconds\);[\s\S]*DriveArmTwistBonesCS\(CSPose, DeltaSeconds\);[\s\S]*ConvertComponentPosesToLocalPosesSafe\(CSPose, Output\.Pose\)'
Test-RequiredText $failures "Avatar arm fallback basis helper" $bodySolverMathHeader 'FMediaPipeAvatarArmBasisInput'
Test-RequiredText $failures "Avatar arm fallback basis implementation" $bodySolverMath 'BuildAvatarArmBasis'
Test-RequiredText $failures "Avatar arm fallback basis runtime" $questArmSolve 'BuildAvatarArmBasis\(AvatarArmBasisInput\)[\s\S]*FVector HipRightWorld = AvatarArmBasis\.RightWorld;[\s\S]*TryGetTorsoBasisWorld\(HipRightWorld, ShoulderRightWorld, UpWorld, ForwardWorld\)'
Test-RequiredText $failures "Avatar arm fallback component pose-write basis runtime" $questArmSolve 'AvatarForwardComp[\s\S]*bUseTargetFaceForwardAxis \? FVector::RightVector : FVector::ForwardVector[\s\S]*const FVector HipRightComp = bHasTorsoBasis [\s\S]*: AvatarRightComp;[\s\S]*FVector ForwardComp = bHasTorsoBasis [\s\S]*: AvatarForwardComp;'
Test-RequiredText $failures "Wallace avatar arm fallback basis regression" $bodySolverMathTests 'Wallace no-torso fallback right is derived from the avatar frame, not world \+Y'
Test-RequiredText $failures "HMD-relative shoulder rollback bypass policy" $armGuardPolicy 'bUseHmdRelativeAvatarArmFrame'
Test-RequiredText $failures "HMD-relative shoulder rollback bypass runtime" $questArmSolve 'ShouldApplyShoulderRollbackGuard\([\s\S]*bQuestConstrainedArmSolveApplied,[\s\S]*bUseHmdRelativeAvatarArmFrame'
Test-RequiredText $failures "HMD-relative shoulder rollback bypass regression" $armGuardPolicyTests 'HMD-relative Quest arm mode is never hard-held by shoulder rollback'
Test-RequiredText $failures "Pose frame continuity helper" $poseFrameContinuity 'ResolveFrameAvailability'
Test-RequiredText $failures "Pose frame continuity holds dropouts" $poseFrameContinuity 'return bHasHeldFrame \? EFrameAvailability::Held : EFrameAvailability::None;'
Test-RequiredText $failures "Pose frame continuity reset helper" $poseFrameContinuity 'ResetHeldFrame'
Test-RequiredText $failures "Pose frame continuity resets associated hands" $poseFrameContinuity 'FMediaPipeRawHandPair& HeldHands'
Test-RequiredText $failures "Pose frame continuity runtime availability" $animInstance 'ResolveFrameAvailability\(bHasLivePoseFrame \? &Frame : nullptr, bHasPoseFrame\)'
Test-RequiredText $failures "Pose frame continuity refreshes target transform" $animInstance 'RuntimeStateKey = SkelComp->GetUniqueID\(\);[\s\S]*TargetCompTransform = SkelComp->GetComponentTransform\(\);'
Test-RequiredText $failures "Pose frame continuity reset runtime" $animInstance 'ResetHeldFrame\(\s*bHasPoseFrame,\s*PoseFrame,\s*PoseTimestampSeconds,\s*bHasPoseHands,\s*PoseHands,\s*PoseHandsTimestampUs\)'
Test-RequiredText $failures "Pose timestamp rewind resets pose yaw runtime state" $animInstance 'ActivePoseTimestampUs < LastPoseTimestampUs[\s\S]*ResetPoseYawAlignRuntimeState\(RuntimeStateKey\)'
Test-RequiredText $failures "Pose timestamp rewind resets Quest wrist runtime state" $animInstance 'ActivePoseTimestampUs < LastPoseTimestampUs[\s\S]*ResetQuestWristRuntimeState\(RuntimeStateKey\)'
Test-RequiredText $failures "Quest semantic wrist roll continuous unwrap helper" $questWristApplyPolicy 'ContinueAngleDegrees[\s\S]*FindDeltaAngleDegrees'
Test-RequiredText $failures "Quest semantic wrist roll avoids pre-clamp normalization" $questHandRotation 'Keep the semantic roll accumulator unwrapped[\s\S]*RotationSemanticRollLastTwistDeg = QuestTwistDeg'
Test-RequiredText $failures "Quest semantic wrist roll wrap regression" $questWristApplyPolicyTests 'Quest semantic wrist roll keeps positive continuity across the \+/-180 wrap'
Test-RequiredText $failures "Pose frame continuity regression" $poseFrameContinuityTests 'TestingKit3\.MediaPipe\.PoseFrameContinuity\.HoldLastFrameOnDropout'
Test-RequiredText $failures "Pose frame continuity dropout regression" $poseFrameContinuityTests 'Dropout after a live frame holds last frame'
Test-RequiredText $failures "Pose frame continuity holds associated hands" $poseFrameContinuityTests 'Dropout keeps the frame-associated raw hand flag until an explicit reset'
$preUpdatePrefixMatch = [regex]::Match(
    $animInstance,
    'void FAnimNode_MediaPipePoseDriven::PreUpdate\(const UAnimInstance\* InAnimInstance\)\s*\{(?<block>[\s\S]*?)const USkeletalMeshComponent\* SkelComp',
    [System.Text.RegularExpressions.RegexOptions]::Multiline)
if ($preUpdatePrefixMatch.Success -and $preUpdatePrefixMatch.Groups["block"].Value -match 'bHasPoseHands\s*=\s*false') {
    Add-Failure -Failures $failures -Message "PreUpdate clears frame-associated raw hand data before proving a replacement body frame exists."
}
Test-RequiredText $failures "Quest arms-down target solve" $questArmSolve 'SolveInput\.bEnableDownStraighten\s*=\s*CVarQuestConstrainedArmDownStraighten\.GetValueOnAnyThread\(\) != 0;'
Test-RequiredText $failures "Quest arms-down target diagnostics" $questArmSolve 'SolveResult\.bDownStraightened[\s\S]*bConstrainedArmDownStraightened'
Test-RequiredText $failures "Quest final reach history feeds solver" $questArmSolve 'SolveInput\.bHasReachContinuityHistory\s*=\s*bHasRecentHmdRelativeReachContinuity;[\s\S]*StoreHmdRelativeReachContinuity\(WristWorld\)'
Test-RequiredText $failures "Quest arms-down fallback solve" $questArmSolve 'FallbackInput\.bEnableDownStraighten\s*=\s*CVarQuestConstrainedArmDownStraighten\.GetValueOnAnyThread\(\) != 0;'
Test-RequiredText $failures "Quest constrained arm max reach CVar header" $runtimeCVarsHeader 'CVarQuestConstrainedArmMaxReachFraction'
Test-RequiredText $failures "Quest constrained arm max reach CVar source" $runtimeCVarsSource 'mp\.QuestConstrainedArmMaxReachFraction'
Test-RequiredText $failures "Quest constrained arm max reach runtime clamp" $questArmSolve 'QuestConstrainedArmMaxReachFraction\s*=\s*FMath::Clamp\([\s\S]*CVarQuestConstrainedArmMaxReachFraction\.GetValueOnAnyThread\(\)'
Test-RequiredText $failures "Quest constrained arm max reach fallback runtime" $questArmSolve 'FallbackInput\.MaxReachFraction\s*=\s*QuestConstrainedArmMaxReachFraction;'
Test-RequiredText $failures "Quest constrained arm max reach pre-solve clamp" $questArmSolve 'const float MaxReachCm\s*=\s*\(RefUpperLen \+ RefLowerLen\) \* QuestConstrainedArmMaxReachFraction;'
Test-RequiredText $failures "Quest constrained arm max reach target runtime" $questArmSolve 'SolveInput\.MaxReachFraction\s*=\s*QuestConstrainedArmMaxReachFraction;'
Test-RequiredText $failures "Quest constrained arm reach-step CVar header" $runtimeCVarsHeader 'CVarQuestConstrainedArmMaxReachStepCm'
Test-RequiredText $failures "Quest constrained arm reach-step CVar source" $runtimeCVarsSource 'mp\.QuestConstrainedArmMaxReachStepCm'
Test-RequiredText $failures "Quest constrained arm reach-step runtime" $questArmSolve 'SolveInput\.MaxReachStepCm\s*=\s*CVarQuestConstrainedArmMaxReachStepCm\.GetValueOnAnyThread\(\);'
Test-RequiredText $failures "Quest constrained arm pre-solve reach-step runtime" $questArmSolve 'ApplyReachStepContinuity\(ReachContinuityInput\)'
Test-RequiredText $failures "Quest constrained arm reach-step solver" $constrainedArmSolver 'bReachContinuityApplied'
Test-RequiredText $failures "Quest constrained arm reach-step diagnostics" $diagnosticFormatter 'questArmReachContinuity='
Test-RequiredText $failures "Quest constrained arm reach-step regression" $constrainedArmSolverTests 'Sudden target reach collapse is continuity-limited'
Test-RequiredText $failures "Quest constrained arm reach-scale CVar header" $runtimeCVarsHeader 'CVarQuestConstrainedArmReachScaleCalibration'
Test-RequiredText $failures "Quest constrained arm reach-scale CVar source" $runtimeCVarsSource 'mp\.QuestConstrainedArmReachScaleCalibration'
Test-RequiredText $failures "Quest constrained arm uniform reach-scale CVar header" $runtimeCVarsHeader 'CVarQuestConstrainedArmReachScaleUniform'
Test-RequiredText $failures "Quest constrained arm uniform reach-scale CVar source" $runtimeCVarsSource 'mp\.QuestConstrainedArmReachScaleUniform'
Test-RequiredText $failures "Quest constrained arm reach-scale policy helper" $questWristApplyPolicyHeader 'FMediaPipeQuestReachScaleCalibrationInput'
Test-RequiredText $failures "Quest constrained arm reach-scale runtime" $questArmSolve 'ReachScaleInput\.bApplyUniformScale[\s\S]*CVarQuestConstrainedArmReachScaleUniform'
Test-RequiredText $failures "Quest constrained arm reach-scale diagnostics" $diagnosticFormatter 'questArmReachScale='
Test-RequiredText $failures "Quest arm length calibration CVar header" $runtimeCVarsHeader 'CVarQuestArmLengthCalibrationStartup'
Test-RequiredText $failures "Quest arm length calibration CVar source" $runtimeCVarsSource 'mp\.QuestArmLengthCalibrationStartup'
Test-RequiredText $failures "Quest arm length corrected down reach CVar header" $runtimeCVarsHeader 'CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction'
Test-RequiredText $failures "Quest arm length corrected down reach CVar source" $runtimeCVarsSource 'mp\.QuestArmLengthCalibrationDownMinCorrectedReachFraction'
Test-RequiredText $failures "Quest arm length calibration stage state" $questWristCalibrationState 'ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands'
Test-RequiredText $failures "Quest arm length calibration runtime" $questArmSolve 'mp\.QuestArmLengthCalibration: stage=ForwardReach accepted=1'
Test-RequiredText $failures "Quest arm length corrected down reach runtime" $questArmSolve 'correctedDownReachCmL='
Test-RequiredText $failures "Quest arm down-frame correction runtime" $questArmSolve 'CVarQuestArmDownFrameCorrection'
Test-RequiredText $failures "Quest arm dropout down fallback CVar header" $runtimeCVarsHeader 'CVarQuestArmDropoutDownFallback'
Test-RequiredText $failures "Quest arm dropout down fallback CVar source" $runtimeCVarsSource 'mp\.QuestArmDropoutDownFallback'
Test-RequiredText $failures "Quest arm dropout down fallback runtime" $questArmSolve 'CVarQuestArmDropoutDownFallback'
Test-RequiredText $failures "Quest arm dropout down fallback uses direct reach" $questArmSolve 'CalibratedDirectReachCm[\s\S]*FVector::Dist\(ShoulderWorld, WristWorld\)'
Test-RequiredText $failures "Quest arm dropout down fallback diagnostics" $diagnosticFormatter 'questArmDropoutDown='
Test-RequiredText $failures "Quest arm length calibration diagnostics" $diagnosticFormatter 'questArmLenCalibStage='
Test-RequiredText $failures "Quest constrained arm source elbow hint gate" $questArmSolve 'CVarQuestConstrainedArmMediaPipeElbowHint\.GetValueOnAnyThread\(\) > KINDA_SMALL_NUMBER'
Test-RequiredText $failures "Quest constrained arm reach-scale reset" $questWristCalibrationState 'bHasHmdRelativeReachObservedMax = false'
Test-RequiredText $failures "Quest constrained arm reach-scale regression" $questWristApplyPolicyTests 'Reach-scale calibration extends a high observed wearer reach toward avatar full reach'
Test-RequiredText $failures "Quest constrained arm pre-solver reach continuity regression" $questWristApplyPolicyTests 'pre-solver full-to-bent reach collapse'
Test-RequiredText $failures "Quest constrained solved-plane CVar header" $runtimeCVarsHeader 'CVarQuestConstrainedArmSolvedPlaneMinSin'
Test-RequiredText $failures "Quest constrained solved-plane CVar source" $runtimeCVarsSource 'mp\.QuestConstrainedArmSolvedPlaneMinSin'
Test-RequiredText $failures "Quest constrained solved-plane runtime threshold" $questArmSolve 'bQuestConstrainedArmSolveApplied[\s\S]*CVarQuestConstrainedArmSolvedPlaneMinSin\.GetValueOnAnyThread\(\)'
Test-RequiredText $failures "Quest legacy reach-assist stable pole" $questArmSolve 'StablePoleSideWorld\s*=\s*bIsLeft \? -ShoulderRightWorld : ShoulderRightWorld;'
Test-RequiredText $failures "Constrained arm down straighten availability" $constrainedArmSolver 'if \(!bEnableDownStraighten \|\| FullReachCm <= KINDA_SMALL_NUMBER\)'
Test-RequiredText $failures "Constrained arm no-torso side pole" $constrainedArmSolver 'SidePoleWorld\s*=\s*Input\.bIsLeft \? -Input\.ShoulderRightWorld : Input\.ShoulderRightWorld;'
Test-RequiredText $failures "Constrained arm side hemisphere lock" $constrainedArmSolver 'LockPoleToSideHemisphere'
Test-RequiredText $failures "Constrained arm move cap side invariant" $constrainedArmSolver 'KeepsCurrentArmSide'
Test-RequiredText $failures "Constrained arm source elbow hint helper" $constrainedArmSolver 'BuildSourceElbowHint'
Test-RequiredText $failures "Constrained arm source elbow hint runtime" $questArmSolve 'FMediaPipeConstrainedArmSourceElbowHintInput SourceElbowHintInput'
Test-RequiredText $failures "Constrained arm source elbow hint runtime" $questArmSolve 'SolveInput\.CurrentElbowWorld\s*=\s*ConstrainedSolveElbowHintWorld;'
Test-RequiredText $failures "Constrained arm source elbow hint diagnostics" $questArmSolve 'bConstrainedArmSourceElbowHintApplied'
Test-RequiredText $failures "Constrained arm source elbow hint regression" $constrainedArmSolverTests 'Source elbow hint transfers the MediaPipe elbow pole into the target Quest endpoint frame'
Test-RequiredText $failures "Embodiment log gate current arm target half-life" $embodimentLogGate 'armTargetHL=0\\\.00'
Test-RequiredText $failures "Embodiment log gate current arm rotation half-life" $embodimentLogGate 'armRotHL=0\\\.00'
Test-RequiredText $failures "Embodiment log gate current hand direct rotation" $embodimentLogGate 'handRotHL=0\\\.00'
Test-RequiredText $failures "Embodiment log gate current reach-scale default" $embodimentLogGate 'reachScale=1'
Test-RequiredText $failures "Embodiment log gate current loss-hold default" $embodimentLogGate 'armHoldLoss=1'
Test-RequiredText $failures "Embodiment log gate current dropout down fallback default" $embodimentLogGate 'armDropoutDown=1'
Test-RequiredText $failures "Embodiment log gate source elbow hint required" $embodimentLogGate 'questArmSourceElbowHint=1'
Test-RequiredText $failures "Embodiment log gate source elbow vector required" $embodimentLogGate 'questArmSourceElbow='
Test-RequiredText $failures "Embodiment log gate source elbow failure message" $embodimentLogGate 'no non-body-fallback Wallace constrained arm row proved the source-elbow-hint path'
Test-RequiredText $failures "Constrained arm body fallback side input" $questArmSolve 'FallbackInput\.bIsLeft\s*=\s*bIsLeft;'
Test-RequiredText $failures "Constrained arm body fallback shoulder-right input" $questArmSolve 'FallbackInput\.ShoulderRightWorld\s*=\s*ShoulderRightWorld;'
Test-RequiredText $failures "Constrained arm body fallback side-aware degenerate pole" $constrainedArmSolver 'BuildFallbackPole\(ReachDirWorld,\s*Input\.bIsLeft,\s*Input\.ShoulderRightWorld,\s*Input\.UpWorld\)'
Test-RequiredText $failures "Constrained arm fallback continuity side guard runtime" $questArmSolve 'ContinuityInput\.bConstrainElbowToCurrentSide\s*=\s*true;'
Test-RequiredText $failures "Constrained arm fallback continuity side guard solver" $constrainedArmSolver 'Input\.bConstrainElbowToCurrentSide'
Test-RequiredText $failures "Constrained arm degenerate fallback side-pole test" $constrainedArmSolverTests 'Degenerate left arms-down fallback keeps the left-side pole'
Test-RequiredText $failures "Constrained arm fallback wrong-history side test" $constrainedArmSolverTests 'Wrong-side fallback history is not used for continuity'
Test-RequiredText $failures "Constrained arm no-torso side-pose test" $constrainedArmSolverTests 'No-torso side-of-body arms-down trajectory frame solves'
Test-RequiredText $failures "Constrained arm wrong-history side lock" $constrainedArmSolverTests 'Wrong-side previous pole is locked back to the current arm side'
Test-RequiredText $failures "Constrained arm wrong-current side lock" $constrainedArmSolverTests 'Elbow move cap cannot preserve a wrong-side current elbow'
Test-RequiredText $failures "Constrained arm full motion sweep test" $constrainedArmSolverTests 'TestingKit3\.MediaPipe\.QuestConstrainedArm\.FullMotionSweep'
Test-RequiredText $failures "Constrained arm full sweep anatomical lengths" $constrainedArmSolverTests 'full arm sweep upper length remains anatomical'
Test-RequiredText $failures "Constrained arm diagonal thigh straightening test" $constrainedArmSolverTests 'Diagonal thigh-side endpoint reaches near-full straightness'
Test-RequiredText $failures "Constrained arm configured reach cap test" $constrainedArmSolverTests 'Forward full extension uses configured near-full reach cap'
Test-RequiredText $failures "Constrained arm diagonal thigh straightening source" $constrainedArmSolver 'SideOfBodyDownAlpha'
Test-RequiredText $failures "Solved elbow-plane arm basis helper" $bodySolverMath 'TryBuildSolvedElbowPlaneArmRotations'
Test-RequiredText $failures "Solved elbow-plane arm basis regression" $bodySolverMathTests 'TestingKit3\.MediaPipe\.BodySolverMath\.SolvedElbowPlaneArmBasis'
Test-RequiredText $failures "Solved elbow-plane near-full constrained regression" $bodySolverMathTests 'Near-full constrained arm pose write keeps the solved elbow plane'
Test-RequiredText $failures "Constrained arm pose-write uses solved elbow plane" $questArmSolve 'bQuestConstrainedArmSolveApplied \|\|'
Test-RequiredText $failures "Constrained arm pose-write math helper" $questArmSolve 'TryBuildSolvedElbowPlaneArmRotations'
Test-RequiredText $failures "Quest wrist endpoint policy" $questSpaceMapping 'ApplyPolicyInput\.bAllowUsableUntrackedForPositionApply\s*=\s*bConstrainedArmMode;'
Test-RequiredText $failures "Quest wrist endpoint continuity input" $questSpaceMapping 'ApplyPolicyInput\.bHasRecentAcceptedLiveWristPosition\s*=\s*bHasLastAcceptedLiveWristPosition;'
Test-RequiredText $failures "Quest wrist endpoint continuity input" $questSpaceMapping 'ApplyPolicyInput\.MaxUntrackedLiveWristStepCm\s*=\s*FMath::Max\(0\.0f,\s*CVarQuestWristPositionFilterResetDistanceCm\.GetValueOnAnyThread\(\)\);'
Test-RequiredText $failures "Quest wrist accepted live store" $questSpaceMapping 'QuestWristSideState\.bHasLastAcceptedLiveWristPosition\s*=\s*true;'
Test-RequiredText $failures "Quest wrist held target store" $questSpaceMapping 'auto StoreHeldQuestWristTarget'
Test-RequiredText $failures "Quest wrist accepted held update" $questSpaceMapping 'StoreAcceptedLiveWristPosition\(\);[\s\S]*StoreHeldQuestWristTarget\(QuestMappedWristWorld, QuestWristWorld, QuestMappedWristWorld\);'
if ($questSpaceMapping -match 'StoreAcceptedLiveWristPosition\(\);\s*if\s*\(bQuestSideTracked\)') {
    Add-Failure -Failures $failures -Message "Accepted live Quest wrist still updates held continuity only when tracked."
}
Test-RequiredText $failures "Quest wrist apply policy continuity" $questWristApplyPolicy 'Input\.UntrackedLiveWristStepFromLastAcceptedCm\s*>\s*Input\.MaxUntrackedLiveWristStepCm'
Test-RequiredText $failures "Quest wrist apply policy continuity" $questWristApplyPolicy 'Input\.LastAcceptedLiveWristAgeSeconds\s*>\s*Input\.MaxUntrackedLiveWristAgeSeconds'
Test-RequiredText $failures "Quest wrist attempt policy header" $questWristApplyPolicyHeader 'FMediaPipeQuestWristPositionAttemptInput'
Test-RequiredText $failures "Quest wrist attempt policy runtime" $questArmSolve 'ShouldAttemptPositionSolve\(WristAttemptInput\)'
Test-RequiredText $failures "Quest wrist attempt policy uses fresh held target" $questArmSolve 'bHasRecentQuestWristHeldTarget'
Test-RequiredText $failures "Quest wrist attempt policy freshness helper" $questWristApplyPolicyHeader 'HasFreshHeldTargetForPositionAttempt'
Test-RequiredText $failures "Quest wrist attempt policy uses held-loss freshness" $questArmSolve 'HasFreshHeldTargetForPositionAttempt\(HeldTargetLossInput\)[\s\S]*WristAttemptInput\.bHasHeldTarget\s*=\s*bHasRecentQuestWristHeldTarget;'
Test-RequiredText $failures "Quest wrist attempt policy held continuity" $questWristApplyPolicy 'Input\.bQuestArmUsesConstrainedSolve && Input\.bHasHeldTarget'
Test-RequiredText $failures "Quest wrist attempt policy regression" $questWristApplyPolicyTests 'held target continuity during brief loss'
Test-RequiredText $failures "Quest wrist stale held attempt regression" $questWristApplyPolicyTests 'Expired held target cannot waive wrist reliability'
Test-RequiredText $failures "Quest arm-loss hold policy header" $questWristApplyPolicyHeader 'FMediaPipeQuestArmHoldOnLossInput'
Test-RequiredText $failures "Quest arm-loss hold policy helper" $questWristApplyPolicy 'ShouldHoldArmOnQuestHandLoss'
Test-RequiredText $failures "Quest arm-loss hold waits for endpoint policy" $questArmSolve 'ShouldAttemptPositionSolve\(WristAttemptInput\)[\s\S]*ArmHoldInput\.bQuestWristPositionCandidate\s*=\s*bQuestWristPositionCandidate;[\s\S]*ShouldHoldArmOnQuestHandLoss\(ArmHoldInput\)'
Test-RequiredText $failures "Full arm-chain bypasses legacy arm-loss hold" $questArmSolve 'ArmHoldInput\.bHoldOnQuestHandLossEnabled\s*=\s*!bMetaHumanFullArmChainFresh\s*&&[\s\S]*CVarMediaPipeArmHoldOnQuestHandLoss\.GetValueOnAnyThread\(\) != 0;'
Test-RequiredText $failures "Full arm-chain blocks held MediaPipe arm overwrite" $questArmSolve 'if \(!bMetaHumanFullArmChainFresh && bHoldArmOnQuestHandLoss\)'
Test-RequiredText $failures "Quest arm-loss hold endpoint-candidate regression" $questWristApplyPolicyTests 'Quest wrist position candidate suppresses arm-loss hold'
if ($questArmSolve -match 'const bool bHoldArmOnQuestHandLoss\s*=\s*CVarMediaPipeArmHoldOnQuestHandLoss') {
    Add-Failure -Failures $failures -Message "Quest arm-loss hold still bypasses the wrist endpoint attempt policy."
}
Test-RequiredText $failures "Quest frame-coherent arm pose policy header" $questWristApplyPolicyHeader 'FMediaPipeQuestArmPoseWriteInput'
Test-RequiredText $failures "Quest frame-coherent arm pose policy helper" $questWristApplyPolicy 'ShouldWriteFrameCoherentQuestArmPose'
Test-RequiredText $failures "Quest frame-coherent arm pose runtime" $questArmSolve 'ShouldWriteFrameCoherentQuestArmPose\(ArmPoseWriteInput\)[\s\S]*EffectiveArmRotationHalfLifeSeconds = bFrameCoherentQuestArmPoseWrite[\s\S]*ArmMaxStepDegrees = bFrameCoherentQuestArmPoseWrite \? 0\.0f'
Test-RequiredText $failures "Quest frame-coherent arm pose regression" $questWristApplyPolicyTests 'HMD-relative Quest wrist endpoint writes the arm pose as one coherent frame'
Test-RequiredText $failures "Quest wrist expired held target clears authority helper" $questWristApplyPolicyHeader 'FMediaPipeQuestWristHeldTargetLossInput'
Test-RequiredText $failures "Quest wrist expired held target clears authority helper" $questWristApplyPolicy 'ShouldClearPositionAuthorityForHeldTargetLoss'
Test-RequiredText $failures "Quest wrist expired held target clears authority runtime" $questSpaceMapping 'ShouldClearPositionAuthorityForHeldTargetLoss\(HeldTargetLossInput\)[\s\S]*\{\s*FadePositionAuthorityToZero\(\);\s*return false;'
Test-RequiredText $failures "Quest wrist expired held target clears authority regression" $questWristApplyPolicyTests 'Expired held target clears stale wrist authority before reacquisition'
Test-RequiredText $failures "Quest wrist calibration reset clears position continuity helper" $questWristCalibrationState 'ResetPositionContinuity'
Test-RequiredText $failures "Quest wrist calibration reset clears held target" $questWristCalibrationState 'void FQuestWristSideRuntimeState::ResetCalibration\(\)[\s\S]*ResetPositionContinuity\(\);'
Test-RequiredText $failures "Quest wrist HMD avatar anchor reset clears position continuity" $questSpaceMapping 'bHasHmdRelativeAvatarCalibration = true;[\s\S]*Left\.ResetPositionContinuity\(\);[\s\S]*Right\.ResetPositionContinuity\(\);'
Test-RequiredText $failures "Quest wrist HMD translation reset clears position continuity" $questSpaceMapping 'if \(bResetTranslationFilter\)[\s\S]*Left\.ResetPositionContinuity\(!bKeepAcceptedArmLengthCalibration\);[\s\S]*Right\.ResetPositionContinuity\(!bKeepAcceptedArmLengthCalibration\);'
Test-RequiredText $failures "Quest wrist HMD translation reset preserves accepted arm length" $questSpaceMapping 'bKeepAcceptedArmLengthCalibration[\s\S]*ArmLengthCalibrationStage == QuestArmLengthCalibrationStage_Accepted'
Test-RequiredText $failures "Quest wrist calibration reset held target regression" $questWristCalibrationStateTests 'Left held target resets on calibration reset'
if ($questArmSolve -match '!bQuestArmUsesConstrainedSolve \|\| bQuestSideTrackedForArm') {
    Add-Failure -Failures $failures -Message "Quest constrained arm solve is still gated on bQuestSideTrackedForArm before the continuity policy can run."
}
Test-RequiredText $failures "Quest hand smoothing startup" $questHandRotation 'SmoothedQuestHandRotLocal\s*=\s*\(LowerArmRotCS\.Inverse\(\) \* MediaPipeHandTargetCS\.GetNormalized\(\)\)\.GetNormalized\(\);'
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestArmMode 3'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestWristPositionBlend 1\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmBodyFallback 0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmReachScaleCalibration 1'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmReachScaleUniform 1'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmMaxReachFraction 0\.997'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmMaxReachStepCm 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmSolvedPlaneMinSin 0\.08'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraighten 0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraightenThresholdCm 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraightenMaxCm 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraightenMinBelowShoulderRatio 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraightenReachFloorFraction 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestConstrainedArmDownStraightenMaxReachFraction 0\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestArmDropoutDownFallback 1'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestArmDropoutDownFallbackRecentTrackedSeconds 4\.0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestArmDropoutDownFallbackMinDownDominance 0\.55'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestArmDropoutDownFallbackBlendHalfLife 0\.08'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.QuestWristDriveTwistCorrection 0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.MediaPipeDriveMetaHumanArmHelpers 0'"
Test-RequiredText $failures "Quest objective gate" $questObjectiveGate "'mp\.MediaPipeDriveSpine 0'"

Write-Host "Wallace arm source guard"
Write-Host "ProjectRoot: $ProjectRoot"
Write-Host "Checked profile4, editor hand profile, eye-center camera default, component-space write APIs, pose-frame dropout hold, standard helper-bone ownership, MetaHuman sidecar helpers default-off, Oculus-style stateless twist helpers, source-parent-aware twist chains, arm/twist solve order, avatar-frame no-torso arm fallback and component pose-write basis, source-elbow-hint constrained solve, Wallace presentation post-process proof, configurable near-full reach cap, constrained-solve near-full pose-write threshold, frame-coherent Quest arm pose writes, no-torso arms-down straightening, dropout arms-down fallback, side-aware elbow poles, constrained-solve pose-write basis, full arm motion sweep coverage, continuity-gated untracked endpoint policy, current-frame untracked hand rotation policy, accepted-live held-target continuity, expired held-target authority reset, fresh held-target attempt gating, arm-loss hold policy ordering, calibration/anchor position-continuity reset, hand smoothing startup, and objective-gate CVars."

if ($failures.Count -gt 0) {
    Write-Host "FAIL"
    foreach ($failure in $failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host "PASS"
exit 0
