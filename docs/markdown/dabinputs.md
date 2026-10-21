# DAB Inputs

Contents                                                    | Description
------------------------------------------------------------|-------------------------------------
[Overview](#overview)                                       | Tab overview
[Requirements](#requirements)                               | Supported hardware and build requirements
[TV Adapters](class/rtlsdr_frontend)                        | Where you configure the RTL-SDR tuners
[Networks](class/dab_network)                               | Where you configure DAB networks
[Ensembles](class/dab_ensemble)                             | Where you manage the ensembles (DAB multiplexes)
[Services](class/dab_service)                               | Discovered radio service(s) management
[Getting started](#getting-started)                         | How to receive DAB/DAB+ radio
[Fast tuning](#fast-tuning)                                 | Cached reception parameters
[Program guide (EPG)](#program-guide-epg-)                  | DAB service and programme information (SPI)
[Stream status](#stream-status)                             | Meaning of the status columns for DAB
[Debugging](#debugging)                                     | Trace subsystems
[Credits](#credits)                                         | Origin of the signal processing code

---

## Overview

This tab is where you configure/manage DAB and DAB+ (Digital Audio Broadcasting)
radio reception: tuners, networks, ensembles and services. It is only
shown when Tvheadend was built with RTL-SDR support.

DAB is not transported in MPEG-TS, so DAB has its own set of networks,
ensembles (the DAB equivalent of a mux) and services. DAB+ services are
delivered as AAC (LATM) streams, classic DAB services as MPEG-1 Audio
Layer II (MP2) streams; both can be mapped to channels like any other
service. A streaming or recording profile that is limited to one audio
type passes only services of that type.

---

## Buttons

<tvh_include>inc/buttons</tvh_include>

---

## Requirements

* An RTL2832U based USB stick (commonly sold as "RTL-SDR" or DVB-T
  dongle) with a tuner that covers DAB Band III (174 - 240 MHz), e.g.
  R820T/R820T2 or E4000.
* The kernel DVB driver for the stick must not claim the device;
  blacklist `dvb_usb_rtl28xxu` (and `rtl2832`, `rtl2830`) if it does.
* The user running Tvheadend needs access to the USB device (most
  distributions ship udev rules for this with their librtlsdr package).
* Tvheadend built with RTL-SDR support. `./configure` enables it
  automatically when the development files of
  [librtlsdr](https://osmocom.org/projects/rtl-sdr/wiki) and
  [FFTW3](https://www.fftw.org) (single precision) are found, e.g.
  `librtlsdr-dev libfftw3-dev` on Debian/Ubuntu or
  `rtl-sdr-devel fftw-devel` on Fedora. Use `--enable-rtlsdr` to fail
  when they are missing or `--disable-rtlsdr` to build without it.

The decoder runs on a Raspberry Pi 3 or better. SSE2 (x86) and NEON
(ARM) optimised Viterbi decoders are selected at build time.

The sticks are detected when Tvheadend starts; restart Tvheadend after
plugging in a new stick.

---

## Getting started

1. Enable the RTL-SDR adapter in the *TV Adapters* tab.
2. Create a DAB network in the *Networks* tab and associate the adapter
   with it. With *Scan all Band III channels* enabled (the default) the
   ensembles for the channels 5A - 13F are created automatically.
3. The initial scan tunes each ensemble. Ensembles with signal are
   populated with their services, channels without signal are marked
   as failed.
4. Map the services to channels in the *Services* tab.

---

## Fast tuning

Neither the transmitter nor the receiver usually move, so the frequency
offset found while receiving an ensemble is stored with the ensemble
(*Frequency correction*) and the frequency error of the stick with the
adapter (*Frequency error (ppm)*). They are used as start values the
next time an ensemble is tuned, which makes switching between stations
considerably faster. Both values are updated automatically.

---

## Program guide (EPG)

Many DAB ensembles broadcast programme information in the Service and
Programme Information (SPI, ETSI TS 102 371) format. While an ensemble
is tuned, Tvheadend decodes it and feeds the programmes to the EPG of
the channels mapped to the DAB services. The grabber module is called
*DAB: SPI EPG Grabber* and can be disabled in
*Configuration -> Channel / EPG -> EPG Grabber Modules*. The programme
information is received whenever an ensemble is tuned, e.g. while a
station of the ensemble is played or recorded.

In addition the grabber tunes the ensembles itself to keep the EPG up
to date (settings of the grabber module, *Advanced* view):

Setting                        | Description
-------------------------------|------------------------------------------
Cron multi-line                | When to tune the ensembles, cron syntax, one time per line. Default: every day at 03:14. Empty: only while a station is used.
Grab after start               | Also tune them two minutes after Tvheadend has started (default on).
Time limit per ensemble        | Maximum time on one ensemble, default 60 minutes (minimum 15).

The ensembles are tuned one after the other with a low priority
subscription, so playing and recording always take precedence. Once
EPG objects arrive, the grabber stays on an ensemble until no new
objects have arrived for 15 minutes (the carousel is likely complete)
or the time limit is reached. The time limit is at least 15 minutes.
Ensembles without an EPG are recognized and skipped later on, see the
*EPG* column of the ensembles (*Advanced* view): an ensemble is marked
*No* when no EPG component was signalled for 15 minutes of reception
(not while a reconfiguration of the ensemble is announced). Ensembles
marked *No* are checked again after a week; set the column to *Unknown*
to have the next EPG grab check the ensemble again.

Each EPG grab logs (subsystem `dabepg`) how many ensembles it tunes,
or why it did not start (no ensemble to check, grabber disabled, no
grab time configured).

---

## Stream status

*Status -> Stream* shows for a tuned DAB ensemble:

Column                 | Meaning for DAB
-----------------------|--------------------------------------------------
Bandwidth (kb/s)       | Data rate of the decoded sub-channels (audio and EPG).
BER                    | Channel bit error rate before the Viterbi decoder (FIC and sub-channels), measured since the last status update.
PER                    | Share of DAB+ audio frames (AUs) with CRC error, i.e. lost audio. Classic DAB (MP2) frames are counted, but not checked.
Uncorrected Blocks     | Reed-Solomon code words that could not be corrected (DAB+ superframes, EPG packet FEC).
Transport Errors       | Logical frames (24 ms) without DAB+ superframe sync; for classic DAB (MP2), lost frame sync.
Continuity Errors      | Lost EPG packets (continuity index).

The counters (all but bandwidth and BER) add up until they are cleared.
As a rule of thumb, a BER up to a few percent (0.02 .. 0.05, depending on
the protection level) is corrected by the Viterbi decoder; above that,
audio frames get lost.

---

## Debugging

The trace subsystems `rtlsdr` (reception) and `dabepg` (program guide)
can be enabled in *Configuration -> Debugging*.

---

## Credits

Most of the DAB signal processing is derived from
[dab-cmdline](https://github.com/JvanKatwijk/dab-cmdline) by Jan van Katwijk
(Lazy Chair Computing, GNU GPL version 2 or later), ported from C++ to C and
adapted to tvheadend: frame synchronisation, phase reference, OFDM decoder,
FIC handler and FIB processor, MSC handler, EEP/UEP depuncturing, the
Viterbi decoders (with the SPIRAL kernels of Carnegie Mellon University),
the DAB+ superframe handling (fire code and Reed-Solomon decoder, based on
GNU Radio and Phil Karn's code) and the DAB character sets.

The USB input and the sample FIFO started from
[rtl-dab](https://github.com/maydavid/rtl-dab) by David May (GNU GPL
version 3 or later).

The source files name their origin in the copyright header.
