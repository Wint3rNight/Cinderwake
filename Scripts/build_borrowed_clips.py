# Builds the borrowed clips (Step 4e): retargets attacks from Paragon Kwang and Aurora onto Feng Mao, then mirrors them
# (both swing right-handed; Feng Mao's sword is in his left hand). Also mirrors three of Sevarog's own clips for Double
# Execution (after Step 6). The results go in /Game/Retargeted, which is
# git-ignored because it's derived from Fab content: anyone with the Paragon Feng Mao, Kwang and Aurora packs can
# rebuild it with this script.
#
# Run once, with the editor closed, after building (the mirror step needs the BossTest editor module):
#   UnrealEditor-Cmd BossTest.uproject -run=pythonscript -script=<path to this file> -unattended -nullrhi
# Log: Saved/ToolLogs/build_borrowed_clips.txt
import os
import unreal

LOG = os.path.join(unreal.Paths.project_saved_dir(), "ToolLogs", "build_borrowed_clips.txt")
EAL = unreal.EditorAssetLibrary
RIGS = "/Game/Retargeted/Rigs"
OUT = "/Game/Retargeted/FengMao"
out = []
MESHES = {
    "FengMao": "/Game/ParagonFengMao/Characters/Heroes/FengMao/Meshes/FengMao_GDC",
    "Kwang": "/Game/ParagonKwang/Characters/Heroes/Kwang/Meshes/Kwang_GDC",
    "Aurora": "/Game/ParagonAurora/Characters/Heroes/Aurora/Meshes/Aurora",
}
CLIPS = {
    "Kwang": ["Ability_R", "PrimaryAttack_A_Slow"],
    "Aurora": ["Primary_Attack_Slow_C"],
}
ANIMS = {
    "Kwang": "/Game/ParagonKwang/Characters/Heroes/Kwang/Animations/",
    "Aurora": "/Game/ParagonAurora/Characters/Heroes/Aurora/Animations/",
}

def make_rig(hero):
    name = "IK_%s" % hero
    path = RIGS + "/" + name
    # Reused when it exists, so the script can be rerun.
    rig = unreal.load_asset(path) if EAL.does_asset_exist(path) else unreal.IKRigDefinitionFactory.create_new_ik_rig_asset(RIGS, name)
    ctrl = unreal.IKRigController.get_controller(rig)
    ctrl.set_skeletal_mesh(unreal.load_asset(MESHES[hero]))
    ok = ctrl.apply_auto_generated_retarget_definition()
    out.append("%s: auto definition %s, root %s, %d chains" % (name, ok, ctrl.get_retarget_root(), len(ctrl.get_retarget_chains())))
    EAL.save_asset(path)
    return rig

# Retarget.
target = make_rig("FengMao")
made_all = []
for hero in ("Kwang", "Aurora"):
    source = make_rig(hero)
    name = "RTG_%s_to_FengMao" % hero
    path = RIGS + "/" + name
    rtg = unreal.load_asset(path) if EAL.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, RIGS, unreal.IKRetargeter, unreal.IKRetargetFactory())
    rc = unreal.IKRetargeterController.get_controller(rtg)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source)
    rc.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target)
    # Since 5.6 a retargeter runs a stack of ops (pelvis, FK chains, IK...). One made from a script starts empty,
    # which transfers no motion at all, so add the default stack and point every op at the two rigs.
    rc.remove_all_ops()
    rc.add_default_ops()
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, source)
    rc.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target)
    rc.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    rc.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)  # the target only, so reruns don't drift
    EAL.save_asset(path)
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.assets_to_retarget = [EAL.find_asset_data(ANIMS[hero] + c) for c in CLIPS[hero]]
    inputs.source_mesh = unreal.load_asset(MESHES[hero])
    inputs.target_mesh = unreal.load_asset(MESHES["FengMao"])
    inputs.ik_retarget_asset = rtg
    inputs.prefix = hero + "_"
    inputs.target_path = OUT
    inputs.include_referenced_assets = False
    inputs.overwrite_existing_files = True
    made = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    for a in made:
        EAL.save_asset(str(a.package_name))  # the batch creates them in memory only
    made_all += [str(a.asset_name) for a in made]
    out.append("%s: %d ops, retargeted %s" % (name, rc.get_num_retarget_ops(), ", ".join(str(a.asset_name) for a in made)))

