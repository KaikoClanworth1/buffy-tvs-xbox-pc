/* BTVS_REPLAY=<report folder or .zip>: go back to where a report was made.
 *
 *  1. before anything reads them, the report's settings and save are put in
 *     place: BTVS_SETTINGS_INI -> the report's settings.ini, BTVS_SAVE_DIR -> a
 *     fresh copy of the report's save\ under <exe>\replay\ (the player's own
 *     saves and settings are never touched);
 *  2. if the report was made in a level, the game is started with the
 *     report's launch data (launch.bin: what the game handed itself when it
 *     restarted to load that level), so it boots straight into the level;
 *  3. when the trail reports that anchor, the report's input recording is
 *     played from there (Controls' btvs_input_playback), and when it ends the
 *     pads are the player's again: that is the moment the report was made.
 *
 * docs/logs.md says how exact this is.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btvs_log.h"

/* (the level jump through the game's own functions is gone: launch data does it)
 * 1 = on its way, 0 = can't (yet), -1 = not possible at this moment, ask again. */

static struct {
    int active, arrived, playing, done, goto_state, said_manual;
    char folder[MAX_PATH], input[MAX_PATH];
    char anchor[16], anchor_name[96], level[96], checkpoint[96];
    double report_time, anchor_time, arrived_at;
} s_r;

