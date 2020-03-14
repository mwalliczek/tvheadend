#include "tvheadend.h"
#include "tvhpoll.h"

#include "input_sdr_async.h"

int readFromDevice(rtlsdr_frontend_t *lfe) {
	struct sdr_state_t *sdr = &lfe->sdr;
	tvhpoll_event_t ev[2];
	tvhpoll_t *efd;
	char b;
	int nfds;

	/* Setup poll */
	efd = tvhpoll_create(2);
	memset(ev, 0, sizeof(ev));
	ev[0].events = TVHPOLL_IN;
	ev[0].fd = lfe->lfe_control_pipe.rd;
	ev[0].ptr = lfe;
	ev[1].events = TVHPOLL_IN;
	ev[1].fd = lfe->lfe_dvr_pipe.rd;
	ev[1].ptr = &lfe->lfe_dvr_pipe;
	tvhpoll_add(efd, ev, 2);

	while (tvheadend_is_running() && lfe->lfe_dvr_pipe.rd > 0 && lfe->running) {
		nfds = tvhpoll_wait(efd, ev, 1, 150);
		if (nfds < 1) continue;
		if (ev[0].ptr == &lfe->lfe_dvr_pipe) {
			if (read(lfe->lfe_dvr_pipe.rd, &b, 1) > 0) {
				tvhpoll_destroy(efd);
				return 0;
			}
			continue;
		}
		if (ev[0].ptr != lfe) break;
		if (read(lfe->lfe_control_pipe.rd, &b, 1) > 0 && sdr->fifo.count > 0) {
			tvhtrace(LS_RTLSDR, "fifo count %u", sdr->fifo.count);
			tvhpoll_destroy(efd);
			return 1;
		}
	}
	tvhpoll_destroy(efd);
	return 0;
}

