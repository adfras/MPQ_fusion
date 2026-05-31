#include "ump_shared.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "mediapipe/framework/formats/image.h"
#include "mediapipe/framework/formats/image_format.pb.h"
#include "mediapipe/framework/formats/image_frame.h"
#include "mediapipe/tasks/cc/components/containers/landmark.h"
#include "mediapipe/tasks/cc/core/base_options.h"
#include "mediapipe/tasks/cc/vision/core/running_mode.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/hand_landmarker.h"
#include "mediapipe/tasks/cc/vision/hand_landmarker/hand_landmarker_result.h"
#include "mediapipe/tasks/cc/vision/holistic_landmarker/holistic_landmarker.h"
#include "mediapipe/tasks/cc/vision/holistic_landmarker/holistic_landmarker_result.h"
#include "mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarker.h"
#include "mediapipe/tasks/cc/vision/pose_landmarker/pose_landmarker_result.h"

namespace {
using mediapipe::Image;
using mediapipe::ImageFrame;
using mediapipe::ImageFormat;
using mediapipe::tasks::core::BaseOptions;
using mediapipe::tasks::vision::core::RunningMode;
using mediapipe::tasks::vision::hand_landmarker::HandLandmarker;
using mediapipe::tasks::vision::hand_landmarker::HandLandmarkerOptions;
using mediapipe::tasks::vision::hand_landmarker::HandLandmarkerResult;
using mediapipe::tasks::vision::holistic_landmarker::HolisticLandmarker;
using mediapipe::tasks::vision::holistic_landmarker::HolisticLandmarkerOptions;
using mediapipe::tasks::vision::holistic_landmarker::HolisticLandmarkerResult;
using mediapipe::tasks::vision::pose_landmarker::PoseLandmarker;
using mediapipe::tasks::vision::pose_landmarker::PoseLandmarkerOptions;
using mediapipe::tasks::vision::pose_landmarker::PoseLandmarkerResult;
using mediapipe::tasks::components::containers::Landmarks;
using mediapipe::tasks::components::containers::NormalizedLandmarks;

std::unique_ptr<PoseLandmarker> g_landmarker;
std::unique_ptr<HandLandmarker> g_hand_landmarker;
std::unique_ptr<HolisticLandmarker> g_holistic_landmarker;
std::mutex g_landmarker_mutex;

std::mutex g_result_mutex;
MP_Landmarks g_last_normalized;
MP_Landmarks g_last_world;
MP_HandPair g_last_hands;
MP_FacePose g_last_face;
std::atomic<bool> g_has_result{false};
std::atomic<bool> g_has_hands{false};
std::atomic<bool> g_has_holistic_landmarker{false};
std::atomic<int64_t> g_last_timestamp_ms{-1};

inline float OptionalToFloat(const std::optional<float>& value) {
  return value.has_value() ? *value : 0.0f;
}

bool CopyLandmarks(const NormalizedLandmarks& src, MP_Landmarks* dst) {
  if (!dst || src.landmarks.size() < kMpPoseLandmarkCount) {
    return false;
  }
  for (int i = 0; i < kMpPoseLandmarkCount; ++i) {
    const auto& lm = src.landmarks[i];
    dst->landmarks[i] = {lm.x, lm.y, lm.z, OptionalToFloat(lm.visibility), OptionalToFloat(lm.presence)};
  }
  return true;
}

bool CopyLandmarks(const Landmarks& src, MP_Landmarks* dst) {
  if (!dst || src.landmarks.size() < kMpPoseLandmarkCount) {
    return false;
  }
  for (int i = 0; i < kMpPoseLandmarkCount; ++i) {
    const auto& lm = src.landmarks[i];
    dst->landmarks[i] = {lm.x, lm.y, lm.z, OptionalToFloat(lm.visibility), OptionalToFloat(lm.presence)};
  }
  return true;
}

bool CopyHandLandmarks(const NormalizedLandmarks& src, MP_HandLandmarks* dst) {
  if (!dst || src.landmarks.size() < kMpHandLandmarkCount) {
    return false;
  }
  for (int i = 0; i < kMpHandLandmarkCount; ++i) {
    const auto& lm = src.landmarks[i];
    dst->landmarks[i] = {lm.x, lm.y, lm.z, OptionalToFloat(lm.visibility), OptionalToFloat(lm.presence)};
  }
  return true;
}

bool CopyHandLandmarks(const Landmarks& src, MP_HandLandmarks* dst) {
  if (!dst || src.landmarks.size() < kMpHandLandmarkCount) {
    return false;
  }
  for (int i = 0; i < kMpHandLandmarkCount; ++i) {
    const auto& lm = src.landmarks[i];
    dst->landmarks[i] = {lm.x, lm.y, lm.z, OptionalToFloat(lm.visibility), OptionalToFloat(lm.presence)};
  }
  return true;
}

bool CopyFaceLandmarks(const NormalizedLandmarks& src, MP_FaceLandmarks* dst) {
  if (!dst) {
    return false;
  }
  const int count = std::min(static_cast<int>(src.landmarks.size()), kMpFaceLandmarkMaxCount);
  dst->count = count;
  for (int i = 0; i < count; ++i) {
    const auto& lm = src.landmarks[i];
    dst->landmarks[i] = {lm.x, lm.y, lm.z, OptionalToFloat(lm.visibility), OptionalToFloat(lm.presence)};
  }
  for (int i = count; i < kMpFaceLandmarkMaxCount; ++i) {
    dst->landmarks[i] = {};
  }
  return count > 0;
}

void ResetCachedResults() {
  std::lock_guard<std::mutex> result_lock(g_result_mutex);
  g_last_normalized = {};
  g_last_world = {};
  g_last_hands = {};
  g_last_face = {};
  g_has_result.store(false);
  g_has_hands.store(false);
  g_has_holistic_landmarker.store(g_holistic_landmarker != nullptr);
  g_last_timestamp_ms.store(-1);
}

bool InitPoseLandmarker(const char* pose_model_path, const MP_InitOptions* options) {
  if (!pose_model_path || std::strlen(pose_model_path) == 0) {
    return false;
  }

  if (g_holistic_landmarker) {
    g_holistic_landmarker->Close();
    g_holistic_landmarker.reset();
  }

  if (g_landmarker) {
    return true;
  }

  auto pose_options = std::make_unique<PoseLandmarkerOptions>();
  pose_options->base_options.model_asset_path = pose_model_path;
  pose_options->base_options.delegate = BaseOptions::CPU;
  pose_options->running_mode = RunningMode::VIDEO;
  pose_options->num_poses = options ? std::max(1, options->num_poses) : 1;
  if (options) {
    pose_options->min_pose_detection_confidence = options->min_pose_detection_confidence;
    pose_options->min_pose_presence_confidence = options->min_pose_presence_confidence;
    pose_options->min_tracking_confidence = options->min_tracking_confidence;
    pose_options->output_segmentation_masks = options->output_segmentation_masks != 0;
  }

  absl::StatusOr<std::unique_ptr<PoseLandmarker>> result = PoseLandmarker::Create(std::move(pose_options));
  if (!result.ok()) {
    return false;
  }

  g_landmarker = std::move(result.value());
  return true;
}

void InitHandLandmarker(const char* hand_model_path, const MP_InitOptions* options) {
  if (!hand_model_path || std::strlen(hand_model_path) == 0 || g_hand_landmarker) {
    return;
  }

  auto hand_options = std::make_unique<HandLandmarkerOptions>();
  hand_options->base_options.model_asset_path = hand_model_path;
  hand_options->base_options.delegate = BaseOptions::CPU;
  hand_options->running_mode = RunningMode::VIDEO;
  hand_options->num_hands = options ? std::max(1, options->num_hands) : 2;
  if (options) {
    hand_options->min_hand_detection_confidence = options->min_hand_detection_confidence;
    hand_options->min_hand_presence_confidence = options->min_hand_presence_confidence;
    hand_options->min_tracking_confidence = options->min_hand_tracking_confidence;
  }

  absl::StatusOr<std::unique_ptr<HandLandmarker>> hand_result = HandLandmarker::Create(std::move(hand_options));
  if (!hand_result.ok()) {
    g_hand_landmarker.reset();
    return;
  }

  g_hand_landmarker = std::move(hand_result.value());
}

bool InitHolisticLandmarker(const char* holistic_model_path, const MP_InitOptions* options) {
  if (!holistic_model_path || std::strlen(holistic_model_path) == 0) {
    return false;
  }

  if (g_holistic_landmarker) {
    return true;
  }

  auto holistic_options = std::make_unique<HolisticLandmarkerOptions>();
  holistic_options->base_options.model_asset_path = holistic_model_path;
  holistic_options->base_options.delegate = BaseOptions::CPU;
  holistic_options->running_mode = RunningMode::VIDEO;
  holistic_options->output_face_blendshapes = false;
  if (options) {
    holistic_options->min_pose_detection_confidence = options->min_pose_detection_confidence;
    holistic_options->min_pose_presence_confidence = options->min_pose_presence_confidence;
    holistic_options->min_hand_landmarks_confidence = options->min_hand_presence_confidence;
    holistic_options->output_pose_segmentation_masks = options->output_segmentation_masks != 0;
  }

  absl::StatusOr<std::unique_ptr<HolisticLandmarker>> holistic_result =
      HolisticLandmarker::Create(std::move(holistic_options));
  if (!holistic_result.ok()) {
    g_holistic_landmarker.reset();
    return false;
  }

  if (g_landmarker) {
    g_landmarker->Close();
    g_landmarker.reset();
  }
  if (g_hand_landmarker) {
    g_hand_landmarker->Close();
    g_hand_landmarker.reset();
  }

  g_holistic_landmarker = std::move(holistic_result.value());
  return true;
}

inline float Dist2(float ax, float ay, float bx, float by) {
  const float dx = ax - bx;
  const float dy = ay - by;
  return dx * dx + dy * dy;
}

}  // namespace

