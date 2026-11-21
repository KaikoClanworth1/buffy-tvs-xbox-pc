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
#include <math.h>

#include "btvs_log.h"

int btvs_screenshot(const char *path);     /* Graphics (btvs.h); a stand-in in btvs_report.c says no */

/* (the level jump through the game's own functions is gone: launch data does it)
 * 1 = on its way, 0 = can't (yet), -1 = not possible at this moment, ask again. */

static struct {
    int active, arrived, playing, done, goto_state, said_manual;
    int have_spot, placed, shot_in;
    int play_input;                 /* play the recorded input before placing (BTVS_REPLAY_INPUT=1, or a menu report) */          /* the report's exact spot (player matrix) and whether the player was put there */
    float spot[12], health;
    double ready_since;
    char frames[MAX_PATH];          /* the report's frame recording, and how much of it to give back */
    unsigned exact_frames;
    int exact, have_exact_pos;
    float exact_pos[3];
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
    sprintf_s(pat, MAX_PATH, "%s\\Cache\\*.ses", from_save);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    CreateDirectoryA(to_save, NULL);
    sprintf_s(b, MAX_PATH, "%s\\Cache", to_save);
    CreateDirectoryA(b, NULL);
    do {
        sprintf_s(a, MAX_PATH, "%s\\Cache\\%s", from_save, fd.cFileName);
        sprintf_s(b, MAX_PATH, "%s\\Cache\\%s", to_save, fd.cFileName);
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && CopyFileA(a, b, FALSE))
            n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

int btvs_replay_active(void) { return s_r.active; }

int btvs_replay_wants_skip(void) { return s_r.active && !s_r.done && !s_r.play_input && s_r.have_spot; }

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
        else if (!strcmp(line, "player_matrix")) {
            s_r.have_spot = sscanf_s(p, "%f %f %f %f %f %f %f %f %f %f %f %f", &s_r.spot[0], &s_r.spot[1], &s_r.spot[2],
                                     &s_r.spot[3], &s_r.spot[4], &s_r.spot[5], &s_r.spot[6], &s_r.spot[7], &s_r.spot[8],
                                     &s_r.spot[9], &s_r.spot[10], &s_r.spot[11]) == 12;
        } else if (!strcmp(line, "player_health")) s_r.health = (float)atof(p);
        else if (!strcmp(line, "launch") && *p) sprintf_s(launch, MAX_PATH, "%s\\%s", s_r.folder, p);
        else if (!strcmp(line, "frames") && *p) sprintf_s(s_r.frames, MAX_PATH, "%s\\%s", s_r.folder, p);
        else if (!strcmp(line, "exact_frames")) s_r.exact_frames = (unsigned)strtoul(p, NULL, 10);
        else if (!strcmp(line, "exact_pos"))
            s_r.have_exact_pos = sscanf_s(p, "%f %f %f", &s_r.exact_pos[0], &s_r.exact_pos[1], &s_r.exact_pos[2]) == 3;
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

    {
        /* A report made in a level: straight to the spot (the level loads, the
         * player is put there) unless the inputs are asked for; they take as
         * long as the player had played. A report made in the menus has no
         * spot: its inputs are the way there. */
        const char *pi = getenv("BTVS_REPLAY_INPUT");
        s_r.play_input = !s_r.have_spot || (pi && *pi && *pi != '0');
        /* With the report's frame recording the play is given back frame for
         * frame (clock, pad samples, random state): the same every time. */
        if (s_r.play_input && s_r.have_spot && s_r.frames[0] && s_r.exact_frames && is_file(s_r.frames))
            s_r.exact = btvs_exact_replay(s_r.frames, s_r.exact_frames);
    }
    s_r.active = 1;
    btvs_log("replay", "replaying report %s", s_r.folder);
    btvs_log("replay", "  mode: %s", s_r.exact ? "the recorded play is given back frame for frame (frames.rec)"
             : s_r.play_input ? "the recorded input is played first, by time (no frame recording in this report, or BTVS_EXACT=0)"
             : "straight to the spot (BTVS_REPLAY_INPUT=1 plays the recorded input on the way instead)");
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
    if (!s_r.play_input) {
        btvs_log("replay", "at the anchor (%s) after %.1f s: waiting for the level", event, s_r.arrived_at);
    } else if (s_r.exact) {
        s_r.playing = 1;
        btvs_log("replay", "at the anchor (%s) after %.1f s: the recorded frames start when the level is up", event,
                 s_r.arrived_at);
    } else if (s_r.input[0] && btvs_input_playback(s_r.input)) {
        s_r.playing = 1;
        btvs_log("replay", "at the anchor (%s \"%s\") after %.1f s: playing the recorded input", event, name,
                 s_r.arrived_at);
    } else {
        s_r.done = !s_r.have_spot;      /* (the player is still put on the report's spot) */
        btvs_log("replay", "at the anchor (%s \"%s\") after %.1f s: %s. The pads are yours.", event, name, s_r.arrived_at,
                 s_r.input[0] ? "the recording could not be played (see the [INPUT] line)" : "no input recording to play");
    }
}

