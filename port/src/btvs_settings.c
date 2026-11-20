/* btvs_settings - the port's settings store. See btvs_settings.h. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "btvs_settings.h"

#define NAME_MAX_ 48

typedef struct {
    char  section[NAME_MAX_];
    char  key[NAME_MAX_];
    char *value;        /* what getters return (never freed: see btvs_setting_str) */
    char *file_value;   /* what the file holds/should hold when `env` is set, or NULL */
    int   env;          /* value came from BTVS_<SECTION>_<KEY>; not saved */
} entry_t;

typedef struct {
    char section[NAME_MAX_];   /* "" = any */
    char key[NAME_MAX_];
    btvs_setting_cb fn;
    void *user;
} watch_t;

static CRITICAL_SECTION g_lock;
static volatile LONG    g_lock_state;      /* 0 none, 1 initialising, 2 ready */
static int      g_loaded;
static int      g_dirty;
static entry_t *g_ent;
static int      g_ent_n, g_ent_cap;
static watch_t  g_watch[64];
static int      g_watch_n;
static char     g_path[MAX_PATH];

static void lock(void)
{
    if (g_lock_state != 2) {
        if (InterlockedCompareExchange(&g_lock_state, 1, 0) == 0) {
            InitializeCriticalSection(&g_lock);
            InterlockedExchange(&g_lock_state, 2);
        } else {
            while (g_lock_state != 2) Sleep(0);
        }
    }
    EnterCriticalSection(&g_lock);
}
static void unlock(void) { LeaveCriticalSection(&g_lock); }

static char *dupstr(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    *e = '\0';
    return s;
}

static entry_t *find(const char *section, const char *key)
{
    int i;
    for (i = 0; i < g_ent_n; i++)
        if (_stricmp(g_ent[i].section, section) == 0 && _stricmp(g_ent[i].key, key) == 0)
            return &g_ent[i];
    return NULL;
}

static entry_t *add(const char *section, const char *key)
{
    entry_t *e;
    if (g_ent_n == g_ent_cap) {
        int cap = g_ent_cap ? g_ent_cap * 2 : 64;
        entry_t *p = (entry_t *)realloc(g_ent, cap * sizeof *p);
        if (!p) return NULL;
        g_ent = p; g_ent_cap = cap;
    }
    e = &g_ent[g_ent_n++];
    memset(e, 0, sizeof *e);
    strncpy_s(e->section, NAME_MAX_, section, _TRUNCATE);
    strncpy_s(e->key, NAME_MAX_, key, _TRUNCATE);
    return e;
}

/* BTVS_<SECTION>_<KEY>, or NULL. */
static const char *env_override(const char *section, const char *key)
{
    char name[8 + 2 * NAME_MAX_];
    char *p;
    const char *v;
    sprintf_s(name, sizeof name, "BTVS_%s_%s", section, key);
    for (p = name; *p; p++) *p = (char)toupper((unsigned char)*p);
    v = getenv(name);
    return (v && *v) ? v : NULL;
}

static void save_at_exit(void) { btvs_settings_save(); }

static void load_locked(void)
{
    const char *over = getenv("BTVS_SETTINGS_INI");
    char line[1024], section[NAME_MAX_] = "";
    FILE *f;

    g_loaded = 1;
    if (over && *over) {
        strncpy_s(g_path, MAX_PATH, over, _TRUNCATE);
    } else {
        char *slash;
        GetModuleFileNameA(NULL, g_path, MAX_PATH);
        slash = strrchr(g_path, '\\');
        if (slash) slash[1] = '\0'; else g_path[0] = '\0';
        strcat_s(g_path, MAX_PATH, "btvs_settings.ini");
    }
    atexit(save_at_exit);

    if (fopen_s(&f, g_path, "r") != 0 || !f) return;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line), *eq;
        entry_t *e;
        if (*s == '\0' || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) { *end = '\0'; strncpy_s(section, NAME_MAX_, trim(s + 1), _TRUNCATE); }
            continue;
        }
        eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        s = trim(s);
        e = find(section, s);
        if (!e) e = add(section, s);
        if (e) e->value = dupstr(trim(eq + 1));
    }
    fclose(f);
}

/* The entry for (section, key), created from the environment, the file or
 * `def` in that order. Lock held. */
static entry_t *get_locked(const char *section, const char *key, const char *def)
{
    entry_t *e;
    const char *ev;
    if (!g_loaded) load_locked();
    e = find(section, key);
    if (e && (e->env || e->file_value == e->value)) return e;   /* already resolved */
    ev = env_override(section, key);
    if (!e) {
        e = add(section, key);
        if (!e) return NULL;
        if (!ev) { e->value = dupstr(def); g_dirty = 1; }
    }
    if (ev) {
        e->file_value = e->value;           /* may be NULL: nothing to save */
        e->value = dupstr(ev);
        e->env = 1;
    } else {
        e->file_value = e->value;           /* marks "resolved" */
    }
    return e;
}

