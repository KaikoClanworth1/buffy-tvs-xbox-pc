/* BTVS_REPLAY=<report folder or .zip>: go back to where a report was made.
 *
 *  1. before anything reads them, the report's settings and save are put in
 *     place: BTVS_SETTINGS_INI -> the report's settings.ini, BTVS_SAVE_DIR -> a
 *     fresh copy of the report's save\ under <exe>\replay\ (the player's own
 *     saves and settings are never touched);
 *  2. the game is taken to the report's anchor (replay.txt: boot / level /
 *     checkpoint / load) - btvs_game_goto() in btvs_trail.c, the game hooks;
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

/* btvs_trail.c (game hooks): take the game to a level / checkpoint now.
 * 1 = on its way, 0 = can't (yet), -1 = not possible at this moment, ask again. */
int btvs_game_goto(const char *level, const char *checkpoint);

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

int btvs_replay_active(void) { return s_r.active; }

void btvs_replay_init(void)
{
    const char *src = getenv("BTVS_REPLAY");
    char path[MAX_PATH], base[MAX_PATH], line[400], *p;
    size_t len;
    int partial = 0;
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
        delete_tree(s_r.folder);
        n = btvs_unzip(path, s_r.folder);
        if (n <= 0) {
            btvs_log("replay", "BTVS_REPLAY: can't unpack %s (%s). Unpack it yourself and give the folder.", path,
                     n == -2 ? "it was re-packed with compression" : "not found or not a zip");
            return;
        }
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
    }
    fclose(f);
    if (!s_r.anchor[0])
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
        static const char *const part[2] = { "UserData", "TitleData" };
        char from[MAX_PATH], to[MAX_PATH];
        int i;
        for (i = 0; i < 2; i++) {
            sprintf_s(from, MAX_PATH, "%s\\%s", path, part[i]);
            sprintf_s(to, MAX_PATH, "%s\\%s", base, part[i]);
            delete_tree(to);
            if (is_dir(from))
                btvs_copy_tree(from, to, ~0ull, NULL);
        }
    }
    _putenv_s("BTVS_SAVE_DIR", base);

    s_r.active = 1;
    btvs_log("replay", "replaying report %s", s_r.folder);
    btvs_log("replay", "  go to: %s \"%s\" (level \"%s\", checkpoint \"%s\"), then %.1f s of recorded input (%s)",
             s_r.anchor, s_r.anchor_name, s_r.level, s_r.checkpoint, s_r.report_time - s_r.anchor_time,
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
    if (!s_r.arrived) {
        if (!strcmp(s_r.anchor, "boot") || s_r.goto_state == 1)
            return;
        s_r.goto_state = btvs_game_goto(s_r.level, s_r.checkpoint);
        if (s_r.goto_state == 1) {
            btvs_log("replay", "going straight to level \"%s\"%s%s", s_r.level, s_r.checkpoint[0] ? ", checkpoint " : "",
                     s_r.checkpoint);
        } else if (s_r.goto_state == 0 && !s_r.said_manual) {
            s_r.said_manual = 1;
            btvs_log("replay", "can't jump to level \"%s\" directly in this build: play to it; the recorded input "
                               "starts by itself when the game gets there", s_r.level);
        }
        return;
    }
    if (s_r.playing && !btvs_input_playback_active()) {
        s_r.playing = 0;
        s_r.done = 1;
        btvs_log("replay", "the recorded input has ended, %.1f s after the anchor (the report was made %.1f s after it)."
                           " The pads are yours.", btvs_uptime() - s_r.arrived_at, s_r.report_time - s_r.anchor_time);
        btvs_trail("replay_end", "seconds=%.1f", btvs_uptime());
    }
}
