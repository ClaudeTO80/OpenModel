# MGWV — OpenModel Web View format, version 1

MGWV is a render-oriented, little-endian container produced by `WebViewWriter` and consumed by `examples/webgl-fem-viewer`. It is **not** a persistence format: it keeps only what is needed to draw a mesh (use MGMB to save and reload a `MeshData`). Positions are float32, so MGWV cannot round-trip a mesh.

## Layout

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `MGWV CR LF 0x1A LF` |
| 8 | 4 | version (`1`) |
| 12 | 4 | JSON descriptor length in bytes (multiple of 4, padded with spaces) |
| 16 | 4 | binary payload length in bytes |
| 20 | 4 | reserved, zero |
| 24 | JSON length | UTF-8 JSON descriptor |
| 24 + JSON length | payload length | binary payload |

The file size must equal `24 + jsonLength + payloadLength`. The payload starts at a multiple of 4, so typed-array views can be created on it without copying.

## JSON descriptor

- `format`, `version`.
- `origin`: model-space point subtracted from every stored position (the centre of the bounding box).
- `bbox.min`, `bbox.max`: bounds in model space (not relative to `origin`).
- `counts`: `nodes` and `elements` of the source mesh; `vertices`, `triangles`, `boundaryTriangles`, `edges`, `boundaryEdges` of the exported data.
- `blocks`: `[{name, type, dimension, elements}]`, in `MeshData` block order.
- `groups`: `[{name, dimension, tag, material}]`, in `MeshData` physical-group order.
- `chunks`: `{offset, count}` of each array; offsets are relative to the payload start, `count` is the number of 32-bit words.
- `fields`: `[{name, min, max, offset}]`; each field is `vertices` float32 values. An entry with `"location":"element"` is a per-element field: it holds `triangles` float32 values, one per triangle (the value of the element that owns the face; both triangles of a quad share it), and `min`/`max` refer to the exported triangles.
- `modes` (optional): `[{label, frequency, max, offset}]`: vibration modes. Each `offset` points to `3 × vertices` float32 displacements. When present there is no `displacement` chunk; `deformation.max` is the largest magnitude over all modes.
- `deformation` (optional, present with the `displacement` chunk): `{name, max}`: label of the displacement quantity and the largest displacement magnitude, used by viewers to suggest a scale factor.
- `results` (optional): `{label}`: text describing the solver results shown (source file and step). Its presence marks the file as a results view. Files without these keys, and viewers that ignore them, behave as before.

## Payload chunks (all 32-bit little-endian words)

| Chunk | Type | Content |
|---|---|---|
| `positions` | f32 × 3·vertices | `(x, y, z) − origin` of each stored vertex |
| `triIndices` | u32 × 3·triangles | vertex indices; the first `boundaryTriangles` triangles are the skin, the rest are interior faces |
| `triBlock` | u32 × triangles | index into `blocks` of the 2D element coincident with the face (boundary patch) if there is one, otherwise of the owning volume element |
| `triRegion` | u32 × triangles | `0` = none, otherwise `1 +` index into `groups` of the volume group of the owning element (for shell-only faces, of the shell element) |
| `triSurface` | u32 × triangles | `0` = none, otherwise `1 +` index into `groups` of the 2D group of a coincident surface element |
| `edgeIndices` | u32 × 2·edges | vertex index pairs; the first `boundaryEdges` edges lie on the skin |
| `edgeBlock` | u32 × edges | lowest block index among the faces or line elements that use the edge |
| `edgeRegion` | u32 × edges | lowest non-zero volume-group index (1-based, as `triRegion`) among the faces that use the edge; 0 when none. Optional: files written before 0.43 lack it and the viewer then treats every edge as region 0 |
| `vertexNodeId` | u32 × vertices | optional (picking): external node id of each stored vertex |
| `triElementId` | u32 × triangles | optional (picking): external id of the element that owns the face (of the 2D element for faces without a volume element) |
| `triFace` | u32 × triangles | optional (picking): running index of the geometric face; the two triangles of a quadrilateral share it. Skin faces come first |
| `displacement` | f32 × 3·vertices | optional: nodal displacement `(ux, uy, uz)` of each stored vertex, model units; the viewer adds `scale × displacement` to the stored position to draw the deformed shape |
| field `k` | f32 × vertices | nodal values of field `k` |

## Semantics

- A face is **on the skin** if at most one volume element uses it. 2D elements coincident with a volume face do not make it interior: they only contribute `triSurface`.
- Faces between two different blocks are always exported (as interior faces) even when `includeInteriorFaces` is false, so material interfaces stay visible.
- Quadrilaterals are split into two triangles `(0,1,2)`, `(0,2,3)`; both inherit the face attributes. Wireframe edges follow the original quadrilateral sides, not the diagonal.
- Higher-order elements are drawn with their corner nodes only (corner nodes must come first, as in Gmsh, Abaqus and VTK conventions). `Line2`/`Line3` elements become edges; `Point1` is ignored.
- Face winding is not guaranteed: viewers must shade both sides (the reference viewer derives flat normals from screen-space derivatives).

## Limits

- Counts and offsets are 32-bit: payload up to 4 GiB, fewer than 2³² − 1 nodes.
- Each entry of `groups` may carry an optional `"color":"#rrggbb"` (sRGB), for example the colour of the component in the CAD file. Viewers use it instead of their palette.
- `edgeRegion` has the same approximation as `edgeBlock`: an edge shared by faces of two groups follows the lower group, so hiding that group hides the shared edge.
- `edgeBlock` is an approximation for edges shared by several blocks: hiding the lowest block hides the shared edge even if a neighbouring block is still visible.

- The three picking chunks are written together, and only when every node and element id fits 32 bits; viewers offer picking only when they are present.
