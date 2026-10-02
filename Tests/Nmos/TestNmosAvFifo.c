// #*#*#*#*#*#*#*#*#*#*#*#*#* TestNmosAvFifo.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The NMOS bridge of the AV FIFO, in a build with CDTAPI_WITH_NMOS
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "DtTest.h" // Test framework.
#include "cdtapi.h" // DtapiHasNmos.

// dtnmos includes
#include "dtnmos_node.h" // DtNmos_HasServer, through the library's own link.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Build +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The library has the bridge, and its interface brings dtnmos: its header and its
// library, which a program gets from linking CDTAPI alone.
DT_TEST(LinksDtnmos)
{
    DT_ASSERT_EQ(DtapiHasNmos(), 1);
    DT_ASSERT(DtNmos_HasServer() == 0 || DtNmos_HasServer() == 1);
}

DT_TEST_MAIN("NmosAvFifo", DT_RUN(LinksDtnmos))
