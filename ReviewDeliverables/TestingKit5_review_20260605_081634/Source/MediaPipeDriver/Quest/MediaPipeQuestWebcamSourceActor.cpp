#include "MediaPipeQuestWebcamSourceActor.h"

#include "MediaPipePoseLog.h"
#include "MediaPipePoseTrackerComponent.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include "Microsoft/COMPointer.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	TAutoConsoleVariable<int32> CVarAutoQuestWebcamDirectWmfCapture(
		TEXT("mp.AutoQuestWebcamDirectWmfCapture"),
		1,
		TEXT("Use a direct Windows Media Foundation source reader for webcam frames so MediaPipe can request a higher quality capture format."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamMediaPlayerFormatSelection(
		TEXT("mp.AutoQuestWebcamMediaPlayerFormatSelection"),
		0,
		TEXT("Experimental: force UMediaPlayer webcam track formats. Disabled by default because some WmfMedia capture formats expose only 2x2 placeholder textures."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamPreview(
		TEXT("mp.AutoQuestWebcamPreview"),
		1,
		TEXT("Show the direct live webcam feed as a PIE viewport overlay while MediaPipe Manny tracking is active."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamPreviewWidth(
		TEXT("mp.AutoQuestWebcamPreviewWidth"),
		640,
		TEXT("Width in pixels of the live webcam viewport preview overlay."));

	TAutoConsoleVariable<float> CVarAutoQuestWebcamPreviewHz(
		TEXT("mp.AutoQuestWebcamPreviewHz"),
		15.0f,
		TEXT("Maximum refresh rate for the live webcam viewport preview overlay."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamPreferredWidth(
		TEXT("mp.AutoQuestWebcamPreferredWidth"),
		1280,
		TEXT("Preferred live webcam capture width for the MediaPipe Manny source."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamPreferredHeight(
		TEXT("mp.AutoQuestWebcamPreferredHeight"),
		720,
		TEXT("Preferred live webcam capture height for the MediaPipe Manny source."));

	TAutoConsoleVariable<float> CVarAutoQuestWebcamPreferredFps(
		TEXT("mp.AutoQuestWebcamPreferredFps"),
		30.0f,
		TEXT("Preferred live webcam capture frame rate for the MediaPipe Manny source."));

	TAutoConsoleVariable<float> CVarAutoQuestWebcamMinFormatFps(
		TEXT("mp.AutoQuestWebcamMinFormatFps"),
		24.0f,
		TEXT("Minimum preferred webcam capture frame rate before lower-FPS formats are penalized."));

	TAutoConsoleVariable<float> CVarAutoQuestWebcamFormatValidationSeconds(
		TEXT("mp.AutoQuestWebcamFormatValidationSeconds"),
		0.75f,
		TEXT("Seconds to wait before falling back from a selected webcam format that produces invalid MediaTexture frames."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamMinValidFrameWidth(
		TEXT("mp.AutoQuestWebcamMinValidFrameWidth"),
		320,
		TEXT("Minimum readback width considered valid for a selected webcam format."));

	TAutoConsoleVariable<int32> CVarAutoQuestWebcamMinValidFrameHeight(
		TEXT("mp.AutoQuestWebcamMinValidFrameHeight"),
		240,
		TEXT("Minimum readback height considered valid for a selected webcam format."));

	struct FPreferredWebcamFormat
	{
		int32 TrackIndex = INDEX_NONE;
		int32 FormatIndex = INDEX_NONE;
		FIntPoint Dimensions = FIntPoint::ZeroValue;
		float FrameRate = 0.0f;
		FString Type;
		int64 Score = MIN_int64;
	};

	int32 WebcamFormatTypeScore(const FString& Type)
	{
		if (Type.Contains(TEXT("NV12"), ESearchCase::IgnoreCase))
		{
			return 4000;
		}
		if (Type.Contains(TEXT("MJPG"), ESearchCase::IgnoreCase) || Type.Contains(TEXT("MJPEG"), ESearchCase::IgnoreCase))
		{
			return 3000;
		}
		if (Type.Contains(TEXT("YUY"), ESearchCase::IgnoreCase))
		{
			return 1000;
		}
		return 0;
	}

	int64 ScoreWebcamFormat(const FIntPoint Dimensions, const float FrameRate, const FString& Type)
	{
		if (Dimensions.X <= 0 || Dimensions.Y <= 0)
		{
			return MIN_int64;
		}

		const int32 PreferredWidth = FMath::Max(1, CVarAutoQuestWebcamPreferredWidth.GetValueOnGameThread());
		const int32 PreferredHeight = FMath::Max(1, CVarAutoQuestWebcamPreferredHeight.GetValueOnGameThread());
		const float PreferredFps = FMath::Max(1.0f, CVarAutoQuestWebcamPreferredFps.GetValueOnGameThread());
		const float MinFps = FMath::Max(1.0f, CVarAutoQuestWebcamMinFormatFps.GetValueOnGameThread());
		const int64 Area = static_cast<int64>(Dimensions.X) * static_cast<int64>(Dimensions.Y);
		const int64 PreferredArea = static_cast<int64>(PreferredWidth) * static_cast<int64>(PreferredHeight);
		const bool bAtOrUnderPreferredSize = Dimensions.X <= PreferredWidth && Dimensions.Y <= PreferredHeight;
		const bool bMeetsMinFps = FrameRate <= 0.0f || FrameRate >= MinFps;
		const int64 AreaCloseness = -FMath::Abs(PreferredArea - Area);
		const int64 FpsCloseness = -static_cast<int64>(FMath::Abs(FrameRate - PreferredFps) * 1000.0f);

		int64 Score = 0;
		Score += bMeetsMinFps ? 10'000'000'000LL : -10'000'000'000LL;
		Score += bAtOrUnderPreferredSize ? 2'000'000'000LL : -2'000'000'000LL;
		Score += AreaCloseness * 1000LL;
		Score += FpsCloseness;
		Score += WebcamFormatTypeScore(Type);
		return Score;
	}

	int32 MakeCaptureFormatKey(const int32 TrackIndex, const int32 FormatIndex)
	{
		return TrackIndex * 10000 + FormatIndex;
	}

	FString NormalizeCaptureDeviceUrl(const FString& Url)
	{
		FString Normalized = Url;
		Normalized.RemoveFromStart(TEXT("vidcap://"), ESearchCase::IgnoreCase);
		Normalized.ReplaceInline(TEXT("/"), TEXT("\\"));
		return Normalized.TrimStartAndEnd();
	}

	FIntPoint ComputePreviewSize(const FIntPoint CaptureSize)
	{
		if (CaptureSize.X <= 0 || CaptureSize.Y <= 0)
		{
			return FIntPoint::ZeroValue;
		}

		const int32 PreviewWidth = FMath::Clamp(CVarAutoQuestWebcamPreviewWidth.GetValueOnGameThread(), 160, 960);
		const float Scale = static_cast<float>(PreviewWidth) / static_cast<float>(CaptureSize.X);
		return FIntPoint(
			FMath::Max(1, PreviewWidth),
			FMath::Max(1, FMath::RoundToInt(static_cast<float>(CaptureSize.Y) * Scale)));
	}

	bool BuildPreviewBgra(const TArray<uint8>& Rgb, const FIntPoint CaptureSize, const FIntPoint PreviewSize, TArray<uint8>& OutBgra)
	{
		if (CaptureSize.X <= 0 || CaptureSize.Y <= 0 ||
			PreviewSize.X <= 0 || PreviewSize.Y <= 0 ||
			Rgb.Num() < CaptureSize.X * CaptureSize.Y * 3)
		{
			return false;
		}

		OutBgra.SetNumUninitialized(PreviewSize.X * PreviewSize.Y * 4);
		for (int32 Y = 0; Y < PreviewSize.Y; ++Y)
		{
			const int32 SourceY = FMath::Clamp((Y * CaptureSize.Y) / PreviewSize.Y, 0, CaptureSize.Y - 1);
			for (int32 X = 0; X < PreviewSize.X; ++X)
			{
				const int32 SourceX = FMath::Clamp((X * CaptureSize.X) / PreviewSize.X, 0, CaptureSize.X - 1);
				const int32 SourceIndex = (SourceY * CaptureSize.X + SourceX) * 3;
				const int32 DestIndex = (Y * PreviewSize.X + X) * 4;
				OutBgra[DestIndex + 0] = Rgb[SourceIndex + 2];
				OutBgra[DestIndex + 1] = Rgb[SourceIndex + 1];
				OutBgra[DestIndex + 2] = Rgb[SourceIndex + 0];
				OutBgra[DestIndex + 3] = 255;
			}
		}
		return true;
	}

	void PutPreviewPixel(TArray<uint8>& Bgra, const FIntPoint Size, const int32 X, const int32 Y, const FColor Color)
	{
		if (X < 0 || Y < 0 || X >= Size.X || Y >= Size.Y)
		{
			return;
		}

		const int32 Index = (Y * Size.X + X) * 4;
		if (!Bgra.IsValidIndex(Index + 3))
		{
			return;
		}

		Bgra[Index + 0] = Color.B;
		Bgra[Index + 1] = Color.G;
		Bgra[Index + 2] = Color.R;
		Bgra[Index + 3] = Color.A;
	}

	void FillPreviewRect(TArray<uint8>& Bgra, const FIntPoint Size, const int32 X0, const int32 Y0, const int32 Width, const int32 Height, const FColor Color)
	{
		const int32 MinX = FMath::Clamp(X0, 0, Size.X);
		const int32 MinY = FMath::Clamp(Y0, 0, Size.Y);
		const int32 MaxX = FMath::Clamp(X0 + Width, 0, Size.X);
		const int32 MaxY = FMath::Clamp(Y0 + Height, 0, Size.Y);

		for (int32 Y = MinY; Y < MaxY; ++Y)
		{
			for (int32 X = MinX; X < MaxX; ++X)
			{
				PutPreviewPixel(Bgra, Size, X, Y, Color);
			}
		}
	}

	void DrawPreviewDisc(TArray<uint8>& Bgra, const FIntPoint Size, const FIntPoint Center, const int32 Radius, const FColor Color)
	{
		const int32 RadiusSq = Radius * Radius;
		for (int32 Y = Center.Y - Radius; Y <= Center.Y + Radius; ++Y)
		{
			for (int32 X = Center.X - Radius; X <= Center.X + Radius; ++X)
			{
				const int32 Dx = X - Center.X;
				const int32 Dy = Y - Center.Y;
				if (Dx * Dx + Dy * Dy <= RadiusSq)
				{
					PutPreviewPixel(Bgra, Size, X, Y, Color);
				}
			}
		}
	}

	void DrawPreviewLine(TArray<uint8>& Bgra, const FIntPoint Size, FIntPoint A, const FIntPoint B, const int32 Radius, const FColor Color)
	{
		const int32 Dx = FMath::Abs(B.X - A.X);
		const int32 Sx = A.X < B.X ? 1 : -1;
		const int32 Dy = -FMath::Abs(B.Y - A.Y);
		const int32 Sy = A.Y < B.Y ? 1 : -1;
		int32 Err = Dx + Dy;

		while (true)
		{
			DrawPreviewDisc(Bgra, Size, A, Radius, Color);
			if (A == B)
			{
				break;
			}

			const int32 E2 = 2 * Err;
			if (E2 >= Dy)
			{
				Err += Dy;
				A.X += Sx;
			}
			if (E2 <= Dx)
			{
				Err += Dx;
				A.Y += Sy;
			}
		}
	}

	float GetPreviewLandmarkReliability(const FMediaPipePoseLandmark& Landmark)
	{
		return FMath::Max(Landmark.Reliability, Landmark.Visibility * Landmark.Presence);
	}

	bool TryGetPreviewLandmarkPoint(
		const FMediaPipePoseFrame& Frame,
		const EMediaPipePoseLandmark LandmarkId,
		const FIntPoint PreviewSize,
		FIntPoint& OutPoint,
		float& OutReliability)
	{
		if (!Frame.bValid || PreviewSize.X <= 0 || PreviewSize.Y <= 0)
		{
			return false;
		}

		const int32 LandmarkIndex = static_cast<int32>(LandmarkId);
		if (!Frame.Normalized.IsValidIndex(LandmarkIndex))
		{
			return false;
		}

		const FMediaPipePoseLandmark& Landmark = Frame.Normalized.Points[LandmarkIndex];
		if (!FMath::IsFinite(Landmark.X) || !FMath::IsFinite(Landmark.Y) || !FMath::IsFinite(Landmark.Z))
		{
			return false;
		}

		const bool bNearFrame =
			Landmark.X >= -0.25f && Landmark.X <= 1.25f &&
			Landmark.Y >= -0.25f && Landmark.Y <= 1.25f;
		if (!bNearFrame)
		{
			return false;
		}

		const bool bInsideFrame =
			Landmark.X >= 0.0f && Landmark.X <= 1.0f &&
			Landmark.Y >= 0.0f && Landmark.Y <= 1.0f;

		OutReliability = GetPreviewLandmarkReliability(Landmark);
		if (!bInsideFrame)
		{
			OutReliability = FMath::Min(OutReliability, 0.1f);
		}

		OutPoint = FIntPoint(
			FMath::Clamp(FMath::RoundToInt(Landmark.X * static_cast<float>(PreviewSize.X - 1)), 0, PreviewSize.X - 1),
			FMath::Clamp(FMath::RoundToInt(Landmark.Y * static_cast<float>(PreviewSize.Y - 1)), 0, PreviewSize.Y - 1));
		return true;
	}

	FColor LandmarkDebugColor(const float Reliability, const FColor GoodColor)
	{
		if (Reliability < 0.05f)
		{
			return FColor(255, 48, 48, 255);
		}
		if (Reliability < 0.35f)
		{
			return FColor(255, 176, 0, 255);
		}
		return GoodColor;
	}

	void DrawPreviewLandmark(
		TArray<uint8>& Bgra,
		const FIntPoint Size,
		const FMediaPipePoseFrame& Frame,
		const EMediaPipePoseLandmark LandmarkId,
		const FColor GoodColor,
		const int32 Radius)
	{
		FIntPoint Point;
		float Reliability = 0.0f;
		if (!TryGetPreviewLandmarkPoint(Frame, LandmarkId, Size, Point, Reliability))
		{
			return;
		}

		DrawPreviewDisc(Bgra, Size, Point, Radius + 1, FColor(0, 0, 0, 255));
		DrawPreviewDisc(Bgra, Size, Point, Radius, LandmarkDebugColor(Reliability, GoodColor));
	}

	void DrawPreviewConnection(
		TArray<uint8>& Bgra,
		const FIntPoint Size,
		const FMediaPipePoseFrame& Frame,
		const EMediaPipePoseLandmark AId,
		const EMediaPipePoseLandmark BId,
		const FColor GoodColor)
	{
		FIntPoint A;
		FIntPoint B;
		float AReliability = 0.0f;
		float BReliability = 0.0f;
		if (!TryGetPreviewLandmarkPoint(Frame, AId, Size, A, AReliability) ||
			!TryGetPreviewLandmarkPoint(Frame, BId, Size, B, BReliability))
		{
			return;
		}

		const FColor Color = LandmarkDebugColor(FMath::Min(AReliability, BReliability), GoodColor);
		DrawPreviewLine(Bgra, Size, A, B, 2, FColor(0, 0, 0, 255));
		DrawPreviewLine(Bgra, Size, A, B, 1, Color);
	}

	void DrawPreviewMidpointConnection(
		TArray<uint8>& Bgra,
		const FIntPoint Size,
		const FMediaPipePoseFrame& Frame,
		const EMediaPipePoseLandmark AId,
		const EMediaPipePoseLandmark BId,
		const EMediaPipePoseLandmark CId,
		const FColor GoodColor)
	{
		FIntPoint A;
		FIntPoint B;
		FIntPoint C;
		float AReliability = 0.0f;
		float BReliability = 0.0f;
		float CReliability = 0.0f;
		if (!TryGetPreviewLandmarkPoint(Frame, AId, Size, A, AReliability) ||
			!TryGetPreviewLandmarkPoint(Frame, BId, Size, B, BReliability) ||
			!TryGetPreviewLandmarkPoint(Frame, CId, Size, C, CReliability))
		{
			return;
		}

		const FIntPoint Mid((A.X + B.X) / 2, (A.Y + B.Y) / 2);
		const FColor Color = LandmarkDebugColor(FMath::Min3(AReliability, BReliability, CReliability), GoodColor);
		DrawPreviewLine(Bgra, Size, Mid, C, 2, FColor(0, 0, 0, 255));
		DrawPreviewLine(Bgra, Size, Mid, C, 1, Color);
	}

	bool TryGetPreviewFacePoint(
		const FMediaPipePoseFrame& Frame,
		const int32 FaceIndex,
		const FIntPoint PreviewSize,
		FIntPoint& OutPoint)
	{
		if (!Frame.bValid ||
			!Frame.bHasFace ||
			Frame.Face.bHasFace == 0 ||
			FaceIndex < 0 ||
			FaceIndex >= Frame.Face.Normalized.Count ||
			FaceIndex >= MediaPipeFaceLandmarkMaxCount ||
			PreviewSize.X <= 0 ||
			PreviewSize.Y <= 0)
		{
			return false;
		}

		const FMediaPipeRawHandLandmark& Landmark = Frame.Face.Normalized.Landmarks[FaceIndex];
		if (!FMath::IsFinite(Landmark.X) || !FMath::IsFinite(Landmark.Y))
		{
			return false;
		}

		if (Landmark.X < -0.25f || Landmark.X > 1.25f || Landmark.Y < -0.25f || Landmark.Y > 1.25f)
		{
			return false;
		}

		OutPoint = FIntPoint(
			FMath::Clamp(FMath::RoundToInt(Landmark.X * static_cast<float>(PreviewSize.X - 1)), 0, PreviewSize.X - 1),
			FMath::Clamp(FMath::RoundToInt(Landmark.Y * static_cast<float>(PreviewSize.Y - 1)), 0, PreviewSize.Y - 1));
		return true;
	}

	void DrawFacePreviewLandmark(TArray<uint8>& Bgra, const FIntPoint Size, const FIntPoint Point, const FColor Color, const int32 Radius)
	{
		DrawPreviewDisc(Bgra, Size, Point, Radius + 1, FColor(0, 0, 0, 255));
		DrawPreviewDisc(Bgra, Size, Point, Radius, Color);
	}

	void DrawDenseFaceHeadOverlay(TArray<uint8>& Bgra, const FIntPoint Size, const FMediaPipePoseFrame& Frame)
	{
		FIntPoint LeftEye;
		FIntPoint RightEye;
		FIntPoint Nose;
		FIntPoint Chin;
		const bool bHasLeftEye = TryGetPreviewFacePoint(Frame, 33, Size, LeftEye);
		const bool bHasRightEye = TryGetPreviewFacePoint(Frame, 263, Size, RightEye);
		const bool bHasNose = TryGetPreviewFacePoint(Frame, 1, Size, Nose);
		const bool bHasChin = TryGetPreviewFacePoint(Frame, 152, Size, Chin);

		const FColor FaceEyeColor(255, 255, 255, 255);
		const FColor FaceNoseColor(255, 64, 255, 255);
		const FColor FaceChinColor(180, 96, 255, 255);

		if (bHasLeftEye && bHasRightEye)
		{
			DrawPreviewLine(Bgra, Size, LeftEye, RightEye, 2, FColor(0, 0, 0, 255));
			DrawPreviewLine(Bgra, Size, LeftEye, RightEye, 1, FaceEyeColor);
		}
		if (bHasNose && bHasChin)
		{
			DrawPreviewLine(Bgra, Size, Nose, Chin, 2, FColor(0, 0, 0, 255));
			DrawPreviewLine(Bgra, Size, Nose, Chin, 1, FaceNoseColor);
		}

		if (bHasLeftEye)
		{
			DrawFacePreviewLandmark(Bgra, Size, LeftEye, FaceEyeColor, 4);
		}
		if (bHasRightEye)
		{
			DrawFacePreviewLandmark(Bgra, Size, RightEye, FaceEyeColor, 4);
		}
		if (bHasNose)
		{
			DrawFacePreviewLandmark(Bgra, Size, Nose, FaceNoseColor, 5);
		}
		if (bHasChin)
		{
			DrawFacePreviewLandmark(Bgra, Size, Chin, FaceChinColor, 4);
		}
	}

	void DrawPoseDebugOverlay(TArray<uint8>& Bgra, const FIntPoint Size, const FMediaPipePoseFrame& Frame, const bool bHasFrame, const float PoseAgeMs)
	{
		const bool bFresh = bHasFrame && PoseAgeMs >= 0.0f && PoseAgeMs <= 250.0f;
		const FColor StatusColor = bFresh ? FColor(0, 220, 80, 255) : FColor(255, 48, 48, 255);
		FillPreviewRect(Bgra, Size, 0, 0, Size.X, 4, StatusColor);

		if (!bHasFrame || !Frame.bValid)
		{
			return;
		}

		const FColor NoseColor(255, 232, 0, 255);
		const FColor EyeColor(0, 220, 255, 255);
		const FColor EarColor(40, 255, 120, 255);
		const FColor MouthColor(255, 80, 220, 255);
		const FColor ShoulderColor(64, 144, 255, 255);

		DrawPreviewConnection(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftEye, EMediaPipePoseLandmark::RightEye, EyeColor);
		DrawPreviewConnection(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftEar, EMediaPipePoseLandmark::RightEar, EarColor);
		DrawPreviewConnection(Bgra, Size, Frame, EMediaPipePoseLandmark::MouthLeft, EMediaPipePoseLandmark::MouthRight, MouthColor);
		DrawPreviewConnection(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftShoulder, EMediaPipePoseLandmark::RightShoulder, ShoulderColor);
		DrawPreviewMidpointConnection(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftShoulder, EMediaPipePoseLandmark::RightShoulder, EMediaPipePoseLandmark::Nose, NoseColor);

		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::Nose, NoseColor, 5);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftEye, EyeColor, 4);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::RightEye, EyeColor, 4);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftEar, EarColor, 4);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::RightEar, EarColor, 4);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::MouthLeft, MouthColor, 3);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::MouthRight, MouthColor, 3);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::LeftShoulder, ShoulderColor, 5);
		DrawPreviewLandmark(Bgra, Size, Frame, EMediaPipePoseLandmark::RightShoulder, ShoulderColor, 5);
		DrawDenseFaceHeadOverlay(Bgra, Size, Frame);
	}

	float LandmarkReliabilityForStatus(const FMediaPipePoseFrame& Frame, const EMediaPipePoseLandmark LandmarkId)
	{
		const int32 LandmarkIndex = static_cast<int32>(LandmarkId);
		if (!Frame.bValid || !Frame.Normalized.IsValidIndex(LandmarkIndex))
		{
			return -1.0f;
		}

		const FMediaPipePoseLandmark& Landmark = Frame.Normalized.Points[LandmarkIndex];
		if (!FMath::IsFinite(Landmark.X) || !FMath::IsFinite(Landmark.Y))
		{
			return -1.0f;
		}
		return GetPreviewLandmarkReliability(Landmark);
	}

	FString ReliabilityText(const float Reliability)
	{
		return Reliability < 0.0f ? FString(TEXT("--")) : FString::Printf(TEXT("%.2f"), Reliability);
	}

	FString BuildPosePreviewStatusText(
		const FMediaPipePoseFrame& Frame,
		const bool bHasFrame,
		const float PoseAgeMs,
		const FMediaPipePosePipelineStats& Stats)
	{
		const FString PoseState = bHasFrame && Frame.bValid ? TEXT("RAW POSE") : TEXT("NO RAW POSE");
		const FString AgeText = PoseAgeMs >= 0.0f ? FString::Printf(TEXT("%.0fms"), PoseAgeMs) : FString(TEXT("--ms"));
		const FString NoseReliability = ReliabilityText(LandmarkReliabilityForStatus(Frame, EMediaPipePoseLandmark::Nose));
		const FString LeftEarReliability = ReliabilityText(LandmarkReliabilityForStatus(Frame, EMediaPipePoseLandmark::LeftEar));
		const FString RightEarReliability = ReliabilityText(LandmarkReliabilityForStatus(Frame, EMediaPipePoseLandmark::RightEar));
		const FString LeftShoulderReliability = ReliabilityText(LandmarkReliabilityForStatus(Frame, EMediaPipePoseLandmark::LeftShoulder));
		const FString RightShoulderReliability = ReliabilityText(LandmarkReliabilityForStatus(Frame, EMediaPipePoseLandmark::RightShoulder));
		const int32 FaceCount = bHasFrame && Frame.bHasFace && Frame.Face.bHasFace != 0
			? FMath::Clamp(Frame.Face.Normalized.Count, 0, MediaPipeFaceLandmarkMaxCount)
			: 0;

		return FString::Printf(
			TEXT("%s %s | pub %lld proc %lld fail %lld | %dx%d>%dx%d | face %d | N %s E %s/%s S %s/%s"),
			*PoseState,
			*AgeText,
			Stats.TrackerPublishCount,
			Stats.WorkerProcessCount,
			Stats.WorkerProcessFailCount,
			Stats.LastCaptureSize.X,
			Stats.LastCaptureSize.Y,
			Stats.LastInferenceSize.X,
			Stats.LastInferenceSize.Y,
			FaceCount,
			*NoseReliability,
			*LeftEarReliability,
			*RightEarReliability,
			*LeftShoulderReliability,
			*RightShoulderReliability);
	}
}

struct FMediaPipeDirectWmfPreviewOverlay
{
	void Ensure(UTexture2D* Texture, const FIntPoint TextureSize, const FString& StatusText)
	{
		if (!GEngine || !GEngine->GameViewport || !Texture || TextureSize.X <= 0 || TextureSize.Y <= 0)
		{
			return;
		}

		Brush.SetResourceObject(Texture);
		Brush.ImageSize = FVector2D(TextureSize.X, TextureSize.Y);
		if (StatusTextBlock.IsValid())
		{
			StatusTextBlock->SetText(FText::FromString(StatusText));
		}

		if (Widget.IsValid() && Viewport.Get() == GEngine->GameViewport && WidgetTextureSize == TextureSize)
		{
			return;
		}

		Remove();
		Viewport = GEngine->GameViewport;
		Widget =
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(18.0f))
			[
				SNew(SBox)
				.WidthOverride(TextureSize.X)
				.HeightOverride(TextureSize.Y)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(&Brush)
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					.Padding(FMargin(6.0f))
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.58f))
						.Padding(FMargin(5.0f, 2.0f))
						[
							SAssignNew(StatusTextBlock, STextBlock)
							.Text(FText::FromString(StatusText))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
							.ShadowOffset(FVector2D(1.0f, 1.0f))
							.ShadowColorAndOpacity(FLinearColor::Black)
						]
					]
				]
			];

		WidgetTextureSize = TextureSize;
		GEngine->GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(), 1000);
	}

	void Remove()
	{
		if (Widget.IsValid())
		{
			if (UGameViewportClient* ViewportClient = Viewport.Get())
			{
				ViewportClient->RemoveViewportWidgetContent(Widget.ToSharedRef());
			}
			Widget.Reset();
		}
		Viewport.Reset();
		StatusTextBlock.Reset();
		WidgetTextureSize = FIntPoint::ZeroValue;
		Brush.SetResourceObject(nullptr);
	}

	FSlateBrush Brush;
	TSharedPtr<SWidget> Widget;
	TSharedPtr<STextBlock> StatusTextBlock;
	TWeakObjectPtr<UGameViewportClient> Viewport;
	FIntPoint WidgetTextureSize = FIntPoint::ZeroValue;
};