/* Once a frame (btvs_frame_tick). */
void btvs_replay_frame(void)
{
    /* A moment after arriving: the picture, to hold against the report's screenshot.png. */
    if (s_r.active && s_r.shot_in > 0 && --s_r.shot_in == 0) {
        char path[MAX_PATH];
        sprintf_s(path, MAX_PATH, "%s\\replay_arrived.png", s_r.folder);
        DeleteFileA(path);
        if (btvs_screenshot(path))
            btvs_log("replay", "picture of where the replay arrived: %s (the report's: screenshot.png beside it)", path);
    }
    if (!s_r.active || s_r.done)
        return;
    if (!s_r.arrived)
        return;
    if (s_r.playing && s_r.exact) {
        unsigned n, checks, bad;
        float dev, end[3], off = -1.0f;
        int same;
        if (btvs_exact_replay_state() != 3)
            return;
        s_r.playing = 0;
        same = btvs_exact_replay_result(&n, &checks, &dev, &bad, end);
        if (s_r.have_exact_pos)
            off = sqrtf((end[0] - s_r.exact_pos[0]) * (end[0] - s_r.exact_pos[0]) + (end[1] - s_r.exact_pos[1]) * (end[1] - s_r.exact_pos[1])
                        + (end[2] - s_r.exact_pos[2]) * (end[2] - s_r.exact_pos[2]));
        btvs_log("replay", "the recorded frames are in (%u frames, %.1f s after the anchor). The player stands at %.6f %.6f %.6f; "
                 "the report's recording ended at %.6f %.6f %.6f: %.6f units apart. %s",
                 n, btvs_uptime() - s_r.arrived_at, end[0], end[1], end[2], s_r.exact_pos[0], s_r.exact_pos[1], s_r.exact_pos[2],
                 off, same ? "She was on the recorded place at every check on the way." : "Her path DIFFERED on the way (see the [exact] lines).");
        if (same && off >= 0.0f && off < 0.001f) {
            /* Already there, by the same road: nothing to put right. */
            s_r.placed = 1;
            btvs_trail("replay_placed", "x=%.2f y=%.2f z=%.2f drift=%.4f by=frames", end[0], end[1], end[2], off);
        }
        btvs_trail("replay_frames", "frames=%u same=%d x=%.4f y=%.4f z=%.4f apart=%.4f", n, same, end[0], end[1], end[2], off);
    } else if (s_r.playing && !btvs_input_playback_active()) {
        s_r.playing = 0;
        btvs_log("replay", "the recorded input has ended, %.1f s after the anchor (the report was made %.1f s after it).",
                 btvs_uptime() - s_r.arrived_at, s_r.report_time - s_r.anchor_time);
    }
    if (s_r.playing)
        return;
    /* The inputs are in; what the game made of them may have drifted (it steps
     * by measured frame time). Put the player on the report's exact spot with
     * the game's own Teleport, once the report's level is up. */
    if (s_r.have_spot && !s_r.placed) {
        char now_level[96];
        if (!btvs_game_in_level(now_level, sizeof now_level) || _stricmp(now_level, s_r.level) != 0) {
            s_r.ready_since = 0.0;
            return;                                 /* still loading (a report with no recording) */
        }
        if (s_r.ready_since == 0.0)
            s_r.ready_since = btvs_uptime();
        if (btvs_trail_level_loaded_at() <= 0.0 || btvs_trail_in_cutscene()) {
            s_r.ready_since = 0.0;
            return;                                 /* not playable yet, or a cutscene is being skipped */
        }
        if (btvs_uptime() - s_r.ready_since < 0.5)
            return;
        {
            float pm[12], cm[12], hp = 0.0f, d = -1.0f;
            if (btvs_game_state(pm, cm, &hp))
                d = sqrtf((pm[9] - s_r.spot[9]) * (pm[9] - s_r.spot[9]) + (pm[10] - s_r.spot[10]) * (pm[10] - s_r.spot[10])
                          + (pm[11] - s_r.spot[11]) * (pm[11] - s_r.spot[11]));
            s_r.placed = btvs_game_place(s_r.spot, s_r.health);
            btvs_log("replay", "%s (%.2f %.2f %.2f, health %.0f); %s %.2f units away",
                     s_r.placed ? "player put on the report's exact spot" : "could not place the player",
                     s_r.spot[9], s_r.spot[10], s_r.spot[11], s_r.health,
                     s_r.play_input ? "the replayed input had left the player" : "the level starts", d);
            btvs_trail("replay_placed", "x=%.2f y=%.2f z=%.2f drift=%.2f", s_r.spot[9], s_r.spot[10], s_r.spot[11], d);
        }
    }
    s_r.done = 1;
    s_r.shot_in = 45;
    btvs_log("replay", "you are where the report was made, %.1f s after start. The pads are yours.", btvs_uptime());
    btvs_trail("replay_end", "seconds=%.1f", btvs_uptime());
}
