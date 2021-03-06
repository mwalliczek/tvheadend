#include "dab.h"
#include "tvheadend.h"

int main(int argc, char** argv) {
    dab_ensemble_t* ensemble = calloc(1, sizeof(dab_ensemble_t));
    ensemble->subChannels[0].protLevel = 2;
    ensemble->subChannels[0].BitRate = 72;
    dab_service_t* service = calloc(1, sizeof(dab_service_t));
    service->s_dab_ensemble = ensemble;
    service->subChId = 0;
    sdr_dab_service_instance_t* sds = sdr_dab_service_instance_create(service);
    sdr_dab_service_instance_destroy(sds);
    free(service);
    free(ensemble);
    exit(EXIT_SUCCESS);
}