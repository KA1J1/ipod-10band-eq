/* SPDX-License-Identifier: GPL-3.0-only */

#include "custom_eq.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "patch.h"

/* Native preset index 23 / saved ID 122. Reuse Flat's preview. */
const struct {
    uint32_t name, preview;
} cfw_eq_presets[24] = {
    [23] = {CFW_EQ_PRESET_NAME, 0},
};

const uint32_t cfw_eq_preset_ids[24] = {[23] = 122};

PATCH_COPY(cfw_eq_presets, 0, OSOS_EQ_PRESETS, 23 * 8);
PATCH_COPY(cfw_eq_presets, 23 * 8 + 4, OSOS_EQ_PRESETS + 8 * 8 + 4, 4);
PATCH_COPY(cfw_eq_preset_ids, 0, OSOS_EQ_PRESET_IDS, 23 * 4);

/* Version 6 keeps eight named tunes. Versions 1-5 stored fewer tunes or no names
 * and are imported into slot 1 on first boot. */
enum {
    EQ_VERSION = 6,
    EQ_SLOTS = 8,
    /* Fields stored per slot; the profile picker is a UI-only field. */
    EQ_FILE_FIELDS = 1 + 4 * 10,
    EQ_NAME_CHARS = 10,
    /* UI-only field that selects the active slot. */
    EQ_PROFILE_FIELD = EQ_FILE_FIELDS,
    EQ_NAME_BYTES = 24,
    EQ_V1_FIELDS = 13,
    EQ_V3_FIELDS = 21,
    EQ_V1_BANDS = 3,
    EQ_V3_BANDS = 5,
    EQ_LEGACY_FREQUENCY_COUNT = 32,
    EQ_LEGACY_Q_FIRST = 3,
    EQ_LEGACY_Q_STEP = 1,
    EQ_LEGACY_Q_COUNT = 38,
    EQ_LEGACY_GAIN_FIRST = -120,
    EQ_LEGACY_GAIN_STEP = 5,
    EQ_LEGACY_GAIN_COUNT = 49,
};

struct eq_file {
    uint32_t magic, version, count, active;
    char names[EQ_SLOTS][EQ_NAME_BYTES];
    uint32_t slots[EQ_SLOTS][EQ_FILE_FIELDS];
    uint32_t checksum;
};

struct eq_file_v5 {
    uint32_t magic, version, count, active;
    uint32_t slots[EQ_SLOTS][EQ_FILE_FIELDS];
    uint32_t checksum;
};

struct version4_eq_file {
    uint32_t magic, version;
    uint32_t values[EQ_FILE_FIELDS];
    uint32_t checksum;
};

struct legacy_eq_file {
    uint32_t magic, version;
    uint32_t values[EQ_V1_FIELDS];
    uint32_t checksum;
};

struct version3_eq_file {
    uint32_t magic, version;
    uint32_t values[EQ_V3_FIELDS];
    uint32_t checksum;
};

/* Frequency selector of files below version 4, kept for conversion. */
static const int32_t legacy_frequencies[EQ_LEGACY_FREQUENCY_COUNT] = {
    20,   25,   31,   40,   50,   60,   70,   80,  100,  125,   160,
    200,  250,  315,  400,  500,  630,  800, 1000, 1250, 1600,  2000,
    2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000,
};

static uint32_t settings[CFW_EQ_FIELDS];
static uint32_t slots[EQ_SLOTS][EQ_FILE_FIELDS];
static char names[EQ_SLOTS][EQ_NAME_BYTES];
/* Summary text of the Profile row, rebuilt from names[] on demand. */
static char profile_text[EQ_NAME_BYTES + 16];
static struct eq_file file_buffer;
/* Version 5 record, kept static so boot never needs 1.3 KB of stack. */
static struct eq_file_v5 legacy_buffer;
static uint32_t active_slot;
static int initialized;
static unsigned int save_status;
static int dirty;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWEQ"};

