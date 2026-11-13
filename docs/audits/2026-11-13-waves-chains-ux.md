<!-- Copied on 13 Nov 2026 from the owner's Claude Doc "…waves and …chains: UX/UI audit" (https://claude.ai/artifact/E54WPw2jYqdYhLgGnghrUu) so the findings live in the repo. Line references are at beta dffe6f2. -->

# …waves and …chains: UX/UI audit

 · 

## Summary

Both editors have a sound base, but they fall short of the brief in three places: collapsed modules drop every value, the phone layout is not usable as it opens, and the patching model stops well short of Max/MSP and Reaktor. This audit lists 60 findings and a proposed collapsed-module design. Nothing in the repo was changed.
- Collapse. A collapsed module keeps its title and every port, and hides every parameter value. Collapsed heights run 86 to 338 px, so port-heavy modules barely shrink. Modulation cables into a collapsed module all meet at one unlabelled point.
- Phone and touch. On a 344 px screen the patch opens at 25% zoom, where a jack is 5 px across. The Fit button is hidden below 1340 px. The top bar takes 161 px, and the module list covers the whole patch and stays open after adding.
- Max and Reaktor feel. Feedback loops are refused, an input takes one cable and replaces the old one silently, and there are no comments, module names, live subpatches, scopes or math primitives. The quick-add box (double-click the background) is the right idea but has no key and no touch route.
- Desktop. Sliders in the editors do not follow the value-control rules chosen for the rest of Thunder Plus: right-click opens the module menu, the wheel pans, and a typed dB or kHz value is applied as the raw parameter.

What was looked at. Branch beta at dffe6f2 (build 577). Both files were read in full: chains/index.html and waves/index.html. Both were run standalone in headless Chrome at 1440 x 900 (mouse), 800 x 700 (touch) and 344 x 780 (touch).

Not tested. No real Fold 6, no run embedded inside the app frame, no terminal skins, no audio. Findings marked "code" come from reading the source and were not reproduced in a browser.

Severity: P1 blocks normal use, P2 is friction, P3 is polish. Effort: S under an hour, M a few hours, L a day or more.

## For the agent picking this up

The owner approved this work on 13 Nov 2026 as an exception to the feature freeze; his answers are under "Owner decisions" at the end and override any suggestion above them. Take the passes in order, starting with look and feel. Summing and feedback have since been mapped at the end of this document.

### Start here: look and feel first

The owner wants look and feel improved before anything else, so begin with pass 1 in "Suggested order" and do not start pass 2 until he has seen pass 1.
- Read first. CLAUDE.md, the top of HANDOVER.md, then in this document "Collapsed modules", "Look, wording and consistency" and "Owner decisions".
- Pass 1a, collapsed modules. Build the Compact state from the proposal (C1, C2, C3, C6, C7, C8), then collapse all / expand all (C4), the touch trigger (C5) and the label fix (C9). Mini comes after the owner has tried Compact. One PR into beta.
- Pass 1b, signal kinds and cables. Gate gets its own jack shape and dash (M9, U1), the selection outline stops using a signal colour (U1), cables gain hover and selected states and visible bend handles (D6, D9). One PR.
- Pass 1c, wording and type. Port labels (U3), one name per signal kind and per module (U5, U6), title and value-column fit (U7), font sizes and contrast (U8), help-card colours (U9), waves menu scroll (U10), and the touch version of the tip box (T11). One PR.
- Show the owner. For each PR, attach before and after screenshots of both editors at 1440 x 900, 800 x 700 and 344 x 780, in the white theme and in IDM pink & green and 909, which are the skins he uses. Add the checks to the owner's punch list in HANDOVER.md.

