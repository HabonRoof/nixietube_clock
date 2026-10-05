"""Bounded-error Blender mesh reduction, leaving STEP source geometry intact."""
import bpy, bmesh, math
from mathutils.bvhtree import BVHTree

def tree(mesh):
    mesh.calc_loop_triangles()
    return BVHTree.FromPolygons([v.co.copy() for v in mesh.vertices],
                               [tuple(t.vertices) for t in mesh.loop_triangles],all_triangles=True)

def sampled_distance(mesh,bvh):
    step=max(1,len(mesh.vertices)//1500)
    return max((bvh.find_nearest(mesh.vertices[i].co)[3] for i in range(0,len(mesh.vertices),step)),default=0)

def simplify_scene(scene):
    records=[]
    bpy.context.window.scene=scene
    bpy.ops.object.select_all(action='DESELECT')
    for obj in list(scene.objects):
        if obj.type!='MESH': continue
        before=len(obj.data.polygons)
        name=obj.name
        reference=obj.copy(); reference.data=obj.data.copy()
        scene.collection.objects.link(reference); reference.hide_render=True
        if name.startswith('Cathode_digit_'): target,tolerance=3000,.025
        elif name=='Glass_envelope': target,tolerance=6000,.035
        elif name=='Exhaust_tip_estimated': target,tolerance=1200,.025
        elif name=='Anode_square_mesh': target,tolerance=2500,.018
        elif name.startswith('Pin_'): target,tolerance=320,.025
        else: target,tolerance=700,.035
        # Weld coincident CAD surface seams before collapse.
        bm=bmesh.new(); bm.from_mesh(obj.data)
        bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000001)
        bm.to_mesh(obj.data); bm.free(); obj.data.update()
        source_tree=tree(obj.data)
        obj.select_set(True); bpy.context.view_layer.objects.active=obj
        mod=obj.modifiers.new('Render mesh reduction','DECIMATE')
        mod.use_collapse_triangulate=True
        ratio=min(1,target/max(1,len(obj.data.polygons)))
        error=0
        while True:
            mod.ratio=ratio; bpy.context.view_layer.update()
            evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
            mesh=evaluated.to_mesh()
            candidate_tree=tree(mesh)
            error=max(sampled_distance(mesh,source_tree),sampled_distance(obj.data,candidate_tree))
            evaluated.to_mesh_clear()
            if error <= tolerance or ratio>=1: break
            ratio=min(1,ratio*1.8)
        bpy.ops.object.modifier_apply(modifier=mod.name)
        transfer=obj.modifiers.new('Preserve smooth surface normals','DATA_TRANSFER')
        transfer.object=reference; transfer.use_loop_data=True
        transfer.data_types_loops={'CUSTOM_NORMAL'}; transfer.loop_mapping='POLYINTERP_NEAREST'
        bpy.ops.object.modifier_apply(modifier=transfer.name)
        bpy.data.objects.remove(reference,do_unlink=True)
        obj.select_set(False)
        records.append(dict(object=name,before_faces=before,after_faces=len(obj.data.polygons),
                            sampled_surface_error_mm=error,error_limit_mm=tolerance))
    # Inspection instances use the same reduced topology with their own material.
    for inspection in bpy.data.scenes:
        if inspection==scene: continue
        for obj in inspection.objects:
            if obj.type=='MESH' and obj.name.startswith('Cathode_digit_'):
                source=scene.objects.get(obj.name.split('.')[0])
                if source:
                    material=obj.data.materials[0]
                    obj.data=source.data
                    obj.material_slots[0].link='OBJECT'; obj.material_slots[0].material=material
    bpy.data.orphans_purge(do_recursive=True)
    return records
