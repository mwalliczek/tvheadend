#include "rtlsdr_private.h"
#include "sdr_dab_basic_demodulation.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

FILE *pFile;

int count = 0;
int fibCRCsuccessCount = -1;

struct service_queue service_all;

int readFromDevice(rtlsdr_frontend_t *lfe) {
    if (lfe->sdr.fibCRCsuccess > 0 && fibCRCsuccessCount < 0) {
        printf("is synced!\n");
        fibCRCsuccessCount = count;
    }
    uint8_t *input = calloc(262144, sizeof(uint8_t));
    int res = fread (input, 1, 262144, pFile);
    if (res < 262144) {
        printf("read failed\n");
        return 0;
    }
    count += 262144;
    printf("%d %d\n", count, res);
    cbWrite(&(lfe->sdr.fifo), input, 262144);
    free(input);
    return 1;
}

int main(int argc, char** argv) {
    pFile = fopen("input/dab/rtlsdr/rtlsdr_raw", "rb");
    if (pFile == NULL) {
        printf("input/dab/rtlsdr/rtlsdr_raw not found!\n");
        system("wget https://matthias.walliczek.de/rtlsdr_raw -O input/dab/rtlsdr/rtlsdr_raw");
        pFile = fopen("input/dab/rtlsdr/rtlsdr_raw", "rb");
    }

    sdr_init_const();
    initConstPhaseReference();
    initConstViterbi768();
    initConstOfdmDecoder();
    TAILQ_INIT(&service_all);

    rtlsdr_frontend_t *lfe = calloc(1, sizeof(rtlsdr_frontend_t));
    
    lfe->sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    lfe->sdr.mmi->mmi_ensemble = calloc(1, sizeof(dab_ensemble_t));
    lfe->running = 1;
    lfe->lfe_dvr_pipe.rd = 1;
    
    sdr_init(&lfe->sdr);
    
    rtlsdr_demod_thread_fn(lfe);
    
    dab_service_t* ds;
    int service_count = 0;
    TAILQ_FOREACH(ds, &service_all, s_all_link) {
        free(ds);
        service_count++;
    }


    printf("success: fibCRCrate %d (fibCRCsuccessCount %d, snr: %.6f, service_count: %d)\n", lfe->sdr.fibCRCrate, fibCRCsuccessCount, lfe->sdr.mmi->tii_stats.snr / 10000.0, service_count);
    
    sdr_destroy(&lfe->sdr);
    
    free(lfe->sdr.mmi->mmi_ensemble);
    free(lfe->sdr.mmi);
    free(lfe);
    fclose(pFile);

    exit(EXIT_SUCCESS);
}