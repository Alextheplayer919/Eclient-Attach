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
# PUBLIC: nativeGuiTouch is called cross-class from EClientTouchListener.
# -----------------------------------------------------------------------

.method public static native nativeGuiTouch(IFF)Z
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
# On each resumed activity, install the decor-view touch listener.
# -----------------------------------------------------------------------

.method public onActivityResumed(Landroid/app/Activity;)V
    .locals 2

    invoke-virtual {p1}, Landroid/app/Activity;->getWindow()Landroid/view/Window;
    move-result-object v0
    if-eqz v0, :cond_0

    invoke-virtual {v0}, Landroid/view/Window;->getDecorView()Landroid/view/View;
    move-result-object v0
    if-eqz v0, :cond_0

    new-instance v1, Lcom/rubidiumclient/host/EClientTouchListener;
    invoke-direct {v1}, Lcom/rubidiumclient/host/EClientTouchListener;-><init>()V
    invoke-virtual {v0, v1}, Landroid/view/View;->setOnTouchListener(Landroid/view/View$OnTouchListener;)V

    invoke-static {}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeTouchReady()V

    :cond_0
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
