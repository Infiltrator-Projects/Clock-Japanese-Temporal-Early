// SPDX-License-Identifier: GPL-3.0-or-later
/* Seasonal clock resources. Sole owner of clock-face arithmetic and scheduling. */

#define CLOCK_ID "japanese-temporal-early"
#define CLOCK_SECONDS false
#define CLOCK_LOCATION true
#include <infiltratr/temporal_clock_resource.h>
#include "temporal_solar_compact.h"

#include "infiltratr/arithmetic.h"
#include "infiltratr/core.h"
#include "infiltratr/i18n.h"
#include "infiltratr/timing.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MICROSECONDS_PER_SECOND INT64_C(1000000)
#define SECONDS_PER_MINUTE INT64_C(60)
#define SECONDS_PER_HOUR INT64_C(3600)
#define SECONDS_PER_DAY INT64_C(86400)
#define MICROSECONDS_PER_DAY (SECONDS_PER_DAY * MICROSECONDS_PER_SECOND)
#define INFILTRATR_PI 3.141592653589793238462643383279502884L

static bool write_text(char *buffer, size_t capacity, size_t *length,
                       const char *text)
{
    size_t needed;

    if (buffer == NULL || capacity == 0U || text == NULL) {
        return false;
    }
    needed = strlen(text);
    if (needed >= capacity) {
        return false;
    }
    memcpy(buffer, text, needed + 1U);
    if (length != NULL) {
        *length = needed;
    }
    return true;
}

static bool finish_output(char *buffer,size_t capacity,size_t *length,int written)
{
    if (written < 0 || (size_t)written >= capacity) {
        if (buffer != NULL && capacity != 0U) buffer[0]='\0';
        return false;
    }
    if (length != NULL) *length=(size_t)written;
    return true;
}

static int64_t positive_modulo(int64_t value, int64_t modulus)
{
    int64_t remainder = 0;

    if (!infiltratr_i64_floor_divmod(value, modulus, NULL, &remainder)) {
        return 0;
    }
    return remainder;
}

static long double mean_solar_seconds(int64_t unix_microseconds,
                                      double longitude)
{
    const int64_t utc_microseconds =
        positive_modulo(unix_microseconds, MICROSECONDS_PER_DAY);
    long double seconds =
        (long double)utc_microseconds / MICROSECONDS_PER_SECOND +
        (long double)temporal_solar_clamp_longitude(longitude) * 240.0L;

    seconds = fmodl(seconds, (long double)SECONDS_PER_DAY);
    if (seconds < 0.0L) {
        seconds += (long double)SECONDS_PER_DAY;
    }
    return seconds;
}

static long double apparent_solar_seconds(int64_t unix_microseconds,
                                          double longitude)
{
    double equation_minutes = 0.0;
    long double seconds;

    if (!temporal_solar_compact_terms(unix_microseconds, &equation_minutes, NULL)) {
        return mean_solar_seconds(unix_microseconds, longitude);
    }
    seconds = mean_solar_seconds(unix_microseconds, longitude) +
              (long double)equation_minutes * 60.0L;
    seconds = fmodl(seconds, (long double)SECONDS_PER_DAY);
    if (seconds < 0.0L) {
        seconds += (long double)SECONDS_PER_DAY;
    }
    return seconds;
}

static bool solar_day_boundaries(int64_t unix_microseconds,
                                 double latitude,
                                 double solar_depression_degrees,
                                 long double *dawn_seconds,
                                 long double *dusk_seconds)
{
    double declination = 0.0;
    long double hour_angle;
    long double half_day_seconds;

    if (dawn_seconds == NULL || dusk_seconds == NULL ||
        !isfinite(solar_depression_degrees) ||
        solar_depression_degrees < 0.0 ||
        solar_depression_degrees >= 90.0 ||
        !temporal_solar_compact_terms(unix_microseconds, NULL, &declination)) {
        return false;
    }

    if (!temporal_solar_hour_angle(latitude, declination,
                                   solar_depression_degrees, &hour_angle)) {
        return false;
    }
    half_day_seconds =
        hour_angle * (long double)SECONDS_PER_DAY / (2.0L * INFILTRATR_PI);
    /* Pre-Kansei dawn/dusk: 36 minutes before sunrise/after sunset.
     * NAOJ: https://eco.mtk.nao.ac.jp/koyomi/wiki/ (夜明と日暮).
     * .833 degrees defines sunrise/sunset; it is not itself dawn/dusk. */
    *dawn_seconds = (long double)SECONDS_PER_DAY / 2.0L -
                    half_day_seconds - 2160.0L;
    *dusk_seconds = (long double)SECONDS_PER_DAY / 2.0L +
                    half_day_seconds + 2160.0L;
    if (*dawn_seconds <= 0.0L || *dusk_seconds >= SECONDS_PER_DAY)
        return false;
    return true;
}

