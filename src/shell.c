#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

void exec_shell_cmd(struct cmdline *l) {
    char **cmd = l->seq[0];
    if (strcmp(cmd[0], "quit") == 0) { // commande intégrée au shell
        exit(EXIT_SUCCESS);
    }
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
            fprintf(stderr, "%s: %s\n", l->in, errno == ENOENT ? "Fichier inexistant" : "Permission refusée");
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
            fprintf(stderr, "%s: %s\n", l->out,
                    errno == EACCES ? "Permission refusée" : "Erreur lors de l'ouverture du fichier de sortie");
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
    }

    for (int i = 0; i < nb; i++) {
        pids[i] = Fork();

        if (pids[i] == -1) {
            perror("Erreur lors de la création du processus fils");
            exit(EXIT_FAILURE);
        }

        if (pids[i] == 0) {
            char **cmd = l->seq[i];
            redirect_in(l);
            redirect_out(l);
            execvp(cmd[0], cmd);

            fprintf(stderr, "%s: commande non trouvée\n", cmd[0]); // Si execvp échoue
            exit(EXIT_FAILURE);
        } else {
            waitpid(pids[i], NULL, 0);
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
            exit(EXIT_SUCCESS);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        exec_shell_cmd(l);
        exec_cmd(l);
    }
}
