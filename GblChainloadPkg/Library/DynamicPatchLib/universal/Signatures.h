#ifndef DPL_UNIVERSAL_SIGNATURES_H_
#define DPL_UNIVERSAL_SIGNATURES_H_

#include "../../../Include/Library/ScanLib.h"  /* For UINT8 type alias incl. host shim. */

/* UTF-16LE "efisp" — 10 bytes.  Scanned in the entire PE.
   Patch1 rewrites to "nulls" (UTF-16LE) at the matched offset to break
   ABL's EFISP partition recursion when it tries to load itself. */
STATIC CONST UINT8 kEfispUtf16Pattern[] = {
  'e', 0, 'f', 0, 'i', 0, 's', 0, 'p', 0
};

#endif /* DPL_UNIVERSAL_SIGNATURES_H_ */
