#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"
#include <pthread.h>

#define FINISHED_STATE 0
#define RUNNING_STATE 1

typedef struct {
    pid_t pid;
    int state;
    char **command;
    int cmd_size;
} BackgroundProcess;

BackgroundProcess *bg_processes;
int nb_bg_pids;
pthread_mutex_t bg_pids_mutex;

void handler_chld(int sig) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        pthread_mutex_lock(&bg_pids_mutex);
        // mises à jours
        for (int i = 0; i < nb_bg_pids; i++) {
            if (bg_processes[i].pid == pid) {
                bg_processes[i].state = FINISHED_STATE;
            }
        }
        pthread_mutex_unlock(&bg_pids_mutex);
    }
}

void handler_stop(int sig) {
    printf("\n");
}


void handler_suspend(int sig) {
    printf("\n");
}

int exec_shell_cmd(struct cmdline *l) {
    char **cmd = l->seq[0];
    if (strcmp(cmd[0], "quit") == 0) { // commande intégrée au shell
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd[0], "jobs") == 0) {
        pthread_mutex_lock(&bg_pids_mutex);
        for (int i = 0; i < nb_bg_pids; i++) {
            printf("[%d] %d", i + 1, bg_processes[i].pid);
            if (bg_processes[i].state) {
                printf(" En cours d'éxécution ");
            } else {
                printf(" Arrêté ");
            }

            char **c = bg_processes[i].command;
            for (int j = 0; j < bg_processes[i].cmd_size; j++) {
                printf("%s ", c[j]);
            }

            printf("\n");
        }
        pthread_mutex_unlock(&bg_pids_mutex);
        return 1;
    }
    return 0;
}

int cmd_size(char **cmd) {
    int nb = 0;
    while (cmd[nb] != NULL) {
        nb++;
    }
    return nb;
}

int count_cmd(struct cmdline *l) {
    int nb = 0;
    while (l->seq[nb]) {
        nb++;
    }
    return nb;
}

void redirect_in(struct cmdline *l) {
    if (l->in != NULL) {
        int fd_in = Open(l->in, O_RDONLY, 0644);
        if (fd_in == -1) {
            perror(l->in);
            exit(EXIT_FAILURE);
        }
        Dup2(fd_in, 0);
        Close(fd_in);
    }
}

void redirect_out(struct cmdline *l) {
    if (l->out != NULL) {
        int fd_out = Open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out == -1) {
            perror(l->out);
            exit(EXIT_FAILURE);
        }
        Dup2(fd_out, 1);
        Close(fd_out);
    }
}

void exec_cmd(struct cmdline *l) {
    int nb = count_cmd(l);
    pid_t *pids = malloc(sizeof(pid_t) * nb);
    int **pipes = malloc(sizeof(int) * nb - 1);

    for (int i = 0; i < nb - 1; i++) {
        pipes[i] = malloc(sizeof(int) * 2);
        if (pipe(pipes[i]) < 0) {
            perror("Erreur lors de la création du tube");
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < nb; i++) {
        pids[i] = Fork();

        if (pids[i] == -1) {
            perror("Erreur lors de la création du processus fils");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) { // fils
            if (i != 0) { // pas la première commande
                Close(pipes[i - 1][1]);
                if (Dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("Erreur lors de la redirection de l'entrée");
                    exit(EXIT_FAILURE);
                }
            } else { // première commande
                redirect_in(l);
            }

            if (i != nb - 1) { // pas la dernière commande
                Close(pipes[i][0]);
                if (Dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("Erreur lors de la redirection de la sortie");
                    exit(EXIT_FAILURE);
                }
            } else { // dernière commande
                redirect_out(l);
            }

            // changement de groupe
            // A REVOIR !!!
            if (l->bg) {
                Setpgid(0, 0);
            }

            char **cmd = l->seq[i];
            if (execvp(cmd[0], cmd) == -1) {
                perror(cmd[0]);
                exit(EXIT_FAILURE);
            }
        } else { // père
            if (l->bg) {
                pthread_mutex_lock(&bg_pids_mutex);
                printf("[%d] %d\n", i + 1, pids[i]);

                char **cmd = l->seq[i];
                int cmd_length = cmd_size(cmd);

                bg_processes = realloc(bg_processes, (nb_bg_pids + 1) * sizeof(BackgroundProcess));
                bg_processes[nb_bg_pids].command = malloc((cmd_length + 1) * sizeof(char *));

                bg_processes[nb_bg_pids].pid = pids[i];
                bg_processes[nb_bg_pids].state = RUNNING_STATE;
                bg_processes[nb_bg_pids].cmd_size = cmd_length;

                for (int j = 0; j < cmd_length; j++) {
                    bg_processes[nb_bg_pids].command[j] = strdup(cmd[j]);
                }
                bg_processes[nb_bg_pids].command[cmd_length] = NULL;

                nb_bg_pids++;
                pthread_mutex_unlock(&bg_pids_mutex);
            }
        }
    }

    // père

    for (int i = 0; i < nb - 1; i++) {
        Close(pipes[i][0]);
        Close(pipes[i][1]);
    }

    // A REVOIR !!!
    if (!l->bg) {
        for (int i = 0; i < nb; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }

    // libération de la mémoire
    free(pids);
    for (int i = 0; i < nb - 1; i++) {
        free(pipes[i]);
    }
    free(pipes);
}

int main() {
    Signal(SIGCHLD, handler_chld);
    Signal(SIGINT, handler_stop);
    Signal(SIGTSTP, handler_suspend);

    pthread_mutex_init(&bg_pids_mutex, NULL);
    nb_bg_pids = 0;

    while (1) {
        struct cmdline *l;

        printf("shell> ");
        l = readcmd();

        /* If input stream closed, normal termination */
        if (!l) {
            pthread_mutex_destroy(&bg_pids_mutex);

            for (int i = 0; i < nb_bg_pids; i++) {
                for (int j = 0; j < bg_processes[i].cmd_size; j++) {
                    free(bg_processes[i].command[j]);
                }

                free(bg_processes[i].command);
            }

            free(bg_processes);
            printf("exit\n");
            exit(EXIT_SUCCESS);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        if (exec_shell_cmd(l) == 0) {
            exec_cmd(l);
        }
    }
}