Ground rules for pass 1:
- No audio code. Nothing in pass 1 touches chains-worklet.js or waves-worklet.js, so goldens and cache-busters stay as they are. If a change seems to need the worklet, stop and ask.
- Old patches load unchanged. A patch saved with collapsed:true opens as Compact. New fields are optional.
- Colours through variables. Use the variables at c:19 to c:27 so terminal skins restyle the new parts; check termFrameCss (r:14675) and extend it if a new variable is added.
- Cheap to draw. No new per-frame DOM writes; write summary text only when it changes (see docs/audits/2026-11-13-draw-pass.md).
- Both files. Make every editor change in chains/index.html and waves/index.html; run python3 tools/sync-shared.py after touching the shared block.
- Who does what. The owner prefers lower-cost agents for fit-and-finish work with the lead model reviewing the diff; the model table is in HANDOVER.md.
- Line references. c:N is chains/index.html, w:N is waves/index.html, r:N is the root index.html, all at beta dffe6f2. Lines drift, so each finding also names a function or selector to search for.
- Two copies of almost everything. Only the module definitions, their help text, the palette layout and the module displays sit in the SHARED EDITOR block (c:403 to c:1507, copied to waves by python3 tools/sync-shared.py, chains is the source). The editor itself is duplicated by hand: buildNode (c:2461, w:2098), slider (c:2174, w:2009), setCollapsed (c:2531, w:2164), jackPos (c:2672, w:2281), onJackDown (c:2733, w:2340), fitAll (c:2915, w:2510), and the CSS (c:13 to c:294, w:13 to w:277). Every fix marked "both" is made twice.
- Rules from CLAUDE.md and HANDOVER.md. Work on beta through PRs, ask CodeRabbit for a review, add tests to the owner's punch list, update HANDOVER.md at the end of a session. Old patches and songs must keep loading.
- Owner decisions that already apply. Value controls: double-click resets, right-click opens Reset / Type / Learn, Ctrl or Cmd held mid-drag takes big steps, the wheel changes a control only after the pointer rests on it. The wheel scrolls the same way on every screen. Chains is hidden on the folded phone unless switched on in Settings (r:8405); waves is hidden on any device set to Mobile.
- Overlap with earlier audits. docs/USABILITY-AUDIT.md items M1, M9, M12, K9, B5, B11, V14 and docs/audits/2026-11-10-usability.md items U8 and U17 cover some of the same ground. They are cross-referenced below, not repeated.
- Terminal skins. The host only restyles the editors for term skins (skinFrame, r:14691). Any new colour must use the CSS variables at c:19 to c:27, or it will stay white-theme in those skins.

## What already works and should be kept

The base is closer to a hardware-style modular than to Max, and most of it is worth keeping as it is.
- Signal kinds are colour-coded. Audio is teal and solid, control is purple and dashed, gate is pink and dashed, on jacks and cables alike (c:22, c:155, c:193).
- Every slider has its own modulation jack, and a modulated slider shows a live tick and a purple value (slider, c:2174). Reaktor needs extra wiring for this.
- Every jack has a small level meter (.pm, c:199), so signal flow is visible without a scope.
- Touch patching exists: tap one jack, then the other; double-tap a cable to remove it (c:2766, c:2662).
- Cables are straight with optional bends that snap to horizontal, vertical or 45 degrees (c:2790). This is the Max look already.
- Slider drag is relative with Shift for fine steps, and a value can be typed (c:2218, c:2180).
- Quick add: double-click the background, type, Enter (c:3134). This is the Max "new object" gesture.
- Per-module help with an About and a Try line behind the ? button (c:1231).
- Collapsed extras that already point the right way: I/O modules add their track to the title (ioTitle, c:2271), dynamics modules keep a one-line gain-reduction meter (dmini, c:1282), and the bypass button stays in the header with its tracker number.
- Groups, custom modules, Tidy up, autosave with ten backups, 60-step undo, and a touch rule block that enlarges ports, rows and buttons on coarse pointers (c:277 to c:285).

## Collapsed modules

Collapse today hides the whole body and keeps the header and every port, so a collapsed module tells you what it is wired to but nothing about what it is set to. The rule is two lines of CSS (.node.collapsed > .body{display:none}, c:178) plus setCollapsed (c:2531, w:2164).
| 

ID | 

Finding | 

Suggested change | 

Sev / effort | 
| 

C1 | 

No parameter value survives collapse. A collapsed Tape delay shows no time or mix; a collapsed VCO shows no waveform or tuning. | 

Add a one-line summary of the module's key values under the title (see the proposal below). | 

P1 / M | 
| 

C2 | 

Every port stays, so height is set by port count: 86 px for a small module, 282 px for Stereo mixer, 338 px for Life. | 

In the collapsed state show only patched ports, with a count of the hidden ones. | 

P1 / M | 
| 

C3 | 

A cable into a slider jack re-anchors to the left middle of the header when collapsed (jackPos, c:2679, w:2287). Several meet at one point with no label, and the line can cross the next module's close button. | 

Draw one small purple stub per modulated parameter down the left edge, with the parameter name beside it. | 

P1 / M | 
| 

C4 | 

No collapse all, expand all or collapse selection, and no key for it. The only "Collapse all" button belongs to the module list (c:3352). | 

Add all three to the background and module right-click menus, plus a key. | 

P2 / S | 
| 

C5 | 

On touch the dependable trigger is the 20 x 22 px chevron. Double-tap on the title bar relies on the browser turning it into a double-click, on a header that cancels pointer events (c:2498 to c:2515). Code. | 

