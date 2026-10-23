/* lxr/lxr-engine.js: the LXR-02's drum, snare, cymbal and hi-hat voices as Thunder Plus engines.
 *
 * Copyright (C) 2026 the Thunder Plus authors. The sound engine is the LXR firmware (lxr/lxr.wasm, built from
 * lxr/src), Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only. See lxr/LICENSE and lxr/NOTICE.md (and their additional terms).
 *
 * Not official Erica Synths firmware or software, and not affiliated with or endorsed by Erica Synths.
 * "Erica Synths", "LXR" and "LXR-02" are Erica Synths' names.
 *
 * This folder is self-contained. index.html loads this file if it is there (the page and the render Worker); without
 * the folder Thunder Plus runs exactly as before, with no "Firmware" engines. Remove lxr/ to ship without GPL code.
 *
 * The voice parameters are the machine's own: 0-127 knob values with the same pages, value names and ranges as its
 * menus (read from the firmware's tables by lxr/tools/lxr_meta.py into META below). A pad renders the way the machine
 * sounds a hit: power-on state, the init kit, this pad's values loaded as a kit is, the voice triggered, then the
 * voice's own blocks at 44.14 kHz, resampled to the app's rate.
 */
(function(G){
'use strict';
const META = {"nparams":247,"rate":44140,"par":{"PAR_NONE":0,"PAR_MOD_WHEEL":1,"PAR_OSC_WAVE_DRUM1":1,"PAR_OSC_WAVE_DRUM2":2,"PAR_OSC_WAVE_DRUM3":3,"PAR_OSC_WAVE_SNARE":4,"PAR_NRPN_DATA_ENTRY_COARSE":5,"PAR_WAVE1_CYM":6,"PAR_WAVE1_HH":7,"PAR_COARSE1":8,"PAR_FINE1":9,"PAR_COARSE2":10,"PAR_FINE2":11,"PAR_COARSE3":12,"PAR_FINE3":13,"PAR_COARSE4":14,"PAR_FINE4":15,"PAR_COARSE5":16,"PAR_FINE5":17,"PAR_COARSE6":18,"PAR_FINE6":19,"PAR_MOD_WAVE_DRUM1":20,"PAR_MOD_WAVE_DRUM2":21,"PAR_MOD_WAVE_DRUM3":22,"PAR_WAVE2_CYM":23,"PAR_WAVE3_CYM":24,"PAR_WAVE2_HH":25,"PAR_WAVE3_HH":26,"PAR_NOISE_FREQ1":27,"PAR_MIX1":28,"PAR_MOD_OSC_F1_CYM":29,"PAR_MOD_OSC_F2_CYM":30,"PAR_MOD_OSC_GAIN1_CYM":31,"PAR_MOD_OSC_GAIN2_CYM":32,"PAR_MOD_OSC_F1":33,"PAR_MOD_OSC_F2":34,"PAR_MOD_OSC_GAIN1":35,"PAR_MOD_OSC_GAIN2":36,"PAR_FILTER_FREQ_1":37,"PAR_FILTER_FREQ_2":38,"PAR_FILTER_FREQ_3":39,"PAR_FILTER_FREQ_4":40,"PAR_FILTER_FREQ_5":41,"PAR_FILTER_FREQ_6":42,"PAR_RESO_1":43,"PAR_RESO_2":44,"PAR_RESO_3":45,"PAR_RESO_4":46,"PAR_RESO_5":47,"PAR_RESO_6":48,"PAR_VELOA1":49,"PAR_VELOD1":50,"PAR_VELOA2":51,"PAR_VELOD2":52,"PAR_VELOA3":53,"PAR_VELOD3":54,"PAR_VELOA4":55,"PAR_VELOD4":56,"PAR_VELOA5":57,"PAR_VELOD5":58,"PAR_VELOA6":59,"PAR_VELOD6_CLOSED":60,"PAR_VELOD6_OPEN":61,"PAR_VOL_SLOPE1":62,"PAR_VOL_SLOPE2":63,"PAR_VOL_SLOPE3":64,"PAR_VOL_SLOPE4":65,"PAR_VOL_SLOPE5":66,"PAR_VOL_SLOPE6":67,"PAR_REPEAT4":68,"PAR_REPEAT5":69,"PAR_MOD_EG1":70,"PAR_MOD_EG2":71,"PAR_MOD_EG3":72,"PAR_MOD_EG4":73,"PAR_MODAMNT1":74,"PAR_MODAMNT2":75,"PAR_MODAMNT3":76,"PAR_MODAMNT4":77,"PAR_PITCH_SLOPE1":78,"PAR_PITCH_SLOPE2":79,"PAR_PITCH_SLOPE3":80,"PAR_PITCH_SLOPE4":81,"PAR_FMAMNT1":82,"PAR_FM_FREQ1":83,"PAR_FMAMNT2":84,"PAR_FM_FREQ2":85,"PAR_FMAMNT3":86,"PAR_FM_FREQ3":87,"PAR_VOL1":88,"PAR_VOL2":89,"PAR_VOL3":90,"PAR_VOL4":91,"PAR_VOL5":92,"PAR_VOL6":93,"PAR_PAN1":94,"PAR_PAN2":95,"PAR_PAN3":96,"PAR_NRPN_FINE":97,"PAR_NRPN_COARSE":98,"PAR_PAN4":99,"PAR_PAN5":100,"PAR_PAN6":101,"PAR_DRIVE1":102,"PAR_DRIVE2":103,"PAR_DRIVE3":104,"PAR_SNARE_DISTORTION":105,"PAR_CYMBAL_DISTORTION":106,"PAR_HAT_DISTORTION":107,"PAR_VOICE_DECIMATION1":108,"PAR_VOICE_DECIMATION2":109,"PAR_VOICE_DECIMATION3":110,"PAR_VOICE_DECIMATION4":111,"PAR_VOICE_DECIMATION5":112,"PAR_VOICE_DECIMATION6":113,"PAR_VOICE_DECIMATION_ALL":114,"PAR_FREQ_LFO1":115,"PAR_FREQ_LFO2":116,"PAR_FREQ_LFO3":117,"PAR_FREQ_LFO4":118,"PAR_FREQ_LFO5":119,"PAR_FREQ_LFO6":120,"PAR_AMOUNT_LFO1":121,"PAR_AMOUNT_LFO2":122,"PAR_AMOUNT_LFO3":123,"PAR_AMOUNT_LFO4":124,"PAR_AMOUNT_LFO5":125,"PAR_AMOUNT_LFO6":126,"PAR_BANK_CHANGE":127,"PAR_FILTER_DRIVE_1":128,"PAR_FILTER_DRIVE_2":129,"PAR_FILTER_DRIVE_3":130,"PAR_FILTER_DRIVE_4":131,"PAR_FILTER_DRIVE_5":132,"PAR_FILTER_DRIVE_6":133,"PAR_MIX_MOD_1":134,"PAR_MIX_MOD_2":135,"PAR_MIX_MOD_3":136,"PAR_VOLUME_MOD_ON_OFF1":137,"PAR_VOLUME_MOD_ON_OFF2":138,"PAR_VOLUME_MOD_ON_OFF3":139,"PAR_VOLUME_MOD_ON_OFF4":140,"PAR_VOLUME_MOD_ON_OFF5":141,"PAR_VOLUME_MOD_ON_OFF6":142,"PAR_VELO_MOD_AMT_1":143,"PAR_VELO_MOD_AMT_2":144,"PAR_VELO_MOD_AMT_3":145,"PAR_VELO_MOD_AMT_4":146,"PAR_VELO_MOD_AMT_5":147,"PAR_VELO_MOD_AMT_6":148,"PAR_VEL_DEST_1":149,"PAR_VEL_DEST_2":150,"PAR_VEL_DEST_3":151,"PAR_VEL_DEST_4":152,"PAR_VEL_DEST_5":153,"PAR_VEL_DEST_6":154,"PAR_WAVE_LFO1":155,"PAR_WAVE_LFO2":156,"PAR_WAVE_LFO3":157,"PAR_WAVE_LFO4":158,"PAR_WAVE_LFO5":159,"PAR_WAVE_LFO6":160,"PAR_VOICE_LFO1":161,"PAR_VOICE_LFO2":162,"PAR_VOICE_LFO3":163,"PAR_VOICE_LFO4":164,"PAR_VOICE_LFO5":165,"PAR_VOICE_LFO6":166,"PAR_TARGET_LFO1":167,"PAR_TARGET_LFO2":168,"PAR_TARGET_LFO3":169,"PAR_TARGET_LFO4":170,"PAR_TARGET_LFO5":171,"PAR_TARGET_LFO6":172,"PAR_RETRIGGER_LFO1":173,"PAR_RETRIGGER_LFO2":174,"PAR_RETRIGGER_LFO3":175,"PAR_RETRIGGER_LFO4":176,"PAR_RETRIGGER_LFO5":177,"PAR_RETRIGGER_LFO6":178,"PAR_SYNC_LFO1":179,"PAR_SYNC_LFO2":180,"PAR_SYNC_LFO3":181,"PAR_SYNC_LFO4":182,"PAR_SYNC_LFO5":183,"PAR_SYNC_LFO6":184,"PAR_OFFSET_LFO1":185,"PAR_OFFSET_LFO2":186,"PAR_OFFSET_LFO3":187,"PAR_OFFSET_LFO4":188,"PAR_OFFSET_LFO5":189,"PAR_OFFSET_LFO6":190,"PAR_FILTER_TYPE_1":191,"PAR_FILTER_TYPE_2":192,"PAR_FILTER_TYPE_3":193,"PAR_FILTER_TYPE_4":194,"PAR_FILTER_TYPE_5":195,"PAR_FILTER_TYPE_6":196,"PAR_TRANS1_VOL":197,"PAR_TRANS2_VOL":198,"PAR_TRANS3_VOL":199,"PAR_TRANS4_VOL":200,"PAR_TRANS5_VOL":201,"PAR_TRANS6_VOL":202,"PAR_TRANS1_WAVE":203,"PAR_TRANS2_WAVE":204,"PAR_TRANS3_WAVE":205,"PAR_TRANS4_WAVE":206,"PAR_TRANS5_WAVE":207,"PAR_TRANS6_WAVE":208,"PAR_TRANS1_FREQ":209,"PAR_TRANS2_FREQ":210,"PAR_TRANS3_FREQ":211,"PAR_TRANS4_FREQ":212,"PAR_TRANS5_FREQ":213,"PAR_TRANS6_FREQ":214,"PAR_AUDIO_OUT1":215,"PAR_AUDIO_OUT2":216,"PAR_AUDIO_OUT3":217,"PAR_AUDIO_OUT4":218,"PAR_AUDIO_OUT5":219,"PAR_AUDIO_OUT6":220,"PAR_UNUSED_1":221,"PAR_UNUSED_2":222,"PAR_UNUSED_3":223,"PAR_UNUSED_4":224,"PAR_UNUSED_5":225,"PAR_UNUSED_6":226,"PAR_UNUSED_7":227,"PAR_PWM_1":228,"PAR_PWM_2":229,"PAR_PWM_3":230,"PAR_PWM_4":231,"PAR_PWM_5":232,"PAR_PWM_6":233,"PAR_FX_TYPE":234,"PAR_FX_ROUTING":235,"PAR_FX_DRY_WET":236,"PAR_FX_DIST_TYPE":237,"PAR_FX_P1":238,"PAR_FX_P2":239,"PAR_FX_P3":240,"PAR_FX_P4":241,"PAR_FX_RATIO":242,"PAR_FX_WAVE":243,"PAR_FX_REVERB_TYPE":244,"PAR_FX_DELAY_TYPE":245,"PAR_MORPH_KIT_DEST":246,"END_OF_SOUND_PARAMETERS":247,"PAR_ROLL":247,"PAR_MORPH":248,"PAR_ACTIVE_STEP":249,"PAR_STEP_VOLUME":250,"PAR_STEP_PROB":251,"PAR_STEP_NOTE":252,"PAR_EUKLID_LENGTH":253,"PAR_EUKLID_STEPS":254,"PAR_EUKLID_ROTATION":255,"PAR_AUTOM_TRACK":256,"PAR_P1_DEST":257,"PAR_P2_DEST":258,"PAR_P1_VAL":259,"PAR_P2_VAL":260,"PAR_SHUFFLE":261,"PAR_UNUSED":262,"PAR_PATTERN_NEXT":263,"PAR_TRACK_LENGTH":264,"PAR_POS_Y":266,"PAR_FLUX":267,"PAR_SOM_FREQ":268,"PAR_TRACK_ROTATION":269,"PAR_STEP_FLAM":270,"PAR_STEP_SHIFT":271,"PAR_BEGINNING_OF_GLOBALS":272,"PAR_BPM":272,"PAR_MIDI_CHAN_1":273,"PAR_MIDI_CHAN_2":274,"PAR_MIDI_CHAN_3":275,"PAR_MIDI_CHAN_4":276,"PAR_MIDI_CHAN_5":277,"PAR_MIDI_CHAN_6":278,"PAR_FETCH":279,"PAR_FOLLOW":280,"PAR_QUANTISATION":281,"PAR_SCREENSAVER_ON_OFF":282,"PAR_MIDI_MODE":283,"PAR_MIDI_CHAN_7":284,"PAR_MIDI_ROUTING":285,"PAR_MIDI_FILT_TX":286,"PAR_MIDI_FILT_RX":287,"PAR_PRESCALER_CLOCK_IN":288,"PAR_PRESCALER_CLOCK_OUT1":289,"PAR_PRESCALER_CLOCK_OUT2":290,"PAR_TRIG_GATE_MODE":291,"PAR_BAR_RESET_MODE":292,"PAR_MIDI_CHAN_GLOBAL":293,"PAR_MIDI_NOTE1":294,"PAR_MIDI_NOTE2":295,"PAR_MIDI_NOTE3":296,"PAR_MIDI_NOTE4":297,"PAR_MIDI_NOTE5":298,"PAR_MIDI_NOTE6":299,"PAR_MIDI_NOTE7":300,"PAR_PATTERN_MODE":301,"NUM_PARAMS":302},"init":[0,0,0,0,1,0,4,0,31,63,41,63,58,63,45,64,127,63,50,63,0,0,0,0,0,0,0,127,61,0,0,0,0,56,127,127,127,127,127,127,127,68,104,0,0,0,0,0,0,0,21,0,13,0,3,0,10,0,107,0,5,17,46,11,4,10,0,6,0,0,26,93,7,14,44,14,127,81,22,10,14,15,0,0,0,0,0,0,127,127,127,127,127,127,63,63,63,0,0,63,63,63,63,63,63,63,63,63,127,127,127,127,127,127,127,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],"pages":[[[[1,8],[2,9],[33,1],[85,228],[0,0],[0,0],[0,0],[0,0]],[[3,49],[4,50],[30,62],[0,0],[0,0],[0,0],[0,0],[0,0]],[[5,70],[30,78],[6,74],[70,0],[59,149],[60,143],[61,137],[0,0]],[[7,82],[8,83],[33,20],[56,134],[0,0],[0,0],[0,0],[0,0]],[[34,203],[35,197],[36,209],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,37],[16,43],[17,191],[58,128],[0,0],[0,0],[0,0],[0,0]],[[22,115],[26,179],[23,121],[24,155],[27,173],[28,185],[29,161],[25,167]],[[10,88],[11,94],[53,108],[9,102],[52,215],[45,264],[51,273],[42,294]]],[[[1,10],[2,11],[33,2],[85,229],[0,0],[0,0],[0,0],[0,0]],[[3,51],[4,52],[30,63],[0,0],[0,0],[0,0],[0,0],[0,0]],[[5,71],[30,79],[6,75],[70,0],[59,150],[60,144],[61,138],[0,0]],[[7,84],[8,85],[33,21],[56,135],[0,0],[0,0],[0,0],[0,0]],[[34,204],[35,198],[36,210],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,38],[16,44],[17,192],[58,129],[0,0],[0,0],[0,0],[0,0]],[[22,116],[26,180],[23,122],[24,156],[27,174],[28,186],[29,162],[25,168]],[[10,89],[11,95],[53,109],[9,103],[52,216],[45,264],[51,274],[42,295]]],[[[1,12],[2,13],[33,3],[85,230],[0,0],[0,0],[0,0],[0,0]],[[3,53],[4,54],[30,64],[0,0],[0,0],[0,0],[0,0],[0,0]],[[5,72],[30,80],[6,76],[70,0],[59,151],[60,145],[61,139],[0,0]],[[7,86],[8,87],[33,22],[56,136],[0,0],[0,0],[0,0],[0,0]],[[34,205],[35,199],[36,211],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,39],[16,45],[17,193],[58,130],[0,0],[0,0],[0,0],[0,0]],[[22,117],[26,181],[23,123],[24,157],[27,175],[28,187],[29,163],[25,169]],[[10,90],[11,96],[53,110],[9,104],[52,217],[45,264],[51,275],[42,296]]],[[[1,14],[2,15],[12,27],[13,28],[33,4],[85,231],[0,0],[0,0]],[[3,55],[4,56],[14,68],[30,65],[0,0],[0,0],[0,0],[0,0]],[[5,73],[30,81],[6,77],[70,0],[59,152],[60,146],[61,140],[0,0]],[[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0],[0,0]],[[34,206],[35,200],[36,212],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,40],[16,46],[17,194],[58,131],[0,0],[0,0],[0,0],[0,0]],[[22,118],[26,182],[23,124],[24,158],[27,176],[28,188],[29,164],[25,170]],[[10,91],[11,99],[53,111],[9,105],[52,218],[45,264],[51,276],[42,297]]],[[[1,16],[2,17],[33,6],[85,232],[0,0],[0,0],[0,0],[0,0]],[[3,57],[4,58],[14,69],[30,66],[0,0],[0,0],[0,0],[0,0]],[[59,153],[60,147],[61,141],[0,0],[0,0],[0,0],[0,0],[0,0]],[[18,29],[19,30],[20,31],[21,32],[33,23],[33,24],[0,0],[0,0]],[[34,207],[35,201],[36,213],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,41],[16,47],[17,195],[58,132],[0,0],[0,0],[0,0],[0,0]],[[22,119],[26,183],[23,125],[24,159],[27,177],[28,189],[29,165],[25,171]],[[10,92],[11,100],[53,112],[9,106],[52,219],[45,264],[51,277],[42,298]]],[[[1,18],[2,19],[33,7],[85,233],[0,0],[0,0],[0,0],[0,0]],[[3,59],[31,60],[30,67],[0,0],[0,0],[0,0],[0,0],[0,0]],[[59,154],[60,148],[61,142],[0,0],[0,0],[0,0],[0,0],[0,0]],[[18,33],[19,34],[20,35],[21,36],[33,25],[33,26],[0,0],[0,0]],[[34,208],[35,202],[36,214],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,42],[16,48],[17,196],[58,133],[0,0],[0,0],[0,0],[0,0]],[[22,120],[26,184],[23,126],[24,160],[27,178],[28,190],[29,166],[25,172]],[[10,93],[11,101],[53,113],[9,107],[52,220],[45,264],[51,278],[42,299]]],[[[1,18],[2,19],[33,7],[0,0],[0,0],[0,0],[0,0],[0,0]],[[3,59],[32,61],[30,67],[0,0],[0,0],[0,0],[0,0],[0,0]],[[59,154],[60,148],[61,142],[0,0],[0,0],[0,0],[0,0],[0,0]],[[18,33],[19,34],[20,35],[21,36],[33,25],[33,26],[0,0],[0,0]],[[34,208],[35,202],[36,214],[0,0],[0,0],[0,0],[0,0],[0,0]],[[15,42],[16,48],[17,196],[58,133],[0,0],[0,0],[0,0],[0,0]],[[22,120],[26,184],[23,126],[24,160],[27,178],[28,190],[29,166],[25,172]],[[10,93],[11,101],[53,113],[9,107],[52,220],[45,264],[51,284],[42,300]]]],"names":[["","",""],["coa","Oscilltr","Coarse"],["fin","Oscilltr","Fine"],["atk","Veloc EG","Attack"],["dec","Veloc EG","Decay"],["dec","Mod EG","Decay"],["mod","PitchMod","Amount"],["amt","FM","Amount"],["frq","FM","Frequncy"],["drv","Voice","Overdriv"],["vol","Voice","Volume"],["pan","Voice","Panning"],["noi","Noise","Frequncy"],["mix","Nois/Osc","Mix"],["rpt","Veloc EG","RepeatCt"],["frq","Filter","Frequncy"],["res","Filter","Resnance"],["typ","Filter","Type"],["f1","Mod Osc","Freqcy 1"],["f2","Mod Osc","Freqcy 2"],["g1","Mod Osc","Gain 1"],["g2","Mod Osc","Gain 2"],["frq","LFO","Frequncy"],["mod","LFO","Amount"],["wav","LFO","Waveform"],["dst","LFO","DstParam"],["snc","LFO","ClockSnc"],["rtg","LFO","Retriggr"],["ofs","LFO","Offset"],["voi","LFO","DstVoice"],["slp","Veloc EG","Slope"],["d1","Veloc EG","Dcy Clsd"],["d2","Veloc EG","Dcy Open"],["wav","Oscilltr","Waveform"],["wav","Transnt","Waveform"],["vol","Transnt","Volume"],["frq","Transnt","Frequncy"],["eqg","EQ","Gain"],["eqf","EQ","Frequncy"],["rol","Pattern","RollRate"],["mrp","Sound","Morph"],["vel","Step","Velocity"],["nte","Step","Note"],["prb","Step","Prbablty"],["stp","Step","Number"],["len","Pattern","Length"],["stp","Euklid","Steps"],["rot","Euklid","Rotation"],["flm","Step","Flam"],["shf","Step","Shift"],["bpm","Global","Tempo"],["ch","Voice","Channel"],["out","Voice","AudioOut"],["srt","Voice","SampleRt"],["rpt","Pattern","RepeatCt"],["nxt","Pattern","NextPatt"],["mod","Oscilltr","Mode"],["vol","Oscilltr","Volume"],["drv","Filter","Overdriv"],["dst","Velocity","DstParam"],["amt","Velocity","Amount"],["vol","Velocity","Vol mod"],["fch","Parametr","Fetch"],["flw","Sequencr","Follow"],["qnt","Sequencr","Quantize"],["trk","Sequencr","AutTrack"],["dst","Sequencr","Aut Dest"],["val","Sequencr","AutValue"],["shu","Sequencr","Shuffle"],["ssv","Global","Screensv"],["","",""],["x","Generatr","X Positn"],["y","Generatr","Y Positn"],["flx","Generatr","Flux"],["frq","Generatr","Frequncy"],["mid","MIDI","Mode"],["mrt","MIDI","Routing"],["mtx","MIDI","Transmit"],["mrx","MIDI","Receive"],["cki","Clock","In PPQ"],["cko","Clock","Out1 PPQ"],["co2","Clock","Out2 PPQ"],["mod","Clock","GateMode"],["pcr","Sequencr","PCReset"],["ch","MIDI","Channel"],["pwm","Oscilltr","PulsWdth"],["typ","Fx","Type"],["out","Fx","AudioOut"],["d/w","Fx","Dry/Wet"],["drv","Fx","Overdriv"],["col","Fx","Color"],["rat","Fx","Ratio"],["atk","Fx","Attack"],["dec","Fx","Decay"],["tre","Fx","Threshld"],["frq","Fx","Frequncy"],["wav","Fx","Waveform"],["vol","Fx","Volume"],["ton","Fx","Tone"],["gai","Fx","Gain"],["fbk","Fx","Feedback"],["rng","Fx","Range"],["tim","Fx","Time"],["pre","Fx","PreDelay"],["ref","Fx","Reflxion"],["p1","Fx","Param 1"],["p2","Fx","Param 2"],["p3","Fx","Param 3"],["p4","Fx","Param 4"],["kcm","Global","KitChMde"]],"dtypes":[[1,0],[3,2],[3,2],[3,2],[3,2],[1,0],[3,2],[3,2],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[4,0],[3,2],[3,2],[3,2],[3,2],[3,2],[3,2],[3,2],[1,0],[1,0],[4,0],[4,0],[1,0],[1,0],[4,0],[4,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[4,0],[1,0],[4,0],[1,0],[4,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[4,0],[4,0],[4,0],[1,0],[1,0],[4,0],[4,0],[4,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[3,15],[3,15],[3,15],[6,0],[6,0],[6,0],[6,0],[6,0],[6,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[8,0],[8,0],[8,0],[8,0],[8,0],[8,0],[3,6],[3,6],[3,6],[3,6],[3,6],[3,6],[9,0],[9,0],[9,0],[9,0],[9,0],[9,0],[7,0],[7,0],[7,0],[7,0],[7,0],[7,0],[3,7],[3,7],[3,7],[3,7],[3,7],[3,7],[3,5],[3,5],[3,5],[3,5],[3,5],[3,5],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[3,1],[3,1],[3,1],[3,1],[3,1],[3,1],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[3,10],[3,10],[3,10],[3,10],[3,10],[3,10],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[3,3],[3,3],[3,3],[3,3],[3,3],[3,3],[12,0],[12,0],[12,0],[12,0],[12,0],[12,0],[12,0],[1,0],[1,0],[1,0],[1,0],[1,0],[1,0],[3,18],[3,26],[15,0],[3,19],[1,0],[1,0],[1,0],[1,0],[3,20],[3,2],[3,22],[3,21],[1,0]],"menus":{"1":["LP","HP","BP","UBP","Nch","Pek","LP2","off"],"2":["Sin","Tri","Saw","Rec","Noi","PWM"],"5":["off","4/1","2/1","1/1","1/2","1/3","1/4","1/6","1/8","12","16","32"],"6":["sin","tri","sup","sdn","sqr","rnd","xup","xdn"],"7":["off","v1","v2","v3","v4","v5","v6"],"10":["Snp","Ofs","Clk","Ck2","Tik","Kik","Rim","Drp","Hat","Clp","Kk2","Snr","Tom","Sp2"],"15":["FM","Mix"],"3":["St1","St2","L1","R1","L2","R2","FX"]},"dtypeIds":{"0B127":1,"PM63":4,"MENU":3,"ON_OFF":6,"LFO_TARGET":7,"VELO_TARGET":8,"VOICE_LFO":9,"0B255":0,"1B16":5,"PM100":2,"0B100":15,"0B15":13,"1B64":14,"NOTE":12,"0b1":11},"modTargets":[[0,0],[53,114],[1,8],[2,9],[33,1],[85,228],[3,49],[4,50],[30,62],[5,70],[30,78],[6,74],[59,149],[60,143],[61,137],[7,82],[8,83],[33,20],[56,134],[34,203],[35,197],[36,209],[15,37],[16,43],[17,191],[58,128],[22,115],[26,179],[23,121],[24,155],[27,173],[28,185],[10,88],[11,94],[53,108],[9,102],[105,238],[106,239],[107,240],[108,241],[1,10],[2,11],[33,2],[85,229],[3,51],[4,52],[30,63],[5,71],[30,79],[6,75],[59,150],[60,144],[61,138],[7,84],[8,85],[33,21],[56,135],[34,204],[35,198],[36,210],[15,38],[16,44],[17,192],[58,129],[22,116],[26,180],[23,122],[24,156],[27,174],[28,186],[10,89],[11,95],[53,109],[9,103],[105,238],[106,239],[107,240],[108,241],[1,12],[2,13],[33,3],[85,230],[3,53],[4,54],[30,64],[5,72],[30,80],[6,76],[59,151],[60,145],[61,139],[7,86],[8,87],[33,22],[56,136],[34,205],[35,199],[36,211],[15,39],[16,45],[17,193],[58,130],[22,117],[26,181],[23,123],[24,157],[27,175],[28,187],[10,90],[11,96],[53,110],[9,104],[105,238],[106,239],[107,240],[108,241],[1,14],[2,15],[12,27],[13,28],[33,4],[85,231],[3,55],[4,56],[14,68],[30,65],[5,73],[30,81],[6,77],[59,152],[60,146],[61,140],[34,206],[35,200],[36,212],[15,40],[16,46],[17,194],[58,131],[22,118],[26,182],[23,124],[24,158],[27,176],[28,188],[10,91],[11,99],[53,111],[9,105],[105,238],[106,239],[107,240],[108,241],[1,16],[2,17],[33,6],[85,232],[3,57],[4,58],[14,69],[30,66],[59,153],[60,147],[61,141],[18,29],[19,30],[20,31],[21,32],[33,23],[33,24],[34,207],[35,201],[36,213],[15,41],[16,47],[17,195],[58,132],[22,119],[26,183],[23,125],[24,159],[27,177],[28,189],[10,92],[11,100],[53,112],[9,106],[105,238],[106,239],[107,240],[108,241],[1,18],[2,19],[33,7],[85,233],[3,59],[31,60],[32,61],[30,67],[59,154],[60,148],[61,142],[18,33],[19,34],[20,35],[21,36],[33,25],[33,26],[34,208],[35,202],[36,214],[15,42],[16,48],[17,196],[58,133],[22,120],[26,184],[23,126],[24,160],[27,178],[28,190],[10,93],[11,101],[53,113],[9,107],[105,238],[106,239],[107,240],[108,241]],"modVoices":[[2,39],[40,77],[78,115],[116,152],[153,190],[191,228]]};
const PAR = META.par, NP = META.nparams, RATE = META.rate;
const NAME = []; for (const k in PAR) if (PAR[k] < NP) NAME[PAR[k]] = k.replace(/^PAR_/, '');   // a later alias wins (OSC_WAVE_DRUM1 over MOD_WHEEL)
const DT = META.dtypeIds;

/* engine key -> firmware voice: the menu page its knobs come from, the voice it renders, its trigger number */
const VOICES = {
  lxdrum:{page:0, voice:0, name:'Firmware drum', tag:'LXR-02 drum voice: osc, FM, pitch env, transient, filter, LFO', len:2},
  lxsnare:{page:3, voice:3, name:'Firmware snare', tag:'LXR-02 snare voice: osc + noise, repeats, transient, filter', len:2},
  lxcym:{page:4, voice:4, name:'Firmware cymbal', tag:'LXR-02 cymbal voice: three FM oscillators, filter, LFO', len:4},
  lxhat:{page:5, voice:5, name:'Firmware hat', tag:'LXR-02 hi-hat voice: closed or open, FM oscillators, filter', len:2},
};
const VOICE_NAMES = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Closed hat', 'Open hat'];

/* labels for the firmware's 8-character names, by parameter */
const LABELS = [
  [/^COARSE\d$/, 'Coarse'], [/^FINE\d$/, 'Fine'], [/^(OSC_WAVE_|WAVE1_)/, 'Waveform'], [/^PWM_\d$/, 'Pulse width'],
  [/^VELOA\d$/, 'Attack'], [/^VELOD6_CLOSED$/, 'Decay (closed)'], [/^VELOD6_OPEN$/, 'Decay (open)'], [/^VELOD\d$/, 'Decay'], [/^VOL_SLOPE\d$/, 'Slope'], [/^REPEAT\d$/, 'Repeats'],
  [/^MOD_EG\d$/, 'Pitch decay'], [/^PITCH_SLOPE\d$/, 'Pitch slope'], [/^MODAMNT\d$/, 'Pitch amount'],
  [/^VEL_DEST_\d$/, 'Velocity to'], [/^VELO_MOD_AMT_\d$/, 'Velocity amount'], [/^VOLUME_MOD_ON_OFF\d$/, 'Velocity → volume'],
  [/^FMAMNT\d$/, 'FM amount'], [/^FM_FREQ\d$/, 'FM frequency'], [/^MOD_WAVE_DRUM\d$/, 'FM waveform'], [/^MIX_MOD_\d$/, 'FM mode'],
  [/^MOD_OSC_F1/, 'Mod osc 1 freq'], [/^MOD_OSC_F2/, 'Mod osc 2 freq'], [/^MOD_OSC_GAIN1/, 'Mod osc 1 level'], [/^MOD_OSC_GAIN2/, 'Mod osc 2 level'],
  [/^WAVE2_/, 'Mod osc 1 wave'], [/^WAVE3_/, 'Mod osc 2 wave'], [/^NOISE_FREQ1$/, 'Noise frequency'], [/^MIX1$/, 'Noise / osc mix'],
  [/^TRANS\d_WAVE$/, 'Transient'], [/^TRANS\d_VOL$/, 'Transient level'], [/^TRANS\d_FREQ$/, 'Transient pitch'],
  [/^FILTER_FREQ_\d$/, 'Cutoff'], [/^RESO_\d$/, 'Resonance'], [/^FILTER_TYPE_\d$/, 'Filter type'], [/^FILTER_DRIVE_\d$/, 'Filter drive'],
  [/^FREQ_LFO\d$/, 'LFO rate'], [/^SYNC_LFO\d$/, 'LFO sync'], [/^AMOUNT_LFO\d$/, 'LFO amount'], [/^WAVE_LFO\d$/, 'LFO wave'], [/^RETRIGGER_LFO\d$/, 'LFO retrigger'],
  [/^OFFSET_LFO\d$/, 'LFO offset'], [/^TARGET_LFO\d$/, 'LFO to'], [/^VOL\d$/, 'Volume'], [/^VOICE_DECIMATION\d$/, 'Sample rate'],
  [/^(DRIVE\d|SNARE_DISTORTION|CYMBAL_DISTORTION|HAT_DISTORTION)$/, 'Drive']];
const label = n => { const nm = NAME[n] || ''; for (const [re, l] of LABELS) if (re.test(nm)) return l; return nm.toLowerCase().replace(/_/g, ' '); };
/* the firmware's 3-letter option names, spelled out */
const NICE = {Sin:'Sine', Rec:'Square', Noi:'Noise', Nch:'Notch', Pek:'Peak', UBP:'BP (unity)', LP2:'LP 2', off:'Off', sin:'Sine', tri:'Triangle', sup:'Saw up', sdn:'Saw down',
  sqr:'Square', rnd:'Random', xup:'Exp up', xdn:'Exp down', Snp:'Snap', Ofs:'Offset', Clk:'Click', Ck2:'Click 2', Tik:'Tick', Kik:'Kick', Drp:'Drop', Clp:'Clap', Kk2:'Kick 2',
  Snr:'Snare', Sp2:'Snap 2', '12':'1/12', '16':'1/16', '32':'1/32', v1:'Voice 1', v2:'Voice 2', v3:'Voice 3', v4:'Voice 4', v5:'Voice 5', v6:'Voice 6'};
const nice = s => NICE[s] || s;
/* in its section a row needs fewer words (the section says FM, Filter, LFO…); the target lists keep the full names */
const SHORT = {'Pitch decay':'Decay', 'Pitch slope':'Slope', 'Pitch amount':'Amount', 'Velocity amount':'Amount', 'Velocity → volume':'Volume',
  'FM amount':'Amount', 'FM frequency':'Frequency', 'FM waveform':'Waveform', 'FM mode':'Mode', 'Mod osc 1 freq':'Osc 1 freq', 'Mod osc 2 freq':'Osc 2 freq',
  'Mod osc 1 level':'Osc 1 level', 'Mod osc 2 level':'Osc 2 level', 'Mod osc 1 wave':'Osc 1 wave', 'Mod osc 2 wave':'Osc 2 wave', 'Transient':'Sample',
  'Transient level':'Level', 'Transient pitch':'Pitch', 'Filter type':'Type', 'Filter drive':'Drive', 'LFO rate':'Rate', 'LFO sync':'Sync', 'LFO amount':'Amount',
  'LFO wave':'Wave', 'LFO retrigger':'Retrigger', 'LFO offset':'Offset', 'Noise frequency':'Noise freq', 'Noise / osc mix':'Noise mix',
  'Decay (closed)':'Closed decay', 'Decay (open)':'Open decay'};
const PM60 = /^(COARSE\d|FM_FREQ\d|MOD_OSC_F[12](_CYM)?)$/;   // shown as value − 60 (a note), like the machine; the other ±63 knobs as value − 63

/* Modulation targets. On the machine the velocity and the LFOs can reach any voice's parameters (modTargets); a pad's
   reach its own voice's knobs, so each engine lists those, and a pad stores 0 (off) or a position in that list. */
const modParam = g => { const t = META.modTargets[g]; return t ? t[1] : 0; };

function rowFor(n, v){
  const [dt, menu] = META.dtypes[n] || [DT['0B127'], 0], nm = NAME[n] || '', d = META.init[n] | 0, full = label(n), l = SHORT[full] || full;
  if (dt === DT.MENU){ const opts = META.menus[menu] || []; return {l, min:0, max:Math.max(0, opts.length - 1), c:'int', d:Math.min(d, opts.length - 1), f:x => nice(opts[Math.round(x)] || String(Math.round(x)))}; }
  if (dt === DT.PM63){ const z = PM60.test(nm) ? 60 : 63; return {l, min:0, max:127, c:'int', d, f:x => { const y = Math.round(x) - z; return (y > 0 ? '+' : '') + y; }}; }
  if (dt === DT.ON_OFF) return {l, min:0, max:1, c:'int', d:d ? 1 : 0, f:x => Math.round(x) ? 'On' : 'Off'};
  return {l, min:0, max:127, c:'int', d, f:x => String(Math.round(x))};
}
const SKIP = /^(PAN\d|AUDIO_OUT\d|VOICE_LFO\d)$/;

/* Each engine's controls: the voice's menu pages, one section a page, without what a pad has no use for (pan, the
   audio output, the LFO's destination voice: a pad's LFO modulates its own voice). */
const TITLES = ['Oscillator', 'Envelope', '', 'FM', 'Transient', 'Filter', 'LFO', 'Level'];
const ENG = {};
for (const k in VOICES){
  const V = VOICES[k], rows = {}, secs = [], defs = {};
  const pages = META.pages[V.page].map((sub, s) => sub.map(e => e[1]).filter(n => n > 0 && n < NP));
  if (k === 'lxhat'){ const op = PAR.PAR_VELOD6_OPEN; const amp = pages[1]; const i = amp.indexOf(PAR.PAR_VELOD6_CLOSED); if (i >= 0 && !amp.includes(op)) amp.splice(i + 1, 0, op); }
  pages.forEach((ns, s) => {
    const keys = [];
    for (const n of ns){
      const nm = NAME[n] || '';
      if (SKIP.test(nm)) continue;
      if (/^VEL_DEST_\d$/.test(nm)){ keys.push('vdst'); continue; }
      if (/^TARGET_LFO\d$/.test(nm)){ keys.push('ldst'); continue; }
      const key = 'p' + n; rows[key] = rowFor(n, V.voice); defs[key] = rows[key].d; keys.push(key);
    }
    if (k === 'lxhat' && s === 1){ rows.open = {l:'Plays', min:0, max:1, c:'int', d:0, f:x => Math.round(x) ? 'Open' : 'Closed'}; keys.unshift('open'); defs.open = 0; }
    if (s === 2){   // the machine's modulation page: the pitch envelope, then velocity (with how hard the pad is hit)
      const isVel = key => key === 'vdst' || /^(VELO_MOD_AMT_|VOLUME_MOD_ON_OFF)/.test(NAME[+key.slice(1)] || '');
      const pitch = keys.filter(key => !isVel(key)), vel = keys.filter(isVel);
      rows.vel = {l:'Hit', min:1, max:127, c:'int', d:127, f:x => String(Math.round(x))}; defs.vel = 127;
      if (pitch.length) secs.push(['Pitch envelope', pitch]);
      secs.push(['Velocity', ['vel', ...vel]]);
      return;
    }
    if (keys.length){ let t = TITLES[s]; if (s === 3 && k !== 'lxdrum') t = 'Mod oscillators'; secs.push([t, keys]); }
  });
  // the targets: this voice's range of modTargets, the ones that are knobs of this engine
  const [s0, e0] = META.modVoices[V.voice], tgt = [];
  for (let g = s0; g <= e0; g++){ const n = modParam(g); if (('p' + n) in defs && !tgt.some(h => modParam(h) === n)) tgt.push(g); }
  const tRow = (l, n) => ({l, min:0, max:tgt.length, c:'int', d:Math.max(0, tgt.indexOf(META.init[n] | 0) + 1), f:x => { const i = Math.round(x); return i > 0 && tgt[i - 1] ? label(modParam(tgt[i - 1])) : 'Off'; }});
  rows.vdst = tRow('Target', PAR['PAR_VEL_DEST_' + (V.voice + 1)]); defs.vdst = rows.vdst.d;
  rows.ldst = tRow('Target', PAR['PAR_TARGET_LFO' + (V.voice + 1)]); defs.ldst = rows.ldst.d;
  ENG[k] = {rows, secs, defs, tgt};
}

/* drum 2 and 3 use drum 1's controls: their parameters line up page by page and knob by knob */
function pageMap(fromPage, toPage){
  const m = new Map(), A = META.pages[fromPage], B = META.pages[toPage];
  A.forEach((sub, s) => sub.forEach((e, k) => { const a = e[1], b = B[s] && B[s][k] && B[s][k][1]; if (a > 0 && a < NP && b > 0 && b < NP) m.set(a, b); }));
  return m;
}
/* A pad's values from a kit's bytes, for firmware voice src (0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat). */
function fromKit(bytes, src){
  const kind = src < 3 ? 'lxdrum' : src === 3 ? 'lxsnare' : src === 4 ? 'lxcym' : 'lxhat', V = VOICES[kind], E = ENG[kind], o = Object.assign({}, E.defs);
  const map = pageMap(src < 3 ? src : src >= 5 ? 5 : src, V.page);   // kit voice's parameter -> this engine's
  if (kind === 'lxhat') map.set(PAR.PAR_VELOD6_OPEN, PAR.PAR_VELOD6_OPEN);   // on the open hat's page, not the closed one's
  for (const [a, b] of map){ if (('p' + b) in E.defs) o['p' + b] = bytes[a] | 0; }
  // modulation targets: the knob the kit's target points at, as this engine's (a drum 2 target becomes the same knob
  // of the drum this engine renders); a target on another voice can't sound on this pad, so it is dropped
  const vs = Math.min(src, 5);   // the kit voice's own number (the open hat is the hat voice)
  const rel = g => { const a = modParam(g), b = map.get(a); return b ? E.tgt.findIndex(h => modParam(h) === b) + 1 : 0; };
  o.vdst = rel(bytes[PAR.PAR_VEL_DEST_1 + vs] | 0);
  o.ldst = (bytes[PAR.PAR_VOICE_LFO1 + vs] | 0) === vs + 1 ? rel(bytes[PAR.PAR_TARGET_LFO1 + vs] | 0) : 0;
  if (kind === 'lxhat') o.open = src === 6 ? 1 : 0;
  return {kind, o};
}

/* ---------- WebAssembly ---------- */
let X = null, mod = null, fresh = null;
/* The firmware keeps state between hits (filters, oscillator phases, counters its init functions leave alone), as the
   machine does. A pad should render the same every time, so every render starts from a copy of the memory taken just
   after start-up: a freshly powered-on machine. */
function attach(inst){ X = inst.exports; if (X._initialize) X._initialize(); fresh = new Uint8Array(X.memory.buffer).slice(); }
const LxrCore = {
  engines: VOICES, voiceNames:VOICE_NAMES,
  ready: () => !!X,
  module: () => mod,
  // the page: fetch and compile the wasm next to this file, instantiate it here; the Module goes to the render Worker
  async load(url){ const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); mod = await WebAssembly.compile(await r.arrayBuffer());
    attach(await WebAssembly.instantiate(mod, {})); return mod; },
  // the Worker: the page's compiled Module (a Worker may instantiate synchronously)
  attachModule(m){ mod = m; attach(new WebAssembly.Instance(m, {})); },
  isKind: k => !!VOICES[k],
  // XENG entries (live:false: rendered one-shots); r renders
  xeng(){ const o = {}; for (const k in VOICES) o[k] = {live:false, d:() => Object.assign({}, ENG[k].defs), r:(P, sr, hold, V, bpm) => LxrCore.render(k, P, sr, hold, V, bpm)}; return o; },
  // XUI entries: the engine card's rows and sections, and presets (the init kit's voices)
  xui(){
    const o = {}, init = META.init;
    for (const k in VOICES){
      const pre = [['Init', {}]];   // the init kit's sound for this voice (drum: drum 1)
      if (k === 'lxdrum') pre.push(['Init kit drum 2', fromKit(init, 1).o], ['Init kit drum 3', fromKit(init, 2).o]);
      if (k === 'lxhat') pre.push(['Init kit open hat', {open:1}]);
      o[k] = {title:VOICES[k].name, tag:VOICES[k].tag, rows:ENG[k].rows, secs:ENG[k].secs, pre,
        note:'The machine’s own 0–127 knobs, on its pages. Hit is how hard the pad is struck when it renders, for the Velocity target. The LFO and velocity reach this voice’s knobs; Volume is the voice’s own level, before the pad’s Level.'};
    }
    return o;
  },
  len: k => (VOICES[k] && VOICES[k].len) || 2,
  // the firmware's init kit as seven pads (the pad Presets list)
  initKit(){ const b = new Uint8Array(NP); b.set(META.init.slice(0, NP)); return VOICE_NAMES.map((n, i) => Object.assign({name:n}, fromKit(b, i))); },
  // does a pad follow the song tempo (an LFO synced to it)?
  synced(v){ const k = v && v.engine, P = v && v[k]; if (!VOICES[k] || !P) return false; const n = VOICES[k].voice + 1;
    return (P['p' + PAR['PAR_SYNC_LFO' + n]] | 0) > 0 && (P['p' + PAR['PAR_AMOUNT_LFO' + n]] | 0) > 0 && (P.ldst | 0) > 0; },
  // an LXR kit file (.snd: 8 name bytes, then the sound parameters): its name and its seven sounds as pads
  readKit(buf){
    const b = new Uint8Array(buf); if (b.length < 16) throw new Error('not an LXR kit');
    let name = ''; for (let i = 0; i < 8 && b[i]; i++) name += b[i] >= 32 && b[i] < 127 ? String.fromCharCode(b[i]) : '';
    const bytes = new Uint8Array(NP); bytes.set(b.subarray(8, 8 + NP));
    for (let i = 0; i < 6; i++){ if (bytes[PAR.PAR_VEL_DEST_1 + i] >= META.modTargets.length) bytes[PAR.PAR_VEL_DEST_1 + i] = 0; if (bytes[PAR.PAR_TARGET_LFO1 + i] >= META.modTargets.length) bytes[PAR.PAR_TARGET_LFO1 + i] = 0; }
    return {name:name.trim() || 'LXR kit', sounds:VOICE_NAMES.map((n, i) => Object.assign({name:n}, fromKit(bytes, i)))};
  },
  render(k, P, sr, hold, V, bpm){
    if (!X) return new Float32Array(1);
    const Vc = VOICES[k], vi = Vc.voice, blk = X.lxr_block();
    new Uint8Array(X.memory.buffer).set(fresh);
    X.lxr_init(0x2545F491); X.lxr_init_kit();
    const b = new Uint8Array(X.memory.buffer, X.lxr_params(), NP);
    for (let i = 0; i < 6; i++){ b[PAR.PAR_VEL_DEST_1 + i] = 0; b[PAR.PAR_TARGET_LFO1 + i] = 0; b[PAR.PAR_VOICE_LFO1 + i] = i + 1; }
    for (const key in P){ if (/^p\d+$/.test(key)){ const n = +key.slice(1); if (n < NP) b[n] = Math.max(0, Math.min(255, Math.round(P[key]) || 0)); } }
    const T = ENG[k].tgt, tg = r => { r = Math.round(r) || 0; return r > 0 && T[r - 1] ? T[r - 1] : 0; };
    b[PAR.PAR_VEL_DEST_1 + vi] = tg(P.vdst); b[PAR.PAR_TARGET_LFO1 + vi] = tg(P.ldst);
    X.lxr_set_bpm(Math.round(bpm || 120)); X.lxr_load();
    for (let i = 0; i < 20; i++) X.lxr_render(vi, 16);   // 1/3 s for the knob and fader smoothing to settle, as on the machine between hits
    const trig = k === 'lxhat' ? (Math.round(P.open) ? 6 : 5) : vi, note = Math.max(0, Math.min(127, 60 + Math.round(P.tune || 0)));
    X.lxr_trigger(trig, Math.max(1, Math.min(127, Math.round(P.vel || 127))), note);
    const maxS = Math.round(Math.min(12, Math.max(0.05, hold || (V && V.len) || Vc.len)) * RATE), raw = new Float32Array(maxS + blk * 16);
    let n = 0, quiet = 0;
    while (n < maxS){
      const c = X.lxr_render(vi, 16), o = new Float32Array(X.memory.buffer, X.lxr_out(), c);
      raw.set(o, n); n += c;
      let pk = 0; for (let i = 0; i < c; i++){ const a = Math.abs(o[i]); if (a > pk) pk = a; }
      quiet = pk < 3e-5 ? quiet + c : 0;
      if (n > RATE * 0.05 && quiet > RATE * 0.3) break;   // the voice has died away
    }
    return finish(resample(raw.subarray(0, n), RATE, sr), P, sr);
  },
};
/* windowed-sinc resampling from the machine's 44.14 kHz to the app's rate: 32 taps, a Hann window, the kernel from a
   table of 1024 fractional positions (each row summing to 1, so the level is unchanged) */
let KT = null, KTk = '';
function resample(x, from, to){
  if (Math.abs(from - to) < 0.5) return Float32Array.from(x);
  const A = 16, PH = 1024, ratio = from / to, fc = Math.min(1, to / from) * 0.97, key = from + '/' + to;
  if (KTk !== key){ KTk = key; KT = new Float32Array((PH + 1) * 2 * A);
    for (let p = 0; p <= PH; p++){ const f = p / PH; let w = 0;
      for (let m = 0; m < 2 * A; m++){ const d = f + m - (A - 1), u = d * fc, sc = u === 0 ? 1 : Math.sin(Math.PI * u) / (Math.PI * u), k = sc * (0.5 + 0.5 * Math.cos(Math.PI * d / A)); KT[p * 2 * A + m] = k; w += k; }
      for (let m = 0; m < 2 * A; m++) KT[p * 2 * A + m] /= w; } }
  const N = Math.floor(x.length / ratio), y = new Float32Array(N), L = x.length;
  for (let j = 0; j < N; j++){
    const t = j * ratio, i0 = Math.floor(t), row = Math.round((t - i0) * PH) * 2 * A; let s = 0;
    for (let m = 0; m < 2 * A; m++){ const i = i0 - m + A - 1; if (i >= 0 && i < L) s += x[i] * KT[row + m]; }
    y[j] = s;
  }
  return y;
}
/* the pad's Level (dB) and Drive, as the other engines apply them, but no normalising: the voice keeps the level its
   Volume knob gives it, so a kit's balance carries over. Silence at the end trimmed, a 3 ms fade. */
function finish(out, p, sr){
  const lv = Math.pow(10, (p.level || 0) / 20), dr = (p.drive || 0) > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = dr ? 1 / Math.tanh(dr) : 1;
  for (let i = 0; i < out.length; i++){ let y = out[i] * lv; if (dr) y = Math.tanh(y * dr) * dn; out[i] = y; }
  let end = out.length; while (end > 1 && Math.abs(out[end - 1]) < 1e-4) end--;
  const res = out.slice(0, Math.max(end, 1)), fl = Math.min(res.length, Math.round(sr * 0.003)); for (let i = 0; i < fl; i++) res[res.length - 1 - i] *= i / fl;
  return res;
}
G.LxrCore = LxrCore;
})(typeof self !== 'undefined' ? self : globalThis);
