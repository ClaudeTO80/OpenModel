# MGOM — OpenModel binary model format, version 1.1

MGOM is the persistence format for the whole mesh model (`MeshData`): nodes, element blocks, node sets, physical groups, mesh groups, node constraints and CAD bodies. It is written and read by `ModelBinaryIO` (`include/OpenModel/ModelBinaryIO.hpp`). It is intended to replace MGMB for new files. MGMB 1.x files stay readable and writable through `MeshBinaryIO`; MGOM is not a new major version of MGMB, so existing MGMB readers are unaffected.

MGOM is **not** a render format (use MGWV for that) and does not contain the CAD source, the analysis script or solver results. These are separate assets of a project, as described in `PROJECT_PERSISTENCE.md`.

## Goals and design

The format optimises two things, in this order: the size of the file, and the time needed to write and read it.

| Technique | Where it applies | Effect |
|---|---|---|
| Zigzag-encoded deltas in LEB128 varints | node IDs, element IDs, sorted index lists, connectivity (when it wins) | sequential IDs cost one byte instead of eight |
| String table | every name (blocks, groups, sets, bodies, material names) | each name is stored once and referenced by index |
| 32-bit indices | connectivity, node sets, group node lists | used whenever the mesh has fewer than 2³² nodes |
| Per-block connectivity encoding | element blocks | raw fixed-width or delta, whichever is smaller |
| CRC-32 per section | all sections | corruption is detected before any structure is built |
| Bulk copies | coordinates, raw connectivity | a `memcpy` on little-endian hosts |

Coordinates are stored as IEEE-754 binary64 by default, so a mesh round-trips bit for bit. Optionally they are quantized to 16 or 32 bits per axis (lossy, see below). No C++ object layout, padding, pointer or ABI-dependent representation is written. Element type codes are fixed by this document (see below), not by the C++ enum.

## File layout

```text
+----------------------+  offset 0
| header (64 bytes)    |
+----------------------+  offset 64
| section 1 payload    |   sections appear in the order of their IDs
| section 2 payload    |   but the directory, not the order, is authoritative
| ...                  |
| section 9 payload    |
+----------------------+  directoryOffset
| directory            |   9 entries of 40 bytes
+----------------------+  fileSize
```

All integers are little-endian. The directory is at the end of the file, so the header can point to it after the payloads are known.

### Header (64 bytes)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | magic | `M G O M CR LF 0x1A LF` |
| 8 | 2 | major | `1` |
| 10 | 2 | minor | `1` (version 1.0 files are still read; see *Compatibility*) |
| 12 | 4 | endian marker | `0x01020304` |
| 16 | 8 | feature flags | `0`; any other value is rejected |
| 24 | 8 | node count | number of nodes |
| 32 | 8 | element count | total elements of all blocks |
| 40 | 4 | block count | number of element blocks |
| 44 | 4 | section count | `9` |
| 48 | 8 | directory offset | absolute offset of the directory |
| 56 | 8 | file size | exact length of the file |

### Directory entry (40 bytes, nine entries)

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | section ID | 1..9, each exactly once |
| 4 | 4 | codec | `0` (stored). Any other value is rejected; the field is reserved for future compression |
| 8 | 8 | offset | absolute offset of the payload |
| 16 | 8 | size | payload size in bytes |
| 24 | 8 | count | number of items in the section (see below) |
| 32 | 4 | CRC-32 | IEEE 802.3 (reflected, polynomial `0xEDB88320`) over the payload bytes |
| 36 | 4 | reserved | `0` |

The item count is checked by the reader against the content of the section, so a corrupted count is rejected too.

## Primitive encodings

- **varint**: unsigned LEB128, seven bits per byte, least significant group first. At most ten bytes; a tenth byte with bits above the lowest one is malformed.
- **zigzag**: `z = (d << 1) XOR (d >> 63 ? ~0 : 0)` on the 64-bit two's complement value `d`. Maps small signed differences to small unsigned values.
- **delta run**: a sequence of values where each value is written as the zigzag varint of its difference to the previous value. The first value is relative to zero. Order is preserved, so unsorted lists are allowed.
- **delta list**: a count (varint) followed by a delta run.
- **text**: a length (varint) followed by UTF-8 bytes. Not NUL-terminated.
- **f64**: IEEE-754 binary64, eight bytes little-endian.

## Sections

Every section is a self-describing list. All counts below are varints unless stated otherwise.

