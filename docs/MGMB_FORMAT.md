# MGMB — OpenModel Binary format, version 1.2

The layout below is version 1.0. Minor 1 adds the mesh-group section (id 6) and minor 2 the constraint section (id 7), described at the end. A file that has no node constraints is still written as 1.1, so readers that stop at 1.1 keep reading it; a reader accepts every minor up to 2 and expects exactly 5, 6 or 7 sections for minor 0, 1 or 2.

MGMB is a little-endian, sectioned binary format intended for fast persistence of `MeshData`. Version 1.0 is uncompressed and does not carry section checksums. All integer fields are encoded little-endian; floating-point coordinates are IEEE-754 binary64. No C++ object layout, padding, pointers, or ABI-dependent enum representation is written.

## Header (64 bytes)

| Field | Type | Description |
|---|---|---|
| magic | 8 bytes | `MGMB CR LF 0x1A LF` |
| major, minor | u16, u16 | `1, 0` |
| endian marker | u32 | `0x01020304` encoded little-endian |
| feature flags | u64 | Must be zero in v1.0 |
| node count | u64 | Number of nodes |
| element count | u64 | Total elements in all blocks |
| block count | u32 | Number of element blocks |
| section count | u32 | Exactly 5 in v1.0 |
| directory offset | u64 | Absolute file offset |
| file size | u64 | Exact logical file length |

## Directory records (48 bytes each)

Each record contains `section_id:u32`, `flags:u32`, `offset:u64`, `stored_size:u64`, `raw_size:u64`, `count:u64`, `checksum:u32`, and `reserved:u32`. Compression, flags, and checksums are currently unsupported and must be zero; `stored_size` must equal `raw_size`.

Required section IDs:

1. **Node IDs** — contiguous `u64` values in mesh order.
2. **Coordinates** — contiguous triples of `f64` (`x,y,z`) in node order.
3. **Element blocks** — `u32` block count followed by each block's UTF-8 name, stable element-type code, nodes-per-element, element count, connectivity index width (4 or 8 bytes), seven reserved zero bytes, element IDs (`u64`), then connectivity indices. Connectivity indices are zero-based internal node indices, not external node IDs. The width is selected per block.
4. **Node sets** — `u32` count followed by sorted set names, index counts, index width (4 or 8), seven reserved zero bytes, and node indices. Sorting names makes output deterministic despite the in-memory unordered map.
5. **Physical groups** — `u32` count followed by name, dimension (`i32`), tag (`i32`), material name, element-ID count (`u64`), and element IDs (`u64`).

Strings are prefixed by a `u32` byte length. Element type codes are fixed by this format and are explicitly mapped by the implementation; they do not rely on the C++ enum's underlying representation.

## Constraints (minor 2, section id 7)

Written only when the mesh has constraints on the degrees of freedom of its nodes (`MeshData::getConstraints()`); the file is then version 1.2 with seven sections. The section holds a `u32` record count followed by one 64-byte record per constrained node, ordered by node index: node index (`u64`, internal zero-based index), mask (`u8`, bit *i* = degree of freedom *i*+1: ux, uy, uz, rx, ry, rz), seven reserved zero bytes, six `f64` prescribed values (zero for a degree of freedom not in the mask). Readers refuse an empty or out-of-range mask, a node beyond the mesh, a duplicate node, a non-zero reserved byte, a value on a free degree of freedom and a size that does not match the count.

## Reader requirements

Readers must validate magic/version, exact file size, directory bounds, duplicate/missing sections, section overlap, count/size arithmetic, string bounds, supported type codes, index widths, connectivity bounds, and structural `MeshData` invariants before returning a mesh. Unknown mandatory sections are not allowed in v1.0. Future incompatible layouts require a new major version; optional features should use explicit flags/section IDs.

## Current limitations

MGMB v1.0 is uncompressed and does not provide checksums. It is designed for fast local persistence, not as a corruption-proof archival container. Checksums and optional codecs can be added in a compatible future revision. The current writer uses a temporary file and publishes it after successful close; replacement atomicity varies by filesystem/platform.
