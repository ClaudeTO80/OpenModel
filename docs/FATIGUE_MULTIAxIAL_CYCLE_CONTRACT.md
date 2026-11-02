# Multiaxial cycle-counting contract

## Purpose

The scalar Rainflow API is intentionally not reused for multiaxial fatigue. A multiaxial cycle is represented as an ordered path through the full tensor history. The contract in this module is an extension point for future critical-plane, path-based, and invariant-based algorithms; it does not claim to implement one of those algorithms.

## Data model

`MultiaxialLoadCycle` stores:
- the ordered indices of all path samples in the original `StressHistory`;
- whether the path is closed;
- the cycle weight assigned by the counting method;
- the tensor closure residual;
- a stable method identifier.

The indices retain access to time, full tensor, source step, original solver sample, units, and provenance. The implementation does not reduce the path to von Mises stress or principal values.

## Closure

For endpoint tensors (T_a) and (T_b), the current contract reports the Frobenius residual

`||T_a-T_b||_F = sqrt(sum_ij (T_a,ij-T_b,ij)^2)`

A closed cycle is accepted only when this residual does not exceed the caller's absolute tolerance. The tolerance uses the history's tensor unit; it is not silently converted to SI. Open paths may be represented without this condition.

## Algorithm extension point

`IMultiaxialCycleCounter` is the polymorphic interface for future algorithms. An implementation must:
1. validate the input history and its own assumptions;
2. preserve the original path by returning history sample indices;
3. identify its method and report failures explicitly;
4. document its closure, filtering, turning-point, and cycle-weight conventions.

No universal scalar range is imposed. A later fatigue damage model may evaluate each path with a selected material criterion.

## Deliberate non-goals

This module does not yet implement a multiaxial counting algorithm. In particular, it does not claim that tensor-space extrema, von Mises reversals, or independent component Rainflow counts define physically equivalent multiaxial cycles. Those are method-specific choices and need separate verification against published benchmarks.
