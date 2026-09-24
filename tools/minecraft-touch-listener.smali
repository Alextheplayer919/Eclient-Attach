.class public Lcom/rubidiumclient/host/EClientTouchListener;
.super Ljava/lang/Object;
.implements Landroid/view/View$OnTouchListener;
.source "EClientTouchListener.java"

# -----------------------------------------------------------------------
# View-layer touch tap for the E-Client GUI.
#
# Installed on the resumed activity's decor view by EClientLoaderProvider.
# View.dispatchTouchEvent consults mOnTouchListener FIRST, before child
# dispatch, so this sees every touch on the window regardless of how the
# game consumes input internally (libminecraftpe.so imports no AInputQueue
# API in 1.21.111 — native input hooks cannot observe touches).
#
# The single-parameter (pointer 0) extraction keeps the smali minimal;
# action masking is interpreted natively. The return value decides whether
# the event propagates into the app (menu open = consume) — forwarded by
# View's standard dispatch; returning false leaves the game untouched.
# -----------------------------------------------------------------------

.method public constructor <init>()V
    .locals 0
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public onTouch(Landroid/view/View;Landroid/view/MotionEvent;)Z
    .locals 4

    invoke-virtual {p2}, Landroid/view/MotionEvent;->getActionMasked()I
    move-result v0

    const/4 v1, 0x0
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getX(I)F
    move-result v2

    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getY(I)F
    move-result v3

    invoke-static {v0, v2, v3}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeGuiTouch(IFF)Z
    move-result v0

    return v0
.end method
