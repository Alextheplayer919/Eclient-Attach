# Apollon v4.62 (1.21.111) — recovered source, and what we take from it

**Where this came from.** The `Apolon` release on `Alextheplayer919/Eclient-Attach`
carries `Apollon_Client_ImGui_v4.62_1.21.111.1.zip` (4,297,394 bytes,
sha256 `593318306a96ae90…`). Earlier in the project that release had **no assets**
and the only link was an expired user-attachment URL — the file has since
reappeared, so its contents are recorded here as text. **This document is the
durable copy: the zip is minutes away from being 404 again.**

Unpacked, the interesting parts are:

| path | what it is |
|---|---|
| `app/src/main/jni/offsets.h` | 319 lines: `Offsets64`, `Fields64` (aarch64), `Offsets`/`Fields` (v7a), `Offsets86_64` — **mtime 2026-01-09, months newer than the rest of the tree** |
| `app/src/main/jni/hook_classes.h` | the class wrappers: exact signatures for every function it calls |
| `app/src/main/jni/main.cpp` | the hooks, the hex patches (`MemoryPatch::createWithHex`), the module logic |
| `app/src/main/jni/variables.h` | the feature list + config keys (feature names = a usable map of what exists) |

This is the **same table** our profile was built from (`profile/Offsets_1_21_111.h`
§ "APOLLON v4.62"): every symbol we share agrees bit-for-bit, and the two entry
bytes we independently derived match byte-for-byte:

```
FullBright    Apollon: 88 56 A8 52 00 01 27 1E C0 03 5F D6   (FMOV S0,W8 ; RET — W8 = 0x42B40000 = 90.0f)
              ours:    88 56 A8 52 00 01 27 1E C0 03 5F D6   ✅ identical
AntiKnockback Apollon: LerpMotion + "C0 03 5F D6"  (RET — the whole function)
              ours:    LerpMotion + RET               ✅ identical
```

Confirmation matters because it was the one open question about our patch layer:
Apollon's AntiKnockback **is** `Actor::lerpMotion` returning immediately, and this
is a client that people actually run against real servers. Also note the
consequence: **with AntiKnockback on, `lerpMotion` is dead — no velocity write
of any kind can work while it is enabled.**

## How Apollon resolves a symbol

```cpp
getAbsoluteAddress(MineLib, Offsets64::SetYHeadRot)   // == base + offset
```

Exactly our model: the numbers are file offsets into `libminecraftpe.so`, added
to the load base, with no PIE indirection. Our `fn()` does the same thing.

## Method signatures learned from `hook_classes.h`

These are the ones that were ambiguous in our notes:

```cpp
void* setNoJumpDelay(int EntityContext, int a3)       // MobJump::setNoJumpDelay
void* setOnGround   (int EntityContext, bool a3)      // ActorEntityAccess::addOrRemoveComponent<OnGroundFlagComponent>
void* setIsJumping  (int EntityContext, bool a3)      // ActorEntityAccess::addOrRemoveComponent<FlagComponent<MobIsJumpingFlag>>
void  setSprinting  (bool)
void  displayClientMessage(const std::string&, const std::string&)   // player-visible chat line
vec2_t* getAABBDim()                                   // {width, height} — the live hitbox
vec3_t* getPosition()
LayeredAbilities* getAbilities();  LayeredAbilities* getLayer(int);  void setAbility(int index, bool)
```

Note `setIsJumping` is the `MobIsJumping` address and takes **an entity context
int**, not a bare bool: `setIsJumping(0, true)`. Our r40 abilities work used the
same underlying address, so this only pins the argument order.

## Fly, as a real client implements it

`main.cpp` (in the `LocalPlayer::normalTick` hook) does, every tick:

```cpp
if (creativeFly)     thiz->getAbilities()->getLayer(2)->setAbility(10, true);   // MAY_FLY
if (forceCreativeFly) thiz->getAbilities()->getLayer(2)->setAbility(9,  true);  // FLYING  (airborne now)
if (ladderFly)       thiz->setClimbing(true);
if (strafe)          EntityAccess->setOnGround(1, true); EntityAccess->setIsJumping(0, true);
if (noJumpDelay)     EntityAccess->setNoJumpDelay(1, 0);
if (alwaysSprinting) thiz->setSprinting(true);
if (step)            thiz->initializeMaxAutoStep(1.0625);   // step up a full block
else                 thiz->initializeMaxAutoStep(0.5625);   // vanilla
```

**Our r40 Fly is the same mechanism, driven the same way** — `setAbility(10)` for
Creative, `setAbility(9)` for Force, `setClimbing` for Ladder, on layer 2. There is
no secret third fly in this client that we are missing: the two mechanisms we
tried and deleted (velocity write, position write) are not here either. What is
here, and what we do not have, is the **combination**: they re-assert the ability
*and* the step height *and* the sprint flag on **every single tick**, inside the
player's own tick hook, not just on the tick the module was toggled.

Apollon's extra flight option, `damageFly`, is not an ability at all:

```cpp
if (damageFly) { myGameMode->attack(thiz, true); myGameMode->attack(thiz, true); thiz->swing(); }
```

Attacking yourself twice and swinging — knockback you inflict on yourself. It is
gated behind the AntiKnockback patch being *off*, obviously.

Also worth recording: every one of those blocks sits inside `if (!isLANGame)`,
where the LAN flag comes from `InLanGame1/InLanGame2` (NetherNet send/read packet)
— they disable movement features in LAN worlds.