struct FMediaPipeDirectWmfCapture
{
	~FMediaPipeDirectWmfCapture()
	{
		Close();
	}

	bool Open(const FString& CaptureDeviceUrl)
	{
#if PLATFORM_WINDOWS
		Close();
		const HRESULT StartupResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
		if (FAILED(StartupResult))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: MFStartup failed: 0x%08x"), StartupResult);
			return false;
		}
		bMfStarted = true;

		TComPtr<IMFAttributes> DeviceAttributes;
		HRESULT Result = MFCreateAttributes(&DeviceAttributes, 1);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: MFCreateAttributes failed: 0x%08x"), Result);
			Close();
			return false;
		}
		DeviceAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

		IMFActivate** Devices = nullptr;
		UINT32 DeviceCount = 0;
		Result = MFEnumDeviceSources(DeviceAttributes, &Devices, &DeviceCount);
		if (FAILED(Result) || DeviceCount == 0)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: MFEnumDeviceSources failed or returned no devices: 0x%08x count=%u"), Result, DeviceCount);
			Close();
			return false;
		}

		const FString WantedSymbolicLink = NormalizeCaptureDeviceUrl(CaptureDeviceUrl);
		TComPtr<IMFActivate> SelectedDevice;
		for (UINT32 DeviceIndex = 0; DeviceIndex < DeviceCount; ++DeviceIndex)
		{
			WCHAR* SymbolicLinkRaw = nullptr;
			UINT32 SymbolicLinkLength = 0;
			FString SymbolicLink;
			if (SUCCEEDED(Devices[DeviceIndex]->GetAllocatedString(
				MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
				&SymbolicLinkRaw,
				&SymbolicLinkLength)))
			{
				SymbolicLink = FString(SymbolicLinkLength, SymbolicLinkRaw);
				CoTaskMemFree(SymbolicLinkRaw);
			}

			if (WantedSymbolicLink.IsEmpty() || SymbolicLink.Equals(WantedSymbolicLink, ESearchCase::IgnoreCase))
			{
				SelectedDevice = Devices[DeviceIndex];
				break;
			}
		}

		for (UINT32 DeviceIndex = 0; DeviceIndex < DeviceCount; ++DeviceIndex)
		{
			if (Devices[DeviceIndex])
			{
				Devices[DeviceIndex]->Release();
			}
		}
		CoTaskMemFree(Devices);

		if (!SelectedDevice)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: capture device not found for url=%s"), *CaptureDeviceUrl);
			Close();
			return false;
		}

		Result = SelectedDevice->ActivateObject(IID_PPV_ARGS(&MediaSource));
		if (FAILED(Result) || !MediaSource)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: ActivateObject failed: 0x%08x"), Result);
			Close();
			return false;
		}

		TComPtr<IMFAttributes> ReaderAttributes;
		Result = MFCreateAttributes(&ReaderAttributes, 2);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: reader attribute creation failed: 0x%08x"), Result);
			Close();
			return false;
		}
		ReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1);

		Result = MFCreateSourceReaderFromMediaSource(MediaSource, ReaderAttributes, &Reader);
		if (FAILED(Result) || !Reader)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: source reader creation failed: 0x%08x"), Result);
			Close();
			return false;
		}

		return SelectPreferredFormat();
