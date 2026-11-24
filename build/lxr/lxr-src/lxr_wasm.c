/*
 * lxr_wasm.c: a thin WebAssembly interface around the LXR-02 sound engine, for Thunder Plus.
 *
 * Copyright (C) 2026 the Thunder Plus authors. Built on the LXR firmware, Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This file is a modified version of parts of the LXR firmware (it reproduces what the firmware does when a kit
 * is loaded, a voice is triggered and a block of samples is mixed), and is licensed under the GNU General Public
 * License version 3, with the additional permission and terms in NOTICE.md. It is not official Erica Synths firmware.
 *
 * The firmware's own voice, modulation and parameter code is compiled unchanged. This file only:
 *   - holds the kit's parameter bytes (the firmware's activeParameters edit buffer),
 *   - loads them into the voices the way preset_sendDrumsetParametersImmediate() does on the machine
 *     (velocity and LFO destinations through the modTargets table, then every parameter in parameterOrder
 *     through midiParser_ccHandler()),
 *   - triggers a voice the way voiceControl_noteOn() does,
 *   - and renders one voice a block at a time the way mixer_calcNextSampleBlock() does for that voice
 *     (modulation reset, its LFO, its filter, its async and sync blocks, its sample-rate reduction),
 *     at the machine's own rate (REAL_FS, 44.14 kHz), as floats, before the pan and the FX bus.
 */
#include <stdint.h>
#include <string.h>
#include "stm32f7xx_hal.h"
#include "Parameters.h"
#include "menu/menu.h"
#include "Soundengine/config.h"
#include "Soundengine/DSPAudio/DrumVoice.h"
#include "Soundengine/DSPAudio/Snare.h"
#include "Soundengine/DSPAudio/HiHat.h"
#include "Soundengine/DSPAudio/CymbalVoice.h"
#include "Soundengine/DSPAudio/mixer.h"
#include "Soundengine/DSPAudio/modulationNode.h"
#include "Soundengine/DSPAudio/lfo.h"
#include "Soundengine/DSPAudio/ResonantFilter.h"
#include "Soundengine/MIDI/MidiMessages.h"
#include "Soundengine/MIDI/MidiParser.h"
#include "frontPanelParser_ui.h"   /* CC_2 */

void parameterArray_init(void);                                     /* MIDI/ParameterArray.c */
void mixer_decimateBlock(const uint8_t voiceNr, int16_t* buffer);   /* DSPAudio/mixer.c */

#define EXPORT(n) __attribute__((export_name(n)))

/* --- what the rest of the firmware would have provided ------------------------------------------------- */
uint8_t activeParameters[NUM_PARAMS];   /* menu.c: the kit being edited */
RNG_HandleTypeDef hrng;
LxrDwt lxr_dwt;
GPIO_TypeDef lxr_gpio;
static uint32_t rng_state = 0x2545F491u;
/* The machine's noise comes from the STM32's hardware random number generator. Here a xorshift, seeded per
   render so a pad renders the same way every time. */
uint32_t lxr_rng_next(void){ uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x; }
/* The voice volume faders (IO/adcPots.c) sit at the top: a pad's level is Thunder Plus's Level. */
float adc_getFaderVolume(uint8_t idx){ (void)idx; return 1.f; }
/* The tempo that tempo-synced LFOs follow (Sequencer/sequencer.c): the song's, set before each render. */
static uint16_t lxr_bpm = 120;
uint16_t seq_getBpm(void){ return lxr_bpm; }
EXPORT("lxr_set_bpm") void lxr_set_bpm(int bpm){ lxr_bpm = (uint16_t)(bpm < 20 ? 20 : bpm > 400 ? 400 : bpm); }
/* Track mutes and the menu page the FX bus shows: the sequencer and the screen, not the sound. */
void seq_setMute(uint8_t trackNr, uint8_t isMuted){ (void)trackNr; (void)isMuted; }
uint8_t menu_activePage = 0;   /* menu.c: never the sequencer page here, so a note always plays */
uint8_t menu_getSubPage(void){ return 0; }
void menu_switchSubPage(uint8_t subPageNr, uint8_t toggle){ (void)subPageNr; (void)toggle; }

extern uint8_t parameterOrder[END_OF_SOUND_PARAMETERS];   /* Parameters.c */
extern const ModTarg modTargets[];                        /* menu/Cc2Text.c */
extern const uint8_t initKit[];                           /* Preset/initKit.c */
uint8_t getNumModTargets(void);                           /* menu/Cc2Text.c */

static float out_buf[OUTPUT_DMA_SIZE * 64];

EXPORT("lxr_block") int lxr_block(void){ return OUTPUT_DMA_SIZE; }
EXPORT("lxr_rate") float lxr_rate(void){ return REAL_FS; }
EXPORT("lxr_num_params") int lxr_num_params(void){ return END_OF_SOUND_PARAMETERS; }
EXPORT("lxr_params") uint8_t* lxr_params(void){ return activeParameters; }
EXPORT("lxr_out") float* lxr_out(void){ return out_buf; }
EXPORT("lxr_num_mod_targets") int lxr_num_mod_targets(void){ return getNumModTargets(); }
EXPORT("lxr_mod_target_param") int lxr_mod_target_param(int i){ return (i >= 0 && i < getNumModTargets()) ? modTargets[i].param : -1; }

