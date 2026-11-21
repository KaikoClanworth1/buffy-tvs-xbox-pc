/* btvs_input_test: the input layer on its own, without the game.
 *
 *   btvs_input_test                    self-test (made-up clock and controller); exit code 0 = pass
 *   btvs_input_test script <file|text> [seconds]   play a pad script in real time, print each change
 *   btvs_input_test live [seconds]     print what the PC's controllers and keyboard give (reads only:
 *                                      no rumble, the mouse is left alone)
 *   btvs_input_test dump <recording>   print a recording as text
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "btvs.h"
#include "btvs_input_priv.h"

static double s_t;
static int s_focus = 1, s_fail;
static double fake_clock(void) { return s_t; }
static int fake_focus(void) { return s_focus; }
static int yes(void) { return 1; }

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); s_fail++; } } while (0)

static void print_pad(double t, int port, int conn, const btvs_pad *g)
{
    printf("%8.3f p%d %s btn %04X  A%3u B%3u X%3u Y%3u Bk%3u Wh%3u LT%3u RT%3u  L %6d %6d  R %6d %6d\n", t, port + 1,
           conn ? "pad" : "---", g->buttons, g->analog[0], g->analog[1], g->analog[2], g->analog[3], g->analog[4],
           g->analog[5], g->analog[6], g->analog[7], g->lx, g->ly, g->rx, g->ry);
}

/* one 16 ms tick of the game's timer: read both ports */
static void tick(btvs_pad *p0, btvs_pad *p1, int *c1)
{
    int c = btvs_input_poll(0, p0);
    CHECK(c == 1);                              /* port 1 always has a pad */
    c = btvs_input_poll(1, p1);
    if (c1) *c1 = c;
    s_t += 16.0;
}

static void run_to(double ms, btvs_pad *p0, btvs_pad *p1, int *c1)
{
    while (s_t < ms)
        tick(p0, p1, c1);
}

static const char k_script[] =
    "# press Start until the menu, then go into a level\n"
    "0.5 START every 1.0\n"
    "after menu\n"
    "hold 0.2\n"
    "1.0 DOWN\n"
    "+0.5 A\n"
    "+0.5 LS=0.5,-1 LT=128 for 1.0\n"
    "3.0 p2 X for 0.5\n"
    "f480 RSL for f10\n";