Handle double-tap on the header explicitly, and widen the chevron's hit area to 40 px on coarse pointers. | 

P1 / S | 
| 

C6 | 

Collapsed I/O titles truncate: "Chain Out · Trac..." at the default 250 px width (seen at 1440 px). Waves has no equivalent for Host In or Out. | 

Put the track on the summary line, not in the title; add the same for waves Out ("Waves In 1"). | 

P2 / S | 
| 

C7 | 

Meter, Life, Blip drum and the Out level bar vanish when collapsed. Only dynamics modules keep a meter. | 

Give every module with an audio output a thin level strip in the collapsed header. | 

P2 / M | 
| 

C8 | 

Nothing shows that a collapsed module is being modulated or is mapped to MIDI or the bottom panel. | 

Carry the existing modulation, MIDI and bottom-panel marks onto the summary line. | 

P2 / S | 
| 

C9 | 

The chevron's label and tooltip stay "Collapse X" when the module is already collapsed; only aria-expanded flips (c:2465, c:2534). | 

Switch the label to "Expand X". | 

P3 / S | 

### Proposed collapsed design

Three states instead of two, stored per module, with the middle one as the default collapse.
- Open. As today.
- Compact. Header, a summary line of up to three key values, patched ports only, one labelled stub per modulated parameter, and a level strip. Target height 64 to 110 px for any module.
- Mini. Header only, about 42 px. All cables attach to the left and right edges of the header. Meant for the phone and for finished parts of a patch.

The mock-up uses the starter patch's Tape delay with its Time slider modulated and In R unpatched; sizes are indicative, not measured.

Details the builder will need:
- Which values are "key". Add an optional key:[...] list to each module definition inside the shared block. Fall back to the first select plus the first two sliders when it is absent, so all 122 chains and 108 waves modules get a summary on day one and the important ones can be hand-picked later.
- Summary text. Reuse each parameter's existing formatter (fmt), for example "375 ms · 38% · 25% wet". Update it from the same place the readout is written, and only when the text changes.
- Editing while compact. Tapping a summary value could open the existing typed-value box. Treat this as a later step.
- Storage. Keep m.collapsed (true or false) so old patches load, and add m.view with 'compact' or 'mini'. A patch with collapsed:true and no view opens as Compact.
- Cycle. Chevron or double-click steps Open to Compact; a second step or a long-press goes to Mini; the module menu lists all three.
- Cost. markScene and cable redraw already run after setCollapsed; the summary line must not add per-frame DOM writes, in line with the 13 Nov draw-pass audit.

## Max/MSP and Reaktor feel

The editors look like a patcher but behave like a fixed effects rack: every module is 250 px wide with a full header, wiring is restricted, and nothing can be named, annotated or nested. The gaps below are ordered by how much each one limits low-level work later.
| 

ID | 

In Max or Reaktor | 

Today | 

Suggested change | 

Sev / effort | 
| 

M1 | 

Objects are as small as their job: a + or a constant is one short box. | 

Every module is 250 px wide (c:161), not resizable, with chevron, title, help and close. The smallest (Attenuverter) is 167 px tall. | 

Allow a module definition to declare a narrow, header-light form. Decide this before adding math or logic modules, because it changes buildNode, jackPos and Tidy up. | 

P1 / L | 
| 

M2 | 

Feedback is allowed: Max with a delay in the loop, Reaktor by inserting a one-sample delay and marking the wire. | 

Any loop is refused with a toast (reaches, c:2708, w:2324). A module cannot feed itself. | 

Allow loops with an implied one-block delay, and mark that cable. This needs worklet work and an audio review, so it is not a UI-only change. | 

P2 / L | 
| 

M3 | 

Several cords into one signal inlet are summed. | 

One cable per input; a second one replaces the first with no message (connect, c:2716, w:2326). | 

First step: a toast with Undo when a cable is replaced. Later: sum audio at inputs. | 

P2 / S, then L | 
| 

M4 | 

Create by typing: press N, type a name, Enter; the object lands under the pointer. | 

Quick add exists on double-click of the background only (c:3134): no key, no toolbar button, no dependable touch route. The module list has no search box. New modules land at the view centre with random jitter and overlap (addAtCenter, c:2578). | 

Open quick add from a key and from a "+" button; put the same search field at the top of the module list; place the new module beside the selected one. See also the existing owner wish for search in the instrument and effect lists. | 

P1 / M | 
| 

M5 | 

Comments and renamed objects make a patch readable. | 

No comment object. m.label is read for the title (c:2466, w:2101) but nothing can set it. | 

