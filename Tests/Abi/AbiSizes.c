// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* AbiSizes.c *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDtapiLite - Compiles the vendored driver ABI standalone and checks every struct size
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdio.h>

// CDtapiLite includes
#include "DtPcieAbi.h" // Vendored DtPcie driver ABI plus its base types.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+ Test +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// There is nothing to run. Including the header is the test: DtCommon.h carries an
// ASSERT_SIZE on every structure it defines, and this file is compiled as C11 with
// static_assert available, so a structure whose layout drifted fails the build.
//
// A handful of sizes are restated here as a guard against ASSERT_SIZE being compiled
// away by a future toolchain change. If these ever compile while DtCommon.h's own
// assertions do not, the check has silently stopped working.
//

_Static_assert(sizeof(DtIoctlInputDataHdr) == 16, "DtIoctlInputDataHdr must be 16 bytes");
_Static_assert(sizeof(DtIoctlIoConfig) == 200, "DtIoctlIoConfig must be 200 bytes");

int main(void)
{
    printf("ABI check: DtIoctlInputDataHdr=%zu DtIoctlIoConfig=%zu\n",
           sizeof(DtIoctlInputDataHdr), sizeof(DtIoctlIoConfig));
    return 0;
}
