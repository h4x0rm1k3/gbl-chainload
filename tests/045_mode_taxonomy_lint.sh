#!/usr/bin/env bash
# 045_mode_taxonomy_lint.sh — assert mode_1 patches are gated behind GBL_MODE==1
# in the aggregator, and that the universal/oem/mode_1 patch tables exist with
# the expected scope assignment.
set -euo pipefail

cd "$(dirname "$0")/.."

PKG="GblChainloadPkg/Library/DynamicPatchLib"

# 1. PatchTable.c exists and gates mode_1 inclusion behind GBL_MODE==1.
test -f "$PKG/PatchTable.c" || { echo "FAIL: missing PatchTable.c"; exit 1; }
grep -q '#if (GBL_MODE == 1)' "$PKG/PatchTable.c" \
  || { echo "FAIL: PatchTable.c must gate mode_1 patches behind GBL_MODE==1"; exit 1; }

# 2. Universal patches use SCOPE_UNIVERSAL.
grep -q 'SCOPE_UNIVERSAL' "$PKG/universal/universal.c" \
  || { echo "FAIL: universal/universal.c must declare SCOPE_UNIVERSAL"; exit 1; }

# 3. OEM patches use SCOPE_OEM_ONEPLUS.
grep -q 'SCOPE_OEM_ONEPLUS' "$PKG/oem/oneplus_canoe.c" \
  || { echo "FAIL: oem/oneplus_canoe.c must declare SCOPE_OEM_ONEPLUS"; exit 1; }

# 4. Mode-1 patches use SCOPE_MODE_1.
grep -q 'SCOPE_MODE_1' "$PKG/mode_1/mode_1.c" \
  || { echo "FAIL: mode_1/mode_1.c must declare SCOPE_MODE_1"; exit 1; }

# 5. patch9 is mode-1 scope, NOT in universal or oem.
if grep -q 'patch9-avb-locked-recoverable-continue' "$PKG/universal/universal.c" \
   || grep -q 'patch9-avb-locked-recoverable-continue' "$PKG/oem/oneplus_canoe.c"; then
  echo "FAIL: patch9 must be in mode_1/, not universal/ or oem/"; exit 1
fi
grep -q 'patch9-avb-locked-recoverable-continue' "$PKG/mode_1/mode_1.c" \
  || { echo "FAIL: patch9 must be in mode_1/mode_1.c"; exit 1; }

# 6. patch1 is universal scope.
grep -q 'patch1-efisp-recursion' "$PKG/universal/universal.c" \
  || { echo "FAIL: patch1 must be in universal/universal.c"; exit 1; }

# 7. patch7 is oem scope.
grep -q 'patch7-orange-screen' "$PKG/oem/oneplus_canoe.c" \
  || { echo "FAIL: patch7 must be in oem/oneplus_canoe.c"; exit 1; }

# 8. UniversalBaseline policies wired into slot wrappers.
grep -q 'UniversalPolicy_OnVbWriteConfig' \
  GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c \
  || { echo "FAIL: VerifiedBootHook missing UniversalPolicy_OnVbWriteConfig call"; exit 1; }
grep -q 'UniversalPolicy_OnVbReset' \
  GblChainloadPkg/Library/ProtocolHookLib/VerifiedBootHook.c \
  || { echo "FAIL: VerifiedBootHook missing UniversalPolicy_OnVbReset call"; exit 1; }
grep -q 'UniversalPolicy_ShouldDropScmSip' \
  GblChainloadPkg/Library/ProtocolHookLib/ScmHook.c \
  || { echo "FAIL: ScmHook missing universal SIP drop"; exit 1; }
grep -q 'UniversalPolicy_ShouldDropQseeOplusSec' \
  GblChainloadPkg/Library/ProtocolHookLib/QseecomHook.c \
  || { echo "FAIL: QseecomHook missing universal OplusSec drop"; exit 1; }

# 9. ProtocolHook_InstallAll exists.
grep -q 'ProtocolHook_InstallAll' \
  GblChainloadPkg/Library/ProtocolHookLib/InstallAll.c \
  || { echo "FAIL: InstallAll.c missing main entry"; exit 1; }
test -f GblChainloadPkg/Library/ProtocolHookLib/ProtocolHookLib.inf \
  || { echo "FAIL: missing ProtocolHookLib.inf"; exit 1; }
test -f GblChainloadPkg/Include/Library/ProtocolHookLib.h \
  || { echo "FAIL: missing public ProtocolHookLib.h"; exit 1; }

echo "ok 045_mode_taxonomy_lint"
