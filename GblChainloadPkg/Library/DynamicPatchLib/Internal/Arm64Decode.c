/** @file Arm64Decode.c — minimal AArch64 instruction decoders. **/

#include "Arm64Decode.h"
#include "Encode.h"        /* ReadInstrU32 */
#include "PeSections.h"

/* ---- Sign-extend helpers --------------------------------------------- */

STATIC INT32
SignExtend (
  IN UINT32 Value,
  IN UINT32 Bits
  )
{
  UINT32 Mask = (1u << (Bits - 1));
  if (Value & Mask) {
    /* Set all bits above bit (Bits-1). */
    return (INT32)(Value | ~((1u << Bits) - 1u));
  }
  return (INT32)Value;
}

/* ---- Branch decoding ------------------------------------------------- */

BOOLEAN
Arm64DecodeBranch (
  IN  UINT32           Insn,
  IN  UINT32           InsnOff,
  OUT ARM64_INSN_KIND *Kind,
  OUT UINT32          *TargetOff,
  OUT UINT32          *RegOrCond
  )
{
  UINT32 Top    = (Insn >> 24) & 0xFFu;
  UINT32 Top6   = (Insn >> 26) & 0x3Fu;
  INT32  Off;

  *Kind      = ARM64_INSN_OTHER;
  *TargetOff = 0;
  *RegOrCond = 0;

  /* B unconditional: 0b000101_imm26 */
  if (Top6 == 0x05u) {
    Off = SignExtend (Insn & 0x03FFFFFFu, 26) * 4;
    *Kind      = ARM64_INSN_B;
    *TargetOff = (UINT32)((INT32)InsnOff + Off);
    return TRUE;
  }
  /* BL: 0b100101_imm26 */
  if (Top6 == 0x25u) {
    Off = SignExtend (Insn & 0x03FFFFFFu, 26) * 4;
    *Kind      = ARM64_INSN_BL;
    *TargetOff = (UINT32)((INT32)InsnOff + Off);
    return TRUE;
  }

  /* B.cond: 0x54_xxxxxxx0_cond  (bit 4 must be 0) */
  if (Top == 0x54u && (Insn & 0x10u) == 0u) {
    Off = SignExtend ((Insn >> 5) & 0x7FFFFu, 19) * 4;
    *Kind      = ARM64_INSN_BCOND;
    *TargetOff = (UINT32)((INT32)InsnOff + Off);
    *RegOrCond = Insn & 0xFu;
    return TRUE;
  }

  /* CBZ/CBNZ W/X: top byte 0x34 / 0x35 / 0xB4 / 0xB5. */
  if (Top == 0x34u || Top == 0x35u || Top == 0xB4u || Top == 0xB5u) {
    Off = SignExtend ((Insn >> 5) & 0x7FFFFu, 19) * 4;
    *TargetOff = (UINT32)((INT32)InsnOff + Off);
    *RegOrCond = Insn & 0x1Fu;
    if (Top == 0x34u) *Kind = ARM64_INSN_CBZ_W;
    if (Top == 0x35u) *Kind = ARM64_INSN_CBNZ_W;
    if (Top == 0xB4u) *Kind = ARM64_INSN_CBZ_X;
    if (Top == 0xB5u) *Kind = ARM64_INSN_CBNZ_X;
    return TRUE;
  }

  return FALSE;
}

/* ---- ADRP + ADD pair decoding --------------------------------------- */

/* ADRP: bit[31]=1, bits[28:24]=0b10000.
   immlo=bits[30:29], immhi=bits[23:5], imm21=(immhi<<2)|immlo (signed). */
STATIC BOOLEAN
DecodeAdrp (
  IN  UINT32  Insn,
  IN  UINT32  AdrpOff,
  OUT UINT32 *PageBase,
  OUT UINT32 *Rd
  )
{
  UINT32 ImmLo, ImmHi, Imm21;
  INT32  SignedImm21;

  if ((Insn & 0x9F000000u) != 0x90000000u) {
    return FALSE;
  }
  ImmLo = (Insn >> 29) & 0x3u;
  ImmHi = (Insn >> 5)  & 0x7FFFFu;
  Imm21 = (ImmHi << 2) | ImmLo;
  SignedImm21 = SignExtend (Imm21, 21);
  /* Page-relative: (PC & ~0xFFF) + (imm21 << 12). */
  *PageBase = ((AdrpOff & ~0xFFFu)) + (UINT32)(SignedImm21 << 12);
  *Rd       = Insn & 0x1Fu;
  return TRUE;
}

