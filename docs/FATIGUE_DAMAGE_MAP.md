# Fatigue damage map

`FatigueDamageMapBuilder` projects a `FatigueSpectrumReport` onto a `MeshData` without changing the underlying analysis results.

Each entry retains the exact result position, label, damage availability and spectrum status. Coordinates have an explicit kind:

- `Node`: exact mesh-node coordinates, matched by external node ID.
- `ElementCentroid`: arithmetic centroid of the element's mesh nodes, matched by external element ID.
- `Unavailable`: no coordinate is claimed.

Integration-point locations deliberately remain unavailable. The current mesh model does not contain solver integration-point coordinates or element shape-function evaluation at the imported IP locations. Using the element centroid for an IP would be misleading.

Failed analyses remain in the map with `hasDamage=false`; they are not assigned zero damage. The map is a data-transfer layer, not a renderer or a damage interpolation algorithm. It does not extrapolate element values to nodes or interpolate IP values.

## Serialization

`FatigueReportWriter` also exports a damage map through caller-owned streams:

- `writeDamageMapCsv(map, stream)`: one row per original result position;
- `writeDamageMapJson(map, stream)`: schema `openmodel.fatigue-damage-map.v1`.

The JSON keeps location and external entity identity, integration/section point indices, the `has_damage` flag, nullable damage, analysis status, coordinate kind and nullable coordinates. A failed analysis is represented by `has_damage=false` and `damage=null`, never by a numeric zero. Missing IP coordinates are represented as `coordinates=null`.

The CSV and JSON writers do not open files or perform mesh interpolation. The caller owns stream lifetime and destination selection.
