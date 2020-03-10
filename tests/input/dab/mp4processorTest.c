#include "mp4processor.h"
#include "rtlsdr/firecheck.h"

uint8_t* myResult;
int16_t myResultLength;

void callback(uint8_t* result, int16_t resultLength);

int main(int argc, char** argv) {
    mp4processor_t* mp4 = init_mp4processor(72, callback);
    uint8_t *input = calloc(1728, sizeof(uint8_t));
    FILE *pFile;
    int i;
    
    firecheck_init();

    myResult = NULL;
    myResultLength = 0;
    for (i=0; i<=6; i++) {
        char buffer[128];
        snprintf(buffer, 128, "input/dab/mp4in%d", i);
        pFile = fopen (buffer, "rb");
        fread (input, 1, 1728, pFile);
        fclose(pFile);
        mp4Processor_addtoFrame(mp4, input);
    }
    free(input);
    printf("myResult %p, myResultLength %d\n", myResult, myResultLength);
    
    destroy_mp4processor(mp4);
    exit(EXIT_SUCCESS);
}

void callback(uint8_t* result, int16_t resultLength) {
    if (NULL == myResult) {
        FILE *pFile;
        uint8_t *output = calloc(356, sizeof(uint8_t));
        printf("result %p, resultLength %d\n", result, resultLength);
        myResult = result;
        myResultLength = resultLength;
        pFile = fopen ("input/dab/mp4out" , "rb" );
        fread (output, 1, 356, pFile);
        fclose(pFile);

        printf("memcmp: %d\n", memcmp(output, myResult, 356));
        free(output);
    }
}