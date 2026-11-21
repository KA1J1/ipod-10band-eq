/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CFW_OSOS_H
#define CFW_PATCH_H
#define PATCH_COPY(...)
#define OSOS_ACTION_SUPPORT_QUERY 0xdeadbeef
#include "../../../payload/custom_eq.h"

struct osos_path {
    uint16_t volume;
    char text[258];
};

static unsigned char disk[2048];
static uint32_t disk_size, writes, status, refreshes;
static int write_failure, filter_failure;

/* Mirrors payload/custom_eq.ui: precut, then type, frequency, Q and gain per band.
 * The frequency values are positions in the ten-band selector table. */
#define BAND(type, frequency, q, gain)                                                 \
    {.count = 4, .default_value = type}, {.count = 35, .default_value = frequency},    \
        {.count = 58, .default_value = q}, {.count = 241, .default_value = gain}
const struct cfw_ui_field cfw_eq_fields[CFW_EQ_FIELDS] = {
    {.count = 101},
    BAND(2, 5, 4, 120),  /* Band 1: low shelf 60 Hz */
    BAND(1, 18, 4, 120), /* Band 2: peaking 1000 Hz */
    BAND(3, 29, 4, 120), /* Band 3: high shelf 8000 Hz */
    BAND(0, 12, 4, 120), /* Band 4: off, 250 Hz */
    BAND(0, 24, 4, 120), /* Band 5: off, 4000 Hz */
    BAND(0, 17, 4, 120), /* Band 6: off, 800 Hz */
    BAND(0, 20, 4, 120), /* Band 7: off, 1600 Hz */
    BAND(0, 23, 4, 120), /* Band 8: off, 3150 Hz */
    BAND(0, 27, 4, 120), /* Band 9: off, 6300 Hz */
    BAND(0, 32, 4, 120), /* Band 10: off, 12500 Hz */
    {.count = 8}, /* Profile selector, one label per slot */
};
const struct cfw_ui_action cfw_eq_save = {.count = 3};
static const char *(*strings_resolver)(uint32_t property);
static const char *(*indexed_resolver)(uint32_t property, uint32_t index);

void cfw_ui_register_strings(const char *(*resolve)(uint32_t property),
                             const char *(*resolve_indexed)(uint32_t property,
                                                            uint32_t index))
{
    strings_resolver = resolve;
    indexed_resolver = resolve_indexed;
}

/* The selector tables from payload/custom_eq.ui, needed for conversions. */
const int32_t cfw_eq_types[4] = {
    0, 1, 2, 3,
};
const int32_t cfw_eq_frequencies[35] = {
    20, 25, 31, 40, 50, 60, 70, 80, 100,
    125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 5600,
    6300, 7100, 8000, 9000, 10000, 12500, 16000, 20000,
};
const int32_t cfw_eq_q[58] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
    59, 60,
};
const int32_t cfw_eq_gain[241] = {
    -120, -119, -118, -117, -116, -115, -114, -113, -112, -111, -110, -109, -108, -107,
    -106, -105, -104, -103, -102, -101, -100, -99, -98, -97, -96, -95, -94, -93,
    -92, -91, -90, -89, -88, -87, -86, -85, -84, -83, -82, -81, -80, -79,
    -78, -77, -76, -75, -74, -73, -72, -71, -70, -69, -68, -67, -66, -65,
    -64, -63, -62, -61, -60, -59, -58, -57, -56, -55, -54, -53, -52, -51,
    -50, -49, -48, -47, -46, -45, -44, -43, -42, -41, -40, -39, -38, -37,
    -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23,
    -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9,
    -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5,
    6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
    76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
    90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117,
    118, 119, 120,
};

static int osos_file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWEQ"));
    assert(mode == 1 || mode == 2);
    *handle = disk;
    return 0;
}

static int osos_file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == disk);
    *count = disk_size < size ? disk_size : size;
    memcpy(out, disk, *count);
    return 0;
}

static int osos_file_write(void *handle, const void *data, uint32_t size,
                           uint32_t *count)
{
    assert(handle == disk);
    writes++;
    if (write_failure)
        return -1;
    assert(size <= sizeof(disk));
    memcpy(disk, data, size);
    disk_size = size;
    *count = size;
    return 0;
}

static void osos_file_close(void *handle)
{
    assert(handle == disk);
}