typedef struct SeasonalPeriod {
    bool daylight;
    unsigned index;
    long double fraction;
} SeasonalPeriod;

static bool seasonal_period_at(int64_t unix_microseconds,
                               double latitude,
                               double longitude,
                               double solar_depression_degrees,
                               unsigned daylight_parts,
                               unsigned night_parts,
                               SeasonalPeriod *period)
{
    long double dawn;
    long double dusk;
    const long double solar =
        apparent_solar_seconds(unix_microseconds, longitude);
    long double span;
    long double position;
    long double unit;
    unsigned parts;

    if (period == NULL || daylight_parts == 0U || night_parts == 0U ||
        !solar_day_boundaries(unix_microseconds, latitude,
                              solar_depression_degrees, &dawn, &dusk)) {
        return false;
    }

    if (solar >= dawn && solar < dusk) {
        period->daylight = true;
        span = dusk - dawn;
        position = solar - dawn;
        parts = daylight_parts;
    } else {
        period->daylight = false;
        span = (long double)SECONDS_PER_DAY - (dusk - dawn);
        position = solar >= dusk
            ? solar - dusk
            : (long double)SECONDS_PER_DAY - dusk + solar;
        parts = night_parts;
    }

    unit = span / parts;
    {
        long double scaled = position / unit;

        if (scaled < 0.0L) {
            scaled = 0.0L;
        }
        if (scaled >= (long double)parts) {
            scaled = nextafterl((long double)parts, 0.0L);
        }
        period->index = (unsigned)floorl(scaled);
        period->fraction = scaled - (long double)period->index;
    }
    return true;
}


static bool mode_location_valid(const InfiltratrTemporalClockModeInfo *info,
                                bool location_configured,
                                double latitude,
                                double longitude)
{
    if (info == NULL) {
        return false;
    }
    if ((info->requires_latitude || info->requires_longitude) &&
        !location_configured) {
        return false;
    }
    return (!info->requires_latitude ||
            (isfinite(latitude) && latitude >= -90.0 && latitude <= 90.0)) &&
           (!info->requires_longitude ||
            (isfinite(longitude) && longitude >= -180.0 &&
             longitude <= 180.0));
}


/* Names are transliterated proper names, not animal/help labels. Wording
 * belongs to the selected language; the shared phase calculation stays below. */
#define CLOCK_REMAINING_WORDS(hour, watch, twelfth, twelfths) \
    { "hour", hour }, { "watch", watch }, \
    { "twelfth", twelfth }, { "twelfths", twelfths }
#define JAPANESE_BRANCHES \
    { "jp-ne", "Ne" }, { "jp-ushi", "Ushi" }, { "jp-tora", "Tora" }, \
    { "jp-u", "U" }, { "jp-tatsu", "Tatsu" }, { "jp-mi", "Mi" }, \
    { "jp-uma", "Uma" }, { "jp-hitsuji", "Hitsuji" }, { "jp-saru", "Saru" }, \
    { "jp-tori", "Tori" }, { "jp-inu", "Inu" }, { "jp-i", "I" }
#define JAPANESE_NATIVE_BRANCHES \
    { "jp-ne", "子" }, { "jp-ushi", "丑" }, { "jp-tora", "寅" }, \
    { "jp-u", "卯" }, { "jp-tatsu", "辰" }, { "jp-mi", "巳" }, \
    { "jp-uma", "午" }, { "jp-hitsuji", "未" }, { "jp-saru", "申" }, \
    { "jp-tori", "酉" }, { "jp-inu", "戌" }, { "jp-i", "亥" }
