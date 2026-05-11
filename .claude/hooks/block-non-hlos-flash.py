#!/usr/bin/env python3
"""PreToolUse Bash guard: block dangerous fastboot ops.

Reads {"tool_input":{"command":"..."}} on stdin. Emits a
permissionDecision:deny JSON when the command actually invokes one of:

    fastboot flash <non-HLOS>
    fastboot erase <non-HLOS>
    fastboot oem {unlock,lock}
    fastboot --set-active <slot>

HLOS partitions (allowed): system, vendor, product, system_ext, odm,
userdata, cache, metadata — with optional _a/_b slot suffix.

Silent (exit 0, no stdout) for everything else.

The matcher is robust against false positives: it splits the command on
shell operators (; && || | newline), tokenises each subcommand via shlex,
strips leading env assignments / sudo, and only treats `fastboot` as a
match when it's the actual command name. A commit message that *mentions*
fastboot won't trigger.
"""

from __future__ import annotations

import json
import re
import shlex
import sys

HLOS = re.compile(
    r"^(system|vendor|product|system_ext|odm|userdata|cache|metadata)(_a|_b)?$"
)

# Shell pipeline/sequence operators that start a new subcommand.
SUBCMD_SEP = re.compile(r"\s*(?:&&|\|\||;|\|(?!\|)|\n)\s*")


def deny(reason: str) -> "None":
    json.dump(
        {
            "hookSpecificOutput": {
                "hookEventName": "PreToolUse",
                "permissionDecision": "deny",
                "permissionDecisionReason": reason,
            }
        },
        sys.stdout,
    )
    sys.exit(0)


def main() -> None:
    try:
        data = json.load(sys.stdin)
    except Exception:
        return
    cmd = data.get("tool_input", {}).get("command", "") or ""
    if "fastboot" not in cmd:
        return

    for part in SUBCMD_SEP.split(cmd):
        try:
            toks = shlex.split(part, posix=True)
        except ValueError:
            continue
        # Strip leading env assignments (VAR=value) and `sudo`.
        i = 0
        while i < len(toks):
            t = toks[i]
            if t == "sudo":
                i += 1
                continue
            if re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", t):
                i += 1
                continue
            break
        if i >= len(toks):
            continue
        head = toks[i].rsplit("/", 1)[-1]
        if head != "fastboot":
            continue

        positional: list[str] = []
        for t in toks[i + 1 :]:
            if t == "--set-active" or t.startswith("--set-active="):
                deny(
                    "fastboot --set-active is BLOCKED: slot switches can leave "
                    "the device unbootable. Run it yourself in a shell if "
                    "intended."
                )
            if t.startswith("-"):
                continue
            positional.append(t)
        if not positional:
            continue

        sub = positional[0]
        if sub == "flash":
            target = positional[1] if len(positional) > 1 else ""
            if not target:
                deny("fastboot flash with no partition is BLOCKED.")
            if not HLOS.match(target):
                deny(
                    f"fastboot flash {target} is BLOCKED — non-HLOS flash can "
                    "hard-brick the device. Test gbl-chainload EFIs via: "
                    "fastboot stage dist/<file>.efi && fastboot oem boot-efi. "
                    f"If {target} really needs flashing, run it yourself in a "
                    "shell."
                )
        elif sub == "erase":
            target = positional[1] if len(positional) > 1 else ""
            if not target or not HLOS.match(target):
                deny(
                    f"fastboot erase {target or '<no-partition>'} is BLOCKED — "
                    "non-HLOS erase can wipe boot-critical metadata. Run it "
                    "yourself if intended."
                )
        elif sub == "oem":
            sub2 = positional[1] if len(positional) > 1 else ""
            if sub2 in ("unlock", "lock"):
                deny(
                    f"fastboot oem {sub2} is BLOCKED — lock-state transitions "
                    "affect verified boot, TZ fuses, and user-data wipe. Run "
                    "it yourself."
                )
        elif sub == "flashing":
            # Modern equivalents of `oem unlock/lock`. Same blast radius.
            # `get_unlock_ability` is read-only — leave it alone.
            sub2 = positional[1] if len(positional) > 1 else ""
            if sub2 in ("unlock", "lock", "unlock_critical", "lock_critical"):
                deny(
                    f"fastboot flashing {sub2} is BLOCKED — lock-state "
                    "transitions affect verified boot, TZ fuses, and "
                    "user-data wipe. Run it yourself."
                )


if __name__ == "__main__":
    main()