int cfw_eq_publish(const uint32_t values[CFW_EQ_FIELDS])
{
    for (unsigned i = 0; i < CFW_EQ_FIELDS; i++)
        assert(values[i] < cfw_eq_fields[i].count);
    return !filter_failure;
}

void cfw_ui_refresh(const struct cfw_ui_field *field)
{
    assert(field >= cfw_eq_fields && field < cfw_eq_fields + CFW_EQ_FIELDS);
    refreshes++;
}

void cfw_ui_status(const struct cfw_ui_action *action, unsigned value)
{
    assert(value < action->count);
    if (action == &cfw_eq_save) {
        assert(value < 3);
        status = value;
    }
}

#include "../../../payload/custom_eq.c"

static void saved_legacy(uint32_t version, uint32_t precut)
{
    struct legacy_eq_file file = {.magic = 0x51455743, .version = version};
    file.values[0] = precut;
    file.values[4] = 24; /* Band 1 gain, five-tenth steps, 0 dB. */
    file.checksum = checksum(&file, sizeof(file));
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

/* Version 3: five bands with the selector tables from before the additions. */
static void saved_version3(void)
{
    struct version3_eq_file file = {.magic = 0x51455743, .version = 3};
    file.values[CFW_EQ_PRECUT] = 37;
    file.values[1] = 1; /* Band 1: peaking, 60 Hz (old index 5), Q 0.7, 0 dB. */
    file.values[2] = 5;
    file.values[3] = 4;
    file.values[4] = 24;
    file.values[17] = 1; /* Band 5: peaking, 12500 Hz (old index 29), Q 4.0. */
    file.values[18] = 29;
    file.values[19] = 37;
    file.values[20] = 9; /* Band 5 gain: -120 + 5 * 9 = -75 tenths. */
    file.checksum = checksum(&file, sizeof(file));
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

/* Version 4: the ten-band tune before slots existed. */
static void saved_version4(void)
{
    struct version4_eq_file file = {.magic = 0x51455743, .version = 4};
    file.values[CFW_EQ_PRECUT] = 42;
    file.values[1] = 1;  /* Band 1: peaking, 70 Hz, Q 0.5, -2.5 dB. */
    file.values[2] = 6;
    file.values[3] = 2;
    file.values[4] = 95;
    file.checksum = checksum(&file, sizeof(file));
    memcpy(disk, &file, sizeof(file));
    disk_size = sizeof(file);
}

/* Emulates picking a slot in the Profile selector. */
static void switch_to(uint32_t slot)
{
    cfw_eq_model.change(EQ_PROFILE_FIELD, slot);
    cfw_eq_model.commit();
}

static void load(void)
{
    cfw_eq_load_preferences();
}

/* Version 6 file: header, eight names, eight slots, checksum. */
static void check_stored(void)
{
    struct eq_file file;
    assert(disk_size == sizeof(file));
    memcpy(&file, disk, sizeof(file));
    assert(file.magic == 0x51455743 && file.version == 6);
    assert(file.count == EQ_SLOTS && file.active == active_slot);
    assert(file.checksum == checksum(&file, sizeof(file)));
    for (unsigned slot = 0; slot < EQ_SLOTS; slot++) {
        assert(!strncmp(file.names[slot], names[slot], EQ_NAME_BYTES));
        for (unsigned i = 0; i < EQ_FILE_FIELDS; i++) {
            assert(file.slots[slot][i] == slots[slot][i]);
            assert(file.slots[slot][i] < cfw_eq_fields[i].count);
        }
    }
    for (unsigned i = 0; i < EQ_FILE_FIELDS; i++)
        assert(file.slots[active_slot][i] == settings[i]);
}

int main(void)
{
    _Static_assert(sizeof(struct eq_file) == 1524, "eight named slot file size");
    _Static_assert(sizeof(struct eq_file_v5) == 1332, "unnamed eight-slot file size");
    _Static_assert(sizeof(struct version4_eq_file) == 176, "ten-band file size");
    _Static_assert(sizeof(struct version3_eq_file) == 96, "five-band file size");
    _Static_assert(sizeof(struct legacy_eq_file) == 64, "three-band file size");

    /* Version 1 precut is stored in twentieths of a decibel. */
    saved_legacy(1, 7);
    load();
    assert(settings[CFW_EQ_PRECUT] == 35 && settings[CFW_EQ_BAND1_GAIN] == 120);
    assert(active_slot == 0 && writes == 0 && dirty);
    cfw_eq_model.commit();
    assert(writes == 1 && !dirty);
    check_stored();
    /* Every band beyond the three saved ones keeps its default, and the unused
     * slots stay at the defaults too. */
    assert(settings[CFW_EQ_BAND4_TYPE] == 0 && settings[CFW_EQ_BAND10_TYPE] == 0);
    assert(settings[CFW_EQ_BAND10_FREQUENCY] == 32);
    assert(slots[1][CFW_EQ_PRECUT] == 0 && slots[EQ_SLOTS - 1][CFW_EQ_PRECUT] == 0);

    saved_legacy(1, 21);
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 1);
    saved_legacy(2, 37);
    load();
    assert(settings[CFW_EQ_PRECUT] == 37 && settings[CFW_EQ_BAND1_GAIN] == 120);
    assert(writes == 1 && dirty);
    cfw_eq_model.commit();
    assert(writes == 2 && !dirty);

    /* Version 3 converts frequencies and gains by value, then rewrites. */
    saved_version3();
    load();
    assert(settings[CFW_EQ_PRECUT] == 37);
    assert(settings[CFW_EQ_BAND1_TYPE] == 1 && settings[CFW_EQ_BAND1_FREQUENCY] == 5);
    assert(settings[CFW_EQ_BAND1_Q] == 4 && settings[CFW_EQ_BAND1_GAIN] == 120);
    assert(settings[CFW_EQ_BAND5_TYPE] == 1 && settings[CFW_EQ_BAND5_FREQUENCY] == 32);
    assert(settings[CFW_EQ_BAND5_Q] == 37 && settings[CFW_EQ_BAND5_GAIN] == 45);
    assert(writes == 2 && dirty);
    cfw_eq_model.commit();
    assert(writes == 3 && !dirty);
    check_stored();

    /* Out-of-range legacy values fall back to defaults without writing. */
    saved_version3();
    struct version3_eq_file broken;
    memcpy(&broken, disk, sizeof(broken));
    broken.values[2] = 32; /* Past the old frequency table. */
    broken.checksum = checksum(&broken, sizeof(broken));
    memcpy(disk, &broken, sizeof(broken));
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 3);
    broken.values[2] = 5;
    broken.values[3] = 38; /* Past the old Q table. */
    broken.checksum = checksum(&broken, sizeof(broken));
    memcpy(disk, &broken, sizeof(broken));
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 3);
    broken.values[3] = 4;
    broken.values[4] = 49; /* Past the old gain table. */
    broken.checksum = checksum(&broken, sizeof(broken));
    memcpy(disk, &broken, sizeof(broken));
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 3);
    broken.values[4] = 24;
    broken.values[0] = 150; /* Precut past the 101-entry table. */
    broken.checksum = checksum(&broken, sizeof(broken));
    memcpy(disk, &broken, sizeof(broken));
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 3);

    /* Version 4 lands in slot 1 unchanged. */
    saved_version4();
    load();
    assert(settings[CFW_EQ_PRECUT] == 42 && settings[CFW_EQ_BAND1_GAIN] == 95);
    assert(settings[CFW_EQ_BAND1_FREQUENCY] == 6 && settings[CFW_EQ_BAND1_Q] == 2);
    assert(active_slot == 0 && writes == 3 && dirty);
    cfw_eq_model.commit();
    assert(writes == 4 && !dirty);
    check_stored();

    /* Reading the current file does not write again. */
    load();
    assert(writes == 4 && settings[CFW_EQ_PRECUT] == 42);

    /* A version 5 file with the wrong slot count is rejected, not reinterpreted. */
    struct eq_file wrong;
    memcpy(&wrong, disk, sizeof(wrong));
    assert(wrong.count == EQ_SLOTS);
    wrong.count = EQ_SLOTS - 1;
    wrong.checksum = checksum(&wrong, sizeof(wrong));
    memcpy(disk, &wrong, sizeof(wrong));
    disk_size = sizeof(wrong);
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 4);
    saved_version4();
    load();
    assert(writes == 4 && dirty && settings[CFW_EQ_PRECUT] == 42);
    cfw_eq_model.commit();
    assert(writes == 5 && !dirty);

    /* A corrupted checksum keeps the defaults and leaves the file alone. */
    disk[20] ^= 1;
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && writes == 5);
    saved_version4();
    load();
    assert(writes == 5 && dirty);
    cfw_eq_model.commit();
    assert(writes == 6 && !dirty);

    /* Switching slots saves, and each slot keeps its own tune. */
    uint32_t before = writes;
    switch_to(1);
    assert(active_slot == 1 && writes == before + 1);
    assert(settings[CFW_EQ_PRECUT] == 0);
    cfw_eq_model.change(CFW_EQ_PRECUT, 25);
    cfw_eq_model.commit();
    before = writes;
    switch_to(0);
    assert(active_slot == 0 && writes == before + 1);
    assert(settings[CFW_EQ_PRECUT] == 42);
    switch_to(1);
    assert(active_slot == 1 && settings[CFW_EQ_PRECUT] == 25);
    check_stored();

    /* Picking the active slot again changes nothing. */
    before = writes;
    switch_to(1);
    assert(writes == before && active_slot == 1);

    /* Any slot can be picked directly. */
    switch_to(5);
    assert(active_slot == 5 && settings[EQ_PROFILE_FIELD] == 5);
    switch_to(0);
    assert(active_slot == 0 && settings[CFW_EQ_PRECUT] == 42);
    check_stored();

    /* Names live in the file and feed the Profile selector. */
    struct eq_file named;
    memcpy(&named, disk, sizeof(named));
    const char tag[] = "TEST TUNE";
    for (unsigned i = 0; i < sizeof(tag); i++)
        named.names[0][i] = tag[i];
    named.checksum = checksum(&named, sizeof(named));
    memcpy(disk, &named, sizeof(named));
    load();
    assert(strings_resolver);
    assert(!strcmp(indexed_resolver(cfw_eq_fields[EQ_PROFILE_FIELD].property, 0), "TEST TUNE"));
    assert(!strcmp(indexed_resolver(cfw_eq_fields[EQ_PROFILE_FIELD].property, 1), "Slot 2"));
    assert(!strcmp(strings_resolver(cfw_eq_fields[EQ_PROFILE_FIELD].summary_property), "Profile: TEST TUNE"));
    /* The Edit name page edits ten cells through its own API. */
    cfw_eq_model.commit();
    cfw_eq_name_begin();
    assert(strlen(names[active_slot]) <= EQ_NAME_CHARS);
    char original[EQ_NAME_BYTES];
    strcpy(original, names[active_slot]);
    cfw_eq_name_spin(0, 3);
    assert(names[active_slot][0] != original[0]);
    cfw_eq_name_spin(0, -3);
    assert(!strcmp(names[active_slot], original));
    cfw_eq_name_spin(1, 41); /* full wrap plus one */
    char edited[EQ_NAME_BYTES];
    strcpy(edited, names[active_slot]);
    cfw_eq_name_commit();
    load();
    assert(!strcmp(names[active_slot], edited));
    assert(!strncmp(strings_resolver(cfw_eq_fields[EQ_PROFILE_FIELD].summary_property),
                   "Profile: ", 9));

    /* A support query answers without touching anything. */
    before = writes;
    assert(cfw_eq_action("CFW_EQ_Save", OSOS_ACTION_SUPPORT_QUERY) == 1);
    assert(writes == before);

    /* Unknown actions stay unhandled. */
    assert(cfw_eq_action("CFW_EQ_Unknown", 0) == 0);
    assert(cfw_eq_action("CFW_EQ_Save", OSOS_ACTION_SUPPORT_QUERY) == 1);

    /* Edits commit once, and a failed write stays dirty for a retry. */
    cfw_eq_model.change(CFW_EQ_PRECUT, 38);
    assert(settings[CFW_EQ_PRECUT] == 38 && dirty && refreshes > 0);
    write_failure = 1;
    before = writes;
    cfw_eq_model.commit();
    assert(dirty && status == 1 && writes == before + 1);
    write_failure = 0;
    cfw_eq_model.commit();
    assert(!dirty && status == 0 && writes == before + 2);
    check_stored();

    /* A rejected filter snaps back and does not dirty the tune. */
    filter_failure = 1;
    cfw_eq_model.change(CFW_EQ_PRECUT, 39);
    assert(settings[CFW_EQ_PRECUT] == 38 && status == 2 && !dirty);
    filter_failure = 0;
    cfw_eq_model.change(CFW_EQ_PRECUT, 39);
    cfw_eq_model.commit();
    load();
    assert(settings[CFW_EQ_PRECUT] == 39);

    /* A truncated file is ignored. */
    disk_size--;
    load();
    assert(settings[CFW_EQ_PRECUT] == 0 && settings[CFW_EQ_BAND10_Q] == 4);
    return 0;
}
