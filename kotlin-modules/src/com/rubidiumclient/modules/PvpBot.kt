package com.rubidiumclient.modules

/**
 * ═══ PvP Bot (r27) — the system that decides where to move and when to hit ═══
 *
 * WHAT IT IS
 *
 * A per-tick decision loop that fights for you: it watches every nearby player
 * (position, velocity, health, i-frames), picks a movement pattern, holds a
 * fighting distance, attacks on cooldown, and keeps score of which movement
 * pattern actually lands hits in which situation. Losing patterns get replaced;
 * winning ones get reused. Everything it learns survives a restart.
 *
 * WHAT IT WAS TAUGHT (the priors — it is not a blank slate)
 *
 * 1. BACK OFF FIRST. Some people just charge straight at you, so every new
 *    engagement opens with a backpedal window (Opener ticks, default 15 = 0.75 s)
 *    before anything else. Its job is to make the first hit theirs to miss.
 * 2. KEEP A DISTANCE. Approach/retreat is always active and overrides style:
 *    inside Keep-min it backs off, outside Keep-max it closes. The movement
 *    patterns only choose *how* it travels inside that band.
 * 3. SIDEWAYS BEATS STRAIGHT. Strafing starts rated above charging, and pure
 *    advancing is rated below it — a target that walks straight forward is the
 *    easiest thing in the game to hit.
 * 4. DON'T HIT INTO I-FRAMES. Damage immunity is visible in the scan (the
 *    hurt flag), so by default it waits instead of wasting a swing.
 * 5. HIT ON COOLDOWN, NOT EVERY TICK. Attack cooldown defaults to 12 ticks
 *    (~0.6 s), the sword cadence, instead of spamming.
 *
 * HOW IT LEARNS
 *
 * Every attack is a small experiment. The situation is bucketed — distance band
 * x target moving/standing x my health band — and the movement pattern used is
 * scored for that bucket:
 *
 *   the target's health dropped within 12 ticks  → HIT,  pattern +1
 *   12 ticks with no damage on the target        → MISS, pattern −0.5
 *   I take damage while travelling with it        → soft penalty, pattern −0.25
 *
 * A miss streak of Fail-switch (default 3) forces a different pattern
 * immediately — that is the "if it fails to hit for a while, switch WASD
 * combination" rule. Otherwise it plays the best-rated pattern for the current
 * bucket, and deliberately tries under-sampled patterns first (each pattern
 * gets at least a few honest attempts in a bucket before its score counts),
 * so the scores describe reality instead of luck.
 *
 * ESCALATION
 *
 * If it starts taking non-stop hits — Escalate-hits damage events (default 3)
 * inside the 5 s window the native side tracks — it turns KillAura2 on by
 * itself, holds it for KillAura (default 5 s) after the pressure ends, and only
 * restores your previous setting if you asked it to (Restore). Getting
 * repeatedly hit is also the signal that the bot's spacing lost the fight, so
 * it is the right moment to stop being polite about aim.
 *
 * WHAT IT CANNOT DO YET (and says so instead of faking it)
 *
 * Rotation. The ActorRotationComponent is not derived, so the bot computes the
 * exact yaw/pitch it needs — shown live in the status line as the aim error —
 * but it cannot turn the player. Until that lands the bot fights with your
 * crosshair: keep the target roughly in front of you and it handles distance,
 * strafing, attack timing and escalation. `botRotationAvailable()` is the seam;
 * the moment the offsets exist the aim line starts applying with no other
 * change. It also cannot see walls (line-of-sight is not ported), and it moves
 * by writing velocity — see Move mode.
 */
