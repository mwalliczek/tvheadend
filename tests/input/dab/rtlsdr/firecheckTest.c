#include "firecheck.h"

int main(int argc, char** argv) {
    firecheck_init();
    uint8_t test[11] = {0,0,0,0,0,0,0,0,0,0,0};
    int r = firecode_check(test);
    printf("result: %d\n", r);
    exit(EXIT_SUCCESS);
}
