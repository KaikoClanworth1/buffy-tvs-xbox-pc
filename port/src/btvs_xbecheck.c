/**
 * Is the installed Default.xbe the one this executable was built from?
 *
 * The game's code in this exe is one Default.xbe's, translated to C. Another
 * release of the disc (or a patched XBE) has its code and data at other
 * addresses: with it the game would stop at the first call made through a
 * pointer, with nothing on screen to say why. So the file is hashed at start
 * and anything else is refused, plainly. The expected values are in
 * launcher/btvs_release.h, which the launcher checks too before it installs.
 *
 * BTVS_NO_WINDOW (test runs): the message goes to stderr only, no dialog.
 * BTVS_ANY_XBE=1 (developers): warn and carry on.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../launcher/btvs_release.h"

#pragma comment(lib, "bcrypt")

static int sha256_hex(const uint8_t *data, size_t n, char out[65])
{
    BCRYPT_ALG_HANDLE alg;
    uint8_t h[32];
    int i, ok;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return 0;
    ok = BCryptHash(alg, NULL, 0, (PUCHAR)data, (ULONG)n, h, 32) >= 0;
    BCryptCloseAlgorithmProvider(alg, 0);
    for (i = 0; ok && i < 32; i++)
        sprintf_s(out + i * 2, 3, "%02x", h[i]);
    return ok;
}

/* 1: carry on. 0: wrong game, wrong release or a changed file (already said). */
int btvs_xbe_check(const void *xbe, size_t size)
{
    const uint8_t *x = (const uint8_t *)xbe;
    uint32_t base, cert, title = 0, region = 0, version = 0;
    char sha[65] = "", msg[900];
    const char *env;

    if (size >= 0x180 && !memcmp(x, "XBEH", 4)) {
        memcpy(&base, x + 0x104, 4);
        memcpy(&cert, x + 0x118, 4);
        cert -= base;
        if ((uint64_t)cert + 0xB0 <= size) {
            memcpy(&title, x + cert + 8, 4);
            memcpy(&region, x + cert + 0xA0, 4);
            memcpy(&version, x + cert + 0xAC, 4);
        }
    }
    if (sha256_hex(x, size, sha) && !strcmp(sha, BTVS_XBE_SHA256))
        return 1;

    if (title != BTVS_TITLE_ID)
        sprintf_s(msg, sizeof msg,
                  "The Default.xbe in the game folder is not Buffy the Vampire Slayer (title ID %08X, expected %08X).\n\n"
                  "Install the game again from your disc image with Buffy Launcher.",
                  title, BTVS_TITLE_ID);
    else if (region == BTVS_XBE_REGION && version == BTVS_XBE_VERSION)
        sprintf_s(msg, sizeof msg,
                  "The Default.xbe in the game folder is the right release (the " BTVS_RELEASE_NAME "), but the file "
                  "has been changed or damaged: it is not the one from the original disc.\n\n"
                  "Install the game again from an unmodified image of your disc with Buffy Launcher.");
    else
        sprintf_s(msg, sizeof msg,
                  "The installed game is another release of Buffy the Vampire Slayer (region %X, version %u).\n\n"
                  "This build of the port was made from the " BTVS_RELEASE_NAME " and only runs that one: "
                  "another release needs its own build.",
                  region, version);
    fprintf(stderr, "[XBE] %s\n[XBE] sha256 %s, expected %s\n", msg, sha, BTVS_XBE_SHA256);
    fflush(stderr);
    env = getenv("BTVS_ANY_XBE");
    if (env && *env == '1') {
        fprintf(stderr, "[XBE] BTVS_ANY_XBE=1: carrying on anyway\n");
        return 1;
    }
    env = getenv("BTVS_NO_WINDOW");
    if (!(env && *env && *env != '0'))
        MessageBoxA(NULL, msg, "Buffy the Vampire Slayer", MB_ICONERROR);
    return 0;
}