#else
		return false;
#endif
	}

	void Close()
	{
#if PLATFORM_WINDOWS
		Reader.Reset();
		if (MediaSource)
		{
			MediaSource->Shutdown();
			MediaSource.Reset();
		}
		if (bMfStarted)
		{
			MFShutdown();
			bMfStarted = false;
		}
#endif
		bOpen = false;
		CaptureSize = FIntPoint::ZeroValue;
		FrameRate = 0.0;
		SelectedSubtype = FString();
	}

	bool IsOpen() const
	{
		return bOpen;
	}

	bool ReadFrame(TArray<uint8>& OutRgb, FIntPoint& OutSize, int64& OutTimestampUs, double& OutFrameRate)
	{
#if PLATFORM_WINDOWS
		if (!Reader || !bOpen || CaptureSize.X <= 0 || CaptureSize.Y <= 0)
		{
			return false;
		}

		DWORD StreamIndex = 0;
		DWORD Flags = 0;
		LONGLONG SampleTime = 0;
		TComPtr<IMFSample> Sample;
		const HRESULT Result = Reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &StreamIndex, &Flags, &SampleTime, &Sample);
		if (FAILED(Result) || !Sample || (Flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
		{
			return false;
		}

		TComPtr<IMFMediaBuffer> Buffer;
		if (FAILED(Sample->ConvertToContiguousBuffer(&Buffer)) || !Buffer)
		{
			return false;
		}

		BYTE* Data = nullptr;
		DWORD MaxLength = 0;
		DWORD CurrentLength = 0;
		if (FAILED(Buffer->Lock(&Data, &MaxLength, &CurrentLength)) || !Data)
		{
			return false;
		}

		const int32 Width = CaptureSize.X;
		const int32 Height = CaptureSize.Y;
		const int32 SourceStride = Width * 4;
		const int32 RequiredBytes = SourceStride * Height;
		if (static_cast<int32>(CurrentLength) < RequiredBytes)
		{
			Buffer->Unlock();
			return false;
		}

		OutRgb.SetNumUninitialized(Width * Height * 3);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			const BYTE* SourceRow = Data + Y * SourceStride;
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 SourceIndex = X * 4;
				const int32 DestIndex = (Y * Width + X) * 3;
				OutRgb[DestIndex + 0] = SourceRow[SourceIndex + 2];
				OutRgb[DestIndex + 1] = SourceRow[SourceIndex + 1];
				OutRgb[DestIndex + 2] = SourceRow[SourceIndex + 0];
			}
		}
		Buffer->Unlock();

		OutSize = CaptureSize;
		OutTimestampUs = static_cast<int64>(FPlatformTime::Seconds() * 1000000.0);
		OutFrameRate = FrameRate;
		return true;
