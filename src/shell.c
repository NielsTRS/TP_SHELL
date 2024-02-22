#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

pid_t *bg_pids;
int nb_bg_pids = 0;

void handler_chld(int sig) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        // suppresion des processus déja fini
        for (int i = 0; i < nb_bg_pids; i++) {
            if (bg_pids[i] == pid) {
                for (int j = i; j < nb_bg_pids - 1; j++) {
                    bg_pids[j] = bg_pids[j + 1];
                }
                nb_bg_pids--;
                break;
            }
        }
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
        printf("\n%d processus en arrière plan\n", nb_bg_pids);
        for (int i = 0; i < nb_bg_pids; i++) {
            printf("[%d] %d\n", i + 1, bg_pids[i]);
        }
        return 1;
    }
    return 0;
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

        if (pids[i] == 0) {
            if (i != 0) { // pas la première commande
                if (Dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("Erreur lors de la redirection de l'entrée");
                    exit(EXIT_FAILURE);
                }
            } else { // première commande
                redirect_in(l);
            }

            if (i != nb - 1) { // pas la dernière commande
                if (Dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("Erreur lors de la redirection de la sortie");
                    exit(EXIT_FAILURE);
                }
            } else { // dernière commande
                redirect_out(l);
            }

            for (int i = 0; i < nb - 1; i++) {
                Close(pipes[i][0]);
                Close(pipes[i][1]);
            }

            // changement de groupe
            if(l->bg){
                Setpgid(0, 0);
            }

            char **cmd = l->seq[i];
            if (execvp(cmd[0], cmd) == -1) {
                perror(cmd[0]);
                exit(EXIT_FAILURE);
            }
        } else {
            if (l->bg) {
                printf("[%d] %d\n", i + 1, pids[i]);
                bg_pids = realloc(bg_pids, (nb_bg_pids + 1) * sizeof(pid_t));
                bg_pids[nb_bg_pids++] = pids[i];
            }
        }
    }

    // fermeture des pipes
    for (int i = 0; i < nb - 1; i++) {
        Close(pipes[i][0]);
        Close(pipes[i][1]);
    }

    if (!l->bg) {
        for (int i = 0; i < nb; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }

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

    while (1) {
        struct cmdline *l;

        printf("shell> ");
        l = readcmd();

        /* If input stream closed, normal termination */
        if (!l) {
            free(bg_pids);
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
