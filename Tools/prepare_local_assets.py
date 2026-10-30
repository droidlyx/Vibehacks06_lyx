"""Prepare a minimal CC0 asset set from original local vendor archives.
Run with Python to stage sources, then Blender runs this same file for fresh exports.
No existing Ember code, maps or imported Unreal packages are copied.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, subprocess, sys, zipfile

REPO = Path(__file__).resolve().parents[1]
LIBRARY = Path(r'D:\Workspace\vscode_workspace\EMBER_GENERATIONS_LOCAL_SOURCE_20260829')
BLENDER = LIBRARY / '_tools/blender-4.5.13-windows-x64/blender.exe'
RAW = REPO / 'Assets/Source/Quaternius'
OUT = REPO / 'Assets/Prepared'
PACKS = {
 'outfits': ('modular-character-outfits-fantasy/Modular Character Outfits - Fantasy[Standard].zip', 'https://quaternius.itch.io/modular-character-outfits-fantasy'),
 'base': ('universal-base-characters/Universal Base Characters[Standard].zip', 'https://quaternius.itch.io/universal-base-characters'),
 'nature': ('stylized-nature-megakit/Stylized Nature MegaKit[Standard].zip', 'https://quaternius.itch.io/stylized-nature-megakit'),
}
CHARACTERS = [('SK_Traveler','Female_Ranger','Female',None), ('SK_Gatherer','Male_Peasant','Male','Hair_SimpleParted'), ('SK_Keeper','Female_Peasant','Female','Hair_Buns')]
NATURE = [('SM_Tree01','CommonTree_1'),('SM_Tree02','TwistedTree_1'),('SM_Rock01','Rock_Medium_1'),('SM_Rock02','Rock_Medium_2'),('SM_Rock03','Rock_Medium_3'),('SM_Grass01','Grass_Common_Short')]
CLIPS = [('A_Idle','Idle_Loop',1),('A_Walk','Walk_Loop',1),('A_Run','Jog_Fwd_Loop',1),('A_Attack','Punch_Cross',1),('A_Hit','Hit_Chest',1),('A_Death','Death01',1),('A_Dodge','Roll',1),('A_Consume','Consume',2),('A_Carry','Walk_Carry_Loop',2)]

def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def stage():
 RAW.mkdir(parents=True,exist_ok=True); OUT.mkdir(parents=True,exist_ok=True)
 records=[]
 for key,(archive,url) in PACKS.items():
  src=LIBRARY/'vendor-free-assets/quaternius'/archive
  with zipfile.ZipFile(src) as z:
   names=z.namelist(); wanted=[]
   if key=='outfits':
    wanted=[n for n in names if '/Exports/glTF (Godot-Unreal)/Outfits/' in n and Path(n).stem in [c[1] for c in CHARACTERS] and n.endswith('.gltf')]
   elif key=='base':
    wanted=[n for n in names if '/Base Characters/Godot - UE/' in n and n.endswith('.gltf') and 'FullBody' in n]
    wanted += [n for n in names if '/Rigged to Head Bone/glTF (Godot -Unreal)/' in n and n.endswith('.gltf') and Path(n).stem in ['Hair_SimpleParted','Hair_Buns']]
   else: wanted=['glTF/'+name+'.gltf' for _,name in NATURE]
   for name in wanted:
    data=json.loads(z.read(name)); folder=RAW/key/Path(name).stem; folder.mkdir(parents=True,exist_ok=True)
    selected=[(name,Path(name).name)]
    for obj in data.get('buffers',[])+data.get('images',[]):
     uri=obj.get('uri','')
     if uri and not uri.startswith('data:'):
      candidate=(Path(name).parent/uri).as_posix()
      if candidate not in names:
       matches=[n for n in names if Path(n).name==Path(uri).name]
       if not matches: matches=[n for n in names if Path(n).name==Path(uri).name.replace('_png.png','.png')]
       if not matches: raise FileNotFoundError(candidate)
       candidate=matches[0]
      selected.append((candidate,Path(uri).name))
    for entry,filename in set(selected):
     dest=folder/filename; dest.write_bytes(z.read(entry))
     records.append({'path':dest.relative_to(REPO).as_posix(),'sha256':digest(dest),'pack':key,'archive_member':entry})
   license_entry=next(n for n in names if n.endswith('License_Standard.txt'))
   dest=RAW/key/'License_Standard.txt'; dest.write_bytes(z.read(license_entry))
   records.append({'pack':key,'source_url':url,'archive_sha256':digest(src),'license':'CC0-1.0','license_path':dest.relative_to(REPO).as_posix()})
 # Engine-neutral original animation-library files; no authored Ember clips.
 slice_root=LIBRARY/'ai-world-rpg-demo/godot-world-slice'
 for index in [1,2]:
  src=slice_root/f'assets/quaternius/animation/UAL{index}_Standard.glb'
  dst=RAW/f'UAL{index}_Standard.glb';shutil.copy2(src,dst)
  lic=RAW/f'UAL{index}_LICENSE.txt';shutil.copy2(slice_root/f'licenses/UAL{index}_LICENSE.txt',lic)
  records.append({'path':dst.relative_to(REPO).as_posix(),'sha256':digest(dst),'license':'CC0-1.0','license_path':lic.relative_to(REPO).as_posix(),'source_url':f'https://quaternius.com/packs/universalanimationlibrary{str(index) if index==2 else ""}.html','note':'Original engine-neutral animation library, not legacy game animation exports.'})
 (REPO/'Assets/provenance.json').write_text(json.dumps({'assets':records,'generated_models':0,'paid_api_credits_spent':0},ensure_ascii=False,indent=2),encoding='utf-8')
 print('Staged',len(records),'source records')


def blender_build():
 import bpy, bmesh
 from mathutils import Matrix, Vector
 bpy.ops.wm.read_factory_settings(use_empty=True)
 bpy.context.scene.render.fps=30
 bpy.ops.import_scene.gltf(filepath=str(RAW/'UAL1_Standard.glb'))
 rig=next(o for o in bpy.context.scene.objects if o.type=='ARMATURE');rig.name='VerdantRig'
 actions={a.name:a for a in bpy.data.actions}
 for obj in list(bpy.context.scene.objects):
  if obj != rig: bpy.data.objects.remove(obj,do_unlink=True)
 rig.animation_data_clear()
 for pb in rig.pose.bones: pb.matrix_basis=Matrix.Identity(4)
 # Import second library once, retaining its actions and deleting its objects.
 before=set(bpy.data.objects)
 bpy.ops.import_scene.gltf(filepath=str(RAW/'UAL2_Standard.glb'))
 actions.update({a.name:a for a in bpy.data.actions if a.name not in actions})
 for obj in set(bpy.data.objects)-before: bpy.data.objects.remove(obj,do_unlink=True)
 manifest={'characters':[],'animations':[],'environment':[],'blender':bpy.app.version_string}
 def imp(path):
  before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(path));return sorted((o for o in set(bpy.data.objects)-before if not any(c.name.startswith('glTF_not_exported') for c in o.users_collection)),key=lambda o:o.name)
 def deselect():
  for obj in bpy.context.view_layer.objects:obj.select_set(False)
 def normalized_meshes(objs,head_only=False):
  src_rigs=[o for o in objs if o.type=='ARMATURE']
  for arm in src_rigs:
   arm.animation_data_clear()
   for pb in arm.pose.bones: pb.matrix_basis=Matrix.Identity(4)
   bpy.context.view_layer.update()
  meshes=[o for o in objs if o.type=='MESH']
  if head_only:
   # Keep the head/neck portion from the original base-character body.
   arm=src_rigs[0]; neck=arm.data.bones.get('neck_01') or arm.data.bones.get('head')
   cut=(arm.matrix_world@neck.head_local).z-0.012
   for obj in list(meshes):
    bm=bmesh.new();bm.from_mesh(obj.data)
    drop=[v for v in bm.verts if (obj.matrix_world@v.co).z < cut]
    bmesh.ops.delete(bm,geom=drop,context='VERTS');bm.to_mesh(obj.data);bm.free()
    if not len(obj.data.polygons):
     objs.remove(obj);meshes.remove(obj);bpy.data.objects.remove(obj,do_unlink=True)
  # Bake each source rig into the common UAL rest pose, preserving skin weights.
  for arm in src_rigs:
   for pb in sorted(arm.pose.bones,key=lambda p: len(p.parent_recursive)):
    target=rig.data.bones.get(pb.name)
    if target:
     pb.matrix=arm.matrix_world.inverted()@rig.matrix_world@target.matrix_local
     bpy.context.view_layer.update()
  for obj in meshes:
   deselect();obj.select_set(True);bpy.context.view_layer.objects.active=obj
   for mod in list(obj.modifiers):
    if mod.type=='ARMATURE':bpy.ops.object.modifier_apply(modifier=mod.name)
   world=obj.matrix_world.copy();obj.parent=None;obj.matrix_world=world
   # Apply object transforms, leaving meter-unit mesh coordinates at the rig origin.
   bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
   mod=obj.modifiers.new('VerdantSkin','ARMATURE');mod.object=rig
   obj.parent=rig
  for obj in objs:
   if obj.type!='MESH' and obj.name in bpy.data.objects:bpy.data.objects.remove(obj,do_unlink=True)
  return meshes
 def material_info(obj):
  def find_image(socket,visited=None):
   visited=set() if visited is None else visited
   for link in socket.links:
    node=link.from_node
    if node in visited:continue
    visited.add(node)
    if node.type=='TEX_IMAGE' and node.image:return node.image
    for inp in node.inputs:
     found=find_image(inp,visited)
     if found:return found
   return None
  result=[]
  for index,mat in enumerate(obj.data.materials):
   item={'slot':index,'name':mat.name if mat else 'None','color':[.6,.55,.4,1],'roughness':.85,'masked':False}
   if mat and mat.use_nodes:
    bsdf=next((n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED'),None)
    if bsdf:
     item['color']=list(bsdf.inputs['Base Color'].default_value);item['roughness']=float(bsdf.inputs['Roughness'].default_value)
     image=find_image(bsdf.inputs['Base Color'])
     if image:
      p=Path(bpy.path.abspath(image.filepath));item['texture']=p.relative_to(REPO).as_posix() if p.is_relative_to(REPO) else str(p)
     item['masked']=bool(bsdf.inputs['Alpha'].is_linked)
   if mat and mat.get('verdant_tint'):item['tint']=list(mat['verdant_tint'])
   result.append(item)
  return result
 def export(path,objects,animated=False):
  deselect()
  for obj in objects:
   obj.hide_set(False);obj.hide_viewport=False;obj.hide_render=False;obj.hide_select=False
  bpy.context.view_layer.update()
  for obj in objects:obj.select_set(True)
  bpy.context.view_layer.update()
  print('EXPORT_SELECTION',path.name,[(o.name,o.type) for o in objects])
  bpy.context.view_layer.objects.active=rig if rig in objects else objects[0]
  with bpy.context.temp_override(selected_objects=objects,selected_editable_objects=objects,active_object=objects[0],object=objects[0]):
   bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,
     bake_anim=animated,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,
     axis_forward='-Y',axis_up='Z',apply_unit_scale=True,path_mode='AUTO',use_mesh_modifiers=True,mesh_smooth_type='FACE')
  if any(o.type=='MESH' for o in objects):
   from io_scene_fbx import parse_fbx
   tree,_=parse_fbx.parse(str(path))
   assert any(c.id==b'Geometry' for e in tree.elems if e.id==b'Objects' for c in e.elems),('No geometry in FBX',path)
 characters=[]
 chosen=sys.argv[sys.argv.index('--only-character')+1] if '--only-character' in sys.argv else None
 for asset,outfit,sex,hair in CHARACTERS:
  if chosen and asset!=chosen:continue
  meshes=normalized_meshes(imp(RAW/'outfits'/outfit/(outfit+'.gltf')))
  basename=f'Superhero_{sex}_FullBody'
  meshes+=normalized_meshes(imp(RAW/'base'/basename/(basename+'.gltf')),head_only=True)
  if hair:meshes+=normalized_meshes(imp(RAW/'base'/hair/(hair+'.gltf')))
  deselect()
  for ob in meshes:ob.select_set(True)
  mesh=meshes[0];bpy.context.view_layer.objects.active=mesh;bpy.context.view_layer.update()
  with bpy.context.temp_override(active_object=mesh,object=mesh,selected_objects=meshes,selected_editable_objects=meshes):
   bpy.ops.object.join()
  mesh.name=asset
  assert len(mesh.data.vertices)>1000,(asset,len(mesh.data.vertices))
  descriptors=material_info(mesh)
  # A deliberately simple preview matches the engine materials: diffuse + matte roughness.
  for mat,desc in zip(mesh.data.materials,descriptors):
   if not mat:continue
   mat.use_nodes=True;nt=mat.node_tree;nt.nodes.clear()
   bsdf=nt.nodes.new('ShaderNodeBsdfPrincipled');bsdf.inputs['Roughness'].default_value=.85
   output=nt.nodes.new('ShaderNodeOutputMaterial');nt.links.new(bsdf.outputs['BSDF'],output.inputs['Surface'])
   if desc.get('texture'):
    tex=nt.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(REPO/desc['texture']),check_existing=True)
    if 'Hair' in mat.name:
     mix=nt.nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[2].default_value=(.3,.15,.065,1)
     nt.links.new(tex.outputs['Color'],mix.inputs[1]);nt.links.new(mix.outputs[0],bsdf.inputs['Base Color']);mat['verdant_tint']=[.3,.15,.065]
    else:nt.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
   else:bsdf.inputs['Base Color'].default_value=desc['color']
  path=OUT/(asset+'.fbx');export(path,[rig,mesh])
  manifest['characters'].append({'name':asset,'fbx':path.relative_to(REPO).as_posix(),'ue_path':'/Game/Art/Characters/'+asset,'materials':material_info(mesh),'vertices':len(mesh.data.vertices),'triangles':sum(len(p.vertices)-2 for p in mesh.data.polygons),'bones':len(rig.data.bones)})
  characters.append(mesh);mesh.select_set(False);mesh.hide_set(True);mesh.hide_render=True
 # Export separate animations on exactly the same canonical rest skeleton.
 for name,source,index in CLIPS:
  action=actions[source]
  rig.animation_data_create();rig.animation_data.action=action
  if len(action.slots):rig.animation_data.action_slot=action.slots[0]
  scene=bpy.context.scene;scene.frame_start=int(action.frame_range[0]);scene.frame_end=int(action.frame_range[1]);scene.frame_set(scene.frame_start)
  path=OUT/(name+'.fbx');export(path,[rig],True)
  manifest['animations'].append({'name':name,'source_action':source,'library':index,'fbx':path.relative_to(REPO).as_posix(),'ue_path':'/Game/Art/Characters/Animations/'+name,'duration':(scene.frame_end-scene.frame_start)/30})
 rig.animation_data_clear()
 for pb in rig.pose.bones:pb.matrix_basis=Matrix.Identity(4)
 bpy.context.scene.frame_set(0);bpy.context.view_layer.update()
 for name,source in NATURE:
  objs=imp(RAW/'nature'/source/(source+'.gltf'));meshes=[o for o in objs if o.type=='MESH']
  deselect()
  for o in meshes:o.select_set(True)
  mesh=meshes[0];bpy.context.view_layer.objects.active=mesh;bpy.context.view_layer.update()
  if len(meshes)>1:
   with bpy.context.temp_override(active_object=mesh,object=mesh,selected_objects=meshes,selected_editable_objects=meshes):bpy.ops.object.join()
  mesh.name=name
  bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
  path=OUT/(name+'.fbx');export(path,[mesh]);manifest['environment'].append({'name':name,'fbx':path.relative_to(REPO).as_posix(),'ue_path':'/Game/Art/Nature/'+name,'materials':material_info(mesh),'vertices':len(mesh.data.vertices),'triangles':sum(len(p.vertices)-2 for p in mesh.data.polygons)})
  bpy.data.objects.remove(mesh,do_unlink=True)
 # Render freshly assembled characters as an intermediate visual check.
 from math import radians
 for index,mesh in enumerate(characters):
  mesh.hide_set(False);mesh.hide_render=False;mesh.location.x=(index-(len(characters)-1)/2)*1.4
 bpy.ops.object.camera_add(location=(5,-8,3.6));cam=bpy.context.object;direction=Vector((0,0,1))-cam.location;cam.rotation_euler=direction.to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=3.8 if chosen else 5.8;bpy.context.scene.camera=cam
 bpy.ops.object.light_add(type='AREA',location=(1,-4,6));bpy.context.object.data.energy=1000;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=5;bpy.context.object.rotation_euler=(Vector((0,0,1))-bpy.context.object.location).to_track_quat('-Z','Y').to_euler()
 bpy.ops.object.light_add(type='AREA',location=(-3,2,4));bpy.context.object.data.energy=700;bpy.context.object.data.size=4;bpy.context.object.rotation_euler=(Vector((0,0,1))-bpy.context.object.location).to_track_quat('-Z','Y').to_euler()
 scene=bpy.context.scene;scene.world=bpy.data.worlds.new('PreviewWorld');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.14,.18,.16,1);scene.world.node_tree.nodes['Background'].inputs[1].default_value=.5
 scene.render.engine='CYCLES';scene.cycles.samples=16;scene.render.resolution_x=1200;scene.render.resolution_y=700;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/((chosen+'_preview.png') if chosen else 'characters_preview.png'));bpy.ops.render.render(write_still=True)
 if chosen and (OUT/'manifest.json').exists():
  prior=json.loads((OUT/'manifest.json').read_text(encoding='utf-8'));manifest['characters']=[r for r in prior.get('characters',[]) if r['name']!=chosen]+manifest['characters']
 (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8');print('VERDANT_ASSETS_COMPLETE',len(manifest['characters']),len(manifest['animations']),len(manifest['environment']))

if __name__=='__main__':
 if '--blender' in sys.argv:blender_build()
 else:
  stage()
  if '--stage-only' not in sys.argv:
   for spec in CHARACTERS:
    subprocess.run([str(BLENDER),'--background','--factory-startup','--python-exit-code','1','--python',str(Path(__file__).resolve()),'--','--blender','--only-character',spec[0]],check=True)

  if '--stage-only' not in sys.argv:
   from PIL import Image
   previews=[Image.open(OUT/(spec[0]+'_preview.png')).convert('RGB') for spec in CHARACTERS]
   sheet=Image.new('RGB',(1800,350),(50,60,55))
   for index,preview in enumerate(previews):sheet.paste(preview.resize((600,350)),(index*600,0))
   sheet.save(OUT/'characters_preview.png')
