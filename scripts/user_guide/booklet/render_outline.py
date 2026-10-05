"""Render Revision H as white technical line art with six simplified round tube faces.

Run Blender with the original .blend loaded. Never saves over the source model.
"""
import bpy
import math
import bmesh
from pathlib import Path
from mathutils import Vector, Matrix

root = Path(__file__).resolve().parents[3]
out = root / 'output/images'
out.mkdir(parents=True, exist_ok=True)
s = bpy.context.scene
keep = {'front_shell', 'rear_cover', 'tilt_base_15deg',
        'BTN0_button', 'BTN1_button', 'BTN2_button'}
for o in s.objects:
    if o.type in {'MESH', 'CURVE', 'SURFACE', 'FONT'}:
        o.hide_render = o.name not in keep

white = bpy.data.materials.new('Outline white surface')
white.use_nodes = True
nodes = white.node_tree.nodes
nodes.clear()
emission = nodes.new('ShaderNodeEmission')
emission.inputs['Color'].default_value = (1, 1, 1, 1)
emission.inputs['Strength'].default_value = 1
output = nodes.new('ShaderNodeOutputMaterial')
white.node_tree.links.new(emission.outputs[0], output.inputs['Surface'])
black = white.copy()
black.name = 'Technical black edge ink'
black.node_tree.nodes.get('Emission').inputs['Color'].default_value = (0, 0, 0, 1)

for i in range(1, 7):
    glass = bpy.data.objects[f'N{i}_Glass_envelope']
    # Local Z is the tube axis. Replace glass and all electrodes with a round face.
    bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=15, depth=.5)
    disk = bpy.context.object
    disk.name = f'Outline_round_tube_{i}'
    disk.matrix_world = glass.matrix_world @ Matrix.Translation((0, 0, 30.75))
    keep.add(disk.name)

for name in keep:
    o = bpy.data.objects[name]
    o.data = o.data.copy()
    o.data.materials.clear()
    o.data.materials.append(white)
    # Weld CAD triangle seams before detecting structural edges. Mark the
    # perimeter of major planar faces, including their smooth bevel junctions,
    # which silhouette-only rendering can omit.
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.0001)
    bm.normal_update()
    bm.to_mesh(o.data)
    bm.free()
    adjacent = {}
    for poly in o.data.polygons:
        for key in poly.edge_keys:
            adjacent.setdefault(tuple(sorted(key)), []).append(poly)
    for edge in o.data.edges:
        faces = adjacent.get(tuple(sorted(edge.vertices)), [])
        mark = len(faces) == 1
        if len(faces) == 2:
            a, b = faces
            dot = a.normal.dot(b.normal)
            planar_a = max(abs(v) for v in a.normal) > .99999
            planar_b = max(abs(v) for v in b.normal) > .99999
            mark = dot < math.cos(math.radians(35)) or (planar_a != planar_b and dot < .99999)
        edge.select = mark
    # Render marked geometry as narrow strokes as well: Freestyle visibility
    # can split tangent edges on imported CAD faces into incomplete segments.
    ink = bpy.data.curves.new(name + '_structural_ink', 'CURVE')
    ink.dimensions = '3D'
    ink.bevel_depth = .20
    ink.bevel_resolution = 2
    for edge in o.data.edges:
        if not edge.select:
            continue
        faces = adjacent.get(tuple(sorted(edge.vertices)), [])
        normal = sum((p.normal for p in faces), Vector())
        if normal.length:
            normal.normalize()
        spline = ink.splines.new('POLY')
        spline.points.add(1)
        for point, idx in zip(spline.points, edge.vertices):
            p = o.data.vertices[idx].co + normal * .035
            point.co = (*p, 1)
    obj = bpy.data.objects.new(name + '_structural_ink', ink)
    s.collection.objects.link(obj)
    obj.matrix_world = o.matrix_world.copy()
    ink.materials.append(black)

s.render.engine = 'CYCLES'
s.cycles.samples = 8
s.cycles.device = 'CPU'
s.render.resolution_x = 2400
s.render.resolution_y = 1600
s.render.resolution_percentage = 100
s.render.image_settings.file_format = 'PNG'
s.render.image_settings.color_mode = 'RGB'
s.render.film_transparent = False
s.world.use_nodes = True
s.world.node_tree.nodes.get('Background').inputs[0].default_value = (1, 1, 1, 1)
s.world.node_tree.nodes.get('Background').inputs[1].default_value = 1
s.compositing_node_group = None
s.view_settings.view_transform = 'Standard'
s.view_settings.look = 'None'
s.view_settings.exposure = 0
s.view_settings.gamma = 1

target = Vector((125, 13, 42))
azimuth = math.radians(30)    # 左右角度：0 正面，正值偏右
elevation = math.radians(30)  # 俯視角度：0 平視，90 正上方
distance = 650               # 相機距離：越大，時鐘在畫面中越小
s.camera.location = target + Vector((
    distance * math.cos(elevation) * math.sin(azimuth),
    -distance * math.cos(elevation) * math.cos(azimuth),
    distance * math.sin(elevation),
))
s.camera.rotation_euler = (target - s.camera.location).to_track_quat('-Z', 'Y').to_euler()
s.camera.data.type = 'PERSP'
# s.camera.data.type = 'ORTHO'
s.camera.data.lens = 50
s.camera.data.sensor_width = 36
s.camera.data.sensor_fit = 'HORIZONTAL'
s.camera.data.shift_x = 0
s.camera.data.shift_y = 0
s.camera.data.dof.use_dof = False
s.render.use_freestyle = True
fs = s.view_layers[0].freestyle_settings
fs.crease_angle = math.radians(120)
while len(fs.linesets):
    fs.linesets.remove(fs.linesets[0])
lines = fs.linesets.new('Visible exterior outlines')
surface_collection = bpy.data.collections.new('Technical outline surfaces')
s.collection.children.link(surface_collection)
for name in keep:
    surface_collection.objects.link(bpy.data.objects[name])
lines.select_by_collection = False
lines.collection = surface_collection
lines.select_silhouette = True
lines.select_border = True
lines.select_crease = True
lines.select_contour = True
lines.select_external_contour = True
lines.select_material_boundary = False
lines.select_edge_mark = True
lines.linestyle.color = (0, 0, 0)
lines.linestyle.thickness = 3.0
s.render.filepath = str(out / 'nixie_clock_reference_angle_lineart_left_view.png')
bpy.ops.render.render(write_still=True)
print('Outline saved:', s.render.filepath)
