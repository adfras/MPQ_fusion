"""Re-aim the summoned PIE camera at Kellan's legs (front-left low) and log joint heights.

Set AIM_LABEL before exec to change the log tag. Logs pelvis/knee/foot Z plus knee fraction
so every screenshot has paired joint measurements.
"""

import unreal

AIM_LABEL = globals().get("AIM_LABEL", "shot")

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
actors = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
kellan = next(a for a in actors if a.get_actor_label() == "MP_LiveMetaHumanKellan")
mesh = next(c for c in kellan.get_components_by_class(unreal.SkeletalMeshComponent)
            if c.does_socket_exist("thigh_l"))
pelvis = mesh.get_socket_location("pelvis")
thigh_l = mesh.get_socket_location("thigh_l")
knee_l = mesh.get_socket_location("calf_l")
foot_l = mesh.get_socket_location("foot_l")
thigh_r = mesh.get_socket_location("thigh_r")
knee_r = mesh.get_socket_location("calf_r")
foot_r = mesh.get_socket_location("foot_r")

target = unreal.Vector(pelvis.x, pelvis.y, (pelvis.z + min(foot_l.z, foot_r.z)) * 0.5)
fwd = kellan.get_actor_forward_vector()
right = kellan.get_actor_right_vector()
campos = unreal.Vector(
    target.x + fwd.x * 150.0 - right.x * 110.0,
    target.y + fwd.y * 150.0 - right.y * 110.0,
    target.z + 10.0)
cam = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor)[-1]
cam.set_actor_location(campos, False, False)
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(campos, target), False)
pc = unreal.GameplayStatics.get_player_controller(w, 0)
pc.set_view_target_with_blend(cam, 0.0)


def frac(hip, knee, foot):
    denom = hip.z - foot.z
    return (knee.z - foot.z) / denom if abs(denom) > 0.001 else -1.0


unreal.log(
    "[LegCam] {} pelvisZ={:.1f} L(thigh={:.1f} knee={:.1f} foot={:.1f} frac={:.3f}) "
    "R(thigh={:.1f} knee={:.1f} foot={:.1f} frac={:.3f})".format(
        AIM_LABEL, pelvis.z,
        thigh_l.z, knee_l.z, foot_l.z, frac(thigh_l, knee_l, foot_l),
        thigh_r.z, knee_r.z, foot_r.z, frac(thigh_r, knee_r, foot_r)))
