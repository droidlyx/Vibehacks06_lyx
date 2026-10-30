"""Import the newly prepared CC0 subset into this project's Content/Art.
Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Requires Assets/Prepared/manifest.json from prepare_local_assets.py.
"""
from pathlib import Path
import hashlib, json, re
import unreal as u

ROOT=Path(u.Paths.project_dir())
manifest=json.loads((ROOT/'Assets/Prepared/manifest.json').read_text(encoding='utf-8'))
manifest['characters'].sort(key=lambda r:['SK_Traveler','SK_Gatherer','SK_Keeper'].index(r['name']))
assets=u.AssetToolsHelpers.get_asset_tools()
report={'characters':[],'animations':[],'environment':[],'errors':[]}

def import_fbx(row,kind,skeleton=None):
 folder,name=row['ue_path'].rsplit('/',1)
 task=u.AssetImportTask();task.filename=str(ROOT/row['fbx']);task.destination_path=folder;task.destination_name=name
 task.automated=True;task.replace_existing=True;task.replace_existing_settings=True;task.save=True;task.factory=u.FbxFactory()
 opt=u.FbxImportUI();opt.automated_import_should_detect_type=False;opt.import_materials=False;opt.import_textures=False
 if kind=='character':
  opt.mesh_type_to_import=u.FBXImportType.FBXIT_SKELETAL_MESH;opt.import_as_skeletal=True;opt.import_mesh=True;opt.import_animations=False
  if skeleton:opt.skeleton=skeleton
  opt.skeletal_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
 elif kind=='animation':
  opt.mesh_type_to_import=u.FBXImportType.FBXIT_ANIMATION;opt.import_mesh=False;opt.import_as_skeletal=True;opt.import_animations=True;opt.skeleton=skeleton
  opt.anim_sequence_import_data.set_editor_property('animation_length',u.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
  opt.anim_sequence_import_data.set_editor_property('use_default_sample_rate',False)
  opt.anim_sequence_import_data.set_editor_property('custom_sample_rate',30)
 else:
  opt.mesh_type_to_import=u.FBXImportType.FBXIT_STATIC_MESH;opt.import_as_skeletal=False;opt.import_mesh=True;opt.import_animations=False
  opt.static_mesh_import_data.set_editor_property('combine_meshes',True)
  opt.static_mesh_import_data.set_editor_property('normal_import_method',u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
 task.options=opt;assets.import_asset_tasks([task])
 obj=u.load_asset(row['ue_path'])
 if not obj:raise RuntimeError('Missing imported asset '+row['ue_path']+' imported='+str(task.imported_object_paths))
 return obj

def texture(path):
 src=ROOT/path
 if not src.exists():raise FileNotFoundError(src)
 name='T_'+re.sub(r'[^A-Za-z0-9_]','_',src.stem)+'_'+hashlib.sha256(src.read_bytes()).hexdigest()[:6]
 target='/Game/Art/Textures/'+name
 tex=u.load_asset(target)
 if not tex:
  task=u.AssetImportTask();task.filename=str(src);task.destination_path='/Game/Art/Textures';task.destination_name=name;task.automated=True;task.save=True
  assets.import_asset_tasks([task]);tex=u.load_asset(target)
 if not tex:raise RuntimeError('Missing texture '+target)
 tex.set_editor_property('srgb',True)
 return tex

def make_material(asset_name,desc):
 name='M_'+asset_name+'_'+str(desc['slot']).zfill(2);folder='/Game/Art/Materials';target=folder+'/'+name
 mat=u.load_asset(target)
 if mat:u.MaterialEditingLibrary.delete_all_material_expressions(mat)
 else:mat=assets.create_asset(name,folder,u.Material,u.MaterialFactoryNew())
 masked=desc.get('masked',False)
 if masked:
  mat.set_editor_property('blend_mode',u.BlendMode.BLEND_MASKED);mat.set_editor_property('two_sided',True)
 if desc.get('texture'):
  expr=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionTextureSample,-400,0)
  expr.texture=texture(desc['texture'])
  if desc.get('tint'):
   tint=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-400,-180);tint.constant=u.LinearColor(*desc['tint'],1)
   mul=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionMultiply,-180,0)
   u.MaterialEditingLibrary.connect_material_expressions(expr,'RGB',mul,'A');u.MaterialEditingLibrary.connect_material_expressions(tint,'',mul,'B');u.MaterialEditingLibrary.connect_material_property(mul,'',u.MaterialProperty.MP_BASE_COLOR)
  else:u.MaterialEditingLibrary.connect_material_property(expr,'RGB',u.MaterialProperty.MP_BASE_COLOR)
  if masked:u.MaterialEditingLibrary.connect_material_property(expr,'A',u.MaterialProperty.MP_OPACITY_MASK)
 else:
  expr=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant3Vector,-400,0)
  c=desc.get('color',[.6,.55,.4,1]);expr.constant=u.LinearColor(*c[:3],1)
  u.MaterialEditingLibrary.connect_material_property(expr,'',u.MaterialProperty.MP_BASE_COLOR)
 rough=u.MaterialEditingLibrary.create_material_expression(mat,u.MaterialExpressionConstant,-400,200);rough.r=max(.65,float(desc.get('roughness',.85)))
 u.MaterialEditingLibrary.connect_material_property(rough,'',u.MaterialProperty.MP_ROUGHNESS)
 if asset_name.startswith('SK_'):u.MaterialEditingLibrary.set_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
 u.MaterialEditingLibrary.recompile_material(mat);u.EditorAssetLibrary.save_loaded_asset(mat)
 return mat

