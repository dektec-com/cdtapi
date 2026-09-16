# Vendored driver ABI headers

Files in this directory are copied **verbatim** from the DekTec SDK and must stay
byte-identical to their originals. Do not reformat them and do not apply the project
coding rules to them: a clean `diff` against the SDK copy is what makes a future
driver-ABI update a copy rather than a merge.

| File | Copied from | Licence |
|---|---|---|
| `DtCommon.h` | `SDK/Common/Source/DtCommon.h` | BSD-2-Clause, DekTec Digital Video B.V. |
| `DtPcieCommon.h` | `SDK/Common/Source/DtPcieCommon.h` | BSD-2-Clause, DekTec Digital Video B.V. |
| `DtStatusCodes.h` | `SDK/Common/Source/DtStatusCodes.h` | BSD-2-Clause, DekTec Digital Video B.V. |

`.clang-format` in this directory sets `DisableFormat: true`, and the directory is
excluded from clang-tidy and from `Scripts/check_style.sh`.

## Refreshing a vendored file

    cp <sdk>/Common/Source/DtCommon.h Source/Drv/Abi/DtCommon.h
    cp <sdk>/Common/Source/DtPcieCommon.h Source/Drv/Abi/DtPcieCommon.h
    cp <sdk>/Common/Source/DtStatusCodes.h Source/Drv/Abi/DtStatusCodes.h
    cmake --build Build/<preset> --target cdtapilite_abi_check

The ABI check compiles the header standalone as C11 and turns every `ASSERT_SIZE` in it
into a `_Static_assert`. If a structure changed size, that build fails.

## What is deliberately not vendored

The base types (`Int`, `UInt`, `Int64A`, ...) come from `SDK/Common/Import/StandardTypes.h`,
which is DekTec-owned but carries no redistribution grant. CDtapiLite defines them itself
in `Source/Drv/DtlAbiTypes.h` under BSD-3-Clause, sized and aligned to match.
