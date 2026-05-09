/** @file PeSections.h — PE/COFF section helpers used by DynamicPatchLib. **/
#ifndef DPL_PE_SECTIONS_H_
#define DPL_PE_SECTIONS_H_

#include "ScanLib.h"  /* For UINT8/UINT32/BOOLEAN type aliases incl. host shim. */

/** Return TRUE if the file-offset range [Off, Off+Len) lies entirely
    within an executable PE section in Buf. **/
BOOLEAN
IsPeFileOffsetInExecutableSection (
  IN CONST UINT8 *Buf,
  IN UINT32       Size,
  IN UINT32       Off,
  IN UINT32       Len
  );

#endif /* DPL_PE_SECTIONS_H_ */
