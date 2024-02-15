#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"

void redirect_in(struct cmdline *l) {
    if (l->in != NULL) {
        int fd_in = open(l->in, O_RDONLY);
        if (fd_in == -1) {
            fprintf(stderr, "%s: %s\n", l->in, errno == ENOENT ? "Fichier inexistant" : "Permission denied");
            exit(EXIT_FAILURE);
        }
        dup2(fd_in, 0);
        close(fd_in);
    }
}

void redirect_out(struct cmdline *l) {
    if (l->out != NULL) {
        int fd_out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out == -1) {
            fprintf(stderr, "%s: %s\n", l->out,
                    errno == EACCES ? "Permission refusée" : "Erreur lors de l'ouverture du fichier de sortie");
            exit(EXIT_FAILURE);
        }
        dup2(fd_out, 1);
        close(fd_out);
    }
}

void exec_cmd(struct cmdline *l) {
    for (int i = 0; l->seq[i] != 0; i++) {
        char **cmd = l->seq[i];

        if (strcmp(cmd[0], "quit") == 0) { // commande intégrée au shell
            exit(EXIT_SUCCESS);
        } else { // commande à exécuter
            int pid = Fork();
            if (pid == -1) {
                perror("Erreur lors de la création du processus fils");
                exit(1);
            } else if (pid == 0) { // fils
                redirect_in(l);
                redirect_out(l);

                execvp(cmd[0], cmd);
                fprintf(stderr, "%s: commande non trouvée\n", cmd[0]); // Si execvp échoue

                exit(EXIT_FAILURE);
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
            exit(EXIT_SUCCESS);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        exec_cmd(l);
    }
}
