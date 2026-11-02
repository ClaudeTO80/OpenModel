# Fatigue tensor normalization

`TensorNormalizer` converts raw, solver-neutral samples into canonical 3D Cartesian vectors and symmetric tensors.

## Labels and layout
Exactly six explicit labels are required for a symmetric 3D tensor. Normal components map to xx, yy and zz; shear components map to xy, xz and yz. Labels such as `SXX`, `E11`, `S12` and `EYZ` are accepted, independent of their input order. Duplicate, missing or unknown labels are rejected. Vectors require three uniquely labelled components.

## Shear strain
Tensorial shear is used directly. Engineering shear is converted using `epsilon_ij = gamma_ij / 2`. Strain with an unknown convention is rejected. No convention is inferred from the field name.

## Coordinate transformations
The row-major matrix R maps local axes into global axes. Tensor conversion is `T_global = R T_local R^T`; vector conversion is `v_global = R v_local`. Inverse conversion uses R transpose. Rotations must be finite, orthonormal and have determinant +1. Missing or invalid transformations are explicit errors.

## Invariants
The 3D von Mises equivalent stress is calculated as:
`sqrt(0.5[(sxx-syy)^2+(syy-szz)^2+(szz-sxx)^2] + 3(sxy^2+sxz^2+syz^2))`.
Principal values use the analytic symmetric 3x3 eigenvalue solution and are returned in descending order.

Units are not converted. The result retains the input numeric scale; callers must establish compatible units before combining data from different sources.