extern "C" {

bool MP_Init(const char* config_path) {
  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  if (!InitPoseLandmarker(config_path, nullptr)) {
    return false;
  }

  ResetCachedResults();
  return true;
}

bool MP_Init2(const char* pose_model_path, const char* hand_model_path) {
  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  if (!InitPoseLandmarker(pose_model_path, nullptr)) {
    return false;
  }

  InitHandLandmarker(hand_model_path, nullptr);
  ResetCachedResults();
  return true;
}

bool MP_Init3(const char* pose_model_path, const char* hand_model_path, const MP_InitOptions* options) {
  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  if (!InitPoseLandmarker(pose_model_path, options)) {
    return false;
  }

  if (options && options->enable_hands) {
    InitHandLandmarker(hand_model_path, options);
  }

  ResetCachedResults();
  return true;
}

bool MP_Init4(const char* pose_model_path, const char* hand_model_path, const char* holistic_model_path, const MP_InitOptions* options) {
  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  (void)pose_model_path;
  (void)hand_model_path;
  if (!InitHolisticLandmarker(holistic_model_path, options)) {
    return false;
  }

  ResetCachedResults();
  return true;
}

bool MP_ProcessFrame(const uint8_t* rgb_data, int32_t width, int32_t height, int64_t timestamp_us) {
  if (!rgb_data || width <= 0 || height <= 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  if (!g_landmarker && !g_holistic_landmarker) {
    return false;
  }

  const int64_t timestamp_ms = timestamp_us / 1000;
  const int64_t last_ms = g_last_timestamp_ms.load();
  if (last_ms >= 0 && timestamp_ms <= last_ms) {
    return false;
  }

  auto image_frame = std::make_shared<ImageFrame>();
  image_frame->CopyPixelData(ImageFormat::SRGB, width, height, rgb_data, ImageFrame::kDefaultAlignmentBoundary);
  Image image(image_frame);

  if (g_holistic_landmarker) {
    absl::StatusOr<HolisticLandmarkerResult> holistic_or =
        g_holistic_landmarker->DetectForVideo(image, timestamp_ms);
    if (!holistic_or.ok()) {
      return false;
    }

    const HolisticLandmarkerResult& holistic_result = holistic_or.value();
    if (holistic_result.pose_landmarks.landmarks.size() < kMpPoseLandmarkCount ||
        holistic_result.pose_world_landmarks.landmarks.size() < kMpPoseLandmarkCount) {
      return false;
    }

    MP_Landmarks normalized;
    MP_Landmarks world;
    if (!CopyLandmarks(holistic_result.pose_landmarks, &normalized) ||
        !CopyLandmarks(holistic_result.pose_world_landmarks, &world)) {
      return false;
    }

    MP_HandPair hands{};
    if (CopyHandLandmarks(holistic_result.left_hand_landmarks, &hands.left_normalized) &&
        CopyHandLandmarks(holistic_result.left_hand_world_landmarks, &hands.left_world)) {
      hands.has_left = 1;
      hands.left_score = 1.0f;
    }
    if (CopyHandLandmarks(holistic_result.right_hand_landmarks, &hands.right_normalized) &&
        CopyHandLandmarks(holistic_result.right_hand_world_landmarks, &hands.right_world)) {
      hands.has_right = 1;
      hands.right_score = 1.0f;
    }

    MP_FacePose face{};
    if (CopyFaceLandmarks(holistic_result.face_landmarks, &face.normalized)) {
      face.has_face = 1;
      face.has_transform = 0;
      face.score = 1.0f;
    }

    {
      std::lock_guard<std::mutex> result_lock(g_result_mutex);
      g_last_normalized = normalized;
      g_last_world = world;
      g_last_hands = hands;
      g_last_face = face;
    }

    g_has_result.store(true);
    g_has_hands.store(true);
    g_has_holistic_landmarker.store(true);
    g_last_timestamp_ms.store(timestamp_ms);
    return true;
  }

  absl::StatusOr<PoseLandmarkerResult> result = g_landmarker->DetectForVideo(image, timestamp_ms);
  if (!result.ok()) {
    return false;
  }

  const PoseLandmarkerResult& pose_result = result.value();
  if (pose_result.pose_landmarks.empty() || pose_result.pose_world_landmarks.empty()) {
    return false;
  }

  MP_Landmarks normalized;
  MP_Landmarks world;
  if (!CopyLandmarks(pose_result.pose_landmarks[0], &normalized) ||
      !CopyLandmarks(pose_result.pose_world_landmarks[0], &world)) {
    return false;
  }

  MP_HandPair hands{};
  hands.has_left = 0;
  hands.has_right = 0;
  hands.left_score = 0.0f;
  hands.right_score = 0.0f;
  MP_FacePose face{};
  face.has_face = 0;
  face.has_transform = 0;
  face.score = 0.0f;

  if (g_hand_landmarker) {
    absl::StatusOr<HandLandmarkerResult> hand_or = g_hand_landmarker->DetectForVideo(image, timestamp_ms);
    if (hand_or.ok()) {
      const HandLandmarkerResult& hand_result = hand_or.value();
      const int n = static_cast<int>(hand_result.hand_landmarks.size());
      const int m = static_cast<int>(hand_result.hand_world_landmarks.size());

      // Only proceed if we have matching landmark sets.
      const int count = std::min({n, m, 2});
      if (count > 0) {
        // Pose wrists in normalized space for assignment.
        // Pose indices follow the MediaPipe Pose landmark schema:
        // 15 = LeftWrist, 16 = RightWrist.
        constexpr int kPoseLeftWrist = 15;
        constexpr int kPoseRightWrist = 16;
        const float pose_lw_x = pose_result.pose_landmarks[0].landmarks[kPoseLeftWrist].x;
        const float pose_lw_y = pose_result.pose_landmarks[0].landmarks[kPoseLeftWrist].y;
        const float pose_rw_x = pose_result.pose_landmarks[0].landmarks[kPoseRightWrist].x;
        const float pose_rw_y = pose_result.pose_landmarks[0].landmarks[kPoseRightWrist].y;

        struct Cand {
          MP_HandLandmarks norm;
          MP_HandLandmarks world;
          float score = 0.0f;
          float wx = 0.0f;
          float wy = 0.0f;
        };

        Cand cands[2];
        for (int i = 0; i < count; ++i) {
          CopyHandLandmarks(hand_result.hand_landmarks[i], &cands[i].norm);
          CopyHandLandmarks(hand_result.hand_world_landmarks[i], &cands[i].world);
          cands[i].wx = cands[i].norm.landmarks[0].x;
          cands[i].wy = cands[i].norm.landmarks[0].y;

          // If handedness is available, record the top score. Otherwise keep 0.
          if ((int)hand_result.handedness.size() > i && !hand_result.handedness[i].categories.empty()) {
            cands[i].score = hand_result.handedness[i].categories[0].score;
          }
        }

        auto AssignLeft = [&](const Cand& c) {
          hands.has_left = 1;
          hands.left_score = c.score;
          hands.left_normalized = c.norm;
          hands.left_world = c.world;
        };
        auto AssignRight = [&](const Cand& c) {
          hands.has_right = 1;
          hands.right_score = c.score;
          hands.right_normalized = c.norm;
          hands.right_world = c.world;
        };

        if (count == 1) {
          const float dL = Dist2(cands[0].wx, cands[0].wy, pose_lw_x, pose_lw_y);
          const float dR = Dist2(cands[0].wx, cands[0].wy, pose_rw_x, pose_rw_y);
          if (dL <= dR) {
            AssignLeft(cands[0]);
          } else {
            AssignRight(cands[0]);
          }
        } else {
          // Two-hand assignment: choose minimal total distance to pose wrists.
          const float c0L = Dist2(cands[0].wx, cands[0].wy, pose_lw_x, pose_lw_y);
          const float c0R = Dist2(cands[0].wx, cands[0].wy, pose_rw_x, pose_rw_y);
          const float c1L = Dist2(cands[1].wx, cands[1].wy, pose_lw_x, pose_lw_y);
          const float c1R = Dist2(cands[1].wx, cands[1].wy, pose_rw_x, pose_rw_y);
          const float costA = c0L + c1R;  // 0->L, 1->R
          const float costB = c0R + c1L;  // 0->R, 1->L

          if (costA <= costB) {
            AssignLeft(cands[0]);
            AssignRight(cands[1]);
          } else {
            AssignRight(cands[0]);
            AssignLeft(cands[1]);
          }
        }
      }
    }
  }

  {
    std::lock_guard<std::mutex> result_lock(g_result_mutex);
    g_last_normalized = normalized;
    g_last_world = world;
    g_last_hands = hands;
    g_last_face = face;
  }

  g_has_result.store(true);
  g_has_hands.store(g_hand_landmarker != nullptr);
  g_has_holistic_landmarker.store(false);
  g_last_timestamp_ms.store(timestamp_ms);
  return true;
}

bool MP_GetLandmarks(MP_Landmarks* out_normalized, MP_Landmarks* out_world) {
  if (!out_normalized || !out_world) {
    return false;
  }

  if (!g_has_result.load()) {
    return false;
  }

  std::lock_guard<std::mutex> result_lock(g_result_mutex);
  *out_normalized = g_last_normalized;
  *out_world = g_last_world;
  return true;
}

bool MP_GetHandLandmarks(MP_HandPair* out_hands) {
  if (!out_hands) {
    return false;
  }

  if (!g_has_result.load()) {
    return false;
  }

  std::lock_guard<std::mutex> result_lock(g_result_mutex);
  *out_hands = g_last_hands;
  return true;
}

bool MP_GetFacePose(MP_FacePose* out_face) {
  if (!out_face) {
    return false;
  }

  if (!g_has_result.load()) {
    return false;
  }

  std::lock_guard<std::mutex> result_lock(g_result_mutex);
  *out_face = g_last_face;
  return true;
}

void MP_Shutdown() {
  std::lock_guard<std::mutex> lock(g_landmarker_mutex);
  if (g_landmarker) {
    g_landmarker->Close();
    g_landmarker.reset();
  }
  if (g_hand_landmarker) {
    g_hand_landmarker->Close();
    g_hand_landmarker.reset();
  }
  if (g_holistic_landmarker) {
    g_holistic_landmarker->Close();
    g_holistic_landmarker.reset();
  }
  g_has_result.store(false);
  g_has_hands.store(false);
  g_has_holistic_landmarker.store(false);
  g_last_timestamp_ms.store(-1);
}

}  // extern "C"