#else
		return false;
#endif
	}

	FIntPoint GetCaptureSize() const
	{
		return CaptureSize;
	}

	double GetFrameRate() const
	{
		return FrameRate;
	}

	FString GetSelectedSubtype() const
	{
		return SelectedSubtype;
	}

private:
#if PLATFORM_WINDOWS
	bool SelectPreferredFormat()
	{
		if (!Reader)
		{
			return false;
		}

		struct FCandidate
		{
			TComPtr<IMFMediaType> NativeType;
			FIntPoint Size = FIntPoint::ZeroValue;
			double Fps = 0.0;
			FString TypeName;
			int64 Score = MIN_int64;
		};

		FCandidate Best;
		for (DWORD TypeIndex = 0;; ++TypeIndex)
		{
			TComPtr<IMFMediaType> NativeType;
			const HRESULT TypeResult = Reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TypeIndex, &NativeType);
			if (TypeResult == MF_E_NO_MORE_TYPES)
			{
				break;
			}
			if (FAILED(TypeResult) || !NativeType)
			{
				continue;
			}

			UINT32 Width = 0;
			UINT32 Height = 0;
			if (FAILED(MFGetAttributeSize(NativeType, MF_MT_FRAME_SIZE, &Width, &Height)) || Width == 0 || Height == 0)
			{
				continue;
			}

			UINT32 FpsNumerator = 0;
			UINT32 FpsDenominator = 1;
			MFGetAttributeRatio(NativeType, MF_MT_FRAME_RATE, &FpsNumerator, &FpsDenominator);
			const float CandidateFps = FpsDenominator > 0 ? static_cast<float>(FpsNumerator) / static_cast<float>(FpsDenominator) : 0.0f;

			GUID Subtype = GUID_NULL;
			NativeType->GetGUID(MF_MT_SUBTYPE, &Subtype);
			const FString TypeName = FourccToString(Subtype);
			const FIntPoint CandidateSize(static_cast<int32>(Width), static_cast<int32>(Height));
			const int64 Score = ScoreWebcamFormat(CandidateSize, CandidateFps, TypeName);
			if (Score > Best.Score)
			{
				Best.NativeType = NativeType;
				Best.Size = CandidateSize;
				Best.Fps = CandidateFps;
				Best.TypeName = TypeName;
				Best.Score = Score;
			}
		}

		if (!Best.NativeType)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: no native video format found."));
			return false;
		}

		HRESULT Result = Reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, false);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: failed to deselect streams: 0x%08x"), Result);
			return false;
		}
		Result = Reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, true);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: failed to select video stream: 0x%08x"), Result);
			return false;
		}

		Result = Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, Best.NativeType);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: failed to set native type %dx%d@%.2f %s: 0x%08x"),
				Best.Size.X, Best.Size.Y, Best.Fps, *Best.TypeName, Result);
			return false;
		}

		TComPtr<IMFMediaType> OutputType;
		Result = MFCreateMediaType(&OutputType);
		if (FAILED(Result) || !OutputType)
		{
			return false;
		}
		OutputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		OutputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		OutputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		MFSetAttributeSize(OutputType, MF_MT_FRAME_SIZE, Best.Size.X, Best.Size.Y);
		if (Best.Fps > 0.0)
		{
			const UINT32 FpsNumerator = static_cast<UINT32>(FMath::RoundToInt(Best.Fps * 1000.0));
			MFSetAttributeRatio(OutputType, MF_MT_FRAME_RATE, FpsNumerator, 1000);
		}

		Result = Reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, OutputType);
		if (FAILED(Result))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Direct WMF webcam: failed to set RGB32 output type %dx%d@%.2f from %s: 0x%08x"),
				Best.Size.X, Best.Size.Y, Best.Fps, *Best.TypeName, Result);
			return false;
		}

		CaptureSize = Best.Size;
		FrameRate = Best.Fps > 0.0 ? Best.Fps : 30.0;
		SelectedSubtype = Best.TypeName;
		bOpen = true;
		UE_LOG(LogMediaPipePose, Log, TEXT("Direct WMF webcam selected format: size=%dx%d fps=%.2f native=%s"),
			CaptureSize.X, CaptureSize.Y, FrameRate, *SelectedSubtype);
		return true;
	}

	FString FourccToString(const GUID& Subtype) const
	{
		if (Subtype == MFVideoFormat_NV12)
		{
			return TEXT("NV12");
		}
		if (Subtype == MFVideoFormat_MJPG)
		{
			return TEXT("MJPG");
		}
		if (Subtype == MFVideoFormat_YUY2)
		{
			return TEXT("YUY2");
		}
		if (Subtype == MFVideoFormat_RGB32)
		{
			return TEXT("RGB32");
		}
		return FString::Printf(TEXT("{%08x-%04x-%04x}"), Subtype.Data1, Subtype.Data2, Subtype.Data3);
	}

	TComPtr<IMFMediaSource> MediaSource;
	TComPtr<IMFSourceReader> Reader;
	bool bMfStarted = false;