#define EDO_WORDS(horizontal, vertical) \
    { "edo", horizontal }, { "edo-vertical", vertical }
static const InfiltratrI18nEntry clock_en[] = {

    JAPANESE_BRANCHES,
    EDO_WORDS("{branch} · {bell} bells", "{branch}\n{bell} bells"),
};
static const InfiltratrI18nEntry clock_de[] = {

    EDO_WORDS("{branch} · {bell} Schläge", "{branch}\n{bell} Schläge"),
};
static const InfiltratrI18nEntry clock_es[] = {

    EDO_WORDS("{branch} · {bell} campanadas", "{branch}\n{bell} campanadas"),
};
static const InfiltratrI18nEntry clock_fr[] = {

    EDO_WORDS("{branch} · {bell} coups", "{branch}\n{bell} coups"),
};
static const InfiltratrI18nEntry clock_it[] = {

    EDO_WORDS("{branch} · {bell} rintocchi", "{branch}\n{bell} rintocchi"),
};
static const InfiltratrI18nEntry clock_pt[] = {

    EDO_WORDS("{branch} · {bell} badaladas", "{branch}\n{bell} badaladas"),
};
static const InfiltratrI18nEntry clock_ru[] = {

    EDO_WORDS("{branch} · {bell} ударов", "{branch}\n{bell} ударов"),
};
static const InfiltratrI18nEntry clock_ja[] = {

    JAPANESE_NATIVE_BRANCHES,
    EDO_WORDS("{branch}の刻 · {bell}つ", "{branch}の刻\n{bell}つ"),
};
static const InfiltratrI18nEntry clock_zh[] = {

    JAPANESE_NATIVE_BRANCHES,
    EDO_WORDS("{branch}刻 · {bell}声", "{branch}刻\n{bell}声"),
};
static const InfiltratrI18nEntry clock_zh_hant[] = {

    JAPANESE_NATIVE_BRANCHES,
    EDO_WORDS("{branch}刻 · {bell}聲", "{branch}刻\n{bell}聲"),
};
static const InfiltratrI18nEntry clock_ko[] = {
    /* Other specialized-clock wording keeps the documented English fallback. */
    EDO_WORDS("{branch} · {bell}타", "{branch}\n{bell}타"),
};
#undef CLOCK_REMAINING_WORDS
#undef JAPANESE_BRANCHES
#undef JAPANESE_NATIVE_BRANCHES
#undef EDO_WORDS

#define CLOCK_CATALOG(locale, entries) \
    { locale, entries, sizeof(entries) / sizeof((entries)[0]) }
static const InfiltratrI18nCatalog clock_catalogs[] = {
    CLOCK_CATALOG("en", clock_en), CLOCK_CATALOG("de", clock_de),
    CLOCK_CATALOG("es", clock_es), CLOCK_CATALOG("fr", clock_fr),
    CLOCK_CATALOG("it", clock_it), CLOCK_CATALOG("pt", clock_pt),
    CLOCK_CATALOG("ru", clock_ru), CLOCK_CATALOG("ja", clock_ja),
    CLOCK_CATALOG("ko", clock_ko),
    CLOCK_CATALOG("zh", clock_zh), CLOCK_CATALOG("zh-TW", clock_zh_hant),
    CLOCK_CATALOG("zh-HK", clock_zh_hant), CLOCK_CATALOG("zh-MO", clock_zh_hant),
    CLOCK_CATALOG("zh-Hant", clock_zh_hant),
    CLOCK_CATALOG("zh-Hant-TW", clock_zh_hant),
    CLOCK_CATALOG("zh-Hant-HK", clock_zh_hant)
};
#undef CLOCK_CATALOG

static bool write_localized(char *buffer, size_t capacity, size_t *length,
                            const char *pattern,
                            const InfiltratrI18nArgument *arguments,
                            size_t argument_count)
{
    const size_t needed = infiltratr_i18n_format(
        buffer, capacity, pattern, arguments, argument_count);
    if (needed >= capacity) {
        buffer[0] = '\0';
        return false;
    }
    if (length != NULL) *length = needed;
    return true;
}

