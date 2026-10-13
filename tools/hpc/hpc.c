// SPDX-License-Identifier: MIT
// Copyright (C) 2025 Adam Henault <contact@adamhlt.com>
// Copyright (C) 2025 Lab-STICC

/*
 * hpc.c
 *
 * Extension de --run pour accepter des arguments :
 *  - Mode simple :
 *      --run EXEC [ARGS...]
 *      => exécute EXEC avec ARGS...
 *
 *  - Mode multi :
 *      --run N MONITORED EXEC_MONITORED SEED [ARGS...]
 *      => lance N enfants
 *         - child[MONITORED] exécute EXEC_MONITORED avec ARGS...
 *         - les autres choisissent aléatoirement dans ELF_LIST (sans args)
 *
 * Remarque: on conserve le comportement existant (toggle filter avant/après).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

/* =========================
   Syscalls HPC (IDs fixes)
   ========================= */
#define __NR_hpc_config             441
#define __NR_hpc_read               442
#define __NR_hpc_reset              445
#define __NR_hpc_start              446
#define __NR_hpc_stop               447
#define __NR_hpc_toggle_debug       448
#define __NR_hpc_toggle_filtering   449
#define __NR_hpc_set_pid_filter     450

long hpc_config(uint32_t hpm, uint32_t event) { return syscall(__NR_hpc_config, hpm, event); }
long hpc_read(uint32_t hpm, uint64_t *val)    { return syscall(__NR_hpc_read, hpm, val); }
long hpc_reset(uint32_t hpm)                  { return syscall(__NR_hpc_reset, hpm); }
long hpc_start(void)                          { return syscall(__NR_hpc_start); }
long hpc_stop(void)                           { return syscall(__NR_hpc_stop); }
long hpc_toggle_debug(void)                   { return syscall(__NR_hpc_toggle_debug); }
long hpc_toggle_filter(void)                  { return syscall(__NR_hpc_toggle_filtering); }
long hpc_set_pid_filter(int pid)              { return syscall(__NR_hpc_set_pid_filter, pid); }

static void usage(const char *prog) {
    printf("Usage: %s [OPTION]\n", prog);
    printf("  --start                                Start HPC counters\n");
    printf("  --stop                                 Stop HPC counters\n");
    printf("  --toggle-debug                         Enable/Disable SBI debug logs\n");
    printf("  --toggle-filter                        Enable/Disable filtering\n");
    printf("  --config HPM EVENT                     Configure HPMx with the given event\n");
    printf("  --read HPM                             Read the value of HPMx counter\n");
    printf("  --reset HPM                            Reset HPMx counter to 0\n");
    printf("  --run EXEC [ARGS...]                   Fork then exec EXEC with optional args\n");
    printf("  --run N MONITORED EXEC_MONITORED SEED [ARGS...]\n");
    printf("                                         Fork N children; child[MONITORED] runs EXEC_MONITORED with ARGS...\n");
    printf("                                         others pick randomly from ELF_LIST using SEED (no args).\n");
}

/* =========================
   Liste des .elf non monitorés (tirage aléatoire)
   ========================= */
static const char *ELF_LIST[] = {
    "./aes.elf",
    "./crc32.elf",
    "./edn.elf",
    "./matmult.elf",
    "./minver.elf",
    "./mont64.elf",
    "./nsichneu.elf",
    "./primecount.elf",
    "./statemate.elf",
    "./ud.elf",
};
static const int ELF_LIST_COUNT = (int)(sizeof(ELF_LIST) / sizeof(ELF_LIST[0]));

/* =========================
   PRNG xorshift64* (déterministe)
   ========================= */
static inline uint64_t xs64_next(uint64_t *s) {
    uint64_t x = *s;
    if (x == 0) x = 0x9e3779b97f4a7c15ULL;  /* éviter l'état nul */
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 0x2545F4914F6CDD1DULL;
}

static inline uint32_t xs64_next_u32(uint64_t *s) {
    return (uint32_t)(xs64_next(s) >> 32);
}

/* =========================
   Helpers
   ========================= */