#endif

	bool bOpen = false;
	FIntPoint CaptureSize = FIntPoint::ZeroValue;
	double FrameRate = 0.0;
	FString SelectedSubtype;
};

AMediaPipeQuestWebcamSourceActor::AMediaPipeQuestWebcamSourceActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PoseTracker = CreateDefaultSubobject<UMediaPipePoseTrackerComponent>(TEXT("PoseTracker"));
	if (PoseTracker)
	{
		PoseTracker->PrimaryComponentTick.bStartWithTickEnabled = false;
		PoseTracker->SetComponentTickEnabled(false);
	}
}

AMediaPipeQuestWebcamSourceActor::~AMediaPipeQuestWebcamSourceActor()
{
	RemoveDirectPreviewOverlay();
	delete DirectWmfCapture;
	DirectWmfCapture = nullptr;
}

void AMediaPipeQuestWebcamSourceActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureMediaRuntime();
	OpenConfiguredCaptureDevice();
}

void AMediaPipeQuestWebcamSourceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (PoseTracker)
	{
		PoseTracker->WorldScale = WorldScale;
		PoseTracker->bMirrorLandmarksLR = bMirrorLandmarksLR;
		if (DirectWmfCapture && DirectWmfCapture->IsOpen())
		{
			TArray<uint8> Rgb;
			FIntPoint CaptureSize = FIntPoint::ZeroValue;
			int64 TimestampUs = 0;
			double FrameRate = 0.0;
			DirectWmfCapture->ReadFrame(Rgb, CaptureSize, TimestampUs, FrameRate);
			if (Rgb.Num() > 0 && CaptureSize.X > 0 && CaptureSize.Y > 0)
			{
				UpdateDirectPreviewTexture(Rgb, CaptureSize);
				PoseTracker->ProcessRgbFrame(MoveTemp(Rgb), CaptureSize, TimestampUs, CaptureSize, FrameRate);
			}
		}
		else
		{
			PoseTracker->ProcessFrame();
			ValidateCaptureFormat();
		}
	}
}

void AMediaPipeQuestWebcamSourceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DirectWmfCapture)
	{
		DirectWmfCapture->Close();
		delete DirectWmfCapture;
		DirectWmfCapture = nullptr;
	}
	RemoveDirectPreviewOverlay();

	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed);
		MediaPlayer->Close();
	}

	Super::EndPlay(EndPlayReason);
}

void AMediaPipeQuestWebcamSourceActor::ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName)
{
	CaptureDeviceUrl = InCaptureDeviceUrl;
	CaptureDeviceDisplayName = InDisplayName;
	if (DirectWmfCapture)
	{
		DirectWmfCapture->Close();
		delete DirectWmfCapture;
		DirectWmfCapture = nullptr;
	}
	RemoveDirectPreviewOverlay();
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}
	RejectedCaptureFormatKeys.Reset();
	RejectedCaptureFormatDimensions.Reset();
	ActiveCaptureFormatKey = INDEX_NONE;
	ActiveCaptureFormatDimensions = FIntPoint::ZeroValue;
	LastCaptureFormatSelectWallSeconds = -1.0;

	if (HasActorBegunPlay())
	{
		EnsureMediaRuntime();
		OpenConfiguredCaptureDevice();
	}
}

void AMediaPipeQuestWebcamSourceActor::ConfigureLowLoadDefaults(float MaxHz, const FString& ModelPath, int32 InputMaxDimension)
{
	if (PoseTracker)
	{
		PoseTracker->MaxProcessRateHz = MaxHz;
		PoseTracker->ConfigPath = ModelPath;
		PoseTracker->bEnableHandLandmarker = false;
		PoseTracker->bEnableHolisticLandmarker = true;
		PoseTracker->MinPoseDetectionConfidence = 0.25f;
		PoseTracker->MinPosePresenceConfidence = 0.25f;
		PoseTracker->MinTrackingConfidence = 0.25f;
		PoseTracker->bAsyncMediaTextureReadback = true;
		PoseTracker->bUseSourceConditioning = true;
	}

	if (IConsoleVariable* MaxDimensionCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeInputMaxDimension")))
	{
		MaxDimensionCVar->Set(InputMaxDimension, ECVF_SetByConsole);
	}
}

void AMediaPipeQuestWebcamSourceActor::EnsureMediaRuntime()
{
	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, TEXT("MediaPlayer"));
	}

	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed);
		MediaPlayer->SetLooping(false);
		MediaPlayer->PlayOnOpen = bAutoPlay;
	}

	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, TEXT("MediaTexture"));
	}

	RefreshMediaTextureBinding();
	ConfigureTrackerSource();
}

void AMediaPipeQuestWebcamSourceActor::ConfigureTrackerSource() const
{
	if (!PoseTracker)
	{
		return;
	}

	PoseTracker->SourceType = EMediaPipePoseFrameSource::MediaTexture;
	PoseTracker->SourceMediaTexture = MediaTexture;
	PoseTracker->Initialize();
}

