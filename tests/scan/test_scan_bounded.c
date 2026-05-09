/** test_scan_bounded.c — TDD tests for ScanForBoundedSection and
    IsPeFileOffsetInExecutableSection. **/

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ScanLib.h"
#include "PeSections.h"

/* -------------------------------------------------------------------------
 * Minimal synthetic PE layout:
 *
 *  0x000  DOS header: 'MZ', e_lfanew at 0x3C = 0x80
 *  0x080  PE signature: 'PE\0\0'
 *  0x084  COFF header (20 bytes):
 *           +0  Machine        = 0xAA64
 *           +2  NumberOfSections = 1
 *           +16 SizeOfOptionalHeader = 0xF0
 *  0x098  Optional header (0xF0 bytes, zeroed)
 *  0x188  Section table entry #0 (40 bytes):
 *           +0x08 (0x190)  VirtualSize      = 0x200
 *           +0x10 (0x198)  SizeOfRawData    = 0x200
 *           +0x14 (0x19C)  PointerToRawData = 0x200
 *           +0x24 (0x1AC)  Characteristics  = 0x20000000 (IMAGE_SCN_MEM_EXECUTE)
 *  0x200–0x3FF  .text raw data (executable section)
 *  0x400–0x7FF  outside any section
 *  Total buffer: 0x800 bytes
 *
 * Section table header layout (IMAGE_SECTION_HEADER, 40 bytes):
 *   +00  Name[8]
 *   +08  VirtualSize (Misc.VirtualSize)
 *   +0C  VirtualAddress
 *   +10  SizeOfRawData
 *   +14  PointerToRawData
 *   +18  PointerToRelocations
 *   +1C  PointerToLinenumbers
 *   +20  NumberOfRelocations (2 bytes)
 *   +22  NumberOfLinenumbers (2 bytes)
 *   +24  Characteristics
 * ------------------------------------------------------------------------- */

#define BUF_SIZE 0x800

static void
WriteU16Le (UINT8 *p, UINT32 v)
{
  p[0] = (UINT8)(v & 0xFF);
  p[1] = (UINT8)((v >> 8) & 0xFF);
}

static void
WriteU32Le (UINT8 *p, UINT32 v)
{
  p[0] = (UINT8)(v & 0xFF);
  p[1] = (UINT8)((v >> 8) & 0xFF);
  p[2] = (UINT8)((v >> 16) & 0xFF);
  p[3] = (UINT8)((v >> 24) & 0xFF);
}

static void
make_synthetic_pe (UINT8 *Buf, UINT32 BufSize)
{
  memset (Buf, 0, BufSize);

  /* DOS header */
  Buf[0] = 'M';  Buf[1] = 'Z';
  WriteU32Le (Buf + 0x3C, 0x80);           /* e_lfanew = 0x80 */

  /* PE signature */
  Buf[0x80] = 'P';  Buf[0x81] = 'E';  Buf[0x82] = 0;  Buf[0x83] = 0;

  /* COFF header at 0x84 */
  WriteU16Le (Buf + 0x84, 0xAA64);         /* Machine = AArch64 */
  WriteU16Le (Buf + 0x86, 1);              /* NumberOfSections = 1 */
  /* SizeOfOptionalHeader is at COFF+16 = 0x84+16 = 0x94 */
  WriteU16Le (Buf + 0x94, 0xF0);           /* SizeOfOptionalHeader = 0xF0 */

  /* Section table at PeOff+24+OptHdrSize = 0x80+24+0xF0 = 0x188 */
  /* Sh = 0x188 */
  /* +0x08 = 0x190: VirtualSize */
  WriteU32Le (Buf + 0x190, 0x200);
  /* +0x10 = 0x198: SizeOfRawData */
  WriteU32Le (Buf + 0x198, 0x200);
  /* +0x14 = 0x19C: PointerToRawData */
  WriteU32Le (Buf + 0x19C, 0x200);
  /* +0x24 = 0x1AC: Characteristics = IMAGE_SCN_MEM_EXECUTE */
  WriteU32Le (Buf + 0x1AC, 0x20000000U);
}

/* -------------------------------------------------------------------------
 * Test helpers
 * ------------------------------------------------------------------------- */

static int  g_pass = 0;
static int  g_fail = 0;

#define EXPECT_EQ(got, want, label)                                      \
  do {                                                                   \
    if ((got) == (want)) {                                               \
      printf ("[PASS] %s\n", (label));                                   \
      g_pass++;                                                          \
    } else {                                                             \
      printf ("[FAIL] %s: got %d, want %d\n", (label), (int)(got),      \
              (int)(want));                                              \
      g_fail++;                                                          \
    }                                                                    \
  } while (0)

/* -------------------------------------------------------------------------
 * Test 1: pattern inside executable section found with ExecOnly=TRUE
 * ------------------------------------------------------------------------- */
static void
test_scan_bounded_in_section (void)
{
  UINT8  Buf[BUF_SIZE];
  UINT32 Off = 0;
  SCAN_RESULT r;

  make_synthetic_pe (Buf, BUF_SIZE);

  /* place 4-byte pattern at 0x250 (inside .text 0x200–0x3FF) */
  Buf[0x250] = 0xDE;  Buf[0x251] = 0xAD;
  Buf[0x252] = 0xBE;  Buf[0x253] = 0xEF;

  r = ScanForBoundedSection (Buf, BUF_SIZE, TRUE,
                             (UINT8 *)"\xDE\xAD\xBE\xEF", NULL, 4, &Off);
  EXPECT_EQ (r,   SCAN_FOUND, "test1: result SCAN_FOUND");
  EXPECT_EQ (Off, 0x250U,     "test1: offset 0x250");
}

/* -------------------------------------------------------------------------
 * Test 2: pattern outside any section not found with ExecOnly=TRUE
 * ------------------------------------------------------------------------- */
static void
test_scan_bounded_excludes_outside (void)
{
  UINT8  Buf[BUF_SIZE];
  UINT32 Off = 0;
  SCAN_RESULT r;

  make_synthetic_pe (Buf, BUF_SIZE);

  /* place 4-byte pattern at 0x500 (outside any section) */
  Buf[0x500] = 0xDE;  Buf[0x501] = 0xAD;
  Buf[0x502] = 0xBE;  Buf[0x503] = 0xEF;

  r = ScanForBoundedSection (Buf, BUF_SIZE, TRUE,
                             (UINT8 *)"\xDE\xAD\xBE\xEF", NULL, 4, &Off);
  EXPECT_EQ (r, SCAN_NOT_FOUND, "test2: result SCAN_NOT_FOUND");
}

/* -------------------------------------------------------------------------
 * Test 3a: two matches (0x250 + 0x500) with ExecOnly=TRUE → SCAN_FOUND @0x250
 * Test 3b: same two matches with ExecOnly=FALSE → SCAN_AMBIGUOUS
 * ------------------------------------------------------------------------- */
static void
test_scan_bounded_two_matches_one_in_section (void)
{
  UINT8  Buf[BUF_SIZE];
  UINT32 Off = 0;
  SCAN_RESULT r;

  make_synthetic_pe (Buf, BUF_SIZE);

  /* pattern at both 0x250 (in .text) and 0x500 (outside) */
  Buf[0x250] = 0xCA;  Buf[0x251] = 0xFE;
  Buf[0x252] = 0xBA;  Buf[0x253] = 0xBE;
  Buf[0x500] = 0xCA;  Buf[0x501] = 0xFE;
  Buf[0x502] = 0xBA;  Buf[0x503] = 0xBE;

  /* ExecOnly=TRUE: only the in-section match counts */
  r = ScanForBoundedSection (Buf, BUF_SIZE, TRUE,
                             (UINT8 *)"\xCA\xFE\xBA\xBE", NULL, 4, &Off);
  EXPECT_EQ (r,   SCAN_FOUND, "test3a: ExecOnly=TRUE → SCAN_FOUND");
  EXPECT_EQ (Off, 0x250U,     "test3a: offset 0x250");

  /* ExecOnly=FALSE: both matches visible → SCAN_AMBIGUOUS */
  Off = 0;
  r = ScanForBoundedSection (Buf, BUF_SIZE, FALSE,
                             (UINT8 *)"\xCA\xFE\xBA\xBE", NULL, 4, &Off);
  EXPECT_EQ (r, SCAN_AMBIGUOUS, "test3b: ExecOnly=FALSE → SCAN_AMBIGUOUS");
}

/* -------------------------------------------------------------------------
 * Test 4: IsPeFileOffsetInExecutableSection directly
 * ------------------------------------------------------------------------- */
static void
test_scan_bounded_off_in_section (void)
{
  UINT8 Buf[BUF_SIZE];

  make_synthetic_pe (Buf, BUF_SIZE);

  /* 0x250 with len=4 is inside executable .text */
  EXPECT_EQ (IsPeFileOffsetInExecutableSection (Buf, BUF_SIZE, 0x250, 4),
             TRUE, "test4a: 0x250 in exec section → TRUE");

  /* 0x500 with len=4 is outside any section */
  EXPECT_EQ (IsPeFileOffsetInExecutableSection (Buf, BUF_SIZE, 0x500, 4),
             FALSE, "test4b: 0x500 not in exec section → FALSE");
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int
main (void)
{
  test_scan_bounded_in_section ();
  test_scan_bounded_excludes_outside ();
  test_scan_bounded_two_matches_one_in_section ();
  test_scan_bounded_off_in_section ();

  printf ("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
