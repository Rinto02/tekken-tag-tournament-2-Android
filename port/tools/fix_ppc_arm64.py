#!/usr/bin/env python3
"""Make the generated re/ppc/ppc_context.h compile for arm64 (NEON).

simde_mm_vsl shifts by a runtime count with simde_mm_slli_epi64 / simde_mm_srli_epi64.
On x86 those take any count, but SIMDe maps them to NEON immediate shifts (vshlq_n /
vshrq_n), which need a compile-time constant, so clang rejects the file for arm64
("argument to '__builtin_neon_vshlq_n_v' must be a constant integer").

The count-in-a-register forms (simde_mm_sll_epi64 / simde_mm_srl_epi64) have identical
behaviour for a runtime count and work on every target, so use them. Idempotent.

Also defines _mm_pause (the x86 spin-wait hint, used by the recompiled spin loops) as the
arm64 `yield` hint; SIMDe does not provide it.

usage: fix_ppc_arm64.py [path/to/re/ppc/ppc_context.h]
"""
import sys
from pathlib import Path

OLD = ("return simde_mm_or_si128(simde_mm_slli_epi64(a, shift), "
       "simde_mm_srli_epi64(simde_mm_slli_si128(a, 8), 64 - shift));")
NEW = ("return simde_mm_or_si128(simde_mm_sll_epi64(a, simde_mm_cvtsi32_si128(int(shift))), "
       "simde_mm_srl_epi64(simde_mm_slli_si128(a, 8), simde_mm_cvtsi32_si128(int(64 - shift))));")

PAUSE_MARK = "#if defined(__aarch64__) || defined(_M_ARM64)\ninline uint64_t __rdtsc()"
PAUSE_DEF = ("#if defined(__aarch64__) || defined(_M_ARM64)\n"
             "#ifndef _mm_pause\n#define _mm_pause() __asm__ __volatile__(\"yield\")\n#endif\n#endif\n\n")

path = Path(sys.argv[1] if len(sys.argv) > 1 else
            Path(__file__).resolve().parents[2] / "re" / "ppc" / "ppc_context.h")
text = path.read_text()
done = []

if NEW in text:
    pass
elif OLD in text:
    text = text.replace(OLD, NEW)
    done.append("simde_mm_vsl")
else:
    sys.exit(f"{path}: simde_mm_vsl shift line not found; the recompiler output changed")

if "#define _mm_pause()" not in text:
    if PAUSE_MARK not in text:
        sys.exit(f"{path}: __rdtsc anchor not found; the recompiler output changed")
    text = text.replace(PAUSE_MARK, PAUSE_DEF + PAUSE_MARK, 1)
    done.append("_mm_pause")

path.write_text(text)
print(f"{path}: " + (("fixed " + ", ".join(done)) if done else "already fixed"))
