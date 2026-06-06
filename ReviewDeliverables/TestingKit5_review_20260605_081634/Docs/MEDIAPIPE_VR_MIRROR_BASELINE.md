# MediaPipe VR Mirror Baseline - Historical

Status: compacted 2026-06-05.

## Retained Fact

The mirror baseline established that self-view and mirror/external view are different visibility problems. The full avatar should remain visible to non-owner/mirror views while local first-person view hides or proxies only the obstructing bones.

## Current Rule

- Keep full mesh visible for mirror/external views.
- Use profile-driven local hidden bones or first-person body proxy for owner view.
- Do not solve self-view problems by hiding the whole avatar.

Current source: `MediaPipeFirstPersonBodyProxyComponent.*`, `MediaPipeAvatarEmbodimentProfile.*`, `MediaPipeEmbodiedAvatarPawn.*`.
