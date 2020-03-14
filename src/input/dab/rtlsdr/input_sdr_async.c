#include "tvheadend.h"
#include "tvhpoll.h"

#include "input_sdr_async.h"

int readFromDevice(rtlsdr_frontend_t *lfe) {
	struct sdr_state_t *sdr = &lfe->sdr;
	char b;
	int nfds;

	while (tvheadend_is_running() && lfe->lfe_dvr_pipe.rd > 0 && lfe->running) {
		nfds = tvhpoll_wait(sdr->efd, sdr->ev, 1, 150);
		if (nfds < 1) continue;
		if (sdr->ev[0].ptr == &lfe->lfe_dvr_pipe) {
			if (read(lfe->lfe_dvr_pipe.rd, &b, 1) > 0) {
				return 0;
			}
			continue;
		}
		if (sdr->ev[0].ptr != lfe) break;
		if (read(lfe->lfe_control_pipe.rd, &b, 1) > 0 && sdr->fifo.count > 0) {
			tvhtrace(LS_RTLSDR, "fifo count %u", sdr->fifo.count);
			return 1;
		}
	}
	return 0;
}

