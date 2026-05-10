/** @file AvbParse.c — AVB structure parser. **/
#include "Internal/AvbBigEndian.h"

#include "../../Include/Library/AvbParseLib.h"

EFI_STATUS EFIAPI
AvbParse_Footer (IN CONST UINT8 *Partition, IN UINT64 PartitionSize, OUT GBL_AVB_FOOTER *FooterOut) {
  CONST UINT8 *Footer;
  if (Partition == NULL || FooterOut == NULL)        return EFI_INVALID_PARAMETER;
  if (PartitionSize < GBL_AVB_FOOTER_SIZE)           return EFI_INVALID_PARAMETER;
  Footer = Partition + PartitionSize - GBL_AVB_FOOTER_SIZE;
  if (Footer[0] != 'A' || Footer[1] != 'V'
      || Footer[2] != 'B' || Footer[3] != 'f') {
    return EFI_NOT_FOUND;
  }
  FooterOut->FooterMajorVersion  = AvbReadU32Be (Footer + 4);
  FooterOut->FooterMinorVersion  = AvbReadU32Be (Footer + 8);
  FooterOut->OriginalImageSize   = AvbReadU64Be (Footer + 12);
  FooterOut->VbmetaOffset        = AvbReadU64Be (Footer + 20);
  FooterOut->VbmetaSize          = AvbReadU64Be (Footer + 28);
  if (FooterOut->VbmetaOffset + FooterOut->VbmetaSize > PartitionSize) return EFI_INVALID_PARAMETER;
  if (FooterOut->OriginalImageSize > PartitionSize) return EFI_INVALID_PARAMETER;
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
AvbParse_VbmetaHeader (IN CONST UINT8 *Vbmeta, IN UINT64 VbmetaSize, OUT GBL_AVB_VBMETA_HEADER *HeaderOut)
{
  if (Vbmeta == NULL || HeaderOut == NULL)         return EFI_INVALID_PARAMETER;
  if (VbmetaSize < GBL_AVB_VBMETA_HEADER_SIZE)     return EFI_INVALID_PARAMETER;

  if (Vbmeta[0] != 'A' || Vbmeta[1] != 'V'
      || Vbmeta[2] != 'B' || Vbmeta[3] != '0') {
    return EFI_NOT_FOUND;
  }

  HeaderOut->AvbMajorVersion              = AvbReadU32Be (Vbmeta + 4);
  HeaderOut->AvbMinorVersion              = AvbReadU32Be (Vbmeta + 8);
  HeaderOut->AuthenticationDataBlockSize  = AvbReadU64Be (Vbmeta + 12);
  HeaderOut->AuxiliaryDataBlockSize       = AvbReadU64Be (Vbmeta + 20);
  HeaderOut->AlgorithmType                = AvbReadU32Be (Vbmeta + 28);
  HeaderOut->HashOffset                   = AvbReadU64Be (Vbmeta + 32);
  HeaderOut->HashSize                     = AvbReadU64Be (Vbmeta + 40);
  HeaderOut->SignatureOffset              = AvbReadU64Be (Vbmeta + 48);
  HeaderOut->SignatureSize                = AvbReadU64Be (Vbmeta + 56);
  HeaderOut->PublicKeyOffset              = AvbReadU64Be (Vbmeta + 64);
  HeaderOut->PublicKeySize                = AvbReadU64Be (Vbmeta + 72);
  HeaderOut->PublicKeyMetadataOffset      = AvbReadU64Be (Vbmeta + 80);
  HeaderOut->PublicKeyMetadataSize        = AvbReadU64Be (Vbmeta + 88);
  HeaderOut->DescriptorsOffset            = AvbReadU64Be (Vbmeta + 96);
  HeaderOut->DescriptorsSize              = AvbReadU64Be (Vbmeta + 104);
  HeaderOut->RollbackIndex                = AvbReadU64Be (Vbmeta + 112);
  HeaderOut->Flags                        = AvbReadU32Be (Vbmeta + 120);
  HeaderOut->RollbackIndexLocation        = AvbReadU32Be (Vbmeta + 124);
  for (int i = 0; i < 48; ++i) HeaderOut->ReleaseString[i] = (CHAR8)Vbmeta[128 + i];

  /* Sanity: header + auth + aux <= VbmetaSize. */
  UINT64 Total = (UINT64)GBL_AVB_VBMETA_HEADER_SIZE
                + HeaderOut->AuthenticationDataBlockSize
                + HeaderOut->AuxiliaryDataBlockSize;
  if (Total > VbmetaSize) return EFI_INVALID_PARAMETER;

  return EFI_SUCCESS;
}
EFI_STATUS EFIAPI AvbParse_NextDescriptor (CONST UINT8 *A, UINT64 S, UINT64 *C, GBL_AVB_DESCRIPTOR_TAG *T, CONST UINT8 **D, UINT64 *L) { (void)A; (void)S; (void)C; (void)T; (void)D; (void)L; return -1; }
EFI_STATUS EFIAPI AvbParse_HashDescriptor (CONST UINT8 *D, UINT64 L, CONST UINT8 **N, UINT32 *NL, CONST UINT8 **DG, UINT32 *DGL) { (void)D; (void)L; (void)N; (void)NL; (void)DG; (void)DGL; return -1; }
EFI_STATUS EFIAPI AvbParse_ChainPartitionDescriptor (CONST UINT8 *D, UINT64 L, CONST UINT8 **N, UINT32 *NL, CONST UINT8 **PK, UINT32 *PKL) { (void)D; (void)L; (void)N; (void)NL; (void)PK; (void)PKL; return -1; }