| ID | Name | Content |
|---|---|---|
| 1 | Strings | count, then `text` for each string. Strings are indexed from zero in the order of first use |
| 2 | Node IDs | count, then a delta run of the external `NodeId` values (`count` = node count) |
| 3 | Coordinates | count (nodes), then a mode byte and the coordinate payload (see *Coordinate section*) |
| 4 | Element blocks | count, then one record per block (below) |
| 5 | Node sets | count, then per set: string ref, delta list of node indices. Sets are written sorted by name |
| 6 | Physical groups | count, then per group: string ref name, zigzag dimension, zigzag tag, string ref material, delta list of element IDs |
| 7 | Mesh groups | count, then per group: string ref name, zigzag dimension, `u8` source (`0` UserDefined, `1` CAD, `2` Imported), count and zigzag values of `sourceEntityIds`, delta list of element IDs, delta list of node indices |
| 8 | Constraints | count, then one record per constrained node, ordered by node index (below) |
| 9 | Bodies | count, then per body: string ref name, zigzag dimension, delta list of element IDs |

A "string ref" is the varint index into the string table.

### Element block record (section 4)

| Field | Encoding |
|---|---|
| name | string ref |
| element type | `u8` code (table below) |
| nodes per element | varint |
| element count | varint |
| index width | `u8`: `4` or `8` |
| connectivity encoding | `u8`: `0` raw, `1` delta |
| element IDs | delta run of `element count` values |
| connectivity | `nodes per element × element count` indices. Raw: fixed-width unsigned integers of the index width. Delta: a delta run over the flat connectivity array |

The writer sizes both encodings and keeps the delta encoding only when it is strictly smaller than the raw one. Index width is `4` whenever the mesh has at most 2³² − 1 nodes, `8` otherwise.

### Element type codes

| Code | Type | Code | Type |
|---|---|---|---|
| 0 | Unknown | 9 | Tetrahedron4 |
| 1 | Point1 | 10 | Tetrahedron10 |
| 2 | Line2 | 11 | Hexahedron8 |
| 3 | Line3 | 12 | Hexahedron20 |
| 4 | Triangle3 | 13 | Hexahedron27 |
| 5 | Triangle6 | 14 | Prism6 |
| 6 | Quadrilateral4 | 15 | Prism15 |
| 7 | Quadrilateral8 | 16 | Pyramid5 |
| 8 | Quadrilateral9 | | |

Any other code is rejected.

### Constraint record (section 8)

Each record is a zigzag varint of the node index minus the previous node index (the first is relative to zero), a `u8` mask, then one f64 for each degree of freedom set in the mask, in bit order.

Mask bit *i* is degree of freedom *i*+1: bit 0 ux, bit 1 uy, bit 2 uz, bit 3 rx, bit 4 ry, bit 5 rz. A mask of zero, or with bits 6 and 7 set, is rejected. Values of free degrees of freedom are not stored; they are zero on read.

### Coordinate section (section 3)

After the node count comes a mode byte:

- **`0` — exact**: `3 × count` f64 values `x, y, z` in node order. Lossless.
- **`16` or `32` — quantized**: for each axis `x, y, z`, the exact minimum and maximum (two f64 values), followed by `3 × count` unsigned integers of 16 or 32 bits, in node order, with the axis of each value cycling `x, y, z`. A value `v` on an axis with minimum `lo` and maximum `hi` is stored as

  `q = round((v − lo) / (hi − lo) × (2^bits − 1))`

  and read back as `lo + q × (hi − lo) / (2^bits − 1)`. An axis with `hi = lo` stores `q = 0` and reads back as `lo`.

Error bound: each reconstructed value differs from the original by at most half a quantization step on its axis, i.e.

| Mode | Maximum error per value |
|---|---|
| 16 bit | (hi − lo) / 131 070 |
| 32 bit | (hi − lo) / 8 589 934 590 |

The bound is relative to the axis extent, not to the magnitude of the coordinates: a model spanning 1 m keeps a 16-bit error of about 8 µm, the same model spanning 1 km has about 8 mm. Quantization is refused when a coordinate is not finite. Topology, node IDs and all other sections are unaffected.

## Reader requirements

A reader must, in this order:

1. Check the magic, the version (1.0 or 1.1), the endian marker, the zero feature flags, and that the file size matches the header.
2. Check that the directory lies at the stated offset, has exactly nine entries with distinct IDs in `1..9`, and that every codec and reserved field is zero.
3. Check that every payload lies between the header and the directory, that payloads do not overlap, and that each CRC-32 matches.
4. Parse each section with bounds checks. Every count must be consistent with the bytes that remain in its section, every section must be consumed exactly, and every count must equal the one in the directory.
5. Check the header node count, element count and block count against the sections, and check that node indices, node IDs, block names and constraint nodes are in range and unique.
6. Install the data into `MeshData` and, unless disabled, run `MeshData::validate()`.