const char *btvs_setting_str(const char *section, const char *key, const char *def)
{
    entry_t *e;
    const char *v;
    lock();
    e = get_locked(section, key, def ? def : "");
    v = (e && e->value) ? e->value : (def ? def : "");
    unlock();
    return v;
}

int btvs_setting_int(const char *section, const char *key, int def)
{
    char d[32], *end;
    const char *v;
    long n;
    sprintf_s(d, sizeof d, "%d", def);
    v = btvs_setting_str(section, key, d);
    n = strtol(v, &end, 0);
    if (end == v) {
        if (!_stricmp(v, "true") || !_stricmp(v, "on") || !_stricmp(v, "yes")) return 1;
        if (!_stricmp(v, "false") || !_stricmp(v, "off") || !_stricmp(v, "no")) return 0;
        return def;
    }
    return (int)n;
}

float btvs_setting_float(const char *section, const char *key, float def)
{
    char d[48], *end;
    const char *v;
    double n;
    sprintf_s(d, sizeof d, "%g", def);
    v = btvs_setting_str(section, key, d);
    n = strtod(v, &end);
    return end == v ? def : (float)n;
}

void btvs_setting_set_str(const char *section, const char *key, const char *value)
{
    entry_t *e;
    watch_t call[64];
    int n = 0, i, existed;
    char sec[NAME_MAX_], k[NAME_MAX_];

    if (!value) value = "";
    lock();
    if (!g_loaded) load_locked();
    existed = find(section, key) != NULL;
    e = get_locked(section, key, value);
    if (!e || (existed && e->value && !e->env && strcmp(e->value, value) == 0)) { unlock(); return; }
    if (!(e->value && strcmp(e->value, value) == 0))
        e->value = dupstr(value);           /* the old string is left alive on purpose */
    e->env = 0;
    e->file_value = e->value;
    g_dirty = 1;
    strcpy_s(sec, NAME_MAX_, e->section);
    strcpy_s(k, NAME_MAX_, e->key);
    for (i = 0; i < g_watch_n; i++)
        if ((!g_watch[i].section[0] || _stricmp(g_watch[i].section, sec) == 0) &&
            (!g_watch[i].key[0] || _stricmp(g_watch[i].key, k) == 0))
            call[n++] = g_watch[i];
    unlock();
    for (i = 0; i < n; i++) call[i].fn(sec, k, call[i].user);
}

void btvs_setting_set_int(const char *section, const char *key, int value)
{
    char s[32];
    sprintf_s(s, sizeof s, "%d", value);
    btvs_setting_set_str(section, key, s);
}

void btvs_setting_set_float(const char *section, const char *key, float value)
{
    char s[48];
    sprintf_s(s, sizeof s, "%g", value);
    btvs_setting_set_str(section, key, s);
}

void btvs_setting_on_change(const char *section, const char *key,
                            btvs_setting_cb fn, void *user)
{
    if (!fn) return;
    lock();
    if (g_watch_n < (int)(sizeof g_watch / sizeof g_watch[0])) {
        watch_t *w = &g_watch[g_watch_n++];
        strncpy_s(w->section, NAME_MAX_, section ? section : "", _TRUNCATE);
        strncpy_s(w->key, NAME_MAX_, key ? key : "", _TRUNCATE);
        w->fn = fn; w->user = user;
    }
    unlock();
}

static int by_section(const void *a, const void *b)
{
    const entry_t *x = *(const entry_t *const *)a, *y = *(const entry_t *const *)b;
    int c = _stricmp(x->section, y->section);
    return c ? c : (x < y ? -1 : x > y);    /* keys keep the order they were added in */
}

int btvs_settings_save(void)
{
    char tmp[MAX_PATH + 8];
    const entry_t **order;
    const char *section = NULL;
    FILE *f;
    int i, ok = 0;

    lock();
    if (!g_loaded) load_locked();
    if (!g_dirty) { unlock(); return 1; }
    order = (const entry_t **)malloc((g_ent_n + 1) * sizeof *order);
    sprintf_s(tmp, sizeof tmp, "%s.tmp", g_path);
    if (order && fopen_s(&f, tmp, "w") == 0 && f) {
        for (i = 0; i < g_ent_n; i++) order[i] = &g_ent[i];
        qsort((void *)order, g_ent_n, sizeof *order, by_section);
        fputs("; Buffy the Vampire Slayer - PC settings. Change these in the game's Options menu.\n", f);
        for (i = 0; i < g_ent_n; i++) {
            const entry_t *e = order[i];
            const char *v = e->env ? e->file_value : e->value;
            if (!v) continue;
            if (!section || _stricmp(section, e->section) != 0) {
                section = e->section;
                fprintf(f, "\n[%s]\n", section);
            }
            fprintf(f, "%s=%s\n", e->key, v);
        }
        ok = (fclose(f) == 0) &&
             MoveFileExA(tmp, g_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (ok) g_dirty = 0;
    }
    free((void *)order);
    unlock();
    return ok;
}

const char *btvs_settings_path(void)
{
    lock();
    if (!g_loaded) load_locked();
    unlock();
    return g_path;
}
