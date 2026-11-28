# HvH bot: item-use investigation (ARM64 1.21.111)

Status: **research only**. The HvH bot is not yet implemented. Do not call these
candidates on a player until the actual arguments, return convention and hold
behavior have been checked on the pinned binary and with a controlled device
trace. Decompilation and an analogous header are not proof of either ABI or
server acceptance.

## Inputs and evidence

* The original complete `INTERNALCPP_OFFSETS_1.21.111.1.7z` in this repository
  extracts to valid offsets JSON with library SHA-256
  `65b9a43178a8b2cde19b6e000613b1453f8f2ea964b094faaf6e206ef041fed1`.
  The later `.7z.txt` upload is a valid 7z containing a truncated JSON prefix;
  it is not a replacement for the complete archive.
* [LeviLamina's GameMode header](https://github.com/LiteLDev/LeviLamina/blob/922ff2e83cd3bccae56970d325cc3669344d8117/src/mc/world/gamemode/GameMode.h)
  describes a **different, newer version and platform**, so is only a hypothesis.
  Its order, with the two Itanium destructor slots counted separately, maps
  `useItem`, `useItemAsAttack`, `useItemOn`, `interact`, `attack`,
  `releaseUsingItem` to slots 12 through 17 respectively. Its signatures and
  layouts must not be copied blindly into the 1.21.111 Android build.
* On the pinned library the extracted GameMode slots are 12 = `0xccc4438`,
  13 = `0xccc4a88`, 14 = `0xccc3650`, 15 = `0xccc015c`, 16 = `0xccc0754`,
  17 = `0xccc4c38`. Slot 14 agrees with the existing use-on hook, and slot 16
  wraps the existing `_attack` entry at `0xccc075c`. This lends weight to the
  *ordering* hypothesis, not to unverified calling conventions.
* Targeted Ghidra [run 46141163789](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/46141163789)
  decompiled ten candidate functions without a whole-program analysis. Slot 12
  accepts a second pointer and appears to construct and copy item-stack-like
  objects; slot 17 has one visible argument and constructs a substantial
  object before calling a player virtual method. Both are consistent with the
  above leads but not definitive. Slot 12 has PIC/control-flow warnings, so
  apparent missing arguments and return types are particularly unreliable.
  Ghidra displays rebased addresses; the ELF addresses above are authoritative.
  Annotation excerpts are truncated; the full report was published as the run
  artifact. The separately requested quick-command address was not in the
  printed result, so this run does **not** validate that command call.
* Gatyo's existing apples-eating gate notices `useItemOn` and applies a 30-tick
  heuristic; its own comments say item use duration was not verified. It is
  not an implementation of drinking/eating. Gatyo's Auto Hotbar source
  identifies potion aux values 31/32/33 as strength variants, but verify the
  desired Strength II item on-device and do not rely on item name alone.

## Implementation/verification plan

1. On a local test world, observe genuine manual apple use and Strength II
   drinking. Trace the slot-12/17 candidates without changing calls, record
   the selected item, incoming argument registers, timestamps and stack count
   changes; compare a normal completion with interruption. Validate the exact
   function boundaries, argument/return ABI and hook forwarding before a call.
2. Locate/verify the player's actual using-item state and buff duration. A
   single `useItem` call might be immediately released by normal input; any
   held-input approach must integrate with the game's input lifecycle and
   release on cancellation. Stack consumption alone is not a server-confirmed
   effect; watch inventory replacement/decrement and effect updates separately.
3. Bot control: use Bypass Fly's `FlyInput` and Gatyo KillAura target filters,
   not an unrelated velocity writer fighting the fly module. A target must be
   reacquired on game ticks, never retained as a dereferenced raw pointer.
   Pause KillAura and competing slot-switch modules during consumption. Only
   return to combat after observed completion; on missing supplies, rejected
   use, correction, target loss, panic, death or timeout, stop safely and
   report the state. Restore only toggles/slots the bot itself changed.
4. Test targeting, retreat hysteresis, target loss, timeouts and restore behavior
   on host first. Then make one instrumented APK and test in a private world;
   server validation and anti-cheat behavior remain separate.

## Experimental Auto Use module

The separate Combat → Auto Use module is **off by default** and is not the HvH
bot. Enable it, then use a one-shot test button or the individual automatic
settings. The experimental routine looks only in the hotbar for an enchanted
golden apple or a regular `potion` with aux 33 (Gatyo's Strength II lead),
switches to it and sends the existing equipment packet, then calls the slot-12
candidate on the next game tick with a hypothesized main-hand argument. It
watches for a stack decrease or item replacement for 3.5 seconds. Timeout stops
automatic retries until the module is toggled off and on. On timeout/cancel it
also calls the unverified slot-17 release candidate. A manual slot change cancels
without overwriting the player's new selection. Gatyo KillAura, CPVP and the
built-in aura/crystal loops pause during the attempt; Auto Totem remains active.
Disable other item-switch modules. No genuine held input is simulated yet, so
this may not finish eating/drinking. Inventory changes are not proof of a
server-applied effect; strength refresh currently uses a timer, not effect
inspection. If the world becomes unavailable during an attempt, the state is
cleared without touching stale game pointers, so the selected slot might not be
restored. Both guessed calls may crash or do nothing; use a backed-up private
world and inspect Console before enabling automatic modes.

## Survival test follow-up

User reports enchanted apple attempts in Survival timed out, with a drinkable
Strength II potion also in the hotbar but not matching the aux-33 filter. This
is not confirmed working. An [October 2025 LeviLamina GameMode header](https://github.com/LiteLDev/LeviLamina/blob/7a63812d0affa642bc3adbfcb2ce85c73308b381/src-server/mc/world/gamemode/GameMode.h)
indicates `bool useItem(ItemStack&)`; Auto Use now records the candidate's
return value, which is only diagnostic and not proof of consumption.

[Targeted Ghidra run 46142962360](https://github.com/Alextheplayer919/Eclient-Attach/actions/runs/46142962360)
identified LocalPlayer slot 151 at `0xce0af1c` as an integer ECS-backed getter,
matching the historical `getItemUseDuration` slot ordering (Windows header's
150 plus the extra Itanium destructor slot). Slots 152/153 look like float
progress-related functions. These are still inferences, not verified semantics.
The Auto Use page adds an opt-in duration trace, only calling slot 151 when its
actual vtable pointer matches that exact address. It also logs the nine hotbar
item names/aux/counts when explicitly requested, to investigate the potion mismatch.
No held-input simulation has been implemented; the experiment may still fail.