/* ADD imm: bit[31]=sf, bits[30:24]=0b0010001, bits[23:22]=sh (need 0).
   imm12=bits[21:10], Rn=bits[9:5], Rd=bits[4:0]. */
STATIC BOOLEAN
DecodeAddImm (
  IN  UINT32  Insn,
  OUT UINT32 *Imm12,
  OUT UINT32 *Rn,
  OUT UINT32 *Rd
  )
{
  /* Top 7 bits after sf must be 0b0010001 (= 0x11). Also require sh==0. */
  if ((Insn & 0x7F800000u) != 0x11000000u) {
    return FALSE;
  }
  *Imm12 = (Insn >> 10) & 0xFFFu;
  *Rn    = (Insn >> 5)  & 0x1Fu;
  *Rd    = Insn         & 0x1Fu;
  return TRUE;
}

BOOLEAN
Arm64DecodeAdrpAdd (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       AdrpOff,
  OUT UINT32      *TargetAddr
  )
{
  UINT32 AdrpInsn, AddInsn;
  UINT32 PageBase, AdrpRd;
  UINT32 Imm12, AddRn, AddRd;

  if (AdrpOff + 8 > Size) {
    return FALSE;
  }
  AdrpInsn = ReadInstrU32 (Buf, AdrpOff);
  AddInsn  = ReadInstrU32 (Buf, AdrpOff + 4);

  if (!DecodeAdrp (AdrpInsn, AdrpOff, &PageBase, &AdrpRd)) {
    return FALSE;
  }
  if (!DecodeAddImm (AddInsn, &Imm12, &AddRn, &AddRd)) {
    return FALSE;
  }
  if (AddRn != AdrpRd || AddRd != AdrpRd) {
    /* Not a self-targeting ADD; not a pointer-construction pair. */
    return FALSE;
  }

  *TargetAddr = PageBase + Imm12;
  return TRUE;
}

SCAN_RESULT
Arm64FindAdrpAddTargeting (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       TargetAddr,
  IN  BOOLEAN      RestrictToExec,
  OUT UINT32      *MatchOff
  )
{
  UINT32 Off, Resolved, Found = 0, FirstOff = 0;

  if (Buf == NULL || MatchOff == NULL || Size < 8) {
    return SCAN_BAD_INPUT;
  }

  for (Off = 0; Off + 8 <= Size; Off += 4) {
    if (RestrictToExec &&
        !IsPeFileOffsetInExecutableSection (Buf, Size, Off, 8)) {
      continue;
    }
    if (Arm64DecodeAdrpAdd (Buf, Size, Off, &Resolved) &&
        Resolved == TargetAddr) {
      if (Found == 0) FirstOff = Off;
      ++Found;
    }
  }

  if (Found == 0) return SCAN_NOT_FOUND;
  if (Found > 1)  return SCAN_AMBIGUOUS;
  *MatchOff = FirstOff;
  return SCAN_FOUND;
}

SCAN_RESULT
Arm64FindCondBranchTargeting (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       TargetOff,
  IN  BOOLEAN      RestrictToExec,
  OUT UINT32      *MatchOff
  )
{
  UINT32          Off, Insn, Tgt, RegOrCond;
  UINT32          Found = 0, FirstOff = 0;
  ARM64_INSN_KIND Kind;

  if (Buf == NULL || MatchOff == NULL || Size < 4) {
    return SCAN_BAD_INPUT;
  }

  for (Off = 0; Off + 4 <= Size; Off += 4) {
    if (RestrictToExec &&
        !IsPeFileOffsetInExecutableSection (Buf, Size, Off, 4)) {
      continue;
    }
    Insn = ReadInstrU32 (Buf, Off);
    if (!Arm64DecodeBranch (Insn, Off, &Kind, &Tgt, &RegOrCond)) {
      continue;
    }
    if (Kind != ARM64_INSN_CBZ_W && Kind != ARM64_INSN_CBNZ_W &&
        Kind != ARM64_INSN_CBZ_X && Kind != ARM64_INSN_CBNZ_X &&
        Kind != ARM64_INSN_BCOND) {
      continue;
    }
    if (Tgt == TargetOff) {
      if (Found == 0) FirstOff = Off;
      ++Found;
    }
  }

  if (Found == 0) return SCAN_NOT_FOUND;
  if (Found > 1)  return SCAN_AMBIGUOUS;
  *MatchOff = FirstOff;
  return SCAN_FOUND;
}
