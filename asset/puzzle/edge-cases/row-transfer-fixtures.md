# Native-gated row-transfer regression fixtures

These two small assets are intended to accompany the source tests; neither is generated during a test run. Paths are resolved from the test source location, not the execution working directory.

| Asset (repository-relative) | Bytes | SHA-256 |
| --- | ---: | --- |
| `asset/puzzle/edge-cases/row-transfer-singleton-star.puzzle` | 112 | `1a4307f3f2ecdfea70dcd1663c37199ff2f72ee8fbec6f9931891b96c2ba375d` |
| `asset/solution/edge-cases/row-transfer-default-v1.solution` | 13378 | `0f03e39a6a5dee4068b502bd5cbf9ab0cab6ecb7f7a9389479a508d8f5e70cea` |

Provenance: copied byte-for-byte from the pre-existing local experiment artifacts `result/solver-external-assembly-handoff/singleton-star.puzzle` and `result/solver-row-completion-transfer/default-v1.solution`, respectively. Those ignored historical artifacts are not required to build or execute `NativeGatedRowTransfer.*`.

The puzzle supplies singleton Salt and asks for a four-Salt, three-bond star. The fixed solution is the historical **default/null completion-transfer** six-product output; the test compares the entire serialized byte vector, including metadata, bodies and tapes. It is not the native-accepted experimental receiver machine, not a newly calculated expected value, and not evidence of physical correctness.

Timeout, exception, wrong-authority and mock-passed gate tests exercise control flow only. The callback seam exposes `RowCompletionTransfer::published` and the returned machine, not a filesystem publication operation. The tests assert those actual boundaries and accept-callback ordering. They do not invoke the native judge. Historical tail/reset limitations remain; native verification still has no final atom-ID interface.

## Experimental Calcification supply

`asset/puzzle/edge-cases/row-transfer-calcify-star.puzzle` (112 bytes, SHA-256 `ce626c08234b0ea0736e28bf4ebee3ca45e3fe0d0652ee4f2f083037860502ec`) is derived from the singleton-star puzzle by changing its sole input atom type from Salt (1) to Air (2), and setting permission bit 12. Its four-Salt output graph, six-product multiplier, and other fields are unchanged. The name is intentionally unchanged. It requires real Calcification; Salt cannot be acquired directly.

`InventoryAssemblyTransfer.*` exercises committed conversion identity, simultaneous holder rebinding, replay completion, denied chemistry permission, Independent rejection and failed-gate nonpublication without requiring native support. The separate explicitly enabled experiment was native-accepted for six products; this generated regression is not broader puzzle coverage. Ordinary/default compilation and production selection remain unchanged.


## Balanced coproduct receiver fixtures (explicit experimental opt-in)

`row-transfer-coproduct-balanced.puzzle` supplies singleton Salt and requests a
bonded Vitae–Mors diatom. `row-transfer-coproduct-balanced-path.puzzle` uses the
same input and permissions but requests the four-vertex path V–M–V–M at
(0,0)..(3,0), with normal bonds and multiplier 1. The latter requires two real
Animismus reactions per product, rather than one. Both are nonproduction,
self-contained binary v3 fixtures; neither counts toward original puzzle coverage.
The new path was encoded from the baseline metadata with explicit atom/bond
records, not synthesized by a runtime fixture-name branch.

Receiver placement filters the original finite atom/rotation candidates against
**all absolute cells** of every existing track, including endpoints. Track cells
serialize relative to the part origin, in plus-instruction order, including for
negative movement. This prevents accidental shared-track routing, but is not a
proof of swept-material or hardware safety: only complete-machine libverify
LegacyGate validation authorizes publication. The feature remains default OFF.
