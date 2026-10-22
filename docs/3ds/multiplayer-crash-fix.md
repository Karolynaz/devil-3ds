# Multiplayer character-selection crash

## Diagnosis

The 2026-10-05 test build (8d8bae1) crashes after choosing a multiplayer
character. The diagnostic rebuild d95bfd9 retains the same game source and
symbol addresses. In its ELF, `__stacksize__` is 32768 bytes. The compiled
`protocol_zt::recv_from_udp()` reserves 65536 + 92 bytes plus saved registers;
`recv_peer()` also has a 65536-byte local receive buffer.

The character-selection transition enters `UiSelectGame()`, which immediately
polls the public-game list. `base_protocol::get_gamelist()` calls receive even
before asynchronous ZeroTier setup creates the listening sockets. This reaches
the oversized UDP frame on the main UI thread.

The reported LR 0x005ABC34 is `__getreent()`'s return instruction. PC is zero
and the stack dump is unreadable. This is consistent with the receive frame
extending beyond the main stack, rather than a renderer failure.

## Changes

- TCP and UDP share one heap-owned 65536-byte buffer; packet limits are unchanged.
- Lobby receive polling waits until both listening sockets exist.
- TCP EOF stops reception instead of repeatedly queueing empty packets.
- CI preserves the unstripped ELF for future address diagnosis and fails if
  that artifact is missing.

## Verification

`python3 tools/tests/test_zerotier_receive.py` compiles the production receive
functions with a 4096-byte stack-frame limit and Address/UndefinedBehavior
Sanitizers. It checks early lobby polling, full-size TCP/UDP payloads, buffer
reuse, and TCP EOF. The original functions fail the frame-size check; the
updated functions pass. The console build and package verification run in CI.

No emulator or console was launched. Retest Multiplayer -> existing character,
return/cancel/reopen, and public-game creation/joining with Wi-Fi enabled. Check
single player as well. This is a test build, not a GitHub Release.