static uint32_t checksum(const void *file, unsigned int size)
{
    const unsigned char *bytes = (const unsigned char *)file;
    uint32_t hash = 2166136261u;
    for (unsigned int i = 0; i < size - 4; i++)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

static void slot_defaults(uint32_t *values)
{
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        values[i] = cfw_eq_fields[i].default_value;
}

/* "Slot 1" ... "Slot 8", used until a name is written. */
static void slot_name_default(unsigned int slot, char *name)
{
    static const char digits[] = "12345678";
    name[0] = 'S';
    name[1] = 'l';
    name[2] = 'o';
    name[3] = 't';
    name[4] = ' ';
    name[5] = digits[slot];
    for (unsigned int i = 6; i < EQ_NAME_BYTES; i++)
        name[i] = 0;
}

static void defaults(void)
{
    for (unsigned int slot = 0; slot < EQ_SLOTS; slot++) {
        slot_defaults(slots[slot]);
        slot_name_default(slot, names[slot]);
    }
    active_slot = 0;
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        settings[i] = slots[0][i];
    settings[EQ_PROFILE_FIELD] = 0;
    cfw_eq_publish(settings);
    initialized = 1;
    dirty = 0;
}

static void refresh_fields(void)
{
    for (unsigned int i = 0; i < CFW_EQ_FIELDS; i++)
        cfw_ui_refresh(&cfw_eq_fields[i]);
}

/* Only call from a UI action: cfw_ui_status notifies the native menu model. */
static void refresh_profiles(void)
{
    cfw_ui_refresh(&cfw_eq_fields[EQ_PROFILE_FIELD]);
}

/* Position of a selector value, or -1 when the current table lacks it. */
static int index_of(const int32_t *values, uint32_t count, int32_t value)
{
    for (unsigned int i = 0; i < count; i++)
        if (values[i] == value)
            return (int)i;
    return -1;
}

static uint32_t frequency_count(void)
{
    return cfw_eq_fields[CFW_EQ_BAND1_FREQUENCY].count;
}

/* Band slots are type, frequency, Q and gain, in saved order. */
static int convert_bands(uint32_t *values, const uint32_t *legacy,
                         unsigned int bands)
{
    values[CFW_EQ_PRECUT] = legacy[CFW_EQ_PRECUT];
    for (unsigned int band = 0; band < bands; band++) {
        const uint32_t *from = &legacy[1 + band * 4];
        uint32_t *to = &values[1 + band * 4];
        if (from[0] >= cfw_eq_fields[CFW_EQ_BAND1_TYPE].count ||
            from[1] >= EQ_LEGACY_FREQUENCY_COUNT ||
            from[2] >= EQ_LEGACY_Q_COUNT || from[3] >= EQ_LEGACY_GAIN_COUNT)
            return 0;
        int frequency = index_of(cfw_eq_frequencies, frequency_count(),
                                 legacy_frequencies[from[1]]);
        int q = index_of(cfw_eq_q, cfw_eq_fields[CFW_EQ_BAND1_Q].count,
                         EQ_LEGACY_Q_FIRST + EQ_LEGACY_Q_STEP * (int)from[2]);
        int gain = index_of(cfw_eq_gain, cfw_eq_fields[CFW_EQ_BAND1_GAIN].count,
                            EQ_LEGACY_GAIN_FIRST +
                                EQ_LEGACY_GAIN_STEP * (int)from[3]);
        if (frequency < 0 || q < 0 || gain < 0)
            return 0;
        to[0] = from[0];
        to[1] = (uint32_t)frequency;
        to[2] = (uint32_t)q;
        to[3] = (uint32_t)gain;
    }
    return 1;
}

static int valid_values(const uint32_t *values)
{
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        if (values[i] >= cfw_eq_fields[i].count)
            return 0;
    return 1;
}

/* Printable text and a terminator inside the fixed name field. */
static int valid_name(const char *name)
{
    for (unsigned int i = 0; i < EQ_NAME_BYTES; i++) {
        unsigned char byte = (unsigned char)name[i];
        if (!byte)
            return 1;
        if (byte < 0x20 || byte == 0x7f)
            return 0;
    }
    return 0;
}

/* "Profile: <name>" for the selector row; the list itself shows the names. */
static const char *profile_summary(void)
{
    static const char title[] = "Profile: ";
    char *out = profile_text;
    unsigned int n = 0;
    for (unsigned int i = 0; i < sizeof(title) - 1; i++)
        out[n++] = title[i];
    const char *source = names[active_slot];
    while (n < EQ_NAME_BYTES + 12 && *source)
        out[n++] = *source++;
    out[n] = 0;
    return out;
}

/* cfw_ui_property() asks this before the generated labels, so the Profile row
 * and the character rows show real text. */
static const char *profile_string(uint32_t property)
{
    const struct cfw_ui_field *profile = &cfw_eq_fields[EQ_PROFILE_FIELD];
    if (property == profile->summary_property)
        return profile_summary();
    if (property == profile->property)
        return profile_summary();
    return 0;
}

/* Small API for the name canvas: one character cell at a time. */

/* Ten editable cells only: pad with spaces, drop anything past them, trim. */
void cfw_eq_name_begin(void)
{
    char rebuilt[EQ_NAME_BYTES];
    char *name = names[active_slot];
    unsigned int length = 0;
    int changed = 0;
    for (unsigned int i = 0; i < EQ_NAME_BYTES; i++)
        rebuilt[i] = 0;
    for (unsigned int i = 0; i < EQ_NAME_CHARS; i++) {
        rebuilt[i] = name[i] ? name[i] : ' ';
        if (rebuilt[i] != ' ')
            length = i + 1;
    }
    for (unsigned int i = length; i < EQ_NAME_BYTES; i++)
        rebuilt[i] = 0;
    for (unsigned int i = 0; i < EQ_NAME_BYTES; i++) {
        if (name[i] != rebuilt[i])
            changed = 1;
        name[i] = rebuilt[i];
    }
    if (changed)
        dirty = 1;
}
static void save_preferences(void);

/* Character picker of the Edit name page. */
static const char name_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -._";
enum { EQ_SPACE_INDEX = 36 };

static unsigned int charset_index(char character)
{
    for (unsigned int i = 0; name_chars[i]; i++)
        if (name_chars[i] == character)
            return i;
    return EQ_SPACE_INDEX;
}

const char *cfw_eq_name_text(void)
{
    return names[active_slot];
}

unsigned int cfw_eq_name_length(void)
{
    return EQ_NAME_CHARS;
}

void cfw_eq_name_spin(unsigned int position, int delta)
{
    unsigned int length = sizeof(name_chars) - 1;
    if (position >= EQ_NAME_CHARS || !length)
        return;
    for (unsigned int i = 0; i < EQ_NAME_CHARS; i++)
        if (!names[active_slot][i])
            names[active_slot][i] = ' ';
    int value = (int)charset_index(names[active_slot][position]) + delta;
    while (value < 0)
        value += (int)length;
    while ((unsigned int)value >= length)
        value -= (int)length;
    names[active_slot][position] = name_chars[value];
    unsigned int end = EQ_NAME_CHARS;
    while (end && names[active_slot][end - 1] == ' ')
        end--;
    for (unsigned int i = end; i < EQ_NAME_BYTES; i++)
        names[active_slot][i] = 0;
    dirty = 1;
}

void cfw_eq_name_commit(void)
{
    if (dirty)
        save_preferences();
}

/* List rows of the Profile selector: the stored names, not the static labels. */
static const char *profile_indexed(uint32_t property, uint32_t index)
{
    if (property == cfw_eq_fields[EQ_PROFILE_FIELD].property && index < EQ_SLOTS)
        return names[index];
    return 0;
}

static int strings_registered;

static void register_strings(void)
{
    if (!strings_registered) {
        cfw_ui_register_strings(profile_string, profile_indexed);
        strings_registered = 1;
    }
}

static void save_preferences(void)
{
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        slots[active_slot][i] = settings[i];
    file_buffer.magic = 0x51455743;
    file_buffer.version = EQ_VERSION;
    file_buffer.count = EQ_SLOTS;
    file_buffer.active = active_slot;
    for (unsigned int slot = 0; slot < EQ_SLOTS; slot++)
        for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
            file_buffer.slots[slot][i] = slots[slot][i];
    for (unsigned int slot = 0; slot < EQ_SLOTS; slot++)
        for (unsigned int i = 0; i < EQ_NAME_BYTES; i++)
            file_buffer.names[slot][i] = names[slot][i];
    file_buffer.checksum = checksum(&file_buffer, sizeof(file_buffer));
    save_status =
        !cfw_preferences_write(&settings_path, &file_buffer, sizeof(file_buffer));
    if (!save_status)
        dirty = 0;
}

static int import_version(uint32_t version, uint32_t *values)
{
    if (version == 4) {
        struct version4_eq_file legacy;
        if (!cfw_preferences_read(&settings_path, &legacy, sizeof(legacy)) ||
            legacy.checksum != checksum(&legacy, sizeof(legacy)))
            return 0;
        for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
            values[i] = legacy.values[i];
        return valid_values(values);
    }
    if (version == 3) {
        struct version3_eq_file legacy;
        if (!cfw_preferences_read(&settings_path, &legacy, sizeof(legacy)) ||
            legacy.checksum != checksum(&legacy, sizeof(legacy)))
            return 0;
        return convert_bands(values, legacy.values, EQ_V3_BANDS);
    }
    if (version == 1 || version == 2) {
        struct legacy_eq_file legacy;
        if (!cfw_preferences_read(&settings_path, &legacy, sizeof(legacy)) ||
            legacy.checksum != checksum(&legacy, sizeof(legacy)) ||
            (legacy.version == 1 && legacy.values[CFW_EQ_PRECUT] > 20))
            return 0;
        if (!convert_bands(values, legacy.values, EQ_V1_BANDS))
            return 0;
        if (legacy.version == 1)
            values[CFW_EQ_PRECUT] = legacy.values[CFW_EQ_PRECUT] * 5;
        return 1;
    }
    return 0;
}

void cfw_eq_load_preferences(void)
{
    defaults();
    register_strings();
    if (!cfw_preferences_read(&settings_path, &file_buffer, 8) ||
        file_buffer.magic != 0x51455743)
        return;
    uint32_t version = file_buffer.version;
    int upgraded = 0;
    if (version == EQ_VERSION) {
        if (!cfw_preferences_read(&settings_path, &file_buffer,
                                  sizeof(file_buffer)) ||
            file_buffer.count != EQ_SLOTS ||
            file_buffer.active >= file_buffer.count ||
            file_buffer.checksum != checksum(&file_buffer, sizeof(file_buffer)))
            return;
        /* Validate everything first, so a bad later slot cannot leave a half load. */
        for (unsigned int slot = 0; slot < EQ_SLOTS; slot++) {
            if (!valid_name(file_buffer.names[slot]))
                return;
            for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
                if (file_buffer.slots[slot][i] >= cfw_eq_fields[i].count)
                    return;
        }
        for (unsigned int slot = 0; slot < EQ_SLOTS; slot++) {
            for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
                slots[slot][i] = file_buffer.slots[slot][i];
            for (unsigned int i = 0; i < EQ_NAME_BYTES; i++)
                names[slot][i] = file_buffer.names[slot][i];
        }
        active_slot = file_buffer.active;
    } else if (version == 5) {
        struct eq_file_v5 *legacy = &legacy_buffer;
        if (!cfw_preferences_read(&settings_path, legacy, sizeof(*legacy)) ||
            legacy->count != EQ_SLOTS || legacy->active >= legacy->count ||
            legacy->checksum != checksum(legacy, sizeof(*legacy)))
            return;
        for (unsigned int slot = 0; slot < EQ_SLOTS; slot++)
            for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
                if (legacy->slots[slot][i] >= cfw_eq_fields[i].count)
                    return;
        for (unsigned int slot = 0; slot < EQ_SLOTS; slot++)
            for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
                slots[slot][i] = legacy->slots[slot][i];
        active_slot = legacy->active;
        upgraded = 1;
    } else {
        uint32_t imported[EQ_FILE_FIELDS];
        for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
            imported[i] = cfw_eq_fields[i].default_value;
        if (!import_version(version, imported) || !valid_values(imported))
            return;
        for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
            slots[0][i] = imported[i];
        active_slot = 0;
        upgraded = 1;
    }
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        settings[i] = slots[active_slot][i];
    settings[EQ_PROFILE_FIELD] = active_slot;
    if (!cfw_eq_publish(settings))
        return;
    /* Keep the imported tune in memory; the next save writes the current version.
       Writing during startup touches the disk before the OS is ready for it. */
    if (upgraded)
        dirty = 1;
}

static int select_slot(uint32_t slot)
{
    if (slot >= EQ_SLOTS)
        return 0;
    if (!initialized)
        defaults();
    uint32_t previous = active_slot;
    if (slot == previous)
        return 0;
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        slots[previous][i] = settings[i];
    for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
        settings[i] = slots[slot][i];
    active_slot = slot;
    settings[EQ_PROFILE_FIELD] = slot;
    if (!cfw_eq_publish(settings)) {
        active_slot = previous;
        for (unsigned int i = 0; i < EQ_FILE_FIELDS; i++)
            settings[i] = slots[previous][i];
        settings[EQ_PROFILE_FIELD] = previous;
        cfw_eq_publish(settings);
        save_status = 2;
        cfw_ui_status(&cfw_eq_save, save_status);
        return 0;
    }
    dirty = 1;
    save_preferences();
    refresh_fields();
    refresh_profiles();
    cfw_ui_status(&cfw_eq_save, save_status);
    return 1;
}

static void change_value(uint32_t field, uint32_t value)
{
    if (!initialized)
        defaults();
    if (field >= CFW_EQ_FIELDS || value >= cfw_eq_fields[field].count ||
        settings[field] == value)
        return;
    if (field == EQ_PROFILE_FIELD) {
        select_slot(value);
        return;
    }
    uint32_t previous = settings[field];
    settings[field] = value;
    if (cfw_eq_publish(settings)) {
        dirty = 1;
        if (save_status == 2)
            save_status = 0;
        cfw_ui_refresh(&cfw_eq_fields[field]);
    } else {
        settings[field] = previous;
        save_status = 2;
    }
    cfw_ui_status(&cfw_eq_save, save_status);
}

static void commit_values(void)
{
    if (dirty) {
        save_preferences();
        cfw_ui_status(&cfw_eq_save, save_status);
    }
}

const struct cfw_ui_model cfw_eq_model = {settings, change_value, commit_values};

int cfw_eq_action(const char *action, uint32_t argument)
{
    register_strings();
    int open = cfw_string_equal(action, "CFW_EQ_Open");
    int save = cfw_string_equal(action, "CFW_EQ_Save");
    if (!open && !save)
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    if (!initialized)
        defaults();
    if (save)
        save_preferences();
    if (open) {
        refresh_fields();
        refresh_profiles();
    }
    cfw_ui_status(&cfw_eq_save, save_status);
    return 1;
}
