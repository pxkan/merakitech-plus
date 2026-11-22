# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

<!-- Closest schema value. The real platform is an embedded appliance display: a round 480x480 touch screen
     (visible circle r = 234 px) driven by a plain-C software renderer (ARGB8888 framebuffer, AA primitives,
     baked Montserrat outlines) on an ARMv7 Cortex-A7. No browser, no CSS; the web build (Sim Studio) is the
     same C code compiled to WebAssembly for development only. -->

## Users

Home espresso owners of a MerakiTech Gen 1 machine who installed Meraki+ to get more control than the stock
UI gives (brew-by-weight, pre-infusion, steam targets, scheduling) while keeping the machine's own look.
They stand at a kitchen counter, at arm's length from the round display, often with wet or milky hands,
glancing at it while tamping, holding a portafilter or a milk jug. Sessions are short and repeated daily:
start a grind, pull a shot, steam milk, check that the boilers are hot. Settings are changed rarely and
deliberately. A smaller group (tinkerers, the maintainers) reads DIAGNOSTICS.

## Product Purpose

An open-source replacement UI for the machine's round touch display. It runs in the display's spare boot
slot so the stock UI is never touched and one command reverts it. Success: the daily ritual (heat, grind,
brew, steam) is faster and clearer than stock, the extra power (targets, auto-stop, live trace, schedule,
diagnostics) is there when wanted and invisible when not, and the screen looks like it belongs to the
machine.

## Positioning

The only UI for this machine that keeps the manufacturer's visual identity (copper, thin white ring
buttons, curved labels, line icons) while adding brew-by-weight, a live shot view, a roller editor and full
diagnostics; built in the open, verified in a full simulator before it ever reaches a machine.

## Operating Context

- Arm's length, standing, mixed kitchen light; glanceable from a step back during a shot.
- Wet or gloved fingers: generous touch targets, hold-to-start for anything that runs water or steam.
- The round bezel crops everything outside r = 234 px; corners do not exist.
- Two languages: English and Danish (Danish strings run longer).
- Activities: grind, brew (pre-infusion, extraction, auto-stop by weight), hot water, steam (auto by milk
  temperature or manual by wand). Phases also include heating, ready, done, sleep, offline, error (E1–E3),
  power off/on, switching back to the stock UI.

## Capabilities and Constraints

- Display and navigation only may change in a design pass. Anything that goes over the UART (frame policy,
  write queue, recipes, STOP, boot push, setting limits, config persistence) is out of scope.
- Device budget: a brew frame must stay within the v7.0 cost (0.78 ms under ARM emulation), static layers
  are pre-rendered, binary well under the installer's 1.2 MB limit.
- Fonts must be OFL or similar with the licence in the repo; no manufacturer or third-party assets in the
  repo.
- Interaction vocabulary: tap, hold (≥ 0.65 s starts an activity), drag to scroll, swipe right / pull down
  from the top to go back.

## Brand Commitments

- Meraki+ is unofficial and not affiliated with the manufacturer; it borrows the stock UI's language, not
  its assets.
- The v7 identity is binding (the owner likes it): a copper background with a fine guilloche, thin white
  ring buttons with Title Case names curved along the ring, white line icons, a small clock, white on
  copper. Refinements keep it.
- Typeface: Montserrat (SIL OFL 1.1), already baked.

## Evidence on Hand

- A photo of the stock UI for reference (kept outside the repo, never committed).
- The real renderer's preview build (`-DMERAKIPLUS_PREVIEW`, 106 synthetic scenarios, EN + DA) and Sim
  Studio (the real UI as WebAssembly against a simulated machine).
- No user research, telemetry or testimonials exist; do not invent any.

## Product Principles

1. The ritual first: the four activities are one tap or hold away, and everything else is revealed only
   when asked for.
2. Glanceable at arm's length: one dominant thing per screen, readable in a second.
3. Belong to the machine: the stock identity, executed with more care than stock.
4. Never ambiguous about safety: errors, offline and blocked starts are unmistakable; anything that runs
   water or steam needs a deliberate hold.
5. Calm power: depth (diagnostics, raw values) exists, but in a quiet register.

## Accessibility & Inclusion

Text must stay legible at arm's length on copper (contrast ≥ 4.5:1 for small text, ≥ 3:1 for large), touch
targets sized for wet fingers, and state never carried by colour alone (errors carry an icon and words).
