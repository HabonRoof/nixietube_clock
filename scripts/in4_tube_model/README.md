# IN-4 nominal 1:1 model

Created from the ten supplied photographs (IMG_4322–IMG_4331) and published IN-4/RSH31 references. **This is a nominal-scale reconstruction, not a dimensional scan of the user's tube.** Glass curvature, wall thickness, internal parts, numeral centerlines and wiring are approximations. The bottom-to-top stack order follows the user’s specified sequence: **1, 7, 5, 9, 3, 4, 0, 6, 2, 8**.

## Deliverables

- `IN4_nominal.blend`: Blender scene with glass and metal materials, named components, separate collections, camera and lighting. One Blender unit is **1 mm** (`scale_length = 0.001`). Glass displays as wireframe in solid viewport mode so internals can be inspected; renders use the glass material.
- `IN4_detailed.step`: CAD solid assembly, including glass, pins, digits, mesh and supports; millimeters.
- `IN4_mechanical.step`: lighter glass/exhaust/pin model for mechanical layout; millimeters. Glass is a hollow solid with a closed foot and viewing end.
- `IN4_nominal.FCStd`: editable FreeCAD source solids.
- `IN4_preview.png`: assembled view with glass.
- `IN4_front_inspection.png`: top view with glass hidden to inspect electrodes.
- `IN4_digit_layers.png`: ten isolated cathodes, labeled in bottom-to-top order.
- `digit_layers.json`: each digit’s layer index, center height, bounds, and solid validation.

In Blender, choose the scene **Digit layers - isolated inspection** to see each digit separately. The assembled scene has numbered subcollections under **Digits**; select a cathode and use Local View to isolate it. Lead wires have their own **Leads** collection. Each cathode is a single connected CAD solid built from explicit line/Bezier strokes; digit 4 uses joined strokes and digit 8 has joined loops.
- `validation.json`: STEP reimport checks for shape validity, tight bounds and volume agreement.

## Dimensions and coordinates

The chosen coarse-grid reference gives nominal glass diameter **30 mm**, glass height **31 mm**, and total height **41 mm**. RSH31 drawing specifies **18 mm pin-circle diameter**, **1 mm pin diameter**, and **7 mm exposed pin length**. The central exhaust projection is modeled as 10 mm to match the reference total height; its precise shape is estimated. Digits have approximately 17 mm centerline height.

The glass foot is at Z=0, the viewing end is toward +Z, and pins extend toward −Z. Numerals are upright toward +Y when viewed down from +Z. Pin 7 is at −Y. Fourteen pin locations use 24.5° pitch with a 41.5° indexing gap between pins 14 and 1. `parameters.json` records the nominal values and exact pin center coordinates. Verify pin orientation against the physical socket before using this for PCB design; the existing PCB was not modified.

References disagree on envelope dimensions: a separate IN-4 drawing gives Ø31 × 35 mm glass and 46 mm overall, while the coarse-grid reference gives Ø30 × 31 mm and 41 mm overall (maximum diameter 32 mm). No caliper measurements were supplied. The selected dimensions are appropriate for a provisional visual/mechanical model, but an exact specimen match requires glass diameter/height, pin projection, exhaust projection and pin spacing measurements. Do not interpret nominal diameter as a tolerance allowance for an enclosure.

## Sources

- Coarse-grid tube dimensions and variant photos: https://www.tube-tester.com/sites/nixie/data/in-4/in-4-sh2.htm
- RSH31 base drawing: https://www.tube-tester.com/sites/nixie/data/soc/PL31-P/rsh31.gif
- Alternative IN-4 dimensional drawing: https://www.nixies.us/wp-content/uploads/2020/10/in4.pdf
- User photographs: local Downloads/IMG_4322.JPG through IMG_4331.JPG.

## Rebuild

Run `build_cad.py` using FreeCAD's Python environment. It writes both STEP files, the FreeCAD document, nominal dimension metadata and a tessellated interchange file (`geometry.json`). Then run Blender with `--background --python build_blender.py` to build the scene and render previews. Geometry construction is in `build_cad.py`; `parameters.json` is an output record, not a configuration input.

STEP geometry is generated directly with FreeCAD/OpenCascade, avoiding a faceted conversion of Blender meshes. Blender receives a tessellation of those same solids. Blender material appearance and transparency are stored in `.blend`; the STEP export represents geometry and does not preserve the render setup. Editing the Blender mesh does not automatically update STEP; change the CAD generator and regenerate both for synchronized outputs.

## Render mesh simplification

The Blender model uses reduced meshes, with sampled bidirectional surface-distance checks during simplification (0.018–0.035 mm limits, depending on the component). These sampled checks are not a certified maximum-error bound. The ten digit layers and inspection scene remain available. The STEP files retain their CAD solids.

`IN4_before_simplification.blend` preserves the previous dense model. `mesh_optimization.json` records face counts, sampled deviations and a single before/after render comparison. Render timings include cache effects and are not a general performance guarantee. `build_blender.py` applies the same reduction on rebuild.

The simplified Blender scene defaults to **16 Cycles samples with denoising** for quicker previews. Increase samples to 32 or more for final renders. Smooth normals are transferred from the original surfaces to preserve their shading after reduction. The installed Blender executable is `/Applications/Blender.app/Contents/MacOS/Blender`.
