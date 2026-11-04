# Menu and settings audit (2 Nov 2026)

Owner: "Too much text in the Audio & MIDI menu. Audit Edit, View, right-click menus on different screens, and Settings
for redundancy and organization… they feel cumbersome and a little unprofessional." Taken from the menus as they render
in build 356 on a 1600 px desktop window. It started as a proposal; everything in it has since been built (see Status at the end).

## What makes them feel cumbersome (the patterns)

1. **Explanations live in the menus.** Paragraphs under Audio & MIDI, View (Text size, Mixer size, Follow, the command bar)
   and Settings (FLX, IRs, render, mastering). Pro apps put one short label in the menu and the explanation in a tooltip or
   a Help link. Move every paragraph to `title` tooltips and Help; keep at most one short line.
2. **Labels explain themselves in parentheses.** "Remove the song row (rows below move up)", "Clone (paste puts a unique
   copy)", "Delete (the phrase too)", "Mix-paste (only into empty cells)". Short verb labels; the detail in the tooltip.
3. **Menus repeat what is elsewhere.** Kit is in File ▸ Kit *and* ⚙ Settings; Master and Master out are in ⚙ *and* the
   Mixer; render settings in ⚙ *and* File's Render; `build 356` at the bottom of View *and* ⚙.
4. **Right-click menus carry the whole command list.** The Song block's right-click has 35 items, including Undo / Redo,
   Value up / down, Previous / Next block, Start a selection, Select all and Tools ▸. A right-click menu should only hold
   what acts on the thing under the pointer. The Synth pad's (5 items) is the model.
5. **One-of-many choices drawn as long lists.** MIDI "Notes in" shows Every input + All channels + Channel 1…16 as 18 lines;
   ports A–D are four separate blocks plus a separate Clock out row.
6. **Inconsistent wording.** "Remove row 002" vs "Remove the song row"; "Mute / unmute" vs "Mute / unmute this track";
   "Duplicate the pattern" and "Duplicate the track" both show **Ctrl+D** (a real conflict to resolve).

## Proposals, by menu

**Audio & MIDI** (worst offender) — **done 3 Nov** (see HANDOVER "3 Nov: Audio & MIDI ports table")
- Two tabs or two clear sections: AUDIO and MIDI, no paragraphs.
- AUDIO: Output (dropdown) · Test · Level · Latency (Lowest / Balanced / Safest) · Rate · Latency comp · …pump live, one line
  each; "Reset audio engine" as a small button at the bottom.
- MIDI: Connect / status line · Notes in: two dropdowns (input, channel) · Keys play: dropdown · Learn / Unlearn / Reset ·
  Velocity on/off.
- A **ports table**: rows A–D, columns Device (dropdown) · Delay (ms) · Clock out (checkbox). Clock in + Clock from on one
  line under it.

**Edit ▾** (grouped, short, no parentheses)
- Undo · Redo
- Cut · Copy · Paste · Paste into empty cells
- Select all · Clear selection (drop "Start a selection" from the menu; keep Ctrl+B)
- Rows: Insert row · Remove rows
- Blocks (Song): Clone · Clone down · Duplicate pattern · Make unique · Remove from song · Delete with phrase
- Track: Duplicate track · Duplicate with notes · Name… · Mute · Solo
- Loop: Loop pattern · Loop selection
- **Generators ▸** (done 2 Nov) · **Tools ▸**
- Move Value up / down and Previous / Next to the keyboard help (they are keys, not menu commands). Resolve Ctrl+D.

**View ▾**
- Text size and Mixer size as two compact −/+ rows without paragraphs.
- Follow: OFF / PATTERN / ON, one line.
- Command bar: **Off / Thin / Big** as one choice (it is two toggles today).
- Simple phrase screen, Screen settings…, Skin… stay. Remove `build 356` (it belongs in Help / About).

**⚙ Settings**
- Today it mixes song settings, render settings, mastering, kit, app and storage. Suggest:
  - Song settings… (already a sheet): add the FLX compatibility switch and the IR slots there, out of the main menu.
  - Render settings live with File ▸ Render (one "Render settings…" item there).
  - Master / Master out stay in the Mixer only (the mastering window keeps its own).
  - Kit stays in File ▸ Kit only.
  - What's left in ⚙ becomes **Preferences**: App (Mobile / Desktop), Input, Screen, Storage, Updates, Help.

**Right-click menus**
- Song / Arrange block: Open phrase · Copy · Cut · Paste here · Clone · Clone down · Make unique · Mute · Colour… ·
  Remove from song · Delete with phrase, then a row group: Insert row · Remove rows · Loop rows · Name section…
- Phrase / Pattern: Cut · Copy · Paste · Paste into empty · Interpolate · Transpose ▸ · Volume… · Instrument… ·
  **Generators ▸** · Render to slot…
- Mixer: Mute · Solo · Name… · Colour… · Open FX · Open …chains.
- No Undo / Redo / navigation / value-stepping in any right-click menu.

**The File dot** means unsaved changes. Give it a tooltip ("Unsaved changes") so it explains itself.

## Order to build it in (each small, on `beta`)
1. Audio & MIDI rebuilt as above (biggest win, self-contained). **Done 3 Nov.** (2–5 were done in build 358.)
2. Tooltips instead of paragraphs across View and ⚙.
3. Right-click menus trimmed to their context.
4. Edit ▾ regrouped, labels shortened, Ctrl+D conflict resolved.
5. ⚙ → Preferences, with the moves above.

**Status (4 Nov, from HANDOVER):** all five are built: 1 on 3 Nov, 2–5 in build 358. Since then Ctrl+D in Edit ▾ shows only
on the duplicate it does on the screen you're on (build 427). The proposals above are kept as the record of why.