class PvpBot : KotlinModule(
    name = "PvP Bot",
    category = "Combat",
    description = "Fights for you: spacing, strafes, attack timing, auto-KillAura",
) {

    // ── settings ──────────────────────────────────────────────────────────

    private val scanRange = float("Range", "Scan range (blocks)", 6.0f, 2f, 16f,
        "How far it looks for someone to fight. Beyond Attack range it closes first.")
    private val attackRange = float("AttackRange", "Attack range (blocks)", 3.0f, 1f, 6f,
        "Never swings past this. Vanilla reach is ~3.0 on 1.21.")
    private val keepMax = float("KeepMax", "Keep at most (blocks)", 3.2f, 1.5f, 6f,
        "Outside this it closes the gap, whatever pattern is running.")
    private val keepMin = float("KeepMin", "Keep at least (blocks)", 2.0f, 1f, 5f,
        "Inside this it backs off — too close is where trades hurt.")
    private val attackCooldown = int("Cooldown", "Attack cooldown (ticks)", 12, 4, 40,
        "12 ticks ≈ 0.6 s, the sword cadence. Spamming swings wastes hits.")
    private val openerTicks = int("Opener", "Backpedal opener (ticks)", 15, 0, 60,
        "Every new engagement opens by backing off — people charge straight in.")
    private val failSwitch = int("FailSwitch", "Switch pattern after N misses", 3, 1, 10,
        "Three misses in a row ends that WASD combination.")
    private val minTrials = int("MinTrials", "Honest tries per pattern", 3, 1, 10,
        "Each pattern is tried this often per situation before its score counts.")
    private val humanOverride = bool("HumanOverride", "Yield while you steer", true,
        "While you are moving by hand the bot stops steering and only decides attacks.")
    private val waitIFrames = bool("WaitIFrames", "Don't swing into i-frames", true,
        "A target damaged within 10 ticks cannot take another hit — save the swing.")
    private val autoKillAura = bool("AutoKillAura", "Escalate to KillAura2", true,
        "When it starts taking non-stop hits, turn KillAura2 on by itself.")
    private val escalateHits = int("EscalateHits", "Escalate after N hits", 3, 1, 10,
        "Damage events inside the native 5 s window that trigger the escalation.")
    private val killAuraHoldMs = int("KillAuraHold", "Hold KillAura (ms)", 5000, 500, 15000,
        "How long KillAura2 stays on after the pressure stops.")
    private val restoreKillAura = bool("Restore", "Turn it back off when calm", true,
        "Only if KillAura2 was off before the bot escalated.")
    private val showDebug = bool("Debug", "Show live state in the panel", true)
    private val resetLearning = bool("Reset", "Reset learning (toggles off)", false,
        "Flip on then off to erase every learned score.")

    private val targetMode = enum("Target", "Target", arrayOf("Nearest", "Lowest HP"), 0,
        "Which enemy to focus when several are in range.")

    private val targetFilter = enum("Targets", "Who to fight",
        arrayOf("Players", "Players+fallback", "Anything"), 1,
        "Players = only actors matching your own type id. Fallback = if that " +
            "matches nobody while other attackable actors are in range, fight " +
            "anyway and say so. Anything = ignore the filter.")

    // ── movement patterns ("WASD combinations") ───────────────────────────
    //
    // w/a/s/d are weights in the frame of the TARGET, not of the camera:
    // +w walks at them, +s away, +a/+d sideways. `jump` tries a critical hit,
    // `tap` releases movement for the attack tick (knockback reset).

    private class Combo(
        val id: String,
        val w: Float, val a: Float, val s: Float, val d: Float,
        val jump: Boolean = false,
        val tap: Boolean = false,
        val prior: Float,
    )

    private val combos = listOf(
        Combo("backpedal", 0f, 0f, 1f, 0f, prior = 1.0f),
        Combo("strafe-left", 0f, 1f, 0f, 0f, prior = 1.0f),
        Combo("strafe-right", 0f, 0f, 0f, 1f, prior = 1.0f),
        Combo("retreat-left", 0f, 1f, 1f, 0f, prior = 0.6f),
        Combo("retreat-right", 0f, 0f, 1f, 1f, prior = 0.6f),
        Combo("advance-left", 1f, 1f, 0f, 0f, prior = 0.5f),
        Combo("advance-right", 1f, 0f, 0f, 1f, prior = 0.5f),
        Combo("w-tap", 1f, 0f, 0f, 0f, tap = true, prior = 0.3f),
        Combo("jump-in", 1f, 0f, 0f, 0f, jump = true, prior = 0.2f),
        Combo("strafe-jump", 0f, 1f, 0f, 0f, jump = true, prior = 0.2f),
        Combo("advance", 1f, 0f, 0f, 0f, prior = -0.4f),
        Combo("hold", 0f, 0f, 0f, 0f, prior = -2.0f),
    )

    private class Bucket(val dist: Int, val moving: Int, val hp: Int) {
        override fun toString() = "d$dist" + (if (moving == 1) "m" else "s") + "h$hp"
    }

    // ── live state (game thread only) ─────────────────────────────────────

    private val scan = FloatArray(11 * 24)
    private val self = FloatArray(10)
    private val aim = FloatArray(2)
    private val inputDir = FloatArray(3)

    private var tick = 0L
    private var engaged = false
    private var openingUntil = 0L
    private var current: Combo? = null
    private var missStreak = 0
    private var lastAttackTick = -1000L

    /** The swing we are still waiting to classify: (tick, entity, hp at attack, combo, bucket). */
    private class Pending(
        val tick: Long,
        val entity: Int,
        val healthAtAttack: Int,
        val combo: String,
        val bucket: String,
    )
    private var pending: Pending? = null

    private var hits = 0
    private var misses = 0
    private var escalated = false
    private var escalatedAt = 0L
    private var killedAuraWasOn = false
    private var lastStatus = "idle"
    private val aimApplies: Boolean by lazy { NativeBridge.botRotationAvailable() }
    private var walkBlocked = false
    private var walkStatus = "walk: ?"
    private var diagLine = "scan: ?"
    private var lastDiagLogTick = -100000L
    private var walkStatusTick = -1000L

    // ── learning: bucket|combo -> score / trials ──────────────────────────
    private val scores = HashMap<String, Float>()
    private val trials = HashMap<String, Int>()
    private var loaded = false

    private fun prefs(): android.content.SharedPreferences? =
        ModuleRegistry.appPrefs("eclient_bot")

    private fun key(bucket: String, combo: String) = "$bucket|$combo"

    private fun loadLearning() {
        if (loaded) return
        loaded = true
        val p = prefs() ?: return
        for ((k, v) in p.all) {
            when {
                k.startsWith("s|") -> (v as? Float)?.let { scores[k.removePrefix("s|")] = it }
                k.startsWith("t|") -> (v as? Int)?.let { trials[k.removePrefix("t|")] = it }
                k == "hits" -> hits = (v as? Int) ?: 0
                k == "misses" -> misses = (v as? Int) ?: 0
            }
        }
    }

    private fun saveLearning() {
        val p = prefs() ?: return
        val e = p.edit()
        e.clear()
        for ((k, v) in scores) e.putFloat("s|$k", v)
        for ((k, v) in trials) e.putInt("t|$k", v)
        e.putInt("hits", hits)
        e.putInt("misses", misses)
        e.apply()
    }

    private fun clearLearning() {
        scores.clear()
        trials.clear()
        hits = 0
        misses = 0
        saveLearning()
        log("learning reset")
    }

    private fun scoreOf(bucket: String, c: Combo): Float =
        scores[key(bucket, c.id)] ?: c.prior

    private fun trialsOf(bucket: String, c: Combo): Int =
        trials[key(bucket, c.id)] ?: 0

    // ── lifecycle ─────────────────────────────────────────────────────────

    override fun onEnable() {
        loadLearning()
        if (resetLearning.value) {
            resetLearning.value = false
            clearLearning()
        }
        tick = 0
        engaged = false
        pending = null
        missStreak = 0
        hits = 0
        misses = 0
        escalated = false
        current = null
        log("bot scan: " + (try { NativeBridge.botScanDiag() } catch (t: Throwable) { "?" }))
        log("bot online: " + (if (aimApplies) "aiming" else
            "aim is MANUAL (rotation component not derived) — keep them in front of you"))
    }

    override fun onDisable() {
        saveLearning()
        pending = null
        current = null
        engaged = false
        if (escalated && restoreKillAura.value) {
            NativeBridge.nativeSetModuleEnabled("KillAura2", killedAuraWasOn).isEmpty()
            escalated = false
            log("bot offline: KillAura2 restored to $killedAuraWasOn")
        } else {
            log("bot offline: hits=$hits misses=$misses combos=${scores.size} entries")
        }
    }

    override fun statusHint(): String? {
        if (!showDebug.value) return if (isEnabled) lastStatus else null
        return lastStatus
    }

    // ── the loop (20 Hz, on the game thread, inside the world-ready gate) ──

    override fun onTick() {
        ++tick

        if (resetLearning.value) { resetLearning.value = false; clearLearning() }
        if (!NativeBridge.botSelf(self)) { lastStatus = "no data yet"; return }

        val myHealth = self[6].toInt()
        val selfHurt = self[7] > 0.5f
        val hurtIn5s = self[8].toInt()

        // 1) ESCALATION FIRST — if it is being mauled, that fact must not wait
        //    for the rest of the tick's logic to succeed.
        if (autoKillAura.value) checkEscalation(hurtIn5s)

        // 2) WHO IS THERE — one scan per tick; fast decisions come from data
        //    that is current, not from a stale list.
        val mode = when (targetFilter.value) {
            "Players" -> 0
            "Anything" -> 2
            else -> 1
        }
        val n = try {
            NativeBridge.botScan(scan, scanRange.value, mode)
        } catch (t: Throwable) {
            lastStatus = "scan failed: $t"
            return
        }
        if (n <= 0) {
            // Nobody worth fighting: the bot must be completely hands-off.
            // No movement, no swings — standing still is the safe state.
            if (engaged) log("disengaged (no targets)")
            engaged = false
            pending = null
            current = null
            missStreak = 0
            // ...but NEVER silently. The funnel says which stage emptied the
            // list, so "the bot does nothing" always has a specific reason.
            if (tick - walkStatusTick >= 0) {
                diagLine = try { NativeBridge.botScanDiag() } catch (t: Throwable) { "scan: ?" }
            }
            lastStatus = if (n < 0) "world not ready\n$diagLine" else "no targets — $diagLine"
            return
        }

        // 3) RESOLVE THE LAST SWING BEFORE CHOOSING THE NEXT ONE.
        resolvePending(n, myHealth, selfHurt)

        val idx = chooseTarget(n)
        val dist = scan[idx * 11 + 6]
        val tx = scan[idx * 11 + 0]
        val ty = scan[idx * 11 + 1]
        val tz = scan[idx * 11 + 2]
        val tvx = scan[idx * 11 + 3]
        val tvz = scan[idx * 11 + 5]
        val tHealth = scan[idx * 11 + 7].toInt()
        val tFlags = scan[idx * 11 + 9].toInt()
        val tHurt = (tFlags and 1) != 0
        val entity = entityIdAt(idx)

        if (!engaged) {
            engaged = true
            openingUntil = tick + openerTicks.value
            log("engage entity=$entity d=${"%.2f".format(dist)} hp=$tHealth" +
                " (opener: backpedal ${openerTicks.value}t)")
        }

        // Movement availability is a native fact that only changes when the
        // offsets do; sampling it 1x/s keeps the panel honest without paying a
        // JNI string call every tick.
        if (tick - walkStatusTick >= 20) {
            walkStatusTick = tick
            walkStatus = try { NativeBridge.botWalkStatus() } catch (t: Throwable) { "walk: ?" }
            walkBlocked = false
            diagLine = try { NativeBridge.botScanDiag() } catch (t: Throwable) { "scan: ?" }
        }

        // 4) PATTERN SELECTION — opener first, then forced switch on a miss
        //    streak, then best-rated for this situation.
        val bucket = bucketOf(dist, tvx, tvz, myHealth)
        val c = selectCombo(bucket)

        // 5) MOVE — distance control always wins over style, and the human
        //    steering by hand always wins over both.
        val attacking = shouldAttack(dist, tHurt)
        moveStep(c, tx, ty, tz, dist, attacking)

        // 6) AIM — computed every tick; applied only when the rotation
        //    component exists (it does not, yet). The status line shows the
        //    error so the player knows what to fix by looking.
        var aimErr = -1f
        if (NativeBridge.botAim(idx, aim)) aimErr = aim[0]

        // 7) ATTACK on cooldown, then remember the swing for classification.
        if (attacking) {
            if (NativeBridge.botAttack(idx)) {
                lastAttackTick = tick
                pending = Pending(tick, entity, tHealth, c.id, bucket.toString())
            }
        }

        lastStatus = buildStatus(c, bucket, dist, myHealth, tHealth, aimErr, hurtIn5s)
    }

    // ── the rules ─────────────────────────────────────────────────────────

    private fun bucketOf(dist: Float, tvx: Float, tvz: Float, myHealth: Int): Bucket {
        val d = when {
            dist < 2.0f -> 0
            dist < 3.5f -> 1
            else -> 2
        }
        val moving = if (tvx * tvx + tvz * tvz > 0.005f) 1 else 0
        val hp = when {
            myHealth <= 8 -> 0
            myHealth <= 16 -> 1
            else -> 2
        }
        return Bucket(d, moving, hp)
    }

    /**
     * Entity id of scan slot `i`. The native side copies the u32 straight into
     * the float slot (bit-exact), so reading the bits back is the whole decode.
     * floatToRawIntBits — NOT Kotlin's toBits(), which canonicalises NaN and
     * would silently corrupt an id whose bit pattern looks like one.
     */
    private fun entityIdAt(i: Int): Int =
        java.lang.Float.floatToRawIntBits(scan[i * 11 + 10])

    private fun chooseTarget(n: Int): Int {
        var best = 0
        if (targetMode.value == "Lowest HP") {
            for (i in 1 until n) if (scan[i * 11 + 7] < scan[best * 11 + 7]) best = i
        } else {
            // The scan already arrives nearest-first; keep the first that is
            // not mid-invulnerability, else just the nearest.
            best = 0
            for (i in 0 until n) if ((scan[i * 11 + 9].toInt() and 1) == 0) { best = i; break }
        }
        return best
    }

    private fun selectCombo(bucket: Bucket): Combo {
        // Opener: every engagement starts by giving ground.
        if (tick < openingUntil) return combos.first { it.id == "backpedal" }

        // Forced switch: N misses in a row ends that pattern, right now.
        if (missStreak >= failSwitch.value) {
            val prev = current?.id
            val alt = combos
                .filter { it.id != prev && it.id != "hold" }
                .minByOrNull { trialsOf(bucket.toString(), it) } ?: combos[1]
            if (alt.id != prev) {
                log("switch after ${missStreak} misses: $prev → ${alt.id} (${bucket})")
                missStreak = 0
            }
            current = alt
            return alt
        }

        val b = bucket.toString()
        // Systematic exploration first: a pattern with fewer than MinTrials in
        // this situation is tried before any score is trusted.
        val untried = combos.filter { it.id != "hold" && trialsOf(b, it) < minTrials.value }
        if (untried.isNotEmpty()) {
            val pick = untried.minByOrNull { trialsOf(b, it) } ?: untried.first()
            current = pick
            return pick
        }

        val best = combos.filter { it.id != "hold" }.maxByOrNull { scoreOf(b, it) } ?: combos[1]
        current = best
        return best
    }

    private fun shouldAttack(dist: Float, targetHurt: Boolean): Boolean {
        if (dist > attackRange.value) return false
        if (tick - lastAttackTick < attackCooldown.value) return false
        if (waitIFrames.value && targetHurt) return false
        if (pending != null && tick - pending!!.tick < 12) return false // one swing at a time
        return true
    }

    private fun resolvePending(n: Int, myHealth: Int, selfHurt: Boolean) {
        val p = pending ?: return
        var landed = false
        var gone = true
        for (i in 0 until n) {
            if (entityIdAt(i) != p.entity) continue
            gone = false
            val hp = scan[i * 11 + 7].toInt()
            val hurtFlag = (scan[i * 11 + 9].toInt() and 1) != 0
            if (hp < p.healthAtAttack || hurtFlag) landed = true
            break
        }
        if (landed) {
            hits++
            addScore(p.bucket, p.combo, +1f)
            missStreak = 0
            pending = null
        } else if (gone || tick - p.tick >= 12) {
            misses++
            addScore(p.bucket, p.combo, -0.5f)
            missStreak++
            pending = null
        }
        // Taking damage while travelling with a pattern means it is not
        // defending us — that is true whether or not the swing landed.
        if (selfHurt) {
            addScore(p.bucket, p.combo, -0.25f)
            if (missStreak < failSwitch.value) missStreak++
        }
        if (myHealth <= 6 && missStreak < failSwitch.value) missStreak = failSwitch.value
    }

    private fun addScore(bucket: String, combo: String, delta: Float) {
        val k = key(bucket, combo)
        scores[k] = (scores[k] ?: (combos.firstOrNull { it.id == combo }?.prior ?: 0f)) + delta
        trials[k] = (trials[k] ?: 0) + (if (delta > 0f) 1 else 0) + (if (delta < 0f) 1 else 0)
        if ((hits + misses) % 10 == 0) saveLearning()
    }

    private fun moveStep(c: Combo, tx: Float, ty: Float, tz: Float, dist: Float, attackingNow: Boolean) {
        val dx = tx - self[0]
        val dz = tz - self[2]
        val len = kotlin.math.sqrt(dx * dx + dz * dz)
        if (len < 1e-3f) return
        val fx = dx / len
        val fz = dz / len
        val rx = -fz
        val rz = fx

        var mx = fx * (c.w - c.s) + rx * (c.d - c.a)
        var mz = fz * (c.w - c.s) + rz * (c.d - c.a)

        // Distance control beats style: too close backs off, too far closes.
        val err = when {
            dist > keepMax.value -> 1f
            dist < keepMin.value -> -1f
            else -> 0f
        }
        mx += fx * err
        mz += fz * err

        // A tap pattern releases movement on the attacking tick (knockback reset).
        val jump = c.jump && dist < attackRange.value + 0.5f
        if (c.tap && attackingNow) { mx = 0f; mz = 0f }

        if (mx * mx + mz * mz < 1e-4f) return // nothing to do — hands off

        if (humanOverride.value && NativeBridge.getWorldInputDir(inputDir)) {
            // The player is steering by hand: the bot yields the controls and
            // keeps doing what it is good at (timing the swing).
            return
        }
        // WALKING, not flying: this writes the joystick's own input state and
        // the engine moves the player exactly as it moves you. It returns false
        // while the input element layout is underived — then the bot stands
        // still rather than moving badly, and the status line says why.
        if (!NativeBridge.botWalk(mx, mz, jump, false)) walkBlocked = true
    }

    private fun checkEscalation(hurtIn5s: Int) {
        if (!escalated && hurtIn5s >= escalateHits.value) {
            killedAuraWasOn = NativeBridge.nativeModuleEnabled("KillAura2")
            if (!killedAuraWasOn) {
                val ok = NativeBridge.nativeSetModuleEnabled("KillAura2", true).isEmpty()
                log("ESCALATE: $hurtIn5s hits in 5 s → KillAura2 ${if (ok) "on" else "refused"}")
            }
            escalated = true
            escalatedAt = tick
            return
        }
        if (escalated) {
            val calm = hurtIn5s == 0
            val held = tick - escalatedAt > killAuraHoldMs.value / 50 // ms → ticks
            if (calm && held) {
                if (restoreKillAura.value && !killedAuraWasOn) {
                    NativeBridge.nativeSetModuleEnabled("KillAura2", false).isEmpty()
                }
                log("ESCALATE: calm again → KillAura2 handed back")
                escalated = false
            }
        }
    }

    private fun buildStatus(
        c: Combo, bucket: Bucket, dist: Float, myHealth: Int, tHealth: Int,
        aimErr: Float, hurtIn5s: Int,
    ): String {
        val total = hits + misses
        val acc = if (total > 0) (100 * hits / total) else 0
        val aim = if (aimApplies) "aim ${"%.0f".format(aimErr)}°"
                  else "aim manual (Δ${"%.0f".format(if (aimErr < 0) 0f else aimErr)}°)"
        val move = if (walkBlocked) "STANDING ($walkStatus)" else "walking"
        // A silent fallback would hide the fact that the player filter matched
        // nobody, so it is reported every time it is active.
        val fb = if (diagLine.contains("fallback=YES")) "\n⚠ target filter fell back: $diagLine" else ""
        return "FIGHT d=${"%.1f".format(dist)} me=$myHealth them=$tHealth " +
            "$bucket ${c.id}\n" +
            "hits $hits/$total ($acc%) miss-streak=$missStreak | $move | " +
            (if (escalated) "KillAura ENGAGED ($hurtIn5s/5s)" else aim) + fb
    }
}
