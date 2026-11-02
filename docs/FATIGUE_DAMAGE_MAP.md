# Fatigue damage map

`FatigueDamageMapBuilder` projects a `FatigueSpectrumReport` onto a `MeshData` without changing the underlying analysis results.

Each entry retains the exact result position, label, damage availability and spectrum status. Coordinates have an explicit kind:

- `Node`: exact mesh-node coordinates, matched by external node ID.
- `ElementCentroid`: arithmetic centroid of the element's mesh nodes, matched by external element ID.
- `Unavailable`: no coordinate is claimed.

Integration-point locations deliberately remain unavailable. The current mesh model does not contain solver integration-point coordinates or element shape-function evaluation at the imported IP locations. Using the element centroid for an IP would be misleading.

Failed analyses remain in the map with `hasDamage=false`; they are not assigned zero damage. The map is a data-transfer layer, not a renderer or a damage interpolation algorithm. It does not extrapolate element values to nodes or interpolate IP values.