static int selftest(void)
{
    btvs_pad a, b, tmp;
    in_host_pad hp;
    char rec[MAX_PATH], buf[64];
    int c1 = 0, i, n;
    enum { TICKS = 700 };
    static btvs_pad seq[TICKS][2];
    static int seqc[TICKS];

    GetTempPathA(MAX_PATH, rec);
    strcat_s(rec, MAX_PATH, "btvs_input_test.rec");
    btvs_input__test_hooks(fake_clock, fake_focus);
    btvs_input__test_reset();
    btvs_input_allow_mouse_look(0);

    /* 1. bindings table */
    CHECK(btvs_input_action_count() == 24);
    CHECK(!strcmp(btvs_input_action_name(0), "A"));
    CHECK(!strcmp(btvs_input_binding_text(0, 0, buf, sizeof buf), "Space"));
    CHECK(!strcmp(btvs_input_binding_text(4, 2, buf, sizeof buf), "Pad LB"));      /* Black on LB */
    CHECK(!strcmp(btvs_input_binding_text(5, 2, buf, sizeof buf), "Pad RB"));      /* White on RB */

    /* 2. a PC controller through the bindings */
    memset(&hp, 0, sizeof hp);
    hp.buttons = 1u << HB_A | 1u << HB_LB | 1u << HB_START | 1u << HB_UP | 1u << HB_R3;
    hp.lt = 255; hp.rt = 20;                    /* 20 is inside the trigger dead zone */
    hp.lx = 32767; hp.ry = -32768;
    btvs_input__test_host_pad(&hp);
    s_t = 0.0;
    tick(&a, &b, &c1);
    CHECK(a.analog[BTVS_AN_A] == 255 && a.analog[BTVS_AN_BLACK] == 255 && a.analog[BTVS_AN_WHITE] == 0);
    CHECK(a.analog[BTVS_AN_LT] == 255 && a.analog[BTVS_AN_RT] == 0);
    CHECK(a.buttons == (BTVS_BTN_START | BTVS_BTN_UP | BTVS_BTN_RSTICK));
    CHECK(a.lx == 32767 && a.ly == 0 && a.rx == 0 && a.ry == -32767);
    CHECK(c1 == 0 && b.buttons == 0);           /* one controller: port 2 is empty */
    CHECK(btvs_input_connected() == 1);

    /* sticks: inside the PC dead zone nothing, just past it the game's dead zone is already cleared */
    memset(&hp, 0, sizeof hp);
    hp.lx = 3000;                               /* 9 %: inside the default 15 % */
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(a.lx == 0 && a.ly == 0);
    hp.lx = 6000;                               /* 18 % */
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(a.lx > 13107 && a.lx < 15000);
    hp.lx = 23170; hp.ly = 23170;               /* full diagonal */
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(a.lx > 23000 && a.ly > 23000);

    /* 3. no focus: the player's devices are ignored, port 1 is still there */
    hp.buttons = 1u << HB_A;
    btvs_input__test_host_pad(&hp);
    s_focus = 0;
    s_t += 16.0; tick(&a, &b, NULL);
    memset(&tmp, 0, sizeof tmp);
    CHECK(!memcmp(&a, &tmp, sizeof a));
    s_focus = 1;
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_A] == 255);

    /* 4. the script, without focus (as in a hidden test run), recorded */
    btvs_input__test_reset();
    s_focus = 0;
    s_t = 1000.0;
    n = btvs_input_script_load_text(k_script);
    CHECK(n == 6);
    CHECK(btvs_input_connected() == 3);         /* the script uses port 2 */
    CHECK(btvs_input_record_begin(rec));
    for (i = 0; i < TICKS; i++) {
        double rel = s_t - 1000.0;
        if (i == 200)                           /* 3.2 s in: the menu is up */
            btvs_input_script_mark("menu");
        btvs_input_frame();
        seqc[i] = 0;
        btvs_input_poll(0, &seq[i][0]);
        seqc[i] = btvs_input_poll(1, &seq[i][1]);
        /* part 1: Start for 0.15 s at 0.5, 1.5, 2.5 */
        if (i < 200) {
            double ph = rel - 500.0;
            int want = ph >= 0.0 && (ph - 1000.0 * (int)(ph / 1000.0)) < 150.0;
            CHECK(((seq[i][0].buttons & BTVS_BTN_START) != 0) == want);
        } else {
            double m = s_t - (1000.0 + 200 * 16.0);         /* ms since the mark */
            CHECK(!(seq[i][0].buttons & BTVS_BTN_START));   /* part 1 stopped */
            CHECK(((seq[i][0].buttons & BTVS_BTN_DOWN) != 0) == (m >= 1000.0 && m < 1200.0));
            CHECK((seq[i][0].analog[BTVS_AN_A] == 255) == (m >= 1500.0 && m < 1700.0));
            CHECK((seq[i][0].analog[BTVS_AN_LT] == 128) == (m >= 2000.0 && m < 3000.0));
            if (m >= 2000.0 && m < 3000.0)
                CHECK(seq[i][0].lx == 16383 && seq[i][0].ly == -32767);
            CHECK((seq[i][1].analog[BTVS_AN_X] == 255) == (m >= 3000.0 && m < 3500.0));
            /* frames count from the mark: frame i+1 at tick i, the mark at frame 200 */
            CHECK((seq[i][0].rx == -32767) == (i + 1 - 200 >= 480 && i + 1 - 200 < 490));
        }
        CHECK(seqc[i] == 1);
        s_t += 16.0;
    }
    CHECK(btvs_input_script_done());
    btvs_input_record_end();

    /* 5. the recording gives the same pads back, read for read */
    btvs_input__test_reset();
    s_t = 50000.0;
    _putenv_s("BTVS_INPUT_REPLAY_CLOCK", "poll");
    CHECK(btvs_input_playback(rec));
    _putenv_s("BTVS_INPUT_REPLAY_CLOCK", "");
    CHECK(btvs_input_playback_active());
    for (i = 0; i < TICKS; i++) {
        btvs_input_frame();
        if (i % 7 == 3)
            s_t += 40.0;                        /* the clock stumbles; reads still line up */
        btvs_input_poll(0, &a);
        c1 = btvs_input_poll(1, &b);
        if (memcmp(&a, &seq[i][0], sizeof a) || memcmp(&b, &seq[i][1], sizeof b) || c1 != seqc[i]) {
            printf("FAIL: playback differs at read %d\n", i);
            print_pad(s_t, 0, 1, &seq[i][0]); print_pad(s_t, 0, 1, &a);
            s_fail++;
            break;
        }
        s_t += 16.0;
    }
    btvs_input_poll(0, &a);                     /* one read past the end: back to the player */
    CHECK(!btvs_input_playback_active());
    n = btvs_input_dump_recording(rec, stdout);
    CHECK(n > 10 && n < 40);                    /* changes only, not 1400 reads */

    /* 5e. the usual clock is time from the game's first pad read: a run that
     * starts later (a longer load) gets the same pads at the same time after
     * it, and when reads stop for a while the recording does not fall behind */
    btvs_input__test_reset();
    s_t = 300000.0;
    CHECK(btvs_input_playback(rec));
    s_t += 7000.0;                              /* nothing for 7 s: a longer load */
    for (i = 0; i < TICKS - 30; i++) {
        int j = i < 300 ? i : i + 25;           /* 400 ms without a read at tick 300 */
        if (i == 300)
            s_t += 400.0;
        btvs_input_frame();
        btvs_input_poll(0, &a);
        c1 = btvs_input_poll(1, &b);
        if (memcmp(&a, &seq[j][0], sizeof a) || memcmp(&b, &seq[j][1], sizeof b) || c1 != seqc[j]) {
            printf("FAIL: playback by time differs at read %d\n", i);
            print_pad(s_t, 0, 1, &seq[j][0]); print_pad(s_t, 0, 1, &a);
            s_fail++;
            break;
        }
        s_t += 16.0;
    }
    DeleteFileA(rec);

    /* 5a. a mark reached while a recording plays is kept, and the script goes
     * on from that part when the recording ends */
    btvs_input__test_reset();
    s_focus = 0;
    s_t = 80000.0;
    CHECK(btvs_input_script_load_text("0.1 B for 5; after level; 0.2 X for 0.2") == 2);
    CHECK(btvs_input_record_begin(rec));
    for (i = 0; i < 20; i++) tick(&a, &b, NULL);
    btvs_input_record_end();
    btvs_input__test_reset();
    CHECK(btvs_input_script_load_text("0.1 B for 5; after level; 0.2 X for 0.2") == 2);
    CHECK(btvs_input_playback(rec));
    for (i = 0; i < 10; i++) tick(&a, &b, NULL);
    btvs_input_script_mark("level");
    while (btvs_input_playback_active()) tick(&a, &b, NULL);
    tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_B] == 0 && a.analog[BTVS_AN_X] == 0);       /* part 1's clock starts here */
    s_t += 240.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_B] == 0 && a.analog[BTVS_AN_X] == 255);
    DeleteFileA(rec);

    /* 5b. keys held by a script go through the bindings, with no focus */
    btvs_input__test_reset();
    s_focus = 0;
    s_t = 90000.0;
    CHECK(btvs_input_script_load_text("0.1 K:Space K:W for 0.2; 0.5 K:MouseLeft K:J for 0.2") == 2);
    CHECK(btvs_input_connected() == 1);
    tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_A] == 0 && a.ly == 0);
    s_t += 160.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_A] == 255 && a.ly == 32767 && a.lx == 0 && a.buttons == 0);
    s_t += 200.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_A] == 0 && a.ly == 0);
    s_t += 200.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_X] == 255 && a.rx == -32767 && a.ry == 0);

    /* 5c. a script's mouse movement is the right stick; not while the game says no (menus, pause) */
    btvs_input__test_reset();
    s_focus = 0;
    s_t = 95000.0;
    CHECK(btvs_input_script_load_text("0.1 M:10,0 for 0.2; 0.5 M:0,-40 for 0.2; 0.9 M:40,0 for 0.2") == 3);
    btvs_input_allow_mouse_look(1);
    tick(&a, &b, NULL);
    s_t += 160.0; tick(&a, &b, NULL);
    CHECK(a.rx > 18000 && a.rx < 24000 && a.ry == 0 && a.lx == 0);      /* 10 counts a read: part of the way */
    s_t += 400.0; tick(&a, &b, NULL);
    CHECK(a.rx == 0 && a.ry == 32767);                                 /* up, full */
    btvs_input_allow_mouse_look(0);
    s_t += 400.0; tick(&a, &b, NULL);
    CHECK(a.rx == 0 && a.ry == 0);
    btvs_input_allow_mouse_look(1);
    tick(&a, &b, NULL);
    CHECK(a.rx == 32767);
    btvs_input_allow_mouse_look(0);

    /* 5d. rumble goes to the controller of the player the game named, scaled
     * by the strength setting, and stops when the window loses focus */
    btvs_input__test_reset();
    s_focus = 1;
    memset(&hp, 0, sizeof hp);
    btvs_input__test_host_pad(&hp);
    tick(&a, &b, NULL);
    btvs_input_rumble(1, 60000, 60000);                                 /* player 2 has no controller */
    tick(&a, &b, NULL);
    { int rp, rl, rr;
      btvs_input__test_last_rumble(&rp, &rl, &rr);
      CHECK(rl == 0 && rr == 0);
      btvs_input_rumble(0, 40000, 20000);
      btvs_input__test_last_rumble(&rp, &rl, &rr);
      CHECK(rp == 0 && rl == 40000 && rr == 20000);
      s_focus = 0;
      tick(&a, &b, NULL);
      btvs_input__test_last_rumble(&rp, &rl, &rr);
      CHECK(rp == 0 && rl == 0 && rr == 0);
      s_focus = 1;
      tick(&a, &b, NULL);
      btvs_input__test_last_rumble(&rp, &rl, &rr);
      CHECK(rp == 0 && rl == 40000 && rr == 20000);                     /* and comes back with it */
      btvs_input_rumble(0, 0, 0);
      btvs_input__test_last_rumble(&rp, &rl, &rr);
      CHECK(rl == 0 && rr == 0);
    }

    /* 5f. the button pictures follow the device: a controller on player 1, or none */
    btvs_input__test_reset();
    CHECK(btvs_input__glyph_device() == 0);                             /* keyboard and mouse */
    memset(&hp, 0, sizeof hp);
    btvs_input__test_host_pad(&hp);
    tick(&a, &b, NULL);
    CHECK(btvs_input__glyph_device() == 1);                             /* an Xbox-style pad */

    /* 6. rebinding a controller button: X's action onto the pad's Y */
    btvs_input__test_reset();
    s_focus = 1;
    memset(&hp, 0, sizeof hp);
    hp.buttons = 1u << HB_A;                    /* still holding the button that opened the page */
    btvs_input__test_host_pad(&hp);
    btvs_input_capture_begin(2, 2);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(btvs_input_capture_state() == 0 && a.analog[BTVS_AN_A] == 0);    /* the game sees nothing meanwhile */
    hp.buttons = 0;
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    hp.buttons = 1u << HB_Y;
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(btvs_input_capture_state() == 1);
    CHECK(a.analog[BTVS_AN_X] == 0);            /* not until it is let go */
    CHECK(!strcmp(btvs_input_binding_text(2, 2, buf, sizeof buf), "Pad Y"));
    hp.buttons = 0;
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    hp.buttons = 1u << HB_Y;
    btvs_input__test_host_pad(&hp);
    s_t += 16.0; tick(&a, &b, NULL);
    CHECK(a.analog[BTVS_AN_X] == 255 && a.analog[BTVS_AN_Y] == 255);       /* Y's own action is on it too */
    btvs_input__test_reset();                   /* settings are read again: the binding was saved */
    CHECK(!strcmp(btvs_input_binding_text(2, 2, buf, sizeof buf), "Pad Y"));
    btvs_input_bindings_reset();
    CHECK(!strcmp(btvs_input_binding_text(2, 2, buf, sizeof buf), "Pad X"));
    btvs_input_capture_begin(0, 0);             /* nothing pressed for 8 s: gives up */
    s_t += 9000.0;
    CHECK(btvs_input_capture_state() == -1);

    btvs_input__test_reset();
    printf(s_fail ? "%d check(s) FAILED\n" : "self-test passed\n", s_fail);
    return s_fail ? 1 : 0;
}