Any violation throws `ModelBinaryIO::Error`. A reader never returns a partially filled mesh.

## Writer requirements

The writer validates the mesh (unless disabled), encodes all sections, writes to a temporary sibling file, flushes it, and then renames it over the destination. An interrupted write leaves the previous file intact. On platforms where replacing a file is not atomic, the Windows path uses `MoveFileExW` with write-through.

## Measured size and speed

Structured hexahedral grids with sequential node and element IDs, one physical group, release build (-O2) on the development sandbox, median of three runs. Times are indicative only.

| Mesh | MGMB size | MGOM size | MGOM / MGMB | Write (MGMB → MGOM) | Read (MGMB → MGOM) |
|---|---|---|---|---|---|
| 24³ (15,625 nodes, 13,824 hex8) | 1.16 MB | 0.56 MB | 48% | 2.7 → 3.2 ms | 2.0 → 2.9 ms |
| 60³ (226,981 nodes, 216,000 hex8) | 17.6 MB | 8.3 MB | 47% | 35 → 44 ms | 26 → 38 ms |

The size breakdown of the lossless 60³ file explains the cost: coordinates are 5.4 MB (66%) and connectivity 2.4 MB (29%). Quantizing coordinates to 16 bits (below) removes most of the first share.

Quantized mode (16 bit), same meshes, same run:

| Mesh | MGMB | MGOM exact | MGOM 16-bit | Write (MGMB / exact / 16-bit) | Read (MGMB / exact / 16-bit) |
|---|---|---|---|---|---|
| 24³ | 1.16 MB | 0.56 MB (48%) | 0.28 MB (24%) | 2.9 / 2.2 / 2.3 ms | 1.5 / 2.1 / 5.5 ms |
| 60³ | 17.6 MB | 8.3 MB (47%) | 4.2 MB (24%) | 28 / 34 / 35 ms | 23 / 34 / 34 ms |

The 24³ read times are noisy at this size; the 60³ figures are the more reliable ones. After quantization, connectivity is the largest part of the 60³ file (about 58%), so connectivity compression is the next lever, through the reserved codec field.

Lossy error on the warped 20³ grid (9,261 nodes, anisotropic non-integer coordinates): the worst 16-bit error is 7.6·10⁻⁶ of the axis extent, against a bound of 7.6·10⁻⁶; the 32-bit worst error is 1.2·10⁻¹⁰.

Checksum and format structure were also verified outside the C++ code: section CRC-32 values were recomputed with an independent implementation (`zlib.crc32`) on the same files and agree for every section.

`tests/model_binary_io_test.cpp` checks the round trip of every section (including both connectivity encodings), an empty mesh, the size ratio against MGMB, the rejection of a foreign file, a truncated file and an invalid mesh, and an exhaustive single-bit corruption test over the whole file: every flipped bit must be rejected with `ModelBinaryIO::Error`. The quantized mode is checked for the per-axis error bound, size ordering (exact > 32-bit > 16-bit), a degenerate flat axis, refusal of non-finite input, and exhaustive bit-flip rejection of a quantized file.

## Compatibility

- Version 1.0 files (exact coordinates, no mode byte) are read as lossless. Writers always produce 1.1.
- A 1.0 reader rejects 1.1 files, because the minor version is checked for equality there.

## Limits

- No compression codec is implemented. The codec field exists and must be zero. A future minor version can define codecs without changing the layout.
- Quantized coordinates are opt-in and lossy. The error scales with each axis extent, so a model with a very large extent relative to its details needs 32 bits. The default stays lossless.
- The whole file is read into memory and parsed in one pass; the format is not memory-mapped.
- Element block connectivity uses one index width per block. A mesh whose node count crosses 2³² uses 64-bit indices everywhere.
- Bodies store element IDs only; CAD face and group mappings remain in mesh groups and in the project snapshot described in `PROJECT_PERSISTENCE.md`.

## Relation to other formats

| Format | Purpose | Status |
|---|---|---|
| MGMB | mesh persistence, sections without checksums or string table | still read and written by `MeshBinaryIO`; superseded for new files |
| MGOM | model persistence (this document) | new, `ModelBinaryIO` |
| MGWV | render-only payload for the WebGL viewer, float32 | unchanged, not a persistence format |
| `.omproj` | project manifest referencing assets | the `mesh.surface` and `mesh.volume` roles can point to `.omb` files; the manifest contract is unchanged |