## Symbols we did not have (now in the header)

44 entries in `Offsets64` were new to us. They are now in
`profile/Offsets_1_21_111.h` under "RECOVERED APOLLON v4.62 — REFERENCE" together
with a one-line purpose each. The ones that matter for the roadmap:

| symbol | address | why it matters |
|---|---|---|
| `DisplayMessage` | `0x06BF196C` | `LocalPlayer::displayClientMessage` — **write a line of chat to the player.** On-device debugging without adb: "fly: ability set (layer 2, idx 10)". Also the natural way to report a refused toggle from inside the game. |
| `GetAABBDim` | `0x0C93AE58` | live `{width,height}` per actor — the real hitbox, already in our header, used by their anti-bot filter (`y > 2.1f` → skip). |
| `BlockReach` | `0x0CCC4B70` | returns 7.0f/12.0f — the gamemode's reach. Their HitBox module works off this. |
| `NativeKeyHandler` | `0x0CFA5130` | patched to `RET` — they replace the JNI key handler entirely (they own input, hooks @ `0x0CFA5130`). Our input layer could do the same. |
| `ClientInstanceOnTick` | `0x06478734` | their top-level tick hook — a second hook point for us if `LocalPlayer::normalTick` is ever too late in the frame. |
| `PlayerAuthInput` | `0x0A772168` | commented "Player's Head rotation" — a possible **input-side** rotation path (what the client *sends*), distinct from the `+0x218` state vector we write. |
| `HandleDestroyOrAttack` | `0x06462B60` | `ClientInputCallbacks::handleDestroyOrAttackButtonPress` — the exact function a finger triggers; the cleanest anchor if we ever need to know "did the human just attack". |
| `GetNameTag`, `ScreenName`, `InMainMenu`, `GetGameplayUserManager`, `ForEachActivePlayer` | — | roster/UI context (who is on the server, which screen are we on). |
| `GetCameraPos`, `ViewMatrix`, `FovX/FovY`, `LevelRendererPlayer` | — | screen-space maths: world → screen for anything that draws. |

## What Apollon does *not* have

Searched the whole tree: **no `MoveInput`, no `UsingItem`, no velocity setter
(`setVelocity`/`lerpMotion` as a write path), no enTT component hashes.** Their
movement is `setOnGround`/`setIsJumping`/`setNoJumpDelay`/`initializeMaxAutoStep`
plus the two ability flags. So:

* our zero `MoveInput*` / `UsingItemFlag*` entries stay zero — nobody has published
  them for 1.21.111 and they are still ours to derive;
* the input-based walking plan (`botWalk`) remains the only published-shaped route,
  satisfied through `setOnGround`/`setIsJumping`/`setNoJumpDelay` rather than
  through a `MoveInput` struct we do not have;
* **`Hitbox` stays an enTT-side problem** (`0x9b97c2b2`), not an offset-table one —
  Apollon reads the hitbox, it never *writes* one.

## Their full patch list, for comparison

Every `createWithHex` in the aarch64 branch, with our equivalent:

| Apollon patch | address | bytes | ours |
|---|---|---|---|
| `AntiKnockBack` | `LerpMotion` | `C0 03 5F D6` | ✅ same (spec `AntiKnockback`) |
| `FullBright` | `0x06C6B95C` | `88 56 A8 52 00 01 27 1E C0 03 5F D6` | ✅ same bytes |
| `NoHurtCam` | `0x05103870` | `C0 03 5F D6` | ✅ same |
| `NoCamDistortion` | `0x0DF56A14` | `C0 03 5F D6` | ✅ same |
| `NoBoatRotation` | `0x05114BB8` | `C0 03 5F D6` | ✅ same |
| `NoCamSleep` | `0x0DF57550` | `C0 03 5F D6` | ✅ same |
| `PlaceCamera` | `0x0E1DC87C` | `C0 03 5F D6` | ✅ same |
| `SlowDownTriggers` | `0x0954C0E4` | `C0 03 5F D6` | ✅ same |
| `NoSlowDown` | `0x0945D230` | `C0 03 5F D6` | ✅ same |
| `WaterDrown` | `0x09DB7558` | `C0 03 5F D6` | ✅ same (spec `NoWaterDrown`) |
| `LavaDrown` | `0x09DB9528` | `C0 03 5F D6` | ✅ same (spec `NoLavaDrown`) |
| `Noclip` | `0x093D2760` | `C0 03 5F D6` | ✅ same |
| `NoCaveVignette` | `VignetteRenderer` `0x06376084` | `C0 03 5F D6` | ✅ same |
| `AutoClickMine` | `0x0CCC2258` | `C0 03 5F D6` | ✅ same |
| `NativeKeyHandler` | `0x0CFA5130` | `C0 03 5F D6` | — we hook input instead of replacing it |

Fifteen sites, fifteen agreements: **there is no wrong byte patch left in our
profile.** Their file also carries `Font0..Font10` (eleven 4-byte writes of the
same float at `Font` `0x0263C304`, i.e. a UI font-scale slider) which we do not
need.

Their remaining modules are hooks, not patches — `SpeedHack`, `JumpHack`,
`WaterSpeed`, `LavaSpeed`, `Gravity`, `DropItemTime`(=`FastDrop`),
`LevelRendererCamera::FogState`, `ClientInstance::onTick`, `BaseActorRenderer::renderText`
— all of which our profile already carries as `inline constexpr` addresses for the
day we want them.
