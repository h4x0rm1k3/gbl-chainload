/** @file UniversalBaseline.h — universal-mode hook policy declarations.
    These run on every GBL_MODE; the per-mode overlay layers on top.
**/
#ifndef UNIVERSAL_BASELINE_H_
#define UNIVERSAL_BASELINE_H_

#include <Uefi.h>
#include "HookCommon.h"

/* VerifiedBoot policies. */

/** Universal policy for VBRwDeviceState(WRITE_CONFIG). Returns EFI_SUCCESS
    and does NOT forward to the original. **/
EFI_STATUS EFIAPI
UniversalPolicy_OnVbWriteConfig (
  IN UINT32  Op,
  IN VOID   *Buf,
  IN UINT32  BufLen
  );

/** Universal policy for VBDeviceResetState. Returns EFI_SUCCESS without
    forwarding. **/
EFI_STATUS EFIAPI
UniversalPolicy_OnVbReset (VOID);

/* SCM policy: TZ_BLOW_SW_FUSE_ID drop. */

/** If SmcId is the soft-fuse-blow SIP, returns TRUE and writes EFI_SUCCESS
    into FakeStatus; caller short-circuits without forwarding the SMC.
    Returns FALSE for any other SmcId — caller proceeds normally. **/
BOOLEAN
UniversalPolicy_ShouldDropScmSip (
  IN  UINT32       SmcId,
  OUT EFI_STATUS  *FakeStatus
  );

/* Qseecom policy: OplusSec 0x0A write_rpmb_boot_info drop. */

/** Caller already determined Handle == OplusSec. If CmdId is 0x0A
    write_rpmb_boot_info, returns TRUE and writes EFI_SUCCESS into FakeStatus;
    caller short-circuits without forwarding. **/
BOOLEAN
UniversalPolicy_ShouldDropQseeOplusSec (
  IN  UINT32       CmdId,
  OUT EFI_STATUS  *FakeStatus
  );

#endif