/* The firmware's init kit: 8 name bytes, then the parameters from number 0 (preset_init()). */
EXPORT("lxr_init_kit") void lxr_init_kit(void){
  memset(activeParameters, 0, sizeof(activeParameters));
  for (int i = 8; i < END_OF_SOUND_PARAMETERS; i++) activeParameters[i - 8] = initKit[i];
}

/* Power-on state (soundengine_init() without the codec, sequencer and MIDI). */
EXPORT("lxr_init") void lxr_init(uint32_t seed){
  rng_state = seed ? seed : 0x2545F491u;
  mixer_init();
  parameterArray_init();
  for (int i = 0; i < 6; i++) modNode_init(&velocityModulators[i]);
  initDrumVoice();
  Snare_init();
  HiHat_init();
  Cymbal_init();
}

/* preset_sendDrumsetParametersImmediate() for the bytes in activeParameters. */
EXPORT("lxr_load") void lxr_load(void){
  const uint8_t nmt = getNumModTargets();
  for (int i = 0; i < 6; i++){
    if (activeParameters[PAR_VEL_DEST_1 + i] >= nmt) activeParameters[PAR_VEL_DEST_1 + i] = 0;   /* preset_readDrumsetData() */
    if (activeParameters[PAR_TARGET_LFO1 + i] >= nmt) activeParameters[PAR_TARGET_LFO1 + i] = 0;
    if (activeParameters[PAR_VOICE_LFO1 + i] < 1 || activeParameters[PAR_VOICE_LFO1 + i] > 6) activeParameters[PAR_VOICE_LFO1 + i] = 1;
    /* FRONT_CC_VELO_TARGET and FRONT_CC_LFO_TARGET in CortexParser.c: an index into modTargets becomes its parameter */
    const uint8_t velo = (uint8_t)modTargets[activeParameters[PAR_VEL_DEST_1 + i]].param;
    modNode_setDestination(&velocityModulators[i], velo);
    const uint8_t lfoT = (uint8_t)modTargets[activeParameters[PAR_TARGET_LFO1 + i]].param;
    switch (i){
      case 0: case 1: case 2: modNode_setDestination(&voiceArray[i].lfo.modTarget, lfoT); break;
      case 3: modNode_setDestination(&snareVoice.lfo.modTarget, lfoT); break;
      case 4: modNode_setDestination(&cymbalVoice.lfo.modTarget, lfoT); break;
      default: modNode_setDestination(&hatVoice.lfo.modTarget, lfoT); break;
    }
  }
  MidiMsg msg = {0};
  for (uint32_t i = 0; i < END_OF_SOUND_PARAMETERS; i++){
    const uint8_t p = parameterOrder[i], val = activeParameters[p];
    if (p < 128){ msg.status = MIDI_CC; msg.data1 = (uint8_t)((p + 1) & 0x7f); }
    else { msg.status = CC_2; msg.data1 = (uint8_t)((p - 128) & 0x7f); }
    msg.data2 = val;
    midiParser_ccHandler(msg, 1, 0, 0.f);
  }
}

/* voiceControl_noteOn(): voices 0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat. */
EXPORT("lxr_trigger") void lxr_trigger(int voice, int vel, int note){
  if (voice < 3) Drum_trigger((uint8_t)voice, (uint8_t)vel, (uint8_t)note);
  else if (voice < 4) Snare_trigger((uint8_t)vel, (uint8_t)note);
  else if (voice < 5) Cymbal_trigger((uint8_t)vel, (uint8_t)note);
  else HiHat_trigger((uint8_t)vel, (uint8_t)(voice - 5), (uint8_t)note);
}

/* n blocks of one voice (0-2 drums, 3 snare, 4 cymbal, 5 hat) into out_buf, as mixer_calcNextSampleBlock()
   makes them before they are panned and routed. Returns the number of samples. */
EXPORT("lxr_render") int lxr_render(int voice, int n){
  if (n > 64) n = 64;
  int16_t data[OUTPUT_DMA_SIZE];
  Lfo* lfo = voice < 3 ? &voiceArray[voice].lfo : voice == 3 ? &snareVoice.lfo : voice == 4 ? &cymbalVoice.lfo : &hatVoice.lfo;
  ResonantFilter* flt = voice < 3 ? &voiceArray[voice].filter : voice == 3 ? &snareVoice.filter : voice == 4 ? &cymbalVoice.filter : &hatVoice.filter;
  for (int b = 0; b < n; b++){
    modNode_resetTargets();
    modNode_reassignVeloMod();
    lfo_dispatchNextValue(lfo);
    SVF_recalcFreq(flt);
    if (voice < 3){ calcDrumVoiceAsync((uint8_t)voice); calcDrumVoiceSyncBlock((uint8_t)voice, data, OUTPUT_DMA_SIZE); }
    else if (voice == 3){ Snare_calcAsync(); Snare_calcSyncBlock(data, OUTPUT_DMA_SIZE); }
    else if (voice == 4){ Cymbal_calcAsync(); Cymbal_calcSyncBlock(data, OUTPUT_DMA_SIZE); }
    else { HiHat_calcAsync(); HiHat_calcSyncBlock(data, OUTPUT_DMA_SIZE); }
    mixer_decimateBlock((uint8_t)voice, data);
    for (int i = 0; i < OUTPUT_DMA_SIZE; i++) out_buf[b * OUTPUT_DMA_SIZE + i] = data[i] * (1.f / 32768.f);
  }
  return n * OUTPUT_DMA_SIZE;
}
