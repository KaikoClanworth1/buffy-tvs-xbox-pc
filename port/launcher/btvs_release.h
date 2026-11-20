/* The release of the disc this build was translated from.
 *
 * The game's code is compiled into buffy_tvs.exe from one particular
 * Default.xbe (tools/regen.sh). Another release of the disc has its code at
 * other addresses, so its data cannot run under this exe: the launcher
 * refuses to install it and the game refuses to start on it, both with a
 * message that says so (launcher/btvs_launcher.c, src/btvs_xbecheck.c).
 *
 * After regenerating from a different Default.xbe, update these: the numbers
 * are printed by   "Buffy Launcher.exe" --identify <image or game folder>
 */
#ifndef BTVS_RELEASE_H
#define BTVS_RELEASE_H

#define BTVS_TITLE_ID        0x45410012u      /* certificate title ID (EA-018) */
#define BTVS_XBE_SIZE        7819264u         /* bytes */
#define BTVS_XBE_REGION      0x4u             /* certificate region: 1 N. America, 2 Japan, 4 rest of world */
#define BTVS_XBE_VERSION     2u               /* certificate version */
#define BTVS_XBE_SHA256      "f91cd33c980f6b5514aa98925397338ee1de6e7b432dd393efd470c0f0702f40"
#define BTVS_RELEASE_NAME    "European (PAL) release, version 2"

#endif