static void kill_all_children(pid_t *pids, int n) {
    if (!pids) return;
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            if (kill(pids[i], SIGKILL) != 0 && errno != ESRCH) {
                perror("kill");
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    if (!strcmp(argv[1], "--start")) {
        if (hpc_start() < 0) perror("hpc_start");
        else printf("[OK] HPC started\n");

    } else if (!strcmp(argv[1], "--stop")) {
        if (hpc_stop() < 0) perror("hpc_stop");
        else printf("[OK] HPC stopped\n");

    } else if (!strcmp(argv[1], "--toggle-debug")) {
        if (hpc_toggle_debug() < 0) perror("hpc_toggle_debug");
        else printf("[OK] Debug toggle sent\n");

    } else if (!strcmp(argv[1], "--toggle-filter")) {
        if (hpc_toggle_filter() < 0) perror("hpc_toggle_filter");
        else printf("[OK] Filter toggle sent\n");

    } else if (!strcmp(argv[1], "--config")) {
        if (argc != 4) { fprintf(stderr, "Usage: --config HPM EVENT\n"); return 1; }
        uint32_t hpm = (uint32_t)atoi(argv[2]);
        uint32_t event = (uint32_t)atoi(argv[3]);
        if (hpc_config(hpm, event) < 0) perror("hpc_config");
        else printf("[OK] HPM%u configured with event %u\n", hpm, event);

    } else if (!strcmp(argv[1], "--read")) {
        if (argc != 3) { fprintf(stderr, "Usage: --read HPM\n"); return 1; }
        uint32_t hpm = (uint32_t)atoi(argv[2]);
        uint64_t val = 0;
        if (hpc_read(hpm, &val) < 0) perror("hpc_read");
        else printf("[OK] HPM%u = %" PRIu64 "\n", hpm, val);

    } else if (!strcmp(argv[1], "--reset")) {
        if (argc != 3) { fprintf(stderr, "Usage: --reset HPM\n"); return 1; }
        uint32_t hpm = (uint32_t)atoi(argv[2]);
        if (hpc_reset(hpm) < 0) perror("hpc_reset");
        else printf("[OK] HPM%u reset\n", hpm);

    } else if (!strcmp(argv[1], "--run")) {

        /*
         * Deux modes :
         *  - --run EXEC [ARGS...]
         *  - --run N MONITORED EXEC_MONITORED SEED [ARGS...]
         *
         * En mode multi :
         *  - args optionnels (argv[6..]) sont passés AU MONITORÉ uniquement.
         *  - les autres restent sans args.
         */

        int multi = 0;
        int n = 1;
        int monitored_index = 0;
        uint64_t seed = 0;
        const char *exec_monitored = NULL;
        int monitored_args_start = -1; /* = 6 si mode multi et args présents */
        int exec_pos_simple = 2;       /* en simple: argv[2] = EXEC, args à partir de argv[3] */

        /* Détection du mode multi si argv[2], argv[3], argv[5] sont numériques */
        if (argc >= 6) {
            char *e1 = NULL, *e2 = NULL, *e5 = NULL;
            long maybe_n = strtol(argv[2], &e1, 10);
            long maybe_mon = strtol(argv[3], &e2, 10);
            unsigned long long maybe_seed = strtoull(argv[5], &e5, 10);

            if (e1 && *e1 == '\0' && e2 && *e2 == '\0' && e5 && *e5 == '\0') {
                multi = 1;
                n = (int)maybe_n;
                monitored_index = (int)maybe_mon;
                exec_monitored = argv[4];
                seed = (uint64_t)maybe_seed;
                monitored_args_start = 6; /* argv[6..] */
            }
        }

        /* Validation */
        if (!multi) {
            /* Mode simple: --run EXEC [ARGS...] */
            if (argc < 3) { fprintf(stderr, "Usage: --run EXEC [ARGS...]\n"); return 1; }
            n = 1; /* un seul enfant */
        } else {
            /* Mode multi: --run N MONITORED EXEC_MONITORED SEED [ARGS...] */
            if (n <= 0) { fprintf(stderr, "Error: N must be >= 1\n"); return 1; }
            if (monitored_index < 0 || monitored_index >= n) {
                fprintf(stderr, "Error: MONITORED must be in [0..N-1]\n"); return 1;
            }
            if (!exec_monitored || !*exec_monitored) {
                fprintf(stderr, "Error: EXEC_MONITORED must be provided\n"); return 1;
            }
            if (ELF_LIST_COUNT <= 0) {
                fprintf(stderr, "Error: ELF_LIST is empty\n"); return 1;
            }
        }

        /* Toggle filtering ON (ou bascule) avant exécution */
        if (hpc_toggle_filter() < 0) perror("hpc_toggle_filter");
        else printf("[OK] Filter toggle sent\n");

        pid_t *pids = (pid_t *)calloc((size_t)n, sizeof(pid_t));
        if (!pids) { perror("calloc"); return 1; }
        for (int i = 0; i < n; i++) pids[i] = -1;

        uint64_t rng = seed ? seed : 0x9e3779b97f4a7c15ULL;

        for (int i = 0; i < n; i++) {
            const char *chosen = NULL;

            if (!multi) {
                chosen = argv[exec_pos_simple];
            } else {
                if (i == monitored_index) {
                    chosen = exec_monitored;
                } else {
                    /* tirage déterministe pour les autres */
                    uint64_t local = rng;
                    for (int step = 0; step <= i; step++) (void)xs64_next(&local);
                    uint32_t r = xs64_next_u32(&local);
                    int pick = (int)(r % (uint32_t)ELF_LIST_COUNT);
                    chosen = ELF_LIST[pick];
                }
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                kill_all_children(pids, n);
                free(pids);
                return 1;
            }

            if (pid == 0) {
                /* ----- ENFANT i ----- */
                pid_t child_pid = getpid();

                if (multi && i == monitored_index) {
                    if (hpc_set_pid_filter((int)child_pid) < 0) {
                        perror("hpc_set_pid_filter");
                        _exit(127);
                    }
                    fprintf(stderr, "[CHILD %d - MONITORED] set PID filter to %d\n", i, (int)child_pid);

                    /* Construire argv pour le monitoré: [chosen, argv[6..], NULL] */
                    int extra = (monitored_args_start >= 0) ? (argc - monitored_args_start) : 0;
                    char **argv_exec = (char **)malloc(sizeof(char*) * (size_t)(extra + 2));
                    if (!argv_exec) { perror("malloc"); _exit(127); }

                    argv_exec[0] = (char *)chosen;
                    for (int k = 0; k < extra; k++) {
                        argv_exec[1 + k] = argv[monitored_args_start + k];
                    }
                    argv_exec[1 + extra] = NULL;

                    execvp(argv_exec[0], argv_exec);
                    perror("execvp");
                    free(argv_exec);
                    _exit(127);

                } else {
                    if (multi) {
                        fprintf(stderr, "[CHILD %d] not monitored (PID %d)\n", i, (int)child_pid);
                        /* Non monitorés: pas d'args */
                        char *const child_argv[] = { (char*)chosen, NULL };
                        execvp(child_argv[0], child_argv);
                        perror("execvp");
                        _exit(127);
                    } else {
                        /* Simple: argv[2..] = EXEC + ARGS... */
                        execvp(argv[2], &argv[2]);
                        perror("execvp");
                        _exit(127);
                    }
                }

            } else {
                pids[i] = pid;
                printf("[PARENT] Child[%d] PID %d%s  (exec: %s)\n",
                       i, (int)pid,
                       (multi && i == monitored_index ? "  <- MONITORED" : ""),
                       chosen);
            }
        }

        /* Attente + arrêt des concurrents quand le monitoré se termine */
        int remaining = n;
        int monitored_finished = 0;

        while (remaining > 0) {
            int status = 0;
            pid_t done = waitpid(-1, &status, 0);
            if (done < 0) {
                if (errno == EINTR) continue;
                perror("waitpid");
                break;
            }

            int which = -1;
            for (int i = 0; i < n; i++) {
                if (pids[i] == done) { which = i; break; }
            }

            if (WIFEXITED(status)) {
                printf("[PARENT] PID %d (idx %d%s) exited, status=%d\n",
                       (int)done, which,
                       (multi && which == monitored_index ? ", MONITORED" : ""),
                       WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("[PARENT] PID %d (idx %d%s) killed by signal %d\n",
                       (int)done, which,
                       (multi && which == monitored_index ? ", MONITORED" : ""),
                       WTERMSIG(status));
            } else {
                printf("[PARENT] PID %d (idx %d%s) ended (state=0x%x)\n",
                       (int)done, which,
                       (multi && which == monitored_index ? ", MONITORED" : ""),
                       status);
            }

            /* marquer comme terminé */
            for (int i = 0; i < n; i++) {
                if (pids[i] == done) { pids[i] = -1; break; }
            }
            remaining--;

            /* si le monitoré termine en mode multi: tuer les autres */
            if (multi && which == monitored_index && !monitored_finished) {
                monitored_finished = 1;
                for (int i = 0; i < n; i++) {
                    if (pids[i] > 0) {
                        if (kill(pids[i], SIGKILL) == 0) {
                            printf("[PARENT] Sent SIGKILL to Child[%d] PID %d\n", i, (int)pids[i]);
                        } else if (errno != ESRCH) {
                            perror("kill");
                        }
                    }
                }
            }
        }

        free(pids);
        printf("[PARENT] Benchmark done\n");

        /* Toggle filtering OFF (ou bascule) après exécution */
        if (hpc_toggle_filter() < 0) perror("hpc_toggle_filter");
        else printf("[OK] Filter toggle sent\n");

    } else {
        fprintf(stderr, "Unknown argument: %s\n", argv[1]);
        usage(argv[0]);
        return 1;
    }

    return 0;
}
