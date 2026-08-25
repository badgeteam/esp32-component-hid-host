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
    failed |= write_one("gamepad5", gamepad5_desc, gamepad5_len);
    failed |= write_one("gamepad6", gamepad6_desc, gamepad6_len);
    failed |= write_one("gamepad7", gamepad7_desc, gamepad7_len);
    failed |= write_one("gamepad8", gamepad8_desc, gamepad8_len);
    failed |= write_one("gamepad9", gamepad9_desc, gamepad9_len);
    failed |= write_one("gamepad10", gamepad10_desc, gamepad10_len);
    failed |= write_one("gamepad11", gamepad11_desc, gamepad11_len);
    failed |= write_one("gamepad12", gamepad12_desc, gamepad12_len);
    failed |= write_one("callbutton", callbutton_desc, callbutton_len);
    failed |= write_one("gamepad13", gamepad13_desc, gamepad13_len);
    failed |= write_one("gamepad14", gamepad14_desc, gamepad14_len);
    failed |= write_one("wheel1", wheel1_desc, wheel1_len);
    failed |= write_one("wheel2", wheel2_desc, wheel2_len);
    failed |= write_one("rc1", rc1_desc, rc1_len);
    failed |= write_one("rc2", rc2_desc, rc2_len);

    return failed;
}
