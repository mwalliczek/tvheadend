Contents                               | Description
---------------------------------------|------------------------
[Overview](#overview)                  | Panel overview
[Items/Properties](#items)             | Items and Properties

[Return to DAB Inputs](dabinputs)

---

## Overview

This panel displays the parameters of an RTL-SDR stick used as DAB
receiver. Associate the adapter with one or more DAB networks to use it.

The frequency error of the stick is learned while receiving and stored
as *Frequency error (ppm)*. It is used as start value for ensembles that
have not been received before, see [DAB Inputs](dabinputs#fast-tuning).

*Tuner gain* is *Automatic* by default, the tuner adjusts it. For a weak
ensemble (low SNR, audio dropouts) try a high fixed gain, e.g. 40 dB or
more; for a strong one near the transmitter a lower gain avoids overload.
The gain is applied when the next ensemble is tuned.

See [DAB Inputs](dabinputs#requirements) for the supported hardware.

---

## Buttons

<tvh_include>inc/buttons</tvh_include>

---
