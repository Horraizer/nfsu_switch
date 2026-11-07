# Restore the original N2O HUD label

Owner follow-up: the N2O label was also missing in the Turbo comparison.
At base90b6b5d the port loaded only NOS charge segments, omitting SR_NOS_Text.

Added the original INGAMEB.BUN SR_NOS_TEXT texture to a free64x64 atlas slot.
The atlas dimensions and existing regions are unchanged. Added one quad at
HUD_SingleRace.fng's authored SR_NOS_Text position relative to the Needle SAT.
The diagonal letters and subscript are retail artwork, not synthesized text.
Visible for installed nitrous including depleted charge, independent of turbo.
Absent for cars without nitrous. Physics, charge and audio are unchanged.

Original object provenance: package object16796, OP16836/resource8,
parent GaugeCluster, SA16872; input identities remain docs/HUD_253.md.
The supplied Deck reference uses the same bottom-aligned crop as the Turbo
comparison. Existing gear color, units, needle and nitrous states differ;
the comparison validates label presence/placement, not whole-HUD parity.

Evidence: build-artifacts/draw-distance-20261008-root/nos-label-r1.
PC actual HUD compositor loads the original texture and renders the corrected
Turbo plus N2O. Targeted retail boost checks cover two/six charge segments,
empty/full installed bottles, absent equipment and unchanged label geometry.
No NRO rebuild, upload, push, merge or Switch acceptance in this correction.
The ongoing style-point draft remains separately uncommitted; see
root-style-turbo-20261012.md for its exact state and remaining integration.
