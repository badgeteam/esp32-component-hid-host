// Writes the captured report descriptors out as files, to seed the fuzzer with input that gets
// past the first few bytes.

#include <stdio.h>
#include "test_descriptors.h"

static int write_one(const char* name, const uint8_t* descriptor, size_t length) {
    char path[256];
    snprintf(path, sizeof(path), "corpus/%s", name);

    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        return 1;
    }
    fwrite(descriptor, 1, length, file);
    fclose(file);
    return 0;
}

int main(void) {
    int failed = 0;

    failed |= write_one("mouse1", mouse1_desc, mouse1_len);
    failed |= write_one("mouse2", mouse2_desc, mouse2_len);
    failed |= write_one("mouse3", mouse3_desc, mouse3_len);
    failed |= write_one("gamepad1", gamepad1_desc, gamepad1_len);
    failed |= write_one("gamepad2", gamepad2_desc, gamepad2_len);
    failed |= write_one("gamepad3", gamepad3_desc, gamepad3_len);
    failed |= write_one("gamepad4", gamepad4_desc, gamepad4_len);

    return failed;
}
