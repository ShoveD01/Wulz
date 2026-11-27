# CheetahMS800 (wul) 1.0.0

A wave-sequence digital synthesizer in the spirit of the early-90s Cheetah MS800 rack module. **It is an emulation by ear and by description, not a ROM dump:** the 21 waves are my own digital approximations.

**Requires MPC OS 3.10.1 or later, with External Plugin Support enabled** (Preferences → Enhancements (Beta) → Enable Enhancements and External Plugin Support). Copy the folder `wul - VST - CheetahMS800` into a `VST2 Plugins` folder at the root of a drive and restart the MPC.

## Sound engine
- **15 voices**, each with **1-4 stacked tones** (Tone Layers). Layers 2-4 are detuned (Tone Detune, cents); layers 3 and 4 can also be transposed (Layer Interval); Stereo Spread pans them. Detune also makes each layer's sequence drift slightly out of step, so stacks evolve.
- **No filter at all.** All movement comes from the 8-step wave sequence, wave crossfades, pitch and level steps, as on the hardware.
- **Lo-fi playback:** 21 single-cycle waves of 256 samples, 8-bit mu-law companded (about 12-bit dynamic range), read without interpolation, so high notes alias. **8-Bit Grit** adds bit reduction and a non-integer sample-rate hold on the output. **Master Drive** saturates before the grit. A soft limiter keeps 15 loud voices from clipping.
- **Waves:** 01 Saw, 02 Square, 03-05 Pulse 25/12/6, 06 Triangle, 07 Hollow, 08-11 Vocal Ah/Oo/Ee/Oh, 12 Choir, 13-14 Bells, 15 Metal (FM), 16 FM Mallet, 17-19 Organs, 20-21 Digital noise.

## The wave sequence ("Tone")
Eight steps per patch (shared by all layers). Each step has a **Wave** (1-21), **Level** (0-99), **Fade** (0-99), **Pitch** (-24..+24 semitones) and a **Mode**. The sequence clock is **Seq Speed** (steps per second). Entering a step, the sound morphs from the previous one over the step's Fade (scaled by **Crossfade Smooth**, 50 % = as programmed; Fade 0 is a hard cut): wave crossfade, level ramp and pitch glide. Step 1's fade is the attack.
- **Mode: Normal / Loop Pt / Release.** The first Loop Pt step is where the loop restarts (default step 1). **Loop End** (Off, or 1-8) is the last step of the loop. With Loop End off the sequence plays once and holds the last step while the key is down. The first Release step is where the sequence jumps when you let go; the steps after it play through and the voice ends. With no Release step the voice fades out at **Release Rate** (5 ms to 8 s).
- Velocity scales the level; pitch bend (Bend Range 1-12) and sustain pedal work.

## RANDOMIZE
The big button in the top bar, Q-Link 16 on the PERFORM page, or any change of that parameter. It builds a new wave sequence from one of four wave families (pad, vocal, metallic, digital), a contour (swell, decay, pulse, random), fades, pitch jumps, loop/release steps, speed, layers, detune, grit and drive. Volume and Bend Range are never touched.

## Screens and Q-Links
- **F1 PERFORM**: live display of the 8 steps (wave shape and level bar follow the parameters) and the 16 Q-Links as four banks of four: Bank 1 Seq Speed, Crossfade Smoothness, Tone Detune, 8-Bit Grit; Bank 2 Step 1-4 Wave; Bank 3 Step 5-8 Wave; Bank 4 Loop End, Release Rate, Stereo Spread, Randomize.
- **F2** STEPS 1-4 and STEPS 5-8 (sub pages): Wave, Level, Fade, Pitch for each step.
- **F3** MODES (step modes, loop, layering) and OUTPUT (grit, drive, bend range, master level).
- The **red 7-segment LED** shows the preset number when a program is loaded (100-120 show their last two digits), then the value (0-99) of whatever control you touched last.

## 120 factory presets
001-020 Classic MS800 Digital Pads & Evolving Drones; 021-040 Aphex / IDM Rhythmic Wave Sequences & Acid-Like Pulses; 041-060 8-Bit Bells, FM Mallets & Metallic Percussion; 061-080 Gritty Digital Basses & Hollow Organs; 081-100 Vocal Formants, Choirs & Glassy Textures; 101-120 Broken CPU, Glitch & Experimental SFX. Levelled to similar loudness (a three-note chord).

## Honest notes
- Not tuned by ear and not CPU-measured on a real MPC. On a desktop, 15 voices x 4 layers use about 1.7 % of one core.
- The skin is static artwork with live film-strip displays; there is no on-screen preset selector (use the MPC's program list).
- The Randomize button edits parameters from the audio thread and has not been checked on hardware.
- The step display and LED rely on the host refreshing displays when parameters change, as other wul plugins do.
- Skin generator code is adapted from a GPL-3.0 generator: fine for private use, check before redistributing. Font: Ubuntu Sans (Ubuntu Font Licence). "Cheetah MS800" is a name of the original hardware; this is an independent emulation.
