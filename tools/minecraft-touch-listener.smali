.class public Lcom/rubidiumclient/host/EClientTouchListener;
.super Ljava/lang/Object;
.implements Landroid/view/View$OnTouchListener;
.source "EClientTouchListener.java"

# -----------------------------------------------------------------------
# View-layer touch tap for the E-Client GUI + in-game overlay controls.
#
# Installed on the resumed activity's decor view by EClientLoaderProvider.
#
# v2: multi-pointer aware. Every pointer gets its own (pointerId, action,
# x, y) forwarding into nativeGuiTouch: for DOWN/UP-family actions the
# action pointer alone is forwarded; for ACTION_MOVE every current pointer
# is forwarded one by one. The native side consumes any subset (GUI
# drags, the toggle bar, and the Fly module's on-screen buttons); the
# event reaches the game only when NO pointer was consumed.
# -----------------------------------------------------------------------

.method public constructor <init>()V
    .locals 0
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    return-void
.end method

.method public onTouch(Landroid/view/View;Landroid/view/MotionEvent;)Z
    .locals 8

    # v0 = action, v1 = index / loop counter, v2 = pointerId,
    # v3 = x, v4 = y, v5 = any-consumed result, v6 = pointer count
    invoke-virtual {p2}, Landroid/view/MotionEvent;->getActionMasked()I
    move-result v0

    const/4 v5, 0x0

    const/4 v6, 0x2
    if-ne v0, v6, :single

    # ---- ACTION_MOVE: forward every pointer individually ----
    invoke-virtual {p2}, Landroid/view/MotionEvent;->getPointerCount()I
    move-result v6
    const/4 v1, 0x0

    :move_loop
    if-ge v1, v6, :done
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getPointerId(I)I
    move-result v2
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getX(I)F
    move-result v3
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getY(I)F
    move-result v4
    invoke-static {v2, v0, v3, v4}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeGuiTouch(IIFF)Z
    move-result v2
    or-int/2addr v5, v2
    add-int/lit8 v1, v1, 0x1
    goto :move_loop

    # ---- everything else: forward the action pointer only ----
    :single
    invoke-virtual {p2}, Landroid/view/MotionEvent;->getActionIndex()I
    move-result v1
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getPointerId(I)I
    move-result v2
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getX(I)F
    move-result v3
    invoke-virtual {p2, v1}, Landroid/view/MotionEvent;->getY(I)F
    move-result v4
    invoke-static {v2, v0, v3, v4}, Lcom/rubidiumclient/host/EClientLoaderProvider;->nativeGuiTouch(IIFF)Z
    move-result v5

    :done
    return v5
.end method