static bool format_clock_mode(const InfiltratrI18n *language,
                                           const char *mode,
                                           int64_t unix_microseconds,
                                           int32_t utc_offset_seconds,
                                           bool show_seconds,
                                           bool vertical,
                                           bool location_configured,
                                           double latitude,
                                           double longitude,
                                           char *buffer,
                                           size_t capacity,
                                           size_t *length)
{
    (void)mode; (void)show_seconds;
    const InfiltratrTemporalClockModeInfo *info;

    // The extension modes use absolute instants and, when applicable, the
    // selected solar location. Civil UTC offsets remain part of the stable
    // caller contract, but do not affect this custom-mode renderer.
    (void)utc_offset_seconds;
    if (buffer == NULL || capacity == 0U || mode == NULL) {
        return false;
    }
    buffer[0] = '\0';

    const InfiltratrTemporalClockModeInfo selected = {
        CLOCK_ID, INFILTRATR_TEMPORAL_DISPLAY_NAME, CLOCK_SECONDS, CLOCK_LOCATION, CLOCK_LOCATION
    };
    info = strcmp(mode, CLOCK_ID) == 0 ? &selected : NULL;
    if (info == NULL ||
        !mode_location_valid(
            info, location_configured, latitude, longitude)) {
        return false;
    }

    ;

    {
        typedef struct JapaneseToki {
            const char *character;
            const char *bell;
            const char *key;
        } JapaneseToki;
        static const JapaneseToki day_toki[] = {
            { "卯", "六", "jp-u" }, { "辰", "五", "jp-tatsu" },
            { "巳", "四", "jp-mi" }, { "午", "九", "jp-uma" },
            { "未", "八", "jp-hitsuji" }, { "申", "七", "jp-saru" }
        };
        static const JapaneseToki night_toki[] = {
            { "酉", "六", "jp-tori" }, { "戌", "五", "jp-inu" },
            { "亥", "四", "jp-i" }, { "子", "九", "jp-ne" },
            { "丑", "八", "jp-ushi" }, { "寅", "七", "jp-tora" }
        };
        const double depression =
            0.833;
        SeasonalPeriod period;
        const JapaneseToki *toki;

        if (!seasonal_period_at(
                unix_microseconds, latitude, longitude,
                depression, 6U, 6U, &period)) {
            return write_text(
                buffer, capacity, length,
                vertical ? "N/A\n和時" : "N/A 和時");
        }
        toki = period.daylight
            ? &day_toki[period.index]
            : &night_toki[period.index];
        if (language != NULL) {
            const InfiltratrI18nArgument arguments[] = {
                { "branch", infiltratr_i18n_get(language, toki->key) },
                { "bell", toki->bell }
            };
            return write_localized(buffer, capacity, length,
                infiltratr_i18n_get(language, vertical ? "edo-vertical" : "edo"),
                arguments, sizeof(arguments) / sizeof(arguments[0]));
        }
        return finish_output(buffer, capacity, length,
            snprintf(buffer, capacity, vertical ? "%sの刻\n%sつ" : "%sの刻 · %sつ", toki->character, toki->bell));
    }

    ;

    ;

    return false;
}

static bool seasonal_format_clock_mode(const char *mode,
    int64_t unix_microseconds, int32_t utc_offset_seconds,
    bool show_seconds, bool vertical, bool location_configured,
    double latitude, double longitude,
    char *buffer, size_t capacity, size_t *length)
{
    (void)mode;
    return format_clock_mode(NULL, mode, unix_microseconds, utc_offset_seconds,
        show_seconds, vertical, location_configured, latitude, longitude,
        buffer, capacity, length);
}

static bool seasonal_format_clock_mode_localized(
    const char *locale, const char *mode,
    int64_t unix_microseconds, int32_t utc_offset_seconds,
    bool show_seconds, bool vertical, bool location_configured,
    double latitude, double longitude,
    char *buffer, size_t capacity, size_t *length)
{
    (void)mode;
    InfiltratrI18n language;
    if (!infiltratr_i18n_init(&language, clock_catalogs,
            sizeof(clock_catalogs) / sizeof(clock_catalogs[0]), "en")) {
        return false;
    }
    if (locale != NULL) (void)infiltratr_i18n_set_locale(&language, locale);
    return format_clock_mode(&language, mode, unix_microseconds, utc_offset_seconds,
        show_seconds, vertical, location_configured, latitude, longitude,
        buffer, capacity, length);
}