Add Rename to the module menu (the storage is already there) and a plain text note object. | 

P2 / S for rename, M for notes | 
| 

M6 | 

Subpatchers, bpatchers and Reaktor macros nest structure and expose chosen controls. | 

Custom modules are paste templates (c:3039). Groups are fixed boxes that do not follow their members, and their stored collapsed flag is never read (c:3022, w:2615). | 

Make groups follow their members and collapse into one block with only the cables that cross the boundary. This is the route to macros. | 

P2 / L | 
| 

M7 | 

Panel or presentation view separates playing from wiring. | 

Chains can map a control to the app's bottom panel (c:3169); waves cannot. | 

Treat the bottom panel as the panel view and offer the same mapping in waves. | 

P3 / M | 
| 

M8 | 

Hover a wire to read its value; scope and number objects anywhere. | 

Jack meters only. No scope or value display; ADSR, LFO and VCO have no curve or waveform. Cables have no hover or selected state. | 

Show the live level or value when a cable is hovered or tapped; add a Scope and a Value module; draw the envelope and LFO shape in their modules. | 

P2 / M each | 
| 

M9 | 

Port type is visible in shape or marking as well as colour. | 

Control and gate cables share one dash pattern and differ only by purple against pink. Pitch is ordinary control. Any output may go to any input, and every opposite jack lights up as a target (c:2747). | 

Give gate jacks a different shape and dash. Keep wiring permissive, but light matching kinds more strongly than mismatched ones. | 

P2 / S | 
| 

M10 | 

An edit lock: locked, clicks play the patch; unlocked, they edit it. | 

Always editable. On touch a pan can only start on empty background, and a one-pixel slip on a header moves a module (c:2515). | 

Add a Play / Edit switch. In Play, drags pan anywhere and only sliders, selects and keys respond. | 

P2 / M | 
| 

M11 | 

Snap to grid, align and distribute. | 

No grid, snap or align. Tidy up ignores signal flow, lays modules out in document order and deletes every group (c:3026 to c:3037). | 

Snap module drags to a 10 px grid; make Tidy up order by signal flow and keep groups. | 

P3 / M | 
| 

M12 | 

Math, logic, constants, switches and comparators as building blocks. | 

Five control utilities per editor. Waves also lacks Clock, Steps, Stereo mixer, Repeat and Vocoder, which chains has. | 

A "Math and logic" group once M1 exists. Not UI work, listed so the layout is planned for it. | 

P3 / L | 

Max and Reaktor are described here from general knowledge of those products, not from a fresh side-by-side test.

## Phone and touch

At 344 px the editors open in a state nobody can patch in, and several desktop gestures have no touch route at all. Sizes below were measured in headless Chrome with touch emulation, running each editor on its own page. Inside the app the File and Settings buttons are hidden, so the top bar will be shorter than measured here.
| 

ID | 

Finding | 

Suggested change | 

Sev / effort | 
| 

T1 | 

First open fits the whole patch, which lands at the 25% minimum on 344 px and 55% at 800 px (c:3791, zoom floor c:2857). At 25% a jack is 5 px and a slider 18 x 9 px. | 

On a narrow screen open at 80 to 100%, centred on the first module, and leave Fit as a button. | 

P1 / S | 
| 

T2 | 

The Fit button is hidden at 1340 px wide and below (c:286, w:269). Chains has a host-callable chainsEmbed.fit() (c:3455); waves has nothing. Earlier audit: U8. | 

Always show Fit; hide the percent readout first if space is short. | 

P1 / S | 
| 

T3 | 

The module list is a 300 px overlay on a 306 px workspace and stays open after a module is added, so the new module is hidden behind it (addItem, c:2578; setDrawer is never called there). | 

On narrow screens close the list after an add and pan to the new module. | 

P1 / S | 
| 

T4 | 

A 250 px module nearly fills the workspace at 100%, and a one-finger pan only starts on empty background (c:2947). About 56 px of margin is left to grab. Two fingers pan and zoom, unless one lands on a slider row (c:2933). | 

Let two fingers pan and zoom from anywhere, rows included. The Play / Edit switch (M10) removes the conflict fully. | 

P1 / M | 
| 

T5 | 

Header buttons are not enlarged on touch: chevron 20 x 22 px, help 20 x 24, close 26 x 26, bypass 49 x 22, side by side. Close deletes at once (Undo recovers it). The coarse-pointer block (c:277 to c:285) skips them. Earlier audit: M12. | 

40 px hit areas on coarse pointers; move Delete into the module menu on touch, or ask first. | 

P1 / S | 
| 

T6 | 

