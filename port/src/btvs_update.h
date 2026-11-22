/* What the launcher's last update check found, for the game to show
 * (Extras; docs/install.md, "Updates"). The game itself never goes online:
 * "Buffy Launcher.exe" asks GitHub when it is opened and leaves the answer in
 * btvs_update.ini beside the game. */
#ifndef BTVS_UPDATE_H
#define BTVS_UPDATE_H

/* 1 when a newer version of the port than this one was found at the last
 * check; then version is e.g. "0.2.0" and notes one or two plain sentences
 * on what changed (may be ""). 0: nothing newer known (never checked, up to
 * date, an install RecompLauncher updates, or this build is already that
 * version). Either buffer may be NULL. Cheap after the first call. */
int btvs_update_available(char *version, int version_cap, char *notes, int notes_cap);

#endif