static bool render(const InfiltratrTemporalClockProvider *self,
    const char *locale, int64_t instant, int32_t offset, bool seconds,
    bool vertical, double latitude, double longitude,
    char *buffer, size_t capacity, size_t *length)
{
    (void)self;
    if (locale == NULL)
        return seasonal_format_clock_mode(CLOCK_ID, instant, offset, seconds,
            vertical, true, latitude, longitude, buffer, capacity, length);
    return seasonal_format_clock_mode_localized(locale, CLOCK_ID, instant,
        offset, seconds, vertical, true, latitude, longitude,
        buffer, capacity, length);
}

/* Bracket and refine the first visible boundary against this very renderer.
 * Rendering and scheduling cannot disagree over moving seasonal boundaries,
 * Wendetage or floating-point quantisation. This is a bounded one-shot search.
 */
static bool next_tick(const InfiltratrTemporalClockProvider *self,
    int64_t instant, int32_t offset, bool seconds,
    double latitude, double longitude, uint32_t *milliseconds)
{
    char initial[256], future[256];
    uint32_t low = 0U, high = 1U, step = 1U;
    const uint32_t maximum = 129600000U;
    (void)self;
    if (milliseconds == NULL || !render(NULL, NULL, instant, offset, seconds,
            false, latitude, longitude, initial, sizeof(initial), NULL))
        return false;
    /* Solar events are undefined in polar N/A periods. Retry without
     * searching 36 hours of identical unavailable clock faces. */
    if (strncmp(initial, "N/A", 3U) == 0) {
        *milliseconds = 60000U;
        return true;
    }
    for (;;) {
        const int64_t delta = (int64_t)high * 1000;
        if (instant > INT64_MAX - delta ||
            !render(NULL, NULL, instant + delta, offset, seconds, false,
                latitude, longitude, future, sizeof(future), NULL))
            return false;
        if (strcmp(initial, future) != 0) break;
        if (high == maximum)
            return false; /* A valid clock must actually change. */
        low = high;
        if (step < 60000U) step = step > 30000U ? 60000U : step * 2U;
        high = maximum - high < step ? maximum : high + step;
    }
    while (high - low > 1U) {
        const uint32_t middle = low + (high - low) / 2U;
        if (!render(NULL, NULL, instant + (int64_t)middle * 1000, offset,
                seconds, false, latitude, longitude, future, sizeof(future), NULL))
            return false;
        if (strcmp(initial, future) == 0) low = middle;
        else high = middle;
    }
    *milliseconds = high;
    return true;
}

static const InfiltratrTemporalResourceMetadata metadata = {
    sizeof(InfiltratrTemporalResourceMetadata),
    INFILTRATR_TEMPORAL_RESOURCE_METADATA_ABI,
    INFILTRATR_TEMPORAL_RESOURCE_CLOCK, CLOCK_ID, INFILTRATR_TEMPORAL_DISPLAY_NAME,
    CLOCK_SECONDS, CLOCK_LOCATION, CLOCK_LOCATION
};
static const InfiltratrTemporalClockProvider provider = {
    sizeof(InfiltratrTemporalClockProvider),
    INFILTRATR_TEMPORAL_CLOCK_PROVIDER_ABI, CLOCK_ID, render, next_tick
};
#if defined(_WIN32)
#define RESOURCE_EXPORT __declspec(dllexport)
#else
#define RESOURCE_EXPORT __attribute__((visibility("default")))
#endif
RESOURCE_EXPORT const InfiltratrTemporalResourceMetadata *
infiltratr_temporal_resource_metadata_v1(void);
RESOURCE_EXPORT const InfiltratrTemporalClockProvider *
infiltratr_temporal_clock_provider_v1(void);
RESOURCE_EXPORT const InfiltratrTemporalResourceMetadata *
infiltratr_temporal_resource_metadata_v1(void) { return &metadata; }
RESOURCE_EXPORT const InfiltratrTemporalClockProvider *
infiltratr_temporal_clock_provider_v1(void) { return &provider; }