There is no long-press handler. The module menu (Group, Copy, Duplicate, Delete, Make custom module) opens only if the browser turns a long press into a right-click. Code. | 

Add an explicit 500 ms long-press on modules, cables, sliders and the background. | 

P1 / M | 
| 

T7 | 

Touch sliders are the browser's own range control: they jump to the finger and have no fine mode (the relative-drag path returns early for touch, c:2219). Earlier audit: M1. | 

Use the same relative drag on touch, with an 8 px threshold so a scroll does not change a value. | 

P1 / M | 
| 

T8 | 

No touch route for multi-select, marquee, cable bends, copy-drag or quick add. All need Shift, Alt or a double-click. | 

A Select toggle in the bar for multi-select and marquee; bends and quick add from the long-press menu. | 

P2 / M | 
| 

T9 | 

The value readout is the only way to type a value and it is 64 x 18 px, opened by double-tap (c:2207). | 

Open the type box from a single tap on the readout on coarse pointers, or from the slider's long-press menu. | 

P2 / S | 
| 

T10 | 

A header drag starts after 1 px of movement (c:2515), so a tap can nudge a module and add an undo step. | 

8 px threshold on touch, 3 px on mouse. | 

P2 / S | 
| 

T11 | 

The tip box is one paragraph that mixes mouse and touch gestures ("Hold Option (Alt on Windows)", "right-click to group"). At 344 px it is 282 x 376 px, about 60% of the workspace (c:363, w:348). Earlier audits: U8, U17, V14. | 

Show a short touch-only version on coarse pointers and start it collapsed after the first visit. | 

P2 / S | 
| 

T12 | 

The top bar wraps to three rows, 161 px, at 344 px (.bar, c:39). | 

One row on narrow screens: patch name, add, undo, and a single overflow menu. | 

P2 / M | 
| 

T13 | 

Waves on-screen keys are a single octave about 478 px wide with no wrap or scroll; at 344 px the strip starts at F and C to E are off-screen. No octave buttons, fixed velocity (w:241 to w:246, w:3260). | 

Octave down and up buttons, and a strip that scrolls or wraps. Applies only if waves is switched on for Mobile. | 

P2 / S | 
| 

T14 | 

Some modules are far taller than a phone screen: pump GATE is 1739 px (2503 px with touch row heights) with 47 sliders; Random generator is 1190 px. | 

Collapsible sections inside long modules, closed by default except the first. | 

P2 / L | 

## Desktop: mouse and keyboard

The main desktop problem is that sliders in the editors behave differently from sliders everywhere else in Thunder Plus, followed by cables that get in the way of the controls under them.
| 

ID | 

Finding | 

Suggested change | 

Sev / effort | 
| 

D1 | 

Right-click on a slider opens the module menu (Make custom module, Group, Copy, Duplicate, Delete, Select all), not the Reset / Type / Learn menu chosen for value controls. Embedded chains adds only "Add to the bottom panel" (c:3162). Earlier audit: M9. | 

Give slider rows the app's value menu: Reset, Type a value, MIDI learn, Add to bottom panel, with the module items below. | 

P1 / M | 
| 

D2 | 

A typed value is applied as the raw parameter unless the readout is a percent or milliseconds (typeBox, c:2180 to c:2200, w:2015 to w:2040). Typing 6 into a dB or kHz readout clamps to the parameter's range, not to 6 dB. Code. | 

Convert through the formatter: search the parameter range for the value whose formatted text matches the typed number. | 

P1 / M | 
| 

D3 | 

Every press on a slider or select takes an undo snapshot before anything changes (c:2219, c:2252; w:2044, w:2078). The 60-step stack fills with no-ops, and embedded each one is a step in the song's undo. | 

Take the snapshot on the first real change of a drag. | 

P1 / S | 
| 

D4 | 

Cables are drawn above modules (svg.cables, z-index 1000000, c:154) and their 16 px hit band takes clicks meant for sliders and buttons underneath; only jacks are passed through (c:2656). Code. | 

Pass presses through to any control under a cable, and take cable clicks only on the bare canvas or with a modifier. | 

P1 / M | 
| 

D5 | 

The wheel over a slider pans the patch (c:2889). Elsewhere in the app the wheel changes a control once the pointer has rested on it. | 

Owner's call, since the wheel is also the pan gesture here. See the open questions. | 

P2 / M | 
| 

D6 | 

Cables have no hover state and cannot be selected, so Delete does not remove one. Selecting a module does not highlight its cables. | 

Thicken a cable on hover, let a click select it, and fade unrelated cables while a module is selected. | 

P2 / M | 
| 

D7 | 

