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

## WebGL viewer adapter

The WebGL viewer already supports flat-shaded element scalar fields through `WebElementField`. The fatigue adapter provides a strict conversion:

```cpp
auto converted = FatigueDamageMapWebViewAdapter::makeElementField(
    damageMap, mesh, "Fatigue damage");

if (!converted) {
    // Inspect converted.status and converted.diagnostic.
    return;
}

WebViewOptions options;
options.elementFields.push_back(std::move(converted.field));
options.resultsLabel = "Fatigue spectrum damage";
const auto written = WebViewWriter::writeFile("fatigue.mgwv", mesh, options);
```

This path deliberately accepts element positions only. It requires one finite, non-negative damage value for every mesh element, rejects duplicate IDs and incomplete coverage, and preserves the mesh's global element ordering (block by block). It does not synthesize values for failed rows, convert nodal data to elements, or collapse integration-point values to an element value.

The viewer's existing scalar mode and legend can then display the field without a new rendering pipeline. Nodal and integration-point visualization require separate, explicitly defined adapters and rendering semantics.