static int run_live(double secs, int scripted)
{
    btvs_pad last[BTVS_PORTS], g;
    int lastc[BTVS_PORTS] = { -1, -1, -1, -1 }, p, c;
    DWORD t0 = GetTickCount();
    memset(last, 0xEE, sizeof last);
    btvs_input_allow_mouse_look(0);
    if (!scripted)
        btvs_input__test_hooks(NULL, yes);      /* a console tool never has "the game window" */
    while ((GetTickCount() - t0) / 1000.0 < secs) {
        for (p = 0; p < BTVS_PORTS; p++) {
            c = btvs_input_poll(p, &g);
            if (c != lastc[p] || memcmp(&g, &last[p], sizeof g)) {
                print_pad((GetTickCount() - t0) / 1000.0, p, c, &g);
                last[p] = g;
                lastc[p] = c;
            }
        }
        if (scripted && btvs_input_script_done())
            break;
        Sleep(16);
    }
    for (p = 0; p < BTVS_PORTS; p++)
        if (btvs_input_device_name(p)[0])
            printf("player %d: %s\n", p + 1, btvs_input_device_name(p));
    return 0;
}

int main(int argc, char **argv)
{
    char ini[MAX_PATH];
    /* never the game's settings file */
    if (!getenv("BTVS_SETTINGS_INI")) {
        GetTempPathA(MAX_PATH, ini);
        strcat_s(ini, MAX_PATH, "btvs_input_test.ini");
        DeleteFileA(ini);
        SetEnvironmentVariableA("BTVS_SETTINGS_INI", ini);
        _putenv_s("BTVS_SETTINGS_INI", ini);
    }
    if (argc < 2)
        return selftest();
    if (!strcmp(argv[1], "dump") && argc > 2)
        return btvs_input_dump_recording(argv[2], stdout) < 0;
    if (!strcmp(argv[1], "script") && argc > 2) {
        _putenv_s("BTVS_NO_WINDOW", "1");       /* as in a test run: no devices at all */
        if (btvs_input_script_load_file(argv[2]) < 0)
            btvs_input_script_load_text(argv[2]);
        return run_live(argc > 3 ? atof(argv[3]) : 30.0, 1);
    }
    if (!strcmp(argv[1], "live"))
        return run_live(argc > 2 ? atof(argv[2]) : 10.0, 0);
    fprintf(stderr, "usage: btvs_input_test [script <file|text> [secs] | live [secs] | dump <recording>]\n");
    return 2;
}