Missing keys: select all, arrow-key nudge, zoom in and out, fit, collapse, group, quick add, Escape to deselect. Select all, Group and Tidy up exist only in the right-click menu. | 

Add them. In waves, single letters play notes (w:3247), so use Ctrl or Cmd combinations there. | 

P2 / S | 
| 

D8 | 

Ctrl-click or Cmd-click on a slider resets it (c:2223), as do Alt-click and double-click. Holding Ctrl before starting a drag therefore resets the slider; big steps only work if Ctrl goes down mid-drag. | 

Keep double-click and Alt-click for reset; drop the Ctrl-click reset. | 

P3 / S | 
| 

D9 | 

Bend handles on cables show only while Alt is held (c:2799), and the only hint is in the tip paragraph. | 

Show handles on the hovered or selected cable. | 

P3 / S | 
| 

D10 | 

No minimap and no zoom to selection. A large patch is navigated by pan and Fit only. | 

Zoom to selection first (cheap); a minimap later if patches grow. | 

P3 / M | 

Already logged elsewhere and still open: app keys swallowed inside the frames (K9), two undo systems (B5), and files dropped on the frames (B11) in docs/USABILITY-AUDIT.md.

## Look, wording and consistency

The owner's usual skins change how the editors read, and that was the one thing here checked only by approximation: the host's own skin stylesheet (termFrameCss, r:14675) was injected into each editor page for IDM pink & green and 909.
| 

ID | 

Finding | 

Suggested change | 

Sev / effort | 
| 

U1 | 

