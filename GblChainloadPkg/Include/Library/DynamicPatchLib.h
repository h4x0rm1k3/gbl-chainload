#ifndef DYNAMIC_PATCH_LIB_H_
#define DYNAMIC_PATCH_LIB_H_

#include "PatchDesc.h"   /* sibling under Include/Library/ */

typedef enum {
  PATCH_RESULT_OK              = 0,
  PATCH_RESULT_OPTIONAL_MISS   = 1,
  PATCH_RESULT_MANDATORY_MISS  = 2,
} PATCH_WORST;

typedef struct {
  UINT32       AppliedCount;
  UINT32       MissedCount;
  PATCH_WORST  WorstOutcome;
} PATCH_RESULT;

VOID
DynamicPatch_Apply (
  IN OUT UINT8         *Buf,
  IN     UINT32         Size,
  OUT    PATCH_RESULT  *Result
  );

#endif /* DYNAMIC_PATCH_LIB_H_ */
