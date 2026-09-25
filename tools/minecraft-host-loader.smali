.class public Lcom/rubidiumclient/host/EClientLoaderProvider;
.super Landroid/content/ContentProvider;
.implements Landroid/app/Application$ActivityLifecycleCallbacks;
.source "EClientLoaderProvider.java"

# -----------------------------------------------------------------------
# Attach vector (unchanged): the class initializer loads the native host
# before Minecraft's Application.onCreate() runs.
# -----------------------------------------------------------------------

.method public static constructor <clinit>()V
    .locals 1
    const-string v0, "eclient_host"
    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V
    return-void
.end method

.method public constructor <init>()V
    .locals 0
    invoke-direct {p0}, Landroid/content/ContentProvider;-><init>()V
    return-void
.end method

# -----------------------------------------------------------------------
# Native entry points, resolved BY NAME from libeclient_host.so.
# Since r25 the decor-view touch listener is no longer installed (the Views
# overlay owns its own windows), so nativeGuiTouch is retained only to keep the
# touch ABI and the patcher's listener checks intact. nativeTouchReady IS live:
# it is the KotlinBridge bind point.
# -----------------------------------------------------------------------

.method public static native nativeGuiTouch(IIFF)Z
.end method

.method public static native nativeTouchReady()V
.end method

# -----------------------------------------------------------------------
# ContentProvider.onCreate: register activity lifecycle callbacks so the
# touch listener can be installed on every resumed activity window.
# -----------------------------------------------------------------------

.method public onCreate()Z
    .locals 2

    invoke-virtual {p0}, Lcom/rubidiumclient/host/EClientLoaderProvider;->getContext()Landroid/content/Context;
    move-result-object v0

    if-eqz v0, :cond_1

    instance-of v1, v0, Landroid/app/Application;
    if-eqz v1, :cond_1

    check-cast v0, Landroid/app/Application;
    invoke-virtual {v0, p0}, Landroid/app/Application;->registerActivityLifecycleCallbacks(Landroid/app/Application$ActivityLifecycleCallbacks;)V

    :cond_1
    const/4 v0, 0x1
    return v0
.end method

# -----------------------------------------------------------------------
# On each resumed activity: bind the native bridge, then (re)attach the UI.
#
# r25: the decor-view OnTouchListener is INTENTIONALLY NOT INSTALLED any more.
# It sat directly in the game's own input dispatch path and always returned
# false; the Views overlay owns its touches through its own WindowManager
# windows, so nothing of ours belongs in that path. The listener class and the
# nativeGuiTouch export are still shipped/kept — the touch ABI is unchanged,
# it is simply never wired in.
#
# nativeTouchReady() MUST stay: it is the KotlinBridge bind point
# (KotlinBridge::bind) — without it no Kotlin module ever ticks.
# -----------------------------------------------------------------------

.method public onActivityResumed(Landroid/app/Activity;)V
    .locals 0

    invoke-static {}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeTouchReady()V

    # Kotlin module layer: register modules (idempotent; the class lives in the
    # injected classesN.dex).
    invoke-static {}, Lcom/rubidiumclient/modules/ModuleRegistry;->init()V

    # Kotlin Views overlay (chip + panel). attach() ALWAYS rebuilds: the
    # framework tears sub-windows down on pause/rotation, and a stale reference
    # is how the overlay used to vanish or swallow game input.
    invoke-static {p1}, Lcom/rubidiumclient/modules/ui/EClientUi;->attach(Landroid/app/Activity;)V

    return-void
.end method

# Remaining lifecycle stubs (unused but required by the interface).

.method public onActivityCreated(Landroid/app/Activity;Landroid/os/Bundle;)V
    .locals 0
    return-void
.end method

.method public onActivityStarted(Landroid/app/Activity;)V
    .locals 0
    return-void
.end method

.method public onActivityPaused(Landroid/app/Activity;)V
    .locals 0
    return-void
.end method

.method public onActivityStopped(Landroid/app/Activity;)V
    .locals 0
    return-void
.end method

.method public onActivitySaveInstanceState(Landroid/app/Activity;Landroid/os/Bundle;)V
    .locals 0
    return-void
.end method

.method public onActivityDestroyed(Landroid/app/Activity;)V
    .locals 0
    return-void
.end method

# -----------------------------------------------------------------------
# Standard (benign) ContentProvider boilerplate — unchanged.
# -----------------------------------------------------------------------

.method public query(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)Landroid/database/Cursor;
    .locals 1
    const/4 v0, 0x0
    return-object v0
.end method

.method public getType(Landroid/net/Uri;)Ljava/lang/String;
    .locals 1
    const/4 v0, 0x0
    return-object v0
.end method

.method public insert(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;
    .locals 1
    const/4 v0, 0x0
    return-object v0
.end method

.method public delete(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I
    .locals 1
    const/4 v0, 0x0
    return v0
.end method

.method public update(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I
    .locals 1
    const/4 v0, 0x0
    return v0
.end method
