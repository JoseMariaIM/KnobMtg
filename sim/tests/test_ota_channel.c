/* Update channel routing: which firmware image this device is told to
 * flash (see the "update channels" block in wifi_ota.h).
 *
 * This is the one place in the OTA path where a quiet mistake means
 * flashing the wrong build onto somebody else's device - and the two
 * decisions it rests on are a hand-edited list in a GitHub workflow
 * input and a substring search over JSON. Both are exercised here
 * because neither is observable from the device afterwards: a unit that
 * was enrolled by accident just silently updates.
 */
#include "test_harness.h"
#include "../../knobby/src/presentation/ota/wifi_ota.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* The shape release.yml publishes. test_version deliberately sits BEFORE
   version: a parser that searched for "version" without requiring the
   opening quote would read the test build's number as the stable one and
   hand every device the test build. */
static const char *OTA_JSON =
    "{\n"
    "  \"test_version\": \"v1.5.0-rc1\",\n"
    "  \"test_bin\": \"test/knobby.ino.bin\",\n"
    "  \"test_devices\": \"A1B2C3, de45f6\",\n"
    "  \"version\": \"v1.4.0\",\n"
    "  \"bin\": \"knobby.ino.bin\"\n"
    "}\n";

int main(void)
{
    char value[64];

    test_harness_init();

    /* ---- Field extraction ---- */
    assert(ota_json_string_field(OTA_JSON, "version", value, sizeof(value)));
    assert(strcmp(value, "v1.4.0") == 0);
    assert(ota_json_string_field(OTA_JSON, "test_version", value, sizeof(value)));
    assert(strcmp(value, "v1.5.0-rc1") == 0);
    assert(ota_json_string_field(OTA_JSON, "bin", value, sizeof(value)));
    assert(strcmp(value, "knobby.ino.bin") == 0);
    assert(ota_json_string_field(OTA_JSON, "test_bin", value, sizeof(value)));
    assert(strcmp(value, "test/knobby.ino.bin") == 0);
    printf("PASS: each channel's version and binary path read back distinctly\n");

    /* An absent key must fail rather than return the neighbouring
       value - "no test build published" has to be distinguishable from
       "here is one", since the first falls back to stable. */
    assert(!ota_json_string_field(OTA_JSON, "beta_version", value, sizeof(value)));
    assert(!ota_json_string_field("{}", "version", value, sizeof(value)));
    assert(!ota_json_string_field(NULL, "version", value, sizeof(value)));
    /* A value longer than the destination is truncated, not written past
       the end of the buffer. */
    assert(ota_json_string_field(OTA_JSON, "test_bin", value, 5));
    assert(strcmp(value, "test") == 0);
    printf("PASS: a missing key fails and a long value truncates\n");

    /* ---- Tester list matching ---- */
    assert(ota_json_string_field(OTA_JSON, "test_devices", value, sizeof(value)));
    assert(ota_device_in_list(value, "A1B2C3"));
    assert(ota_device_in_list(value, "a1b2c3"));       /* case does not matter */
    assert(ota_device_in_list(value, "DE45F6"));       /* nor does the list's case */
    assert(!ota_device_in_list(value, "A1B2C4"));
    printf("PASS: listed ids match regardless of case, unlisted ones do not\n");

    /* Whole entries only. A truncated or over-long id from a mistyped
       workflow input must not match by prefix - that would enrol a
       device nobody named. */
    assert(!ota_device_in_list("A1B2C3D", "A1B2C3"));
    assert(!ota_device_in_list("A1B2C3", "A1B2C3D"));
    assert(!ota_device_in_list("A1B2C3", "A1B2"));
    assert(ota_device_in_list("  A1B2C3  ,  DE45F6  ", "DE45F6")); /* padding is separator, not id */
    printf("PASS: only whole entries match\n");

    /* Nothing published and nothing asked for: the list is how a device
       gets onto test builds, so every degenerate input has to mean no. */
    assert(!ota_device_in_list("", "A1B2C3"));
    assert(!ota_device_in_list(",, ,", "A1B2C3"));
    assert(!ota_device_in_list(NULL, "A1B2C3"));
    assert(!ota_device_in_list("A1B2C3", ""));
    assert(!ota_device_in_list("A1B2C3", NULL));
    printf("PASS: an empty list or id never enrols anything\n");

    /* ---- The decision itself ---- */
    assert(ota_channel_is_test(OTA_JSON, "A1B2C3"));
    assert(ota_channel_is_test(OTA_JSON, "DE45F6"));
    assert(!ota_channel_is_test(OTA_JSON, "A1B2C4"));
    /* A routing file with no list at all, or a list that is there but
       empty, puts everybody on stable. Anything else would mean a
       half-published or mistyped file promoting devices nobody chose -
       and since nothing on the device can opt itself in, this function
       is the entire gate. */
    assert(!ota_channel_is_test("{\"version\":\"v1.4.0\"}", "A1B2C3"));
    assert(!ota_channel_is_test("{\"test_devices\":\"\"}", "A1B2C3"));
    assert(!ota_channel_is_test("", "A1B2C3"));
    printf("PASS: only a device named in the published list is a tester\n");

    /* Nothing has checked yet, so this device does not consider itself a
       tester: being one is something it can only learn from the list. */
    assert(!ota_on_test_channel());
    printf("PASS: a device does not claim the test channel before checking\n");

    printf("PASS: ota channel routing\n");
    return 0;
}
