/*
 * lxr_host_stubs.c: what the rest of the LXR firmware would provide, for a HOST build (Thunder Plus native module).
 *
 * Copyright (C) 2026 the Thunder Plus authors. SPDX-License-Identifier: GPL-3.0-only (with the additional permission and terms in lxr/NOTICE.md).
 *
 * lxr/src/lxr_wasm.c (the interface the web app calls) compiles MidiParser.c, which refers to the sequencer, the menu, the UART / USB MIDI
 * output and the system tick. The sound path never calls them: for the wasm, wasm-ld drops the unreachable functions. A static library on a
 * host linker keeps every object a symbol is taken from, so the references have to resolve: these definitions do nothing and are never
 * reached from lxr_render / lxr_trigger / lxr_load. Mirrors the dead-code set the wasm leaves out; if the firmware pin changes, the link
 * error names what to add here.
 */
#include <stdint.h>
#include "stm32f7xx_hal.h"
#include "Soundengine/config.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/Sequencer/sequencer.h"
#include "Parameters.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/FxBus.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/FX/FxRamBuffer.h"
extern unsigned char mixer_decimation_cnt[24], mixer_voice_samples[12];   /* sizes as the pinned firmware defines them (mixer.c) */
#include <string.h>

uint8_t frontParser_activeTrack;
volatile uint32_t systick_ticks;
PatternSet seq_patternSet;
uint8_t menu_getActiveVoice(void){ return 0; }
uint8_t menu_getViewedPattern(void){ return 0; }
void menu_repaint(void){}
void seq_addNote(uint8_t trackNr, uint8_t vel, uint8_t note){ (void)trackNr; (void)vel; (void)note; }
uint8_t seq_getExtSync(void){ return 0; }
uint8_t seq_isRunning(void){ return 0; }
uint8_t seq_isTrackMuted(uint8_t trackNr){ (void)trackNr; return 0; }
void seq_midiNoteOff(uint8_t chan){ (void)chan; }
void seq_recordAutomation(uint8_t voice, uint8_t dest, uint8_t value){ (void)voice; (void)dest; (void)value; }
void seq_sendMidiNoteOn(const uint8_t channel, const uint8_t note, const uint8_t veloc){ (void)channel; (void)note; (void)veloc; }
void seq_setNextPattern(const uint8_t patNr){ (void)patNr; }
void seq_setRunning(uint8_t isRunning){ (void)isRunning; }
void seq_sync(void){}
void sync_midiResetPosition(void){}
void sync_midiStartStop(uint8_t isStart){ (void)isStart; }
void uart_sendFrontpanelByte(uint8_t data){ (void)data; }
void uart_sendMidi(MidiMsg msg){ (void)msg; }
void usb_sendMidi(MidiMsg msg){ (void)msg; }
void voiceControl_noteOn(uint8_t voice, uint8_t note, uint8_t vel){ (void)voice; (void)note; (void)vel; }

/* ---- the power-on state -----------------------------------------------------------------------------------------------------------------
 * The firmware keeps its voices in global structs, and its init functions (soundengine_init() without the codec, as lxr_init() in lxr_wasm.c does it) leave some fields
 * of them alone: the web page restores the whole wasm memory to the copy taken at start-up before every render ("a freshly powered-on machine", lxr-engine.js attach()).
 * A host process has no such memory image, so the module copies the five structs the voices live in the first time it is used (before anything has touched them: the
 * state the process started with, the same bytes the wasm's data segment holds) and puts them back before every power-on. Found by diffing the process's data after
 * a run that dirtied every voice against the data of a fresh one: nothing else differed (native/tests/test_lxr.cpp checks the render is the same after any other). */
static unsigned char snap_voices[sizeof(voiceArray)], snap_snare[sizeof(snareVoice)], snap_hat[sizeof(hatVoice)], snap_cym[sizeof(cymbalVoice)], snap_vel[sizeof(velocityModulators)];
static unsigned char snap_fxbus[sizeof(fxBus)], snap_fxbuf[sizeof(FX_BUFFER)], snap_mix[sizeof(mixer_decimation_rate)+sizeof(mixer_audioRouting)+24+12];
static int snap_taken = 0;
void lxr_host_snapshot(void){
  if (snap_taken) return;
  memcpy(snap_voices, voiceArray, sizeof(voiceArray)); memcpy(snap_snare, &snareVoice, sizeof(snareVoice)); memcpy(snap_hat, &hatVoice, sizeof(hatVoice));
  memcpy(snap_cym, &cymbalVoice, sizeof(cymbalVoice)); memcpy(snap_vel, velocityModulators, sizeof(velocityModulators));
  memcpy(snap_fxbus, &fxBus, sizeof(fxBus)); memcpy(snap_fxbuf, FX_BUFFER, sizeof(FX_BUFFER));
  memcpy(snap_mix, mixer_decimation_rate, sizeof(mixer_decimation_rate)); memcpy(snap_mix+28, mixer_audioRouting, 6); memcpy(snap_mix+34, mixer_decimation_cnt, 24); memcpy(snap_mix+58, mixer_voice_samples, 12);
  snap_taken = 1;
}
void lxr_host_restore(void){
  if (!snap_taken) return;
  memcpy(voiceArray, snap_voices, sizeof(voiceArray)); memcpy(&snareVoice, snap_snare, sizeof(snareVoice)); memcpy(&hatVoice, snap_hat, sizeof(hatVoice));
  memcpy(&cymbalVoice, snap_cym, sizeof(cymbalVoice)); memcpy(velocityModulators, snap_vel, sizeof(velocityModulators));
  memcpy(&fxBus, snap_fxbus, sizeof(fxBus)); memcpy(FX_BUFFER, snap_fxbuf, sizeof(FX_BUFFER));
  memcpy(mixer_decimation_rate, snap_mix, sizeof(mixer_decimation_rate)); memcpy(mixer_audioRouting, snap_mix+28, 6); memcpy(mixer_decimation_cnt, snap_mix+34, 24); memcpy(mixer_voice_samples, snap_mix+58, 12);
}
/* the parameter numbers the page sets by hand (lxr-engine.js render): PAR_VEL_DEST_1, PAR_TARGET_LFO1, PAR_VOICE_LFO1 */
int lxr_host_par(int which){ return which == 0 ? PAR_VEL_DEST_1 : which == 1 ? PAR_TARGET_LFO1 : which == 2 ? PAR_VOICE_LFO1 : -1; }
