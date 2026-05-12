/** @file Arm64Decode.h — minimal AArch64 instruction decoders used by patches.

    Only the kinds the patch engine needs today:
      ADRP + ADD (immediate) pair — resolve a load-address into the binary
      CBZ Wn / CBNZ Wn / CBZ Xn / CBNZ Xn — for finding gating branches
      B.cond — for finding conditional skips
      B (unconditional) / BL — for completeness

    Self-contained: depends only on ScanLib.h (for UINT and BOOLEAN type aliases). **/

#ifndef DPL_ARM64_DECODE_H_
#define DPL_ARM64_DECODE_H_

#include "ScanLib.h"

typedef enum {
  ARM64_INSN_OTHER  = 0,
  ARM64_INSN_CBZ_W,       /* CBZ  Wn, imm19 */
  ARM64_INSN_CBNZ_W,      /* CBNZ Wn, imm19 */
  ARM64_INSN_CBZ_X,       /* CBZ  Xn, imm19 */
  ARM64_INSN_CBNZ_X,      /* CBNZ Xn, imm19 */
  ARM64_INSN_BCOND,       /* B.cond imm19 */
  ARM64_INSN_B,           /* B    imm26 (unconditional) */
  ARM64_INSN_BL,          /* BL   imm26 */
} ARM64_INSN_KIND;

/** Decode a single AArch64 branch instruction.

    @param Insn       Raw 32-bit instruction word.
    @param InsnOff    File offset of this instruction (used to compute
                      absolute branch target from the encoded offset).
    @param Kind       Out: the recognized kind (ARM64_INSN_OTHER if none).
    @param TargetOff  Out: absolute file offset the branch points to.
    @param RegOrCond  Out: for CBZ/CBNZ — the Rt register number (0–31);
                      for B.cond — the condition code (0–15);
                      undefined for B/BL/OTHER.
    @return TRUE iff Kind != ARM64_INSN_OTHER (i.e. recognized branch). **/
BOOLEAN
Arm64DecodeBranch (
  IN  UINT32           Insn,
  IN  UINT32           InsnOff,
  OUT ARM64_INSN_KIND *Kind,
  OUT UINT32          *TargetOff,
  OUT UINT32          *RegOrCond
  );

/** Decode an ADRP at AdrpOff together with the ADD-immediate at AdrpOff+4
    and resolve the combined byte address they load.

    Requires:
      - Insn at AdrpOff is ADRP (top byte form `1_xx_10000`).
      - Insn at AdrpOff+4 is ADD-imm (shift=00) whose Rd == ADRP's Rd
        and Rn == ADRP's Rd (i.e. `ADD Rd, Rd, #imm12`).

    @return TRUE on a clean decode, FALSE otherwise (TargetAddr untouched). **/
BOOLEAN
Arm64DecodeAdrpAdd (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       AdrpOff,
  OUT UINT32      *TargetAddr
  );

/** Walk every 4-byte-aligned offset in Buf and count ADRP+ADD pairs whose
    decoded target equals TargetAddr.

    @param RestrictToExec  When TRUE, only count pairs whose ADRP location
                           lies inside an executable PE section.
    @param MatchOff        Out: file offset of the unique ADRP (only on
                           SCAN_FOUND).
    @return SCAN_FOUND if exactly one pair matches, SCAN_NOT_FOUND if zero,
            SCAN_AMBIGUOUS if more than one. **/
SCAN_RESULT
Arm64FindAdrpAddTargeting (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       TargetAddr,
  IN  BOOLEAN      RestrictToExec,
  OUT UINT32      *MatchOff
  );

/** Walk every 4-byte-aligned offset in Buf and count branches that
    target TargetOff.

    Recognized branch kinds: CBZ_W, CBNZ_W, CBZ_X, CBNZ_X, BCOND.
    B / BL are intentionally excluded — patch6 is only interested in
    conditional gates.

    @return SCAN_FOUND with MatchOff set if exactly one match, SCAN_NOT_FOUND
            if zero, SCAN_AMBIGUOUS if more than one. **/
SCAN_RESULT
Arm64FindCondBranchTargeting (
  IN  CONST UINT8 *Buf,
  IN  UINT32       Size,
  IN  UINT32       TargetOff,
  IN  BOOLEAN      RestrictToExec,
  OUT UINT32      *MatchOff
  );

#endif /* DPL_ARM64_DECODE_H_ */