# Mirror, from a fresh copy of each retargeted clip (mirroring a clip twice would flip it back). An existing mirrored
# clip is kept: the attack assets reference it, so the engine won't delete it. To rebuild, delete Content/Retargeted
# with the editor closed, then run this again.
mesh = unreal.load_asset(MESHES["FengMao"])
TABLE = RIGS + "/MT_FengMao"
table = unreal.load_asset(TABLE) if EAL.does_asset_exist(TABLE) else unreal.BossTestEditorTools.create_mirror_table(mesh.skeleton, RIGS, "MT_FengMao")
EAL.save_asset(TABLE)
for name in made_all:
    src, dst = OUT + "/" + name, OUT + "/" + name + "_Mirrored"
    if EAL.does_asset_exist(dst):
        out.append("%s_Mirrored: already built, kept" % name)
        continue
    copy = EAL.duplicate_asset(src, dst)
    if not unreal.BossTestEditorTools.mirror_animation(copy, table):
        raise RuntimeError("mirroring %s failed (see the log)" % name)
    EAL.save_asset(dst)
    out.append("%s_Mirrored: mirrored" % name)

# Sevarog's own clips, mirrored (the council's variety run after Step 6): Double Execution starts from his other side,
# a silhouette tell. Copied from the Paragon pack into /Game/Retargeted/Sevarog (derived content, git-ignored) and
# mirrored there; the Paragon originals are never touched.
SEV_ANIMS = "/Game/ParagonSevarog/Characters/Heroes/Sevarog/Animations/"
SEV_OUT = "/Game/Retargeted/Sevarog"
sev = unreal.load_asset("/Game/ParagonSevarog/Characters/Heroes/Sevarog/Meshes/Sevarog")
SEV_TABLE = RIGS + "/MT_Sevarog"
sev_table = unreal.load_asset(SEV_TABLE) if EAL.does_asset_exist(SEV_TABLE) else unreal.BossTestEditorTools.create_mirror_table(sev.skeleton, RIGS, "MT_Sevarog")
EAL.save_asset(SEV_TABLE)
# The hammer hangs off weapon_r at a fixed offset along the bone's own -Y; mirroring swaps weapon_r with weapon_l and
# mirrors their rotations, which flips that offset the wrong way (measured: at the strike the head sat near his body,
# 2.3 m from the reflection). So weapon_r is rewritten every key: its world position reflected across X, its world
# rotation the mirror image (the quaternion's y and z negated: a proper rotation that keeps the hammer's head-to-handle
# axis, which has no X), back into its parent's space in the mirrored pose.
E, ML = unreal.AnimPoseExtensions, unreal.MathLibrary
POSE = unreal.AnimPoseEvaluationOptions()
def fix_hammer(original, mirrored):
    keys = unreal.AnimationLibrary.get_num_frames(mirrored) + 1
    length = mirrored.get_play_length()
    pos, rot, scl = [], [], []
    for i in range(keys):
        t = min(length, length * i / max(1, keys - 1))
        wo = E.get_bone_pose(E.get_anim_pose_at_time(original, t, POSE), "weapon_r", unreal.AnimPoseSpaces.WORLD)
        pm = E.get_anim_pose_at_time(mirrored, t, POSE)
        wm = E.get_bone_pose(pm, "weapon_r", unreal.AnimPoseSpaces.WORLD)
        lm = E.get_bone_pose(pm, "weapon_r", unreal.AnimPoseSpaces.LOCAL)
        parent = ML.compose_transforms(ML.invert_transform(lm), wm)  # world = local * parent
        want = unreal.Transform()
        want.translation = unreal.Vector(-wo.translation.x, wo.translation.y, wo.translation.z)
        q = wo.rotation
        want.rotation = unreal.Quat(q.x, -q.y, -q.z, q.w)
        want.scale3d = wo.scale3d
        local = ML.compose_transforms(want, ML.invert_transform(parent))
        pos.append(local.translation)
        rot.append(local.rotation)
        scl.append(local.scale3d)
    if not mirrored.controller.set_bone_track_keys("weapon_r", pos, rot, scl, False):
        raise RuntimeError("couldn't rewrite weapon_r on %s" % mirrored.get_name())
for name in ("Swing1_Slow", "Swing2_Slow", "Swing2_Return2Idle_120fps"):
    dst = SEV_OUT + "/" + name + "_Mirrored"
    if EAL.does_asset_exist(dst):
        out.append("%s_Mirrored: already built, kept" % name)
        continue
    copy = EAL.duplicate_asset(SEV_ANIMS + name, dst)
    if not unreal.BossTestEditorTools.mirror_animation(copy, sev_table):
        raise RuntimeError("mirroring Sevarog's %s failed (see the log)" % name)
    fix_hammer(unreal.load_asset(SEV_ANIMS + name), copy)
    EAL.save_asset(dst)
    out.append("%s_Mirrored: mirrored, hammer reflected" % name)
open(LOG, "w").write("\n".join(out))