void AMediaPipeQuestWebcamSourceActor::OpenConfiguredCaptureDevice()
{
	if (!MediaPlayer)
	{
		return;
	}

	if (CaptureDeviceUrl.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source: capture device URL is empty."));
		return;
	}

	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}
	RejectedCaptureFormatKeys.Reset();
	RejectedCaptureFormatDimensions.Reset();
	ActiveCaptureFormatKey = INDEX_NONE;
	ActiveCaptureFormatDimensions = FIntPoint::ZeroValue;
	LastCaptureFormatSelectWallSeconds = -1.0;
	if (DirectWmfCapture)
	{
		DirectWmfCapture->Close();
		delete DirectWmfCapture;
		DirectWmfCapture = nullptr;
	}
	RemoveDirectPreviewOverlay();

	MediaPlayer->Close();
	MediaPlayer->PlayOnOpen = bAutoPlay;

	const FString ResolvedMediaFile = FPaths::IsRelative(CaptureDeviceUrl)
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), CaptureDeviceUrl))
		: FPaths::ConvertRelativePathToFull(CaptureDeviceUrl);
	if (FPaths::FileExists(ResolvedMediaFile))
	{
		MediaPlayer->SetLooping(true);
		if (!MediaPlayer->OpenFile(ResolvedMediaFile))
		{
			UE_LOG(LogMediaPipePose, Error, TEXT("Quest webcam source: failed to open video file: %s"), *ResolvedMediaFile);
			return;
		}

		UE_LOG(LogMediaPipePose, Log, TEXT("Quest webcam source using video file: %s"), *ResolvedMediaFile);
		return;
	}

	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = false;
	const bool bLiveCapture = CaptureDeviceUrl.StartsWith(TEXT("vidcap://"), ESearchCase::IgnoreCase);
	if (bLiveCapture && CVarAutoQuestWebcamDirectWmfCapture.GetValueOnGameThread() != 0)
	{
		DirectWmfCapture = new FMediaPipeDirectWmfCapture();
		if (DirectWmfCapture->Open(CaptureDeviceUrl))
		{
			if (PoseTracker)
			{
				PoseTracker->ResetForSourceDiscontinuity();
				PoseTracker->ResetRuntimeStats();
			}
			UE_LOG(
				LogMediaPipePose,
				Log,
				TEXT("Quest webcam source using direct WMF capture: %s url=%s size=%dx%d fps=%.2f native=%s"),
				CaptureDeviceDisplayName.IsEmpty() ? TEXT("unnamed") : *CaptureDeviceDisplayName,
				*CaptureDeviceUrl,
				DirectWmfCapture->GetCaptureSize().X,
				DirectWmfCapture->GetCaptureSize().Y,
				DirectWmfCapture->GetFrameRate(),
				*DirectWmfCapture->GetSelectedSubtype());
			return;
		}

		UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source: direct WMF capture failed; falling back to UMediaPlayer for %s"), *CaptureDeviceUrl);
		delete DirectWmfCapture;
		DirectWmfCapture = nullptr;
	}

	if (!MediaPlayer->OpenUrl(CaptureDeviceUrl))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Quest webcam source: failed to open capture device: %s"), *CaptureDeviceUrl);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Quest webcam source using capture device: %s url=%s"),
		CaptureDeviceDisplayName.IsEmpty() ? TEXT("unnamed") : *CaptureDeviceDisplayName,
		*CaptureDeviceUrl);
}

void AMediaPipeQuestWebcamSourceActor::RefreshMediaTextureBinding() const
{
	if (!MediaTexture)
	{
		return;
	}

	MediaTexture->SetMediaPlayer(MediaPlayer);
#if WITH_EDITOR
	MediaTexture->SetDefaultMediaPlayer(MediaPlayer);
#endif
	MediaTexture->UpdateResource();
}

void AMediaPipeQuestWebcamSourceActor::UpdateDirectPreviewTexture(const TArray<uint8>& Rgb, const FIntPoint CaptureSize)
{
	if (CVarAutoQuestWebcamPreview.GetValueOnGameThread() == 0)
	{
		RemoveDirectPreviewOverlay();
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const float PreviewHz = FMath::Clamp(CVarAutoQuestWebcamPreviewHz.GetValueOnGameThread(), 1.0f, 60.0f);
	if (LastDirectPreviewUpdateSeconds >= 0.0 && NowSeconds - LastDirectPreviewUpdateSeconds < 1.0 / PreviewHz)
	{
		return;
	}

	const FIntPoint PreviewSize = ComputePreviewSize(CaptureSize);
	TArray<uint8> Bgra;
	if (!BuildPreviewBgra(Rgb, CaptureSize, PreviewSize, Bgra))
	{
		return;
	}

	FMediaPipePoseFrame PreviewFrame;
	FMediaPipePosePipelineStats PreviewStats;
	bool bHasPreviewFrame = false;
	if (PoseTracker)
	{
		bHasPreviewFrame = PoseTracker->GetLatestRawFrame(PreviewFrame);
		PoseTracker->GetRuntimeStats(PreviewStats);
	}

	float PoseAgeMs = -1.0f;
	if (bHasPreviewFrame && PreviewFrame.TimestampUs > 0)
	{
		PoseAgeMs = FMath::Max(0.0f, static_cast<float>((NowSeconds - (static_cast<double>(PreviewFrame.TimestampUs) / 1000000.0)) * 1000.0));
	}

	DrawPoseDebugOverlay(Bgra, PreviewSize, PreviewFrame, bHasPreviewFrame, PoseAgeMs);
	const FString StatusText = BuildPosePreviewStatusText(PreviewFrame, bHasPreviewFrame, PoseAgeMs, PreviewStats);

	if (!DirectPreviewTexture || DirectPreviewSize != PreviewSize)
	{
		DirectPreviewTexture = UTexture2D::CreateTransient(PreviewSize.X, PreviewSize.Y, PF_B8G8R8A8, TEXT("MP_DirectWebcamPreviewTexture"));
		if (!DirectPreviewTexture)
		{
			return;
		}
		DirectPreviewTexture->SRGB = true;
		DirectPreviewTexture->NeverStream = true;
		DirectPreviewTexture->Filter = TF_Bilinear;
		DirectPreviewTexture->UpdateResource();
		DirectPreviewSize = PreviewSize;
	}

	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, PreviewSize.X, PreviewSize.Y);
	uint8* TextureData = static_cast<uint8*>(FMemory::Malloc(Bgra.Num()));
	if (!TextureData)
	{
		delete Region;
		return;
	}
	FMemory::Memcpy(TextureData, Bgra.GetData(), Bgra.Num());
	DirectPreviewTexture->UpdateTextureRegions(
		0,
		1,
		Region,
		PreviewSize.X * 4,
		4,
		TextureData,
		[](uint8* SrcData, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(SrcData);
			delete Regions;
		});

	if (!DirectPreviewOverlay)
	{
		DirectPreviewOverlay = new FMediaPipeDirectWmfPreviewOverlay();
	}
	DirectPreviewOverlay->Ensure(DirectPreviewTexture, DirectPreviewSize, StatusText);
	LastDirectPreviewUpdateSeconds = NowSeconds;
}

void AMediaPipeQuestWebcamSourceActor::RemoveDirectPreviewOverlay()
{
	if (DirectPreviewOverlay)
	{
		DirectPreviewOverlay->Remove();
		delete DirectPreviewOverlay;
		DirectPreviewOverlay = nullptr;
	}
	DirectPreviewSize = FIntPoint::ZeroValue;
	LastDirectPreviewUpdateSeconds = -1.0;
}

