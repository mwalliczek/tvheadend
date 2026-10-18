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

*Tuner gain* is 49.6 dB (the maximum of the R820T tuner) by default,
best for weak ensembles. Close to a transmitter, if strong ensembles
show a low SNR or audio dropouts, a lower gain avoids overload;
*Automatic* lets the tuner adjust it. The gain is applied when the next
ensemble is tuned.

See [DAB Inputs](dabinputs#requirements) for the supported hardware.

---

## Buttons

<tvh_include>inc/buttons</tvh_include>

---
