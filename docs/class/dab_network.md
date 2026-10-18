Contents                                   | Description
-------------------------------------------|------------------------
[Overview](#overview)                      | Tab overview
[Force Scanning](#force-scanning)          | Force scanning a network
[Items/Properties](#items)                 | Items and properties

[Return to DAB Inputs](dabinputs)

---

## Overview

A DAB network groups the ensembles (DAB multiplexes) that can be
received with the adapters associated with it.

When a network is created with *Scan all Band III channels* enabled,
an ensemble is created for each DAB Band III channel (5A - 13F) and
scanned, so all stations in reach are found without entering any
frequencies. Ensembles without signal are marked as failed.

---

## Buttons

<tvh_include>inc/buttons</tvh_include>

---

## Force Scanning

Force scanning can take some time. You may continue to use Tvheadend
while a scan is in progress, but doing so will increase the time needed
for it to complete. Note that the time required can vary depending on a
number of factors, such as how many tuners you have available and the
number of ensembles on each network.

---
