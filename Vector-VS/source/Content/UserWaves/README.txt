VECTOR VS - USER WAVES (wave numbers 0-31)
------------------------------------------
Put your own single-cycle WAV files in this folder. They fill wave slots 0, 1, 2 ... in file-name order (a, b, c ... or 00, 01, 02 ...).

  * A file whose length is a multiple of 2048 samples is cut into 2048-sample cycles, one wave each (a 32-cycle file fills all 32 slots).
  * Any other length is treated as ONE cycle and stretched to 2048 samples.
  * Mono or stereo, 16/24-bit or 32-bit float, any sample rate (only the first channel is used).
  * Slots with no file behind them play a generated stand-in wave, so every wave number always makes a sound.
  * Wave 128 is noise, waves 32-127 are the 96 ROM waves (folder Waves).

The wave pictures on the screen are drawn when the skin is generated (python make_skin.py), so after changing files here the pictures for slots 0-31 only match once the skin has been rebuilt. The sound always uses the files as they are.
