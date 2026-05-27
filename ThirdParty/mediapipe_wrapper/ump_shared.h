#pragma once

#include <cstdint>

#if defined(_WIN32)
  #define MP_EXPORT __declspec(dllexport)
#else
  #define MP_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

static const int kMpPoseLandmarkCount = 33;
static const int kMpHandLandmarkCount = 21;

struct MP_Landmark {
  float x;
  float y;
  float z;
  float visibility;
  float presence;
};

struct MP_Landmarks {
  MP_Landmark landmarks[kMpPoseLandmarkCount];
};

MP_EXPORT bool MP_Init(const char* config_path);
// Optional extended init: enable hands if hand_model_path is provided.
// If the export is missing in the DLL, callers should fall back to MP_Init (pose-only).
MP_EXPORT bool MP_Init2(const char* pose_model_path, const char* hand_model_path);

struct MP_InitOptions {
  int32_t size_bytes;
  int32_t enable_hands;
  int32_t num_poses;
  float min_pose_detection_confidence;
  float min_pose_presence_confidence;
  float min_tracking_confidence;
  int32_t output_segmentation_masks;
  int32_t num_hands;
  float min_hand_detection_confidence;
  float min_hand_presence_confidence;
  float min_hand_tracking_confidence;
  int32_t reserved0;
};

// Configurable init for Pose Landmarker native options. Running mode remains VIDEO
// because MP_ProcessFrame is synchronous and calls DetectForVideo with timestamps.
MP_EXPORT bool MP_Init3(const char* pose_model_path, const char* hand_model_path, const MP_InitOptions* options);
MP_EXPORT bool MP_ProcessFrame(const uint8_t* rgb_data, int32_t width, int32_t height, int64_t timestamp_us);
MP_EXPORT bool MP_GetLandmarks(MP_Landmarks* out_normalized, MP_Landmarks* out_world);
// Returns the latest hand landmarks (if hands are enabled). May return true with has_left/has_right == 0.
// The returned landmarks are:
// - normalized: full-image normalized coords [0..1]
// - world: meters in the same axis convention as Pose world landmarks (camera axes), but centered on the hand.
//         To fuse with Pose world, treat them as relative-to-wrist and translate by the Pose wrist world position.
struct MP_HandLandmarks {
  MP_Landmark landmarks[kMpHandLandmarkCount];
};

struct MP_HandPair {
  uint8_t has_left;
  uint8_t has_right;
  uint8_t reserved[2];
  float left_score;
  float right_score;
  MP_HandLandmarks left_normalized;
  MP_HandLandmarks left_world;
  MP_HandLandmarks right_normalized;
  MP_HandLandmarks right_world;
};

MP_EXPORT bool MP_GetHandLandmarks(MP_HandPair* out_hands);
MP_EXPORT void MP_Shutdown();

#ifdef __cplusplus
}
#endif
