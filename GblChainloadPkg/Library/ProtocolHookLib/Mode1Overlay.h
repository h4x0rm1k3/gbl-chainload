/** @file Mode1Overlay.h — mode-1-scope hook policy declarations.
    Only active when GBL_MODE == 1. **/
#ifndef MODE1_OVERLAY_H_
#define MODE1_OVERLAY_H_

#include <Uefi.h>
#include <Protocol/EFIVerifiedBoot.h>
#include "HookCommon.h"

#if (GBL_MODE == 1)

/** Mode-1 policy for VBRwDeviceState(READ_CONFIG) post-call mutator.
    Clears is_unlocked and is_unlock_critical in the returned device_info_vb_t.
    Returns OrigStatus unchanged (pass-through). **/
EFI_STATUS EFIAPI
Mode1Policy_OnVbReadConfig_Post (
  IN  EFI_STATUS  OrigStatus,
  IN  VOID       *Buf,
  IN  UINT32      BufLen
  );

/** Mode-1 policy for VBDeviceInit pre/post clear.
    Pass IsPre=TRUE before calling original; IsPre=FALSE after.
    Safe to call with Devinfo==NULL (no-op). **/
VOID EFIAPI
Mode1Policy_OnVbDeviceInit_PrePost (
  IN OUT device_info_vb_t *Devinfo,
  IN     BOOLEAN           IsPre
  );

#endif /* GBL_MODE == 1 */

#endif /* MODE1_OVERLAY_H_ */
