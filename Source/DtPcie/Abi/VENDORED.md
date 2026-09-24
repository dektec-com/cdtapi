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

`.clang-format` in this directory sets `DisableFormat: true`, and the coding rules of
`Scripts/check_style.sh` skip the directory. What that script does check here is that
each header is still the bytes `SHA256SUMS` records, so an edit that is not a refresh
fails the style check, locally in the pre-commit hook and in CI.

## Refreshing a vendored file

    cp <sdk>/Common/Source/DtCommon.h Source/DtPcie/Abi/DtCommon.h
    cp <sdk>/Common/Source/DtPcieCommon.h Source/DtPcie/Abi/DtPcieCommon.h
    cp <sdk>/Common/Source/DtStatusCodes.h Source/DtPcie/Abi/DtStatusCodes.h
    (cd Source/DtPcie/Abi && sha256sum DtCommon.h DtPcieCommon.h DtStatusCodes.h \
        | sed 's/ \*/  /' > SHA256SUMS)
    cmake --build Build/<preset> --target test_Abi

`SHA256SUMS` changes in the same commit as the headers, which is what says the change is
a refresh. The ABI test compiles the headers standalone as C11, where DtCommon.h's own
`ASSERT_SIZE` checks the size of every structure it defines, and restates two of those
sizes itself in case the assertions stop compiling. If a structure changed size, that
build fails.

## What is deliberately not vendored

The base types (`Int`, `UInt`, `Int64A`, ...) come from `SDK/Common/Import/StandardTypes.h`,
which is DekTec-owned but carries no redistribution grant. CDTAPI defines them itself
in `Source/DtPcie/DtAbiTypes.h` under BSD-3-Clause, sized and aligned to match.
