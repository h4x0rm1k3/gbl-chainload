/** @file PeSections.c — PE/COFF section helpers used by DynamicPatchLib.
    Ported from dirty-repo oneplus_canoe.c; no EDK-II headers required. **/

#include "PeSections.h"

#ifdef __HOST_BUILD__
#include <string.h>   /* memcmp */
#define CompareMem(a, b, n)  memcmp((a), (b), (n))
#else
#include <Library/BaseMemoryLib.h>
#endif

/** Read a little-endian UINT16 from an unaligned byte pointer. **/
STATIC UINT32
ReadU16Le (
  IN CONST UINT8 *Buf
  )
{
  return (UINT32)Buf[0] | ((UINT32)Buf[1] << 8);
}

/** Read a little-endian UINT32 from an unaligned byte pointer. **/
STATIC UINT32
ReadU32Le (
  IN CONST UINT8 *Buf
  )
{
  return (UINT32)Buf[0]         |
         ((UINT32)Buf[1] << 8)  |
         ((UINT32)Buf[2] << 16) |
         ((UINT32)Buf[3] << 24);
}

BOOLEAN
IsPeFileOffsetInExecutableSection (
  IN CONST UINT8 *Buf,
  IN UINT32       Size,
  IN UINT32       Off,
  IN UINT32       Len
  )
{
  UINT32 PeOff;
  UINT32 NumberOfSections;
  UINT32 OptionalHeaderSize;
  UINT32 SectionOff;
  UINT32 i;

  if (Buf == NULL || Size < 0x100 || Len == 0 || Off + Len < Off ||
      Off + Len > Size) {
    return FALSE;
  }
  if (Buf[0] != 'M' || Buf[1] != 'Z') return FALSE;
  PeOff = ReadU32Le (Buf + 0x3C);
  if (PeOff > Size || PeOff + 0x18 > Size) return FALSE;
  if (CompareMem (Buf + PeOff, "PE\0\0", 4) != 0) return FALSE;

  NumberOfSections   = ReadU16Le (Buf + PeOff + 6);
  OptionalHeaderSize = ReadU16Le (Buf + PeOff + 20);
  SectionOff         = PeOff + 24 + OptionalHeaderSize;
  if (NumberOfSections == 0 || SectionOff > Size) return FALSE;

  for (i = 0; i < NumberOfSections; i++) {
    UINT32 Sh               = SectionOff + i * 40;
    UINT32 VirtualSize;
    UINT32 SizeOfRawData;
    UINT32 PointerToRawData;
    UINT32 Characteristics;
    UINT32 RawEnd;
    UINT32 RelOff;
    UINT32 MappedLimit;

    if (Sh + 40 > Size) return FALSE;
    VirtualSize      = ReadU32Le (Buf + Sh + 8);
    SizeOfRawData    = ReadU32Le (Buf + Sh + 16);
    PointerToRawData = ReadU32Le (Buf + Sh + 20);
    Characteristics  = ReadU32Le (Buf + Sh + 36);
    if ((Characteristics & 0x20000000U) == 0) continue; /* IMAGE_SCN_MEM_EXECUTE */
    if (PointerToRawData + SizeOfRawData < PointerToRawData) continue;
    RawEnd      = PointerToRawData + SizeOfRawData;
    if (Off < PointerToRawData || Off + Len > RawEnd) continue;
    RelOff      = Off - PointerToRawData;
    MappedLimit = (VirtualSize != 0) ? VirtualSize : SizeOfRawData;
    if (RelOff + Len < RelOff || RelOff + Len > MappedLimit) continue;
    return TRUE;
  }
  return FALSE;
}
