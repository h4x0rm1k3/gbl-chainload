/* Host test for patch1 (efisp recursion) against available fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../../GblChainloadPkg/Include/Library/PatchDesc.h"

/* Pull in ScanLib symbols needed by universal.c. */

/* The patch table we are testing. */
extern CONST PATCH_DESC kUniversalPatches[];
extern CONST UINTN      kUniversalPatchesCount;

static UINT8 *
load_file (const char *path, UINT32 *size_out)
{
  FILE *f = fopen (path, "rb");
  if (!f) {
    perror (path);
    return NULL;
  }
  fseek (f, 0, SEEK_END);
  long sz = ftell (f);
  fseek (f, 0, SEEK_SET);
  UINT8 *buf = (UINT8 *)malloc ((size_t)sz);
  if (!buf || (long)fread (buf, 1, (size_t)sz, f) != sz) {
    fclose (f);
    free (buf);
    return NULL;
  }
  fclose (f);
  *size_out = (UINT32)sz;
  return buf;
}

/*
 * Returns 1 if the fixture was tested (regardless of PATCH_OK vs PATCH_MISS),
 * 0 if the file was absent (skip).
 * Asserts on any unexpected outcome.
 */
static int
test_patch1_against (const char *path)
{
  UINT32 size = 0;
  UINT8 *buf  = load_file (path, &size);
  if (!buf) {
    printf ("skip patch1 against %s (file missing)\n", path);
    return 0;
  }

  /* Locate ApplyEfispRecursion in the universal patch table by name. */
  PATCH_APPLY apply = NULL;
  for (UINTN i = 0; i < kUniversalPatchesCount; ++i) {
    if (strcmp (kUniversalPatches[i].Name, "patch1-efisp-recursion") == 0) {
      apply = kUniversalPatches[i].Apply;
      break;
    }
  }
  assert (apply != NULL && "patch1-efisp-recursion not found in kUniversalPatches");

  PATCH_OUTCOME o = apply (buf, size);
  printf ("patch1 vs %s -> outcome=%d\n", path, (int)o);

  /* Only PATCH_OK and PATCH_MISS are acceptable outcomes for any fixture. */
  assert ((o == PATCH_OK || o == PATCH_MISS) && "unexpected outcome");

  if (o == PATCH_OK) {
    /* Verify the rewrite: UTF-16LE "nulls" must now be present. */
    UINT8 nulls_pat[] = { 'n', 0, 'u', 0, 'l', 0, 'l', 0, 's', 0 };
    int found_nulls = 0;
    for (UINT32 i = 0; i + 10 <= size; ++i) {
      if (memcmp (buf + i, nulls_pat, 10) == 0) {
        found_nulls = 1;
        break;
      }
    }
    assert (found_nulls && "rewrite did not produce UTF-16LE 'nulls'");

    /* "efisp" must no longer exist anywhere in the buffer. */
    UINT8 efisp_pat[] = { 'e', 0, 'f', 0, 'i', 0, 's', 0, 'p', 0 };
    int still_present = 0;
    for (UINT32 i = 0; i + 10 <= size; ++i) {
      if (memcmp (buf + i, efisp_pat, 10) == 0) {
        still_present = 1;
        break;
      }
    }
    assert (!still_present && "efisp still present after patch");

    /* Idempotency: re-applying must miss (pattern is gone). */
    PATCH_OUTCOME o2 = apply (buf, size);
    assert (o2 == PATCH_MISS && "second application should miss");
    printf ("ok patch1 idempotency vs %s\n", path);
  }

  free (buf);
  return 1;
}

int
main (void)
{
  int ran = 0;
  ran += test_patch1_against (
    "/home/vivy/gbl-chainload/images/infiniti/LinuxLoader_infiniti.efi");
  ran += test_patch1_against (
    "/home/vivy/gbl-chainload/images/infiniti-EU-16.0.5.703/abl.bin");
  ran += test_patch1_against (
    "/home/vivy/gbl-chainload/images/fixtures/canoe-A.07/abl_a.bin");

  if (ran == 0) {
    fprintf (stderr,
             "FAIL: no fixtures available — at least one must be present\n");
    return 1;
  }
  printf ("ALL PASS (%d fixture%s exercised)\n", ran, ran == 1 ? "" : "s");
  return 0;
}
