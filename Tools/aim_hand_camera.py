"""Re-aim the summoned PIE camera at Kellan's hands (front, chest height) and log curl angles."""

import math

import unreal

AIM_LABEL = globals().get("AIM_LABEL", "hands")

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
actors = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
kellan = next(a for a in actors if a.get_actor_label() == "MP_LiveMetaHumanKellan")
mesh = next(c for c in kellan.get_components_by_class(unreal.SkeletalMeshComponent)
            if c.does_socket_exist("hand_l"))

hand_l = mesh.get_socket_location("hand_l")
hand_r = mesh.get_socket_location("hand_r")
target = unreal.Vector((hand_l.x + hand_r.x) * 0.5, (hand_l.y + hand_r.y) * 0.5,
                       (hand_l.z + hand_r.z) * 0.5)
fwd = kellan.get_actor_forward_vector()
campos = unreal.Vector(target.x + fwd.x * 95.0, target.y + fwd.y * 95.0, target.z + 12.0)
cams = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.CameraActor)
cam = cams[-1]
cam.set_actor_location(campos, False, False)
cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(campos, target), False)
pc = unreal.GameplayStatics.get_player_controller(w, 0)
pc.set_view_target_with_blend(cam, 0.0)


def curl(side):
    hand = mesh.get_socket_location("hand_" + side)
    m1 = mesh.get_socket_location("middle_01_" + side)
    m3 = mesh.get_socket_location("middle_03_" + side)
    a = unreal.Vector(m1.x - hand.x, m1.y - hand.y, m1.z - hand.z)
    b = unreal.Vector(m3.x - m1.x, m3.y - m1.y, m3.z - m1.z)
    al = math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z)
    bl = math.sqrt(b.x * b.x + b.y * b.y + b.z * b.z)
    if al < 1e-5 or bl < 1e-5:
        return -1.0
    d = max(-1.0, min(1.0, (a.x * b.x + a.y * b.y + a.z * b.z) / (al * bl)))
    return math.degrees(math.acos(d))


unreal.log("[HandCam] {} curlL={:.1f} curlR={:.1f}".format(AIM_LABEL, curl("l"), curl("r")))