bool AMediaPipeQuestWebcamSourceActor::SelectPreferredCaptureFormat()
{
	if (CVarAutoQuestWebcamMediaPlayerFormatSelection.GetValueOnGameThread() == 0)
	{
		return false;
	}

	if (!MediaPlayer || !MediaPlayer->GetUrl().StartsWith(TEXT("vidcap://"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	const int32 VideoTrackCount = MediaPlayer->GetNumTracks(EMediaPlayerTrack::Video);
	FPreferredWebcamFormat BestFormat;
	for (int32 TrackIndex = 0; TrackIndex < VideoTrackCount; ++TrackIndex)
	{
		const int32 FormatCount = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, TrackIndex);
		for (int32 FormatIndex = 0; FormatIndex < FormatCount; ++FormatIndex)
		{
			const FIntPoint Dimensions = MediaPlayer->GetVideoTrackDimensions(TrackIndex, FormatIndex);
			const float FrameRate = MediaPlayer->GetVideoTrackFrameRate(TrackIndex, FormatIndex);
			const FString Type = MediaPlayer->GetVideoTrackType(TrackIndex, FormatIndex);
			const int32 FormatKey = MakeCaptureFormatKey(TrackIndex, FormatIndex);
			if (RejectedCaptureFormatKeys.Contains(FormatKey) || RejectedCaptureFormatDimensions.Contains(Dimensions))
			{
				continue;
			}
			const int64 Score = ScoreWebcamFormat(Dimensions, FrameRate, Type);
			UE_LOG(
				LogMediaPipePose,
				Verbose,
				TEXT("Quest webcam source format candidate: track=%d format=%d size=%dx%d fps=%.2f type=%s score=%lld"),
				TrackIndex,
				FormatIndex,
				Dimensions.X,
				Dimensions.Y,
				FrameRate,
				*Type,
				Score);
			if (Score > BestFormat.Score)
			{
				BestFormat.TrackIndex = TrackIndex;
				BestFormat.FormatIndex = FormatIndex;
				BestFormat.Dimensions = Dimensions;
				BestFormat.FrameRate = FrameRate;
				BestFormat.Type = Type;
				BestFormat.Score = Score;
			}
		}
	}

	if (BestFormat.TrackIndex == INDEX_NONE || BestFormat.FormatIndex == INDEX_NONE)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source: no usable video capture formats remain; keeping current/default stream."));
		return false;
	}

	const int32 PreviousTrack = MediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Video);
	const int32 PreviousFormat = PreviousTrack != INDEX_NONE ? MediaPlayer->GetTrackFormat(EMediaPlayerTrack::Video, PreviousTrack) : INDEX_NONE;
	bool bSetFormat = MediaPlayer->SetTrackFormat(EMediaPlayerTrack::Video, BestFormat.TrackIndex, BestFormat.FormatIndex);
	const bool bSelectedTrack = MediaPlayer->SelectTrack(EMediaPlayerTrack::Video, BestFormat.TrackIndex);
	if (!bSetFormat && bSelectedTrack)
	{
		bSetFormat = MediaPlayer->SetTrackFormat(EMediaPlayerTrack::Video, BestFormat.TrackIndex, BestFormat.FormatIndex);
	}

	if (bSetFormat && bSelectedTrack)
	{
		ActiveCaptureFormatKey = MakeCaptureFormatKey(BestFormat.TrackIndex, BestFormat.FormatIndex);
		ActiveCaptureFormatDimensions = BestFormat.Dimensions;
		LastCaptureFormatSelectWallSeconds = FPlatformTime::Seconds();
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("Quest webcam source selected format: previousTrack=%d previousFormat=%d selectedTrack=%d format=%d size=%dx%d fps=%.2f type=%s set=%d select=%d preferred=%dx%d@%.1f"),
			PreviousTrack,
			PreviousFormat,
			BestFormat.TrackIndex,
			BestFormat.FormatIndex,
			BestFormat.Dimensions.X,
			BestFormat.Dimensions.Y,
			BestFormat.FrameRate,
			*BestFormat.Type,
			bSetFormat ? 1 : 0,
			bSelectedTrack ? 1 : 0,
			FMath::Max(1, CVarAutoQuestWebcamPreferredWidth.GetValueOnGameThread()),
			FMath::Max(1, CVarAutoQuestWebcamPreferredHeight.GetValueOnGameThread()),
			FMath::Max(1.0f, CVarAutoQuestWebcamPreferredFps.GetValueOnGameThread()));
	}
	else
	{
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("Quest webcam source failed to select preferred format: previousTrack=%d previousFormat=%d targetTrack=%d format=%d size=%dx%d fps=%.2f type=%s set=%d select=%d"),
			PreviousTrack,
			PreviousFormat,
			BestFormat.TrackIndex,
			BestFormat.FormatIndex,
			BestFormat.Dimensions.X,
			BestFormat.Dimensions.Y,
			BestFormat.FrameRate,
			*BestFormat.Type,
			bSetFormat ? 1 : 0,
			bSelectedTrack ? 1 : 0);
	}

	return bSetFormat && bSelectedTrack;
}

void AMediaPipeQuestWebcamSourceActor::ValidateCaptureFormat()
{
	if (CVarAutoQuestWebcamMediaPlayerFormatSelection.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!MediaPlayer || !PoseTracker || ActiveCaptureFormatKey == INDEX_NONE ||
		!MediaPlayer->GetUrl().StartsWith(TEXT("vidcap://"), ESearchCase::IgnoreCase))
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const double ValidationSeconds = FMath::Clamp(
		static_cast<double>(CVarAutoQuestWebcamFormatValidationSeconds.GetValueOnGameThread()),
		0.25,
		5.0);
	if (LastCaptureFormatSelectWallSeconds < 0.0 || NowSeconds - LastCaptureFormatSelectWallSeconds < ValidationSeconds)
	{
		return;
	}

	FMediaPipePosePipelineStats Stats;
	PoseTracker->GetRuntimeStats(Stats);
	const int32 MinValidWidth = FMath::Max(1, CVarAutoQuestWebcamMinValidFrameWidth.GetValueOnGameThread());
	const int32 MinValidHeight = FMath::Max(1, CVarAutoQuestWebcamMinValidFrameHeight.GetValueOnGameThread());
	const bool bHasReadback = Stats.ComponentAsyncReadbackCompleteCount > 0 || Stats.ComponentConversionCount > 0;
	const bool bTinyCapture = bHasReadback && Stats.LastCaptureSize.X > 0 && Stats.LastCaptureSize.Y > 0 &&
		(Stats.LastCaptureSize.X < MinValidWidth || Stats.LastCaptureSize.Y < MinValidHeight);
	const bool bRejectedByNativeInput = Stats.WorkerInvalidInputCount >= 3 || Stats.WorkerProcessFailCount >= 3;
	if (!bTinyCapture && !bRejectedByNativeInput)
	{
		return;
	}

	UE_LOG(
		LogMediaPipePose,
		Warning,
		TEXT("Quest webcam source rejecting capture format: key=%d requested=%dx%d actual=%dx%d invalidInput=%lld processFail=%lld publish=%lld"),
		ActiveCaptureFormatKey,
		ActiveCaptureFormatDimensions.X,
		ActiveCaptureFormatDimensions.Y,
		Stats.LastCaptureSize.X,
		Stats.LastCaptureSize.Y,
		Stats.WorkerInvalidInputCount,
		Stats.WorkerProcessFailCount,
		Stats.TrackerPublishCount);

	RejectedCaptureFormatKeys.Add(ActiveCaptureFormatKey);
	if (bTinyCapture)
	{
		RejectedCaptureFormatDimensions.Add(ActiveCaptureFormatDimensions);
	}
	ActiveCaptureFormatKey = INDEX_NONE;
	ActiveCaptureFormatDimensions = FIntPoint::ZeroValue;
	LastCaptureFormatSelectWallSeconds = -1.0;

	if (SelectPreferredCaptureFormat())
	{
		RefreshMediaTextureBinding();
		PoseTracker->ResetForSourceDiscontinuity();
		PoseTracker->ResetRuntimeStats();
		if (bAutoPlay)
		{
			MediaPlayer->Play();
		}
		return;
	}

	PoseTracker->ResetForSourceDiscontinuity();
	PoseTracker->ResetRuntimeStats();
}

void AMediaPipeQuestWebcamSourceActor::OnMediaOpened(FString OpenedUrl)
{
	RefreshMediaTextureBinding();
	const bool bCaptureFormatSelected = CVarAutoQuestWebcamMediaPlayerFormatSelection.GetValueOnGameThread() != 0
		&& SelectPreferredCaptureFormat();
	if (bCaptureFormatSelected)
	{
		RefreshMediaTextureBinding();
	}
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
		PoseTracker->ResetRuntimeStats();
		PoseTracker->ProcessFrame();
	}

	if (bAutoPlay && MediaPlayer)
	{
		MediaPlayer->Play();
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Quest webcam source media opened: %s"), *OpenedUrl);
}

void AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed(FString FailedUrl)
{
	UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source media open failed: %s"), *FailedUrl);
}
