#include "rtlsdr_private.h"
#include "sdr_dab_basic_demodulation.h"

void process_mscBlock(struct sdr_state_t *sdr, int16_t data[], int16_t blkno) {
}

FILE *pFile;

int count = 0;

int readFromDevice(rtlsdr_frontend_t *lfe) {
    if (lfe->sdr.fibCRCsuccess > 0) {
        printf("is synced!\n");
        return 0;
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
        exit(EXIT_SUCCESS);
    }

    initConstPhaseReference();
    initConstViterbi768();
    initConstOfdmDecoder();

    rtlsdr_frontend_t *lfe = calloc(1, sizeof(rtlsdr_frontend_t));
    
    lfe->sdr.mmi = calloc(1, sizeof(dab_ensemble_instance_t));
    lfe->sdr.mmi->mmi_ensemble = calloc(1, sizeof(dab_ensemble_t));
    lfe->running = 1;
    lfe->lfe_dvr_pipe.rd = 1;
    
    sdr_init(&lfe->sdr);
    
    rtlsdr_demod_thread_fn(lfe);
    
    printf("success: %d\n", lfe->sdr.fibCRCsuccess);
    
    sdr_destroy(&lfe->sdr);
    
    free(lfe->sdr.mmi->mmi_ensemble);
    free(lfe->sdr.mmi);
    free(lfe);
    fclose(pFile);

    exit(EXIT_SUCCESS);
}