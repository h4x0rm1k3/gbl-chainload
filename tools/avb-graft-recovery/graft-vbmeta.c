#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define AVB_FOOTER_MAGIC "AVBf"
#define AVB_FOOTER_SIZE  64
#define AVB_VBMETA_MAGIC "AVB0"

static uint64_t read_u64_be (const uint8_t *p) {
  return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
       | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8)  |  (uint64_t)p[7];
}

int main (int argc, char **argv) {
  if (argc != 4) {
    fprintf (stderr, "Usage: %s <stock.img> <custom.img> <out.img>\n", argv[0]);
    return 2;
  }
  FILE *fs = fopen (argv[1], "rb");
  FILE *fc = fopen (argv[2], "rb");
  if (!fs || !fc) { perror ("open"); return 1; }
  fseek (fs, 0, SEEK_END); long ssz = ftell (fs); fseek (fs, 0, SEEK_SET);
  fseek (fc, 0, SEEK_END); long csz = ftell (fc); fseek (fc, 0, SEEK_SET);
  if (ssz <= AVB_FOOTER_SIZE) { fprintf (stderr, "stock too small\n"); return 1; }

  uint8_t *stock = malloc (ssz);
  uint8_t *custom = malloc (ssz);
  if (!stock || !custom) { fprintf (stderr, "OOM\n"); return 1; }
  memset (custom, 0, ssz);
  fread (stock, 1, ssz, fs);
  fread (custom, 1, csz < ssz ? csz : ssz, fc);
  fclose (fs); fclose (fc);

  uint8_t *footer = stock + ssz - AVB_FOOTER_SIZE;
  if (memcmp (footer, AVB_FOOTER_MAGIC, 4) != 0) {
    fprintf (stderr, "AvbFooter magic missing in stock\n"); return 1;
  }
  uint64_t orig = read_u64_be (footer + 12);
  uint64_t vbm_off = read_u64_be (footer + 20);
  uint64_t vbm_sz  = read_u64_be (footer + 28);
  fprintf (stderr, "donor footer: orig=%llu vbm_off=%llu vbm_sz=%llu\n",
           (unsigned long long)orig, (unsigned long long)vbm_off, (unsigned long long)vbm_sz);

  if (vbm_off + vbm_sz > (uint64_t)ssz) { fprintf (stderr, "footer inconsistent\n"); return 1; }

  memcpy (custom + vbm_off, stock + vbm_off, ssz - vbm_off);

  FILE *fo = fopen (argv[3], "wb");
  if (!fo) { perror ("open out"); return 1; }
  fwrite (custom, 1, ssz, fo);
  fclose (fo);
  fprintf (stderr, "wrote %ld bytes to %s\n", ssz, argv[3]);
  free (stock); free (custom);
  return 0;
}