In terminal-based skins (IDM, IDM pink & green, IDM pink paper, 909, Matrix, Haring) control and gate take colours from the skin. In IDM pink & green both resolve to the same pink (--mod and --gate = #ff2d8a), which is also the selection colour, so the two kinds cannot be told apart on jacks or cables. | 

Stop relying on hue: a different dash and jack shape for gate (M9), and a selection outline that is not a signal colour. | 

P1 / S | 
| 

U2 | 

Embedded waves cannot reach Voices or On-screen keys: both live in the Settings menu that is hidden when embedded (w:44), and the host's Patch tab offers only quality, tempo, tips, MIDI and backups (r:34849). Code. | 

Add both to the host's Patch tab through patchSettings (w:2995). | 

P1 / S | 
| 

U3 | 

Five ports show raw keys as their labels because they are missing from PORT_LABEL (w:1611): Sample & Hold noise, Blades res, Meta mod, Flex pat, Step gate pat. | 

Add the five labels in the shared block. | 

P2 / S | 
| 

U4 | 

Per-module user presets in chains are saved on the device only (chains.presets, c:2381): not in the patch, not in the song, not in Export custom modules. Waves has no per-module presets. MIDI-learn mappings are also device-local and keyed by module id (w:1780). | 

Store user presets with the song or include them in the export; decide whether waves gets the Preset row. | 

P2 / M | 
| 

U5 | 

Three names for one signal kind: cv in the data, mod in CSS and the interface, "Control" in the module list. The waves bus meters are titled "Out 1-8" while the Out module calls them "Waves In 1" (w:330, w:1620). | 

One word in the interface for each. | 

P3 / S | 
| 

U6 | 

Module names mix title case and sentence case ("Envelope Follow", "Sample & Hold" beside "Slew limiter", "Comb resonator"), and several differ between the list and the module title ("Tape" and "Tape delay", "Random" and "Random generator"). | 

Sentence case throughout, one name per module. | 

P3 / S | 
| 

U7 | 

Long titles are cut with an ellipsis at 250 px next to three buttons, and long readouts overflow the fixed 64 px value column (c:163, c:206). Code. | 

Let the title wrap to two lines; size the value column to its content. | 

P3 / S | 
| 

U8 | 

About 17 distinct font sizes between 9 and 22 px, including 9 to 11 px muted labels on the bypass button, IN / OUT bars and canvas text. The CPU readout's amber is about 2.6 to 1 against white (c:58). | 

Five sizes with a 11 px floor; darken the amber. | 

P3 / M | 
| 

U9 | 

Help cards are bordered in the old family colours while the patch uses category colours (w:3089), so a module's help card does not match the module. | 

Use catStyle for the cards. | 

P3 / S | 
| 

U10 | 

The waves right-click menu has no maximum height or scroll; chains has both (c:86 against w:86). | 

Copy the chains rule. | 

P3 / S | 
| 

U11 | 

Jacks, modules and canvases have no role, label or tab stop; the toast has no live region; menus lack menuitem roles. There is no keyboard route to patch, select or move a module. | 

Start with labels and the live region; keyboard patching is a larger piece. | 

P3 / L | 

## Code hygiene found on the way

Four small items that are not design questions but will trip up whoever edits these files.
| 

ID | 

Finding | 

Suggested change | 

Sev / effort | 
| 

H1 | 

Opened on their own, both editors register ./sw.js (c:3861, w:3389), but neither chains/ nor waves/ contains that file, so each standalone load logs a failed registration. Embedded loads skip it. | 

Remove the registration or point it at the app's worker. | 

P3 / S | 
| 

H2 | 

Dead or unreachable: group collapsed flag never read (c:3022, w:2615); m.label never set; .fam-* edge colours overridden by .node.cat (c:147, c:1070); wavesEmbed.settings() has no caller and clicks a hidden button (w:2987); hidden #setkeys and paintKeys in chains (c:323, c:3778); help text for modules chains does not have. | 

Remove, or wire up where a finding above needs it (M5 uses m.label, M6 uses the group flag). | 

P3 / S | 
| 

H3 | 

border-radius:0 !important (c:29) cancels the radius set on the bypass button, port meters and slider marks. | 

Delete the cancelled rules or exempt them. | 

P3 / S | 
| 

H4 | 

In standalone chains, Space is swallowed (preventDefault, c:3495) although nothing plays there. | 

Only handle Space when embedded. | 

P3 / S | 

## Suggested order and open questions

Five passes, each of which can stop cleanly, reordered on 13 Nov so that look and feel comes first at the owner's request. All five are approved.
- Look and feel. Collapsed modules (C1 to C9), signal kinds (M9, U1), cable states (D6, D9), and wording and type (U3, U5 to U10, T11), as three PRs set out in "Start here". Done when the owner can read a fully collapsed patch in his IDM skins without opening a module, and control and gate are distinguishable in every skin.
- Fixes. D2, D3, D4, T1, T2, T3, T5, T10, U2, H1 to H4. Small, low risk, no new design. Done when a patch opens readable on the Fold, typed values land where typed, and one slider drag is one undo step.
- Phone pass. T4, T6, T7, T8, T9, T12 to T14, and the Play / Edit switch (M10). Done when a chain can be built from empty on the folded phone with one hand.
- Desktop feel. D1, D5, D7, D8, D10, quick add and search (M4), summing inputs (M3, editor only, see the map), rename (M5), hover values (M8), snap and Tidy up (M11), presets (U4) and labels for assistive tech (U11).
- Structure for low-level work. Narrow modules (M1), feedback (M2, once waves and chains are stable), live groups and macros (M6), panel mapping in waves (M7), scopes and math modules (M8, M12). M2 changes how the audio graph is ordered, so plan it with the native port's graph runners (stage N3 in docs/port/WORKPLAN.md); the editor interface itself is kept by the port, which shows these pages in a web view.

### Owner decisions (13 Nov 2026)

| # | Question | Decision | Affects |
|---|---|---|---|
| 1 | Which passes may go ahead under the freeze? | All of them: "we can make these changes". | All passes |
| 2 | Collapsed summary default: first select plus first two sliders? | Yes, as a test. The owner will validate it in use, so keep the key list easy to change per module. | C1 |
| 3 | Wheel over a slider | Change the value after the pointer has rested on it, as in the rest of the app. The wheel still pans elsewhere on the patch. | D5 |
| 4 | Feedback loops | Wanted: "a valuable tool in modular environments". A future feature if it is too big. Map first: write up the audio-graph change and its risk before building. | M2 |
| 5 | Second cable into an input | Summing preferred, provided it is not difficult or risky and does not break much. If it is a big change, map first and apply once waves and chains are stable; math or summing modules were planned anyway. Until then, replace with a toast and Undo. | M3, M12 |
| 6 | Modules of different widths | Yes. | M1 |
| 7 | Waves on the phone | Offer it as an option if the interface can be made to fit. Chains comes first because of bussing; waves will probably matter as much later. The setting already exists (Settings, App, "Show waves on Mobile", r:8400, r:9249), so the work is making waves fit: T1 to T14, above all the on-screen keys (T13). | T13, pass 3 |
| 8 | Delete on touch | Ask for confirmation. | T5 |

Summing (5) and feedback (4) were mapped afterwards; see the next section. Summing turns out to be an editor-only change and can move up to pass 4. Feedback is about a day of work and its level guard needs an audio review.

## Map: summing inputs and feedback loops

Both engines already do most of this: summing needs no audio change at all, and feedback needs a few lines in each worklet plus a level guard. The chains engine was probed in Node at dffe6f2 with the repo's own test helpers (the script lives outside the repo); the waves engine was read, not run. cw:N is chains/chains-worklet.js, ww:N is waves/waves-worklet.js.

### Summing (M3): small, editor only

The engine adds every cable into an input's buffer already (_gather, cw:5238 to cw:5256, ww:5091 to ww:5106), for audio inputs and slider jacks alike. In the probe a signal reached Chain Out at 0.2805 through a Utility and 0.2500 direct; with both cables in the same input it read 0.5305. The only thing stopping it is the editor, which removes the old cable before adding the new one.
| 

Where | 

Change | 
| 

connect (c:2713 to c:2723, w:2322 to w:2331) | 

Stop removing the existing cable. Refuse an exact duplicate (the engine drops duplicates anyway, Graph.connect, cw:5053). | 
| 

Picking a cable up from a patched input (edgeInto, c:2739, w:2346) | 

It returns the first cable found. With several, pick the one nearest the pointer. | 
| 

Cable drawing | 

Several cables end on one point. Accept the overlap at first; fan the last few pixels later if it reads badly. | 
| 

Host calls into the patch (thunderChains.mod, r:17465) | 

Not read. Check it does not assume one cable per input. | 
| 

Tests | 

Add "two cables into one input equals the sum" to tests/chains.test.js and tests/waves.test.js. | 
- What changes for the owner. Dropping a cable on a patched input now adds to it; to replace, drag the old cable off first. Two equal signals come out 6 dB louder, with no automatic level trim.
- Old patches. Unchanged, since none has two cables in one input. No worklet edit, so no cache-buster bump and no golden changes.
- Waves voices. A mono audio source feeding a per-voice module is already divided across the voices (ww:5101 to ww:5103), so sums stay consistent there.
- Size and risk. A few hours across both editors plus tests; low risk.

### Feedback (M2): about a day, engine already half-works

The editor refuses loops, but the engine does not need it to. Graph.compile (cw:5057 to cw:5068, ww:4949 to ww:4959) sorts modules by signal flow and appends whatever is left over, which is every module in or after a loop, in document order. The probe showed three things:
- A loop runs today, delayed by one block of 128 samples: 2.7 ms at 48 kHz, 2.9 ms at 44.1 kHz.
- Order is accidental. With the same patch stored in a different module order, Chain Out ran before the loop and the whole output arrived 128 samples late.
- Nothing limits it. With loop gain above 1 the output reached Infinity within about a second.

What to build:
- Mark the cable that closes the loop. Where reaches refuses today (c:2715, w:2324), set fb:true on that cable, save it in the patch, and draw it differently (dotted, with a small delay tag). A module patched into itself (c:2714) can use the same path.
- Order without it. In compile, leave fb cables out of the sort but keep them in the incoming list, so every other module is ordered properly and the loop reads the previous block. Graph.connect and loadPatch (cw:5196, ww:5026) need to carry the flag. About three lines per worklet. With no fb cable the sort is exactly what it is now, so existing renders should stay bit-identical.
- Guard the level. In _gather, on fb cables only, turn non-finite samples into 0 and limit the rest (a hard limit near plus or minus 4, or a soft clip). This is the part that needs an audio review, because it decides how a loop sounds when pushed.
- Waves voices. The per-voice flag is worked out in run order (ww:4961 to ww:4964). Decide whether a feedback cable makes its target per-voice; simplest is that it does not.
- Tests. An impulse through a loop repeats every 128 samples whatever the module order; a loop with gain 2 stays finite.

Already safe with loops, because they keep a visited set: _active (cw:5159), waves _cull (ww:5048), and the host's chainMods and chainReach (r:31059, r:31071). Not checked: alignChainTracks, Tidy up, the bypass path (_bypRun), custom-module capture, and the bottom-panel chain rail.

Limits worth knowing before saying yes:
- One block, not one sample. The loop delay is fixed at 128 samples (BLOCK, cw:19, ww:17), which puts a comb at 375 Hz at 48 kHz and 344 Hz at 44.1 kHz. That suits feedback through delays, reverbs, filters and cross-modulation. It does not give tuned plucked-string loops or sample-accurate FM feedback.
- Single-sample feedback is a rewrite. It means running the graph one sample at a time. If it is ever wanted, it belongs in the native core, not here.
- Native port. The C++ graph runners (stage N3) must apply the same fb rule. Having the flag in the song file makes that testable.
- Older builds. A song with a feedback cable should still play in an older build through the leftover ordering, possibly one block different. Not verified.

### Recommendation

Build summing with pass 4. Hold feedback until waves and chains are stable, as the owner suggested, then build it as one PR with the guard reviewed by the lead model.
