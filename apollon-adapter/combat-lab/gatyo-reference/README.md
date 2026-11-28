# Gatyo Fake Player reference

These files are extracted unchanged from the user-provided `gatyo shitsrc.zip`
for a local, inspectable copy of the Fake Player and the major modules it calls
(network injection, virtual actor/aura integration, render, effects, pop counter,
and common helpers). They are **reference source**, not compiled as-is: they
have further dependencies on Gatyo's global state, hooks, UI, GL and other
modules. The compiled adaptation lives in `../gatyo_fake.h` and
`../fake_simulation.h`. `../steve_skin.h` is Gatyo's extracted skin asset.
