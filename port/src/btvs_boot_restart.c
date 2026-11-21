/**
 * The game restarts itself, so something has to start it again.
 *
 * To load a level the game calls XLaunchNewImage on its own image with a
 * command line in the launch data ("root:Z:\ ... controller:..."): the
 * console reboots into the same game with all its memory free, and the new
 * run reads the launch data and goes to the level. Menu -> level, level ->
 * level and level -> menu all go this way.
 *
 * Here the program started by the player (or by run.sh) is only a
 * supervisor: it starts the game as a child process (the same exe, with
 * BTVS_CHILD=1) and waits. When the game asks to be launched again, the
 * runtime writes the launch data page to a file and the child ends with
 * XBOX_EXIT_RELAUNCH; the supervisor then starts the next child with that
 * file. Any other exit code ends the supervisor with the same code.
 *
 * So a restart really is a new process (nothing of the old run is left, as
 * on the console), and whoever started the supervisor waits on one process
 * for the whole session. The children are in a job that dies with the
 * supervisor: stopping the supervisor by PID stops the game.
 *
 * Test runs: BTVS_EXIT_AFTER counts for the whole session (each child gets
 * what is left of it); everything else in the environment is inherited
 * (BTVS_NO_WINDOW, BTVS_MUTE, BTVS_AFFINITY ...). BTVS_NO_SUPERVISOR=1: the
 * game runs in the process that was started, and a restart just ends it.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XBOX_EXIT_RELAUNCH 0x42544C52u

/* 1: this process was the supervisor and the session is over (*exit_code is
 * the game's). 0: this process is the game; carry on. */
int btvs_supervise(int *exit_code)
{
    char exe[MAX_PATH], dir[MAX_PATH], launch[MAX_PATH], next[MAX_PATH], *sl;
    const char *ea = getenv("BTVS_EXIT_AFTER");
    double limit = ea ? atof(ea) : 0.0;
    DWORD t0 = GetTickCount(), code = 0;
    HANDLE job;
    int runs = 0, have_launch = 0;

    if (getenv("BTVS_CHILD") || getenv("BTVS_NO_SUPERVISOR"))
        return 0;
    if (!GetModuleFileNameA(NULL, exe, sizeof exe))
        return 0;
    strcpy_s(dir, sizeof dir, exe);
    if ((sl = strrchr(dir, '\\')) != NULL)
        *sl = 0;
    sprintf_s(launch, sizeof launch, "%s\\btvs_launch_%lu.bin", dir, GetCurrentProcessId());
    sprintf_s(next, sizeof next, "%s\\btvs_launch_%lu.next", dir, GetCurrentProcessId());

    job = CreateJobObjectA(NULL, NULL);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
        memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }
    {   /* the supervisor itself keeps to the test run's CPUs and priority too */
        const char *aff = getenv("BTVS_AFFINITY");
        DWORD_PTR mask = aff ? (DWORD_PTR)strtoull(aff, NULL, 16) : 0;
        if (mask && SetProcessAffinityMask(GetCurrentProcess(), mask))
            SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    }
    SetEnvironmentVariableA("BTVS_CHILD", "1");
    SetEnvironmentVariableA("RECOMP_RELAUNCH_FILE", next);

    for (;;) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char left[32];
        double used = (GetTickCount() - t0) / 1000.0;
        if (limit > 0.0) {
            if (limit - used < 1.0)
                break;                                     /* the test run's time is up */
            sprintf_s(left, sizeof left, "%d", (int)(limit - used));
            SetEnvironmentVariableA("BTVS_EXIT_AFTER", left);
        }
        SetEnvironmentVariableA("RECOMP_LAUNCH_DATA_FILE", have_launch ? launch : NULL);
        if (runs) {
            /* scripted input: a script is for one run of the game. The n-th
             * restart plays BTVS_PAD_SCRIPT_<n> (1 = the first restart), or
             * nothing. */
            char name[40];
            sprintf_s(name, sizeof name, "BTVS_PAD_SCRIPT_%d", runs);
            SetEnvironmentVariableA("BTVS_PAD_SCRIPT", getenv(name));
        }
        SetEnvironmentVariableA("BTVS_RESTARTS", runs ? (sprintf_s(left, sizeof left, "%d", runs), left) : NULL);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        if (!CreateProcessA(exe, GetCommandLineA(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "[boot] cannot start the game process (error %lu)\n", GetLastError());
            code = 1;
            break;
        }
        if (job)
            AssignProcessToJobObject(job, pi.hProcess);
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        runs++;
        if (code != XBOX_EXIT_RELAUNCH)
            break;
        /* the launch data the game left is the next run's */
        DeleteFileA(launch);
        if (!MoveFileA(next, launch)) {
            fprintf(stderr, "[boot] the game asked to be started again but left no launch data\n");
            code = 1;
            break;
        }
        have_launch = 1;
        fprintf(stderr, "[boot] the game restarts itself (run %d)\n", runs + 1);
        fflush(stderr);
    }
    DeleteFileA(launch);
    DeleteFileA(next);
    if (job)
        CloseHandle(job);
    *exit_code = (int)code;
    return 1;
}
