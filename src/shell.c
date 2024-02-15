/*
 * Copyright (C) 2002, Simon Nieuviarts
 */

#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

void execcmd(struct cmdline *l) {

    for (int i = 0; l->seq[i] != 0; i++) {
        char **cmd = l->seq[i];
        if (strcmp(cmd[0], "quit") == 0) {
            exit(0);
        } else {
            int pid = Fork();
            if (pid == -1) {
                perror("Erreur lors de la création du processus fils");
                exit(1);
            } else if (pid == 0) { // fils
                if (l->in != NULL) {
                    int fd_in = open(l->in, O_RDONLY);
                    if (fd_in == -1) {
                        fprintf(stderr, "%s: %s\n", l->in, errno == ENOENT ? "Fichier inexistant" : "Permission denied");
                        exit(1);
                    }
                    dup2(fd_in, 0);
                    close(fd_in);
                }
                if (l->out != NULL) {
                    int fd_out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd_out == -1) {
                        fprintf(stderr, "%s: %s\n", l->out, errno == EACCES ? "Permission denied" : "Erreur lors de l'ouverture du fichier de sortie");
                        exit(1);
                    }
                    dup2(fd_out, 1);
                    close(fd_out);
                }

                execvp(cmd[0], cmd);
                fprintf(stderr, "%s: command not found\n", cmd[0]); // Si execvp échoue
                exit(0);
            } else { // père
                waitpid(pid, NULL, 0);
            }
        }
    }
}

int main() {
    while (1) {
        struct cmdline *l;

        printf("shell> ");
        l = readcmd();

        /* If input stream closed, normal termination */
        if (!l) {
            printf("exit\n");
            exit(0);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        if (l->in) printf("in: %s\n", l->in);
        if (l->out) printf("out: %s\n", l->out);

        /* Display each command of the pipe */
        execcmd(l);
    }
}