def apply_materials(mesh,row):
 skeletal=isinstance(mesh,u.SkeletalMesh)
 slots=list(mesh.get_editor_property('materials' if skeletal else 'static_materials'))
 def norm(name):return re.sub(r'[^A-Za-z0-9_]','_',str(name))
 for index,slot in enumerate(slots):
  original=norm(slot.get_editor_property('imported_material_slot_name'))
  candidates=[d for d in row['materials'] if norm(d['name'])==original]
  if not candidates:
   candidates=[d for d in row['materials'] if norm(d['name']).split('_00')[0]==original.split('_00')[0]]
  if not candidates:raise RuntimeError('Unmapped material slot '+row['name']+' '+original)
  desc=candidates[0];mat=make_material(row['name'],desc)
  if skeletal:
   slot.set_editor_property('material_interface',mat);slots[index]=slot
  else:mesh.set_material(index,mat)
 if skeletal:mesh.set_editor_property('materials',slots)
 u.EditorAssetLibrary.save_loaded_asset(mesh)

try:
 # Explicit factory + command line cvar selects the native FBX import path.
 skeleton=None
 for row in manifest['characters']:
  mesh=import_fbx(row,'character',skeleton)
  if skeleton is None:skeleton=mesh.get_editor_property('skeleton')
  apply_materials(mesh,row)
  report['characters'].append({'path':row['ue_path'],'skeleton':skeleton.get_path_name(),'bounds':str(mesh.get_bounds())})
 for row in manifest['animations']:
  anim=import_fbx(row,'animation',skeleton)
  report['animations'].append({'path':row['ue_path'],'duration':anim.get_play_length()})
 for row in manifest['environment']:
  mesh=import_fbx(row,'static');apply_materials(mesh,row)
  report['environment'].append({'path':row['ue_path'],'bounds':str(mesh.get_bounds())})
 u.EditorAssetLibrary.save_directory('/Game/Art',only_if_is_dirty=False,recursive=True)
 report['success']=True
except Exception as ex:
 report['success']=False;report['errors'].append(str(ex));raise
finally:
 out=ROOT/'Assets/Prepared/unreal_import_report.json';out.write_text(json.dumps(report,indent=2),encoding='utf-8')
 u.log('VERDANT_ART_IMPORT '+json.dumps(report))
