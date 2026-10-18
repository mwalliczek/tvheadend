#ifndef __TVH_RTLSDR_H__
#define __TVH_RTLSDR_H__

void rtlsdr_init();
void rtlsdr_done(void);

struct tvh_hardware;
/* is the hardware an RTL-SDR (DAB) adapter */
int rtlsdr_is_adapter(struct tvh_hardware *th);

#endif /* __TVH_RTLSDR_H__ */
