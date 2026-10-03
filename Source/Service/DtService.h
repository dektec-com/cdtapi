// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# DtService.h *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Messages to and from DtapiService through its services pipe
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stddef.h>
#include <stdint.h>

// CDTAPI includes
#include "DtXml.h"          // The XML of the messages.
#include "cdtapi.h"         // DtapiResult.
#include "cdtapi_service.h" // Variants.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Service +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Sends a command to DtapiService and receives its answer, as DTAPI's
// Dtapi::Service::Proxy does. DtapiService is a DekTec program beside the driver that
// runs, among other things, the PTP clock slave of a DTA-2110 or DTA-2125.
//
// On the pipe, each message is preceded by its length in bytes, little endian: 2 bytes
// when it is below 0xFFFF, and otherwise FF FF 00 00 followed by 4 bytes. The length
// and the message are written separately, as the service reads them.
//
// A command is its number, 4 bytes, followed by an XML document. An answer is the
// number of the command it answers, 4 bytes, the number of an exception, 4 bytes, and,
// when there is no exception, an XML document. Numbers are little endian. The XML is
// text in the service's wchar_t, ending in a zero character: UTF-16 on Windows and
// UTF-32 on Linux. Here it is UTF-8.
//
// A connection is used from one thread at a time.
//

// The name of the services pipe.
#define DT_SERVICE_PIPE_NAME "ServicesPipe"

// The size of a character of the service's text: its wchar_t.
#ifdef _WIN32
    #define DT_SERVICE_CHAR_SIZE 2
#else
    #define DT_SERVICE_CHAR_SIZE 4
#endif

// The exception of an answer without one.
#define DT_SERVICE_NO_EXCEPTION -1

// The commands, in the service's numbering.
typedef enum DtServiceCmd
{
    DT_SERVICE_CMD_CLEANUP_CONNECTION = 0, // Ends the connection; not answered
    DT_SERVICE_CMD_VERSION = 1,
    DT_SERVICE_CMD_ATTACH = 2,
    DT_SERVICE_CMD_DETACH = 3,
    DT_SERVICE_CMD_GET_PARGROUPS = 4,
    DT_SERVICE_CMD_GET_PARDESCS = 5,
    DT_SERVICE_CMD_GET_PARVAL = 6,
    DT_SERVICE_CMD_SET_PARVAL = 7,
    DT_SERVICE_CMD_GET_PARVALS = 8,
    DT_SERVICE_CMD_SET_PARVALS = 9,
    DT_SERVICE_CMD_SAVE_SETTINGS = 10,
} DtServiceCmd;

typedef struct DtService DtService;

// Ends the connection: tells the service, unless the connection has failed, and closes
// the pipe. A NULL Service does nothing.
void DtService_Close(DtService* Service);

// Connects to DtapiService. With a NULL PipeName, to the service: the emulated one in
// the program when CDTAPI_SIM asks for the emulator, and otherwise the one listening
// on DT_SERVICE_PIPE_NAME. With a PipeName, always through that pipe, as the tests
// do. *Service is NULL after a failure.
// Returns:
//   DTAPI_OK
//   DTAPI_E_CONNECT_TO_SERVICE  no service listens, or it does not accept in time
//   DTAPI_E_INVALID_ARG         a NULL Service
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtService_Connect(const char* PipeName, DtService** Service);

// Writes the length prefix of a message of Length bytes into Prefix, which has room for
// 8, and returns the number of bytes written: 2 or 8.
size_t DtService_EncodeLength(uint32_t Length, uint8_t* Prefix);

// Reads the variant in the attributes <Prefix>VT, its type, and <Prefix>VV, its value,
// of Elem into *Value. A string's value points into Elem, and lives as long as it does;
// a string without VV is empty, as DTAPI writes one. Returns false for a missing type,
// an unknown one, or a value that is not of it.
bool DtService_ReadVariant(const DtXmlElem* Elem, const char* Prefix, DtVariant* Value);

// Converts Wire, WireSize bytes of the service's text in characters of CharSize bytes,
// 2 or 4, to UTF-8 in a new *Text, to be freed with DtAlloc_Free. The text ends at its
// first zero character, and Wire must end in one.
// Returns:
//   DTAPI_OK
//   DTAPI_E_COMMUNICATION  Wire is not such text
//   DTAPI_E_INVALID_ARG    a NULL argument, or a CharSize other than 2 or 4
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtService_TextFromWire(const uint8_t* Wire, size_t WireSize, int CharSize,
                                   char** Text);

// Converts the UTF-8 Text to the service's text in characters of CharSize bytes, 2 or 4,
// with its zero character, in a new *Wire of *WireSize bytes, to be freed with
// DtAlloc_Free.
// Returns:
//   DTAPI_OK
//   DTAPI_E_INVALID_ARG  a NULL argument, a CharSize other than 2 or 4, or Text is not
//                        UTF-8
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtService_TextToWire(const char* Text, int CharSize, uint8_t** Wire,
                                 size_t* WireSize);

// Sends command Cmd with the XML document Xml, and receives the answer. Without an
// exception, *ResultXml is the answer's XML, to be freed with DtAlloc_Free, and
// *Exception is DT_SERVICE_NO_EXCEPTION. With one, *ResultXml is NULL and *Exception is
// the service's number of it, an Exc::Reason in DTAPI_Services.h; the result is still
// DTAPI_OK, as the connection works. After any other result the connection is of no
// further use.
// Returns:
//   DTAPI_OK
//   DTAPI_E_CONNECT_TO_SERVICE  the service closed the pipe, or did not answer in time
//   DTAPI_E_COMMUNICATION       an answer to another command, or one that is malformed
//   DTAPI_E_INVALID_ARG         a NULL argument, Cmd is CLEANUP_CONNECTION, or Xml is
//                               not UTF-8
//   DTAPI_E_OUT_OF_MEM
DtapiResult DtService_Transfer(DtService* Service, DtServiceCmd Cmd, const char* Xml,
                               char** ResultXml, int* Exception);

// Writes *Value into the attributes <Prefix>VT and <Prefix>VV of the element just opened,
// as DtService_ReadVariant reads them. An empty variant has VT alone.
void DtService_WriteVariant(DtXmlOut* Out, const char* Prefix, const DtVariant* Value);
