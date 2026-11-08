#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque campaign context (game root + scanned campaign + mod registry). */
typedef struct rl_context rl_context;

typedef struct rl_options {
    const char* mod_id;      /* NULL/"" -> mission default (DefaultMod) */
    int difficulty;          /* 0 easy, 1 medium, 2 hard */
    int credits;             /* -1 = keep the map value */
    int game_speed;          /* spawn.ini GameSpeed */
    int autosave;            /* 0 off, 1=5min, 2=10min, 3=20min */
    int music_volume;        /* -1 keep, 0 off, else percent */
    int play_movies;
    int satellite;
    int ares;
    int phobos;
    int terrain;
    int auto_repair;
    int corr;                /* numeric fix */
    int reverse;             /* two-way unit replacement */
} rl_options;

typedef struct rl_video {
    int width;               /* game resolution == window size (1:1) */
    int height;
    const char* renderer;    /* NULL -> "gdi" */
} rl_video;

typedef struct rl_wine {
    const char* crossover_root; /* NULL -> /Applications/CrossOver.app/... */
    const char* bottle;         /* NULL -> "RA2" */
    const char* lang;           /* NULL -> "zh_CN.GBK" */
    const char* log_dir;        /* NULL -> ~/Library/Logs/ReunionLite */
} rl_wine;

rl_context* rl_create(const char* game_root_utf8);
void rl_destroy(rl_context* ctx);

/* All returned strings are malloc'd UTF-8 JSON; free with rl_free_string. */
char* rl_scan_json(rl_context* ctx, int force_rescan);

/* Compact binary campaign snapshot for fast startup (no JSON on the hot path).
   Format: "RLMB", u32 version, u32 scanMillis, u8 cached, then issues, packs,
   missions.  Free with rl_free_string(). */
unsigned char* rl_scan_binary(rl_context* ctx, int force_rescan, unsigned long* out_size);
char* rl_client_defaults_json(const char* game_root_utf8);
char* rl_drives_json(void);
char* rl_prepare_json(rl_context* ctx, int mission_ordinal, const rl_options* options,
                      const rl_video* video);
char* rl_launch_json(rl_context* ctx, int mission_ordinal, const rl_options* options,
                     const rl_video* video, const rl_wine* wine);
char* rl_sync_json(rl_context* ctx);
void rl_free_string(char* string);

#ifdef __cplusplus
}
#endif
