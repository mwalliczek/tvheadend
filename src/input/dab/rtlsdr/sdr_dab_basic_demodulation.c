#include <math.h>

#include "tvheadend.h"
#include "rtlsdr_private.h"
#include "phasereference.h"
#include "ofdmDecoder.h"
#include "sdr_dab_basic_demodulation.h"

void *rtlsdr_demod_thread_fn(void *arg)
{
	rtlsdr_frontend_t *lfe = arg;
	struct sdr_state_t *sdr = &lfe->sdr;
	float		fineCorrector = 0;
	float		coarseCorrector = 0;
	float _Complex v[T_F / 2];
	int32_t		startIndex;
	int i;
	float _Complex	FreqCorr;
	int32_t		syncBufferIndex = 0;
	float		cLevel = 0;
	float _Complex sample;
	int32_t		counter;
	int32_t		syncBufferSize = 32768;
	int32_t		syncBufferMask = syncBufferSize - 1;
	float		envBuffer[syncBufferSize];
	float _Complex ofdmBuffer[T_s];
	int		ofdmSymbolCount = 0;
	int		dip_attempts = 0;
	int		index_attempts = 0;

	tvhtrace(LS_RTLSDR, "start polling");
	
        /* Setup poll */
        sdr->efd = tvhpoll_create(2);
        memset(sdr->ev, 0, sizeof(sdr->ev));
        sdr->ev[0].events = TVHPOLL_IN;
        sdr->ev[0].fd = lfe->lfe_control_pipe.rd;
        sdr->ev[0].ptr = lfe;
        sdr->ev[1].events = TVHPOLL_IN;
        sdr->ev[1].fd = lfe->lfe_dvr_pipe.rd;
        sdr->ev[1].ptr = &lfe->lfe_dvr_pipe;
        tvhpoll_add(sdr->efd, sdr->ev, 2);
	
	/* Read */
	if (getSamples(lfe, v, T_F / 2, 0) < T_F / 2) {
		tvherror(LS_RTLSDR, "getSamples failed");
		return 0;
	}
	tvhtrace(LS_RTLSDR, "started, sLevel: %.6f", sdr->sLevel);
	while (tvheadend_is_running() && lfe->lfe_dvr_pipe.rd > 0 && lfe->running) {
		//      As long as we are not (time) synced, we try to handle that
		if (!sdr->isSynced) {
			syncBufferIndex = 0;
			cLevel = 0;
			for (i = 0; i < 50; i++) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], 0) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex];
				syncBufferIndex++;
			}
			tvhtrace(LS_RTLSDR, "trying to find a sync, sLevel: %.6f, cLevel: %.6f", sdr->sLevel, cLevel);

			//	We now have initial values for cLevel (i.e. the sum
			//	over the last 50 samples) and sLevel, the long term average.
			//	here we start looking for the null level, i.e. a dip
			counter = 0;
			while (cLevel / 50 > 0.50 * sdr->sLevel) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], coarseCorrector + fineCorrector) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex] -
					envBuffer[(syncBufferIndex - 50) & syncBufferMask];
				syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
				counter++;

				if (counter > 2 * T_F) { // hopeless	
					break;
				}
			}
			//	if we have 5 successive attempts are failing, signal our bosses
			if (counter > 2 * T_F) {
				tvhtrace(LS_RTLSDR, "trying to find a dip failed");
				if (++dip_attempts > 10) {
					dip_attempts = 0;
					//	               syncsignalHandler (false, userData);
				}
				continue;
			}
			tvhtrace(LS_RTLSDR, "found begin of null period");
			//	It seemed we found a dip that started app 65/100 * 50 samples earlier.
			//	We now start looking for the end of the null period.

			counter = 0;
			while (cLevel / 50 < 0.75 * sdr->sLevel) {
				if (getSample(lfe, &sample, &envBuffer[syncBufferIndex], coarseCorrector + fineCorrector) == 0) {
					tvherror(LS_RTLSDR, "getSamples failed");
					return 0;
				}
				cLevel += envBuffer[syncBufferIndex] -
					envBuffer[(syncBufferIndex - 50) & syncBufferMask];
				syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
				counter++;
				if (counter > T_null + 50)  // hopeless
					break;
			}
			if (counter > T_null + 50) {
				tvhtrace(LS_RTLSDR, "found end of null period failed");
				continue;
			}
			tvhtrace(LS_RTLSDR, "found end of null period");
			dip_attempts = 0;
		}
		//      We arrive here when time synchronized, either from above
		//      or after having processed a frame
		//      We now have to find the exact first sample of the non-null period.
		//      We use a correlation that will find the first sample after the
		//      cyclic prefix.
		//      Now read in Tu samples. The precise number is not really important
		//      as long as we can be sure that the first sample to be identified
		//      is part of the samples read.
		if (getSamples(lfe, ofdmBuffer, T_u, coarseCorrector + fineCorrector) < T_u) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		startIndex = phaseReferenceFindIndex(sdr, ofdmBuffer);
		if (startIndex < 0) { // no sync, try again
			sdr->isSynced = 0;
			if (++index_attempts > 10) {
				tvhtrace(LS_RTLSDR, "sync failed");
			}
			continue;
		}
		index_attempts = 0;
		tvhtrace(LS_RTLSDR, "sync found!, coarseCorrector %f fineCorrector %f", coarseCorrector, fineCorrector);
		sdr->isSynced = 1;
		//	Once here, we are synchronized, we need to copy the data we
		//	used for synchronization for block 0

		memmove(ofdmBuffer, &ofdmBuffer[startIndex],
			(T_u - startIndex) * sizeof(float _Complex));
		int ofdmBufferIndex = T_u - startIndex;

		//	Block 0 is special in that it is used for coarse time synchronization
		//	and its content is used as a reference for decoding the
		//	first datablock.
		//	We read the missing samples in the ofdm buffer
		if (getSamples(lfe, &ofdmBuffer[ofdmBufferIndex],
			T_u - ofdmBufferIndex, coarseCorrector + fineCorrector) < T_u - ofdmBufferIndex) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		processBlock_0(sdr, ofdmBuffer);
		tvhtrace(LS_RTLSDR, "snr: %.6f", sdr->mmi->tii_stats.snr / 10000.0);

		//
		//	Here we look only at the block_0 when we need a coarse
		//	frequency synchronization.
		//	The width is limited to 2 * 35 Khz (i.e. positive and negative)
		if (!sdr->mmi->fibProcessorIsSynced) {
			int correction = phaseReferenceEstimateOffset(sdr, ofdmBuffer);
			if (correction != 100) {
				coarseCorrector += correction * carrierDiff;
				if (fabsf(coarseCorrector) > Khz(35))
					coarseCorrector = 0;
			}
		}
		//
		//	after block 0, we will just read in the other (params -> L - 1) blocks
		//	The first ones are the FIC blocks. We immediately
		//	start with building up an average of the phase difference
		//	between the samples in the cyclic prefix and the
		//	corresponding samples in the datapart.
		///	and similar for the (params. L - 4) MSC blocks
		FreqCorr = 0.0 + 0.0 * I;
		for (ofdmSymbolCount = 1;
			ofdmSymbolCount < (uint16_t)L; ofdmSymbolCount++) {
			if (getSamples(lfe, ofdmBuffer, T_s, coarseCorrector + fineCorrector) < T_s) {
				tvherror(LS_RTLSDR, "getSamples failed");
				return 0;
			}
			for (i = (int)T_u; i < (int)T_s; i++) {
				FreqCorr += ofdmBuffer[i] * conjf(ofdmBuffer[i - T_u]);
			}

			decodeBlock(sdr, ofdmBuffer, ofdmSymbolCount);
		}

		//	we integrate the newly found frequency error with the
		//	existing frequency error.
		fineCorrector += 0.1 * cargf(FreqCorr) / M_PI * carrierDiff;

		//	at the end of the frame, just skip Tnull samples
		if (getSamples(lfe, ofdmBuffer, T_null, coarseCorrector + fineCorrector) < T_null) {
			tvherror(LS_RTLSDR, "getSamples failed");
			return 0;
		}
		if (fineCorrector > carrierDiff / 2) {
			coarseCorrector += carrierDiff;
			fineCorrector -= carrierDiff;
		}
		else
			if (fineCorrector < -carrierDiff / 2) {
				coarseCorrector -= carrierDiff;
				fineCorrector += carrierDiff;
			}
	}
	
	tvhtrace(LS_RTLSDR, "rtlsdr_demod_thread_fn end loop");

	tvhpoll_destroy(sdr->efd);
	
	return 0;
}

