# Development History

This file replaces the many per-version root-level findings/checklist documents that were useful during bring-up but made the public repository difficult to navigate.

## Milestones

| Version | Milestone |
|---|---|
| v0.1 | Initial strict NTSC-U Forbidden Memories recompilation foundation and verified input pipeline. |
| v0.1.1 | Corrected BIOS verification to require the Sony SCPH-1001 ROM instead of accidentally accepting OpenBIOS. |
| v0.1.2 | Added the proven return-continuation entry at `0x80049000`. |
| v0.1.3 | Added the proven interrupt callback entry at `0x80012CD4`. |
| v0.1.4 | Reached the Konami-logo CD initialization path and added controller telemetry/diagnostic timing controls. |
| v0.1.5 | Isolated the ReadTOC timing boundary and added an opt-in ReadTOC delay override. |
| v0.1.6 | Tested combined CD startup timing adjustments. |
| v0.1.7 | Added the pending-command pipeline experiment so delayed drive work can continue while earlier responses wait. |
| v0.1.8 / v0.1.8.1 | Added the Forbidden Memories APv2 controller support/signature-gated bypass and corrected a build guard. |
| v0.1.9 | Added the intro MDEC-out DMA callback seed at `0x8005C1F4`. |
| v0.1.10 | Added the first 3D scene handler veneer at `0x80033DB0`; this baseline was verified through normal gameplay and a complete NPC duel. |
| v0.1.11 | Added PlayStation SPU voice/main volume-sweep modeling to address missing short sound effects. Unit and structural validation pass; broad runtime audio confirmation is pending. |

## Runtime-proven static entries

The build currently guards these game entries because they were reached through continuations, callbacks or indirect command streams that the original direct-call-oriented discovery did not expose as standalone entries:

```text
0x80049000  return continuation
0x80012CD4  interrupt callback
0x8005C1F4  intro MDEC-out DMA callback
0x80033DB0  first 3D scene handler veneer
```

Strict unknown-dispatch fail-fast remains intentional. New missing targets should become visible diagnostics rather than being broadly skipped.

## Current audio focus

Forbidden Memories writes PlayStation SPU volume-sweep control values for short sound effects. Before v0.1.11, those sweep control values could be interpreted as very small fixed gains, producing missing or inconsistent menu, dialog, 3D-navigation, entry and duel-interface sounds while XA/CD music continued normally.

v0.1.11 introduces per-voice and main sweep state, preserves fixed-volume behavior, clocks the sweep at the SPU sample cadence, and exposes sweep telemetry in runtime reports. `scripts/test-spu-sweep.sh` is the local regression check.

## Framework basis

The project was created against a PSXRecomp snapshot identified during bring-up as commit:

```text
7ae10f2986803398f8d557771b43008bdf3ad1e9
```

The framework source is bundled in `psxrecomp/` for this source package. Its own documentation remains available under that directory.
