#include "protection.h"

int main(int argc, char** argv) {
    protection_t* protection = eep_protection_init(72, 2);
    protection_destroy(protection);
    exit(EXIT_SUCCESS);
}