static void delete_tree(const char *dir)
{
    char pat[MAX_PATH], a[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    sprintf_s(pat, MAX_PATH, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")
                || _snprintf_s(a, MAX_PATH, _TRUNCATE, "%s\\%s", dir, fd.cFileName) < 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            delete_tree(a);
        else
            DeleteFileA(a);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    RemoveDirectoryA(dir);
}

static int is_dir(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static int is_file(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* The game keeps the running session (default.ses) on its cache drive, beside
 * its files from the disc: of that folder only the session files travel. */
static int copy_sessions(const char *from_save, const char *to_save)
{
    char pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;
    sprintf_s(pat, MAX_PATH, "%s\Cache\*.ses", from_save);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CreateDirectoryA(to_save, NULL);
    sprintf_s(b, MAX_PATH, "%s\Cache", to_save);
    CreateDirectoryA(b, NULL);
    do {
        sprintf_s(a, MAX_PATH, "%s\Cache\%s", from_save, fd.cFileName);
        sprintf_s(b, MAX_PATH, "%s\Cache\%s", to_save, fd.cFileName);
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && CopyFileA(a, b, FALSE))
            n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

int btvs_replay_active(void) { return s_r.active; }

void btvs_replay_init(void)
{
    const char *src = getenv("BTVS_REPLAY");
    char path[MAX_PATH], base[MAX_PATH], line[400], *p;
    size_t len;
    int partial = 0, later = btvs_restarts() > 0;   /* later: the replayed game restarted itself; only the folders still apply */
    char launch[MAX_PATH] = "";
    FILE *f;

    if (!src || !*src)
        return;
    strcpy_s(path, MAX_PATH, src);
    len = strlen(path);
    while (len && (path[len - 1] == '\\' || path[len - 1] == '/' || path[len - 1] == '"'))
        path[--len] = 0;

    sprintf_s(base, MAX_PATH, "%s\\replay", btvs_exe_dir());
    CreateDirectoryA(base, NULL);
    if (len > 4 && !_stricmp(path + len - 4, ".zip")) {
        const char *name = strrchr(path, '\\');
        const char *name2 = strrchr(path, '/');
        int n;
        if (name2 > name)
            name = name2;
        name = name ? name + 1 : path;
        sprintf_s(s_r.folder, MAX_PATH, "%s\\%.*s", base, (int)strlen(name) - 4, name);
        if (!later)
            delete_tree(s_r.folder);
        n = later ? 1 : btvs_unzip(path, s_r.folder);
        if (n <= 0) {
            btvs_log("replay", "BTVS_REPLAY: can't unpack %s (%s). Unpack it yourself and give the folder.", path,
                     n == -2 ? "it was re-packed with compression" : "not found or not a zip");
            return;
        }
        if (!later)
            btvs_log("replay", "unpacked %s (%d files) to %s", path, n, s_r.folder);
    } else if (is_dir(path)) {
        strcpy_s(s_r.folder, MAX_PATH, path);
    } else {
        btvs_log("replay", "BTVS_REPLAY: %s is not a report folder or zip", path);
        return;
    }

    sprintf_s(path, MAX_PATH, "%s\\replay.txt", s_r.folder);
    if (fopen_s(&f, path, "r") != 0 || !f) {
        btvs_log("replay", "BTVS_REPLAY: no replay.txt in %s", s_r.folder);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        p = strchr(line, '=');
        if (!p)
            continue;
        *p++ = 0;
        if (!strcmp(line, "anchor")) strcpy_s(s_r.anchor, sizeof s_r.anchor, p);
        else if (!strcmp(line, "anchor_name")) strcpy_s(s_r.anchor_name, sizeof s_r.anchor_name, p);
        else if (!strcmp(line, "level")) strcpy_s(s_r.level, sizeof s_r.level, p);
        else if (!strcmp(line, "checkpoint")) strcpy_s(s_r.checkpoint, sizeof s_r.checkpoint, p);
        else if (!strcmp(line, "report_time")) s_r.report_time = atof(p);
        else if (!strcmp(line, "anchor_time")) s_r.anchor_time = atof(p);
        else if (!strcmp(line, "input") && *p) sprintf_s(s_r.input, MAX_PATH, "%s\\%s", s_r.folder, p);
        else if (!strcmp(line, "input_complete") && atoi(p) == 0) partial = 1;
        else if (!strcmp(line, "launch") && *p) sprintf_s(launch, MAX_PATH, "%s\\%s", s_r.folder, p);
    }
    fclose(f);
    /* (reports of the first format could name a level as the anchor; every replay starts at boot now) */
    strcpy_s(s_r.anchor, sizeof s_r.anchor, "boot");
    if (s_r.input[0] && !is_file(s_r.input))
        s_r.input[0] = 0;
    if (s_r.input[0] && partial) {
        btvs_log("replay", "the report's input recording does not begin at the anchor (an earlier report cut it): not played");
        s_r.input[0] = 0;
    }

    /* The report's settings and a throw-away copy of its save. */
    sprintf_s(path, MAX_PATH, "%s\\settings.ini", s_r.folder);
    if (is_file(path))
        _putenv_s("BTVS_SETTINGS_INI", path);
    sprintf_s(path, MAX_PATH, "%s\\save", s_r.folder);
    sprintf_s(base, MAX_PATH, "%s\\replay\\save_run", btvs_exe_dir());
    /* (the game's cache drive in there is kept from one replay to the next:
     * it is the same for every report and takes a while to install) */
    CreateDirectoryA(base, NULL);
    {
        static const char *const part[4] = { "UserData", "TitleData", "HardDisk\\UDATA", "HardDisk\\TDATA" };
        char from[MAX_PATH], to[MAX_PATH];
        int i;
        sprintf_s(to, MAX_PATH, "%s\\HardDisk", base);
        CreateDirectoryA(to, NULL);
        for (i = 0; i < 4 && !later; i++) {
            sprintf_s(from, MAX_PATH, "%s\\%s", path, part[i]);
            sprintf_s(to, MAX_PATH, "%s\\%s", base, part[i]);
            delete_tree(to);
            if (is_dir(from))
                btvs_copy_tree(from, to, ~0ull, NULL);
        }
    }
    if (!later)
        copy_sessions(path, base);
    _putenv_s("BTVS_SAVE_DIR", base);
    if (later) {
        btvs_log("replay", "the replayed game restarted itself: playing on from here (settings and saves still the report's)");
        return;
    }
    /* The game was in a level: start it the way it started itself then. */
    if (launch[0] && is_file(launch))
        _putenv_s("RECOMP_LAUNCH_DATA_FILE", launch);
    else
        launch[0] = 0;

    s_r.active = 1;
    btvs_log("replay", "replaying report %s", s_r.folder);
    btvs_log("replay", "  start: %s (level \"%s\"), then %.1f s of recorded input (%s)",
             launch[0] ? "with the report's launch data, straight into the level" : "a normal start (title screen)",
             s_r.level, s_r.report_time - s_r.anchor_time,
             s_r.input[0] ? "input.rec" : "NONE in the report");
    btvs_log("replay", "  settings: %s; saves: %s", getenv("BTVS_SETTINGS_INI") ? getenv("BTVS_SETTINGS_INI") : "(defaults)", base);
}

/* The trail saw an anchor. Ours? Then the recording plays from here. */
void btvs_replay_on_anchor(const char *event, const char *name)
{
    if (!s_r.active || s_r.arrived || strcmp(event, s_r.anchor) != 0)
        return;
    if (strcmp(event, "boot") != 0 && s_r.anchor_name[0] && _stricmp(name, s_r.anchor_name) != 0)
        return;
    s_r.arrived = 1;
    s_r.arrived_at = btvs_uptime();
    if (s_r.input[0] && btvs_input_playback(s_r.input)) {
        s_r.playing = 1;
        btvs_log("replay", "at the anchor (%s \"%s\") after %.1f s: playing the recorded input", event, name,
                 s_r.arrived_at);
    } else {
        s_r.done = 1;
        btvs_log("replay", "at the anchor (%s \"%s\") after %.1f s: %s. The pads are yours.", event, name, s_r.arrived_at,
                 s_r.input[0] ? "the recording could not be played (see the [INPUT] line)" : "no input recording to play");
    }
}

/* Once a frame (btvs_frame_tick). */
void btvs_replay_frame(void)
{
    if (!s_r.active || s_r.done)
        return;
    if (!s_r.arrived)
        return;
    if (s_r.playing && !btvs_input_playback_active()) {
        s_r.playing = 0;
        s_r.done = 1;
        btvs_log("replay", "the recorded input has ended, %.1f s after the anchor (the report was made %.1f s after it)."
                           " The pads are yours.", btvs_uptime() - s_r.arrived_at, s_r.report_time - s_r.anchor_time);
        btvs_trail("replay_end", "seconds=%.1f", btvs_uptime());
    }
}
