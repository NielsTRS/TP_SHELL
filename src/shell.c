#include <stdio.h>
#include <stdlib.h>
#include "readcmd.h"
#include "csapp.h"
#include <pthread.h>

// constante sur l'état des processus
#define FINISHED_STATE 0
#define RUNNING_STATE 1
#define SUSPENDED_STATE -1

// Structure pour les processus
typedef struct {
    pid_t pid;
    int state;
    char **command;
    int cmd_size;
} Process;

typedef struct {
    Process *processes;
    int nb;
    pthread_mutex_t mutex; // gestionnaire des sections critiques
} ProcessesList;

ProcessesList *bg_processes; // arrière plan
ProcessesList *fg_processes; // premier plan

// handler du SIGCHLD
void handler_chld(int sig) {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        pthread_mutex_lock(&bg_processes->mutex); // section critique
        // mises à jours
        for (int i = 0; i < bg_processes->nb; i++) {
            if (bg_processes->processes[i].pid == pid) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    bg_processes->processes[i].state = FINISHED_STATE;
                } else if (WIFSTOPPED(status)) {
                    bg_processes->processes[i].state = SUSPENDED_STATE;
                }
            }
        }
        pthread_mutex_unlock(&bg_processes->mutex); // fin de section critique

        pthread_mutex_lock(&fg_processes->mutex);
        // mises à jours
        for (int i = 0; i < fg_processes->nb; i++) {
            if (fg_processes->processes[i].pid == pid) {
                if (WIFEXITED(status) || WIFSIGNALED(status)) {
                    fg_processes->processes[i].state = FINISHED_STATE;
                } else if (WIFSTOPPED(status)) {
                    fg_processes->processes[i].state = SUSPENDED_STATE;
                }
            }
        }
        pthread_mutex_unlock(&fg_processes->mutex);
    }
}

// handler du SIGINT
void handler_stop(int sig) {
    pthread_mutex_lock(&fg_processes->mutex);
    for (int i = 0; i < fg_processes->nb; i++) {
        fg_processes->processes[i].state = FINISHED_STATE;
        kill(fg_processes->processes[i].pid, SIGINT);
    }
    pthread_mutex_unlock(&fg_processes->mutex);
    printf("\n");
}

// handle du SIGTSTP
void handler_suspend(int sig) {
    pthread_mutex_lock(&fg_processes->mutex);
    for (int i = 0; i < fg_processes->nb; i++) {
        fg_processes->processes[i].state = SUSPENDED_STATE;
        kill(fg_processes->processes[i].pid, SIGTSTP);
    }
    pthread_mutex_unlock(&fg_processes->mutex);
    printf("\n");
}

// fonction qui traite les commandes intégrées au shell
int exec_shell_cmd(struct cmdline *l) {
    char **cmd = l->seq[0];
    if (strcmp(cmd[0], "quit") == 0) {
        exit(EXIT_SUCCESS);
    }
    if (strcmp(cmd[0], "jobs") == 0) {
        pthread_mutex_lock(&bg_processes->mutex);
        for (int i = 0; i < bg_processes->nb; i++) {
            printf("[%d] %d", i + 1, bg_processes->processes[i].pid);
            if (bg_processes->processes[i].state == RUNNING_STATE) {
                printf(" En cours d'éxécution ");
            } else {
                printf(" Arrêté ");
            }

            char **c = bg_processes->processes[i].command;
            for (int j = 0; j < bg_processes->processes[i].cmd_size; j++) {
                printf("%s ", c[j]);
            }

            printf("\n");
        }
        pthread_mutex_unlock(&bg_processes->mutex);
        return 1;
    }
    if (strcmp(cmd[0], "stop") == 0) {
        pthread_mutex_lock(&bg_processes->mutex);
        pid_t pid_to_stop;
        if (cmd[1] != NULL) {
            if (strncmp(cmd[1], "%", 1) == 0) { // cas de % donc id du job
                int job_id = atoi(cmd[1] + 1);
                if (job_id > 0 && job_id <= bg_processes->nb) {
                    pid_to_stop = bg_processes->processes[job_id - 1].pid;
                } else {
                    printf("Identifiant invalide\n");
                }
            } else { // cas du pid
                pid_to_stop = atoi(cmd[1]);
            }

            if (kill(pid_to_stop, SIGTERM) == 0) {
                printf("Arrêt du processus %d en arrière-plan\n", pid_to_stop);
            } else {
                perror("kill");
            }
        } else {
            printf("La commande stop prend 1 argument \n");
        }

        pthread_mutex_unlock(&bg_processes->mutex);
        return 1;
    }
    if (strcmp(cmd[0], "fg") == 0) {
        pthread_mutex_lock(&bg_processes->mutex);
        if (cmd[1] != NULL) {
            if (bg_processes->nb > 0 && bg_processes->processes[bg_processes->nb - 1].state == RUNNING_STATE) {
                Process *new;
                if (strncmp(cmd[1], "%", 1) == 0) { // cas de % donc id du job
                    int job_id = atoi(cmd[1] + 1);
                    if (job_id > 0 && job_id <= bg_processes->nb) {
                        new = &bg_processes->processes[job_id - 1];
                    } else {
                        printf("Identifiant invalide\n");
                    }
                } else { // cas du pid
                    for (int i = 0; i < bg_processes->nb; i++) {
                        if (bg_processes->processes[i].pid == atoi(cmd[1])) {
                            new = &bg_processes->processes[i];
                            break;
                        }
                    }
                }

                // supprime le processus en arrière plan
                for (int i = 0; i < bg_processes->nb - 1; i++) {
                    bg_processes->processes[i] = bg_processes->processes[i + 1];
                }
                bg_processes->nb--;

                // Ajoute le processus au premier plan
                pthread_mutex_lock(&fg_processes->mutex);
                fg_processes->processes = realloc(fg_processes->processes, (fg_processes->nb + 1) * sizeof(Process));
                fg_processes->processes[fg_processes->nb] = *new;
                fg_processes->nb++;
                pthread_mutex_unlock(&fg_processes->mutex);

                //Setpgid(new->pid, Getpgrp()); // renvoie une erreur permission refusée
                if (kill(new->pid, SIGCONT) == 0) {
                    waitpid(new->pid, NULL, 0);
                } else {
                    printf("Erreur lors de la reprise du processus %d\n", new->pid);
                }
            } else {
                printf("Aucune tache existante\n");
            }

        } else {
            printf("La commande stop prend 1 argument \n");
        }

        pthread_mutex_unlock(&bg_processes->mutex);
        return 1;
    }

    return 0;
}

// donne la taille de la commande
int cmd_size(char **cmd) {
    int nb = 0;
    while (cmd[nb] != NULL) {
        nb++;
    }
    return nb;
}

// compte le nombre de commande
int count_cmd(struct cmdline *l) {
    int nb = 0;
    while (l->seq[nb]) {
        nb++;
    }
    return nb;
}

// redirection entrante
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

// redirection sortante
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

// fonction qui traite les commandes qui ne sont pas intégrées au shell
void exec_cmd(struct cmdline *l) {
    int nb = count_cmd(l);

    // allocation mémoire
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
            if (l->bg) {
                Setpgid(0, 0);
            }

            char **cmd = l->seq[i];
            // execution commande
            if (execvp(cmd[0], cmd) == -1) {
                perror(cmd[0]);
                exit(EXIT_FAILURE);
            }
        } else { // père
            if (l->bg) {
                // Sauvegarde dans la structure des processus en arrière plan
                pthread_mutex_lock(&bg_processes->mutex);
                printf("[%d] %d\n", bg_processes->nb + 1, pids[i]);

                char **cmd = l->seq[i];
                int cmd_length = cmd_size(cmd);

                bg_processes->processes = realloc(bg_processes->processes, (bg_processes->nb + 1) * sizeof(Process));
                bg_processes->processes[bg_processes->nb].command = malloc((cmd_length + 1) * sizeof(char *));

                bg_processes->processes[bg_processes->nb].pid = pids[i];
                bg_processes->processes[bg_processes->nb].pid = pids[i];
                bg_processes->processes[bg_processes->nb].state = RUNNING_STATE;
                bg_processes->processes[bg_processes->nb].cmd_size = cmd_length;

                for (int j = 0; j < cmd_length; j++) {
                    bg_processes->processes[bg_processes->nb].command[j] = strdup(cmd[j]);
                }
                bg_processes->processes[bg_processes->nb].command[cmd_length] = NULL;

                bg_processes->nb++;
                pthread_mutex_unlock(&bg_processes->mutex);
            } else {
                // sauvegarde dans la structure des processus au premier plan
                pthread_mutex_lock(&fg_processes->mutex);

                char **cmd = l->seq[i];
                int cmd_length = cmd_size(cmd);

                fg_processes->processes = realloc(fg_processes->processes, (fg_processes->nb + 1) * sizeof(Process));
                fg_processes->processes[fg_processes->nb].command = malloc((cmd_length + 1) * sizeof(char *));

                fg_processes->processes[fg_processes->nb].pid = pids[i];
                fg_processes->processes[fg_processes->nb].pid = pids[i];
                fg_processes->processes[fg_processes->nb].state = RUNNING_STATE;
                fg_processes->processes[fg_processes->nb].cmd_size = cmd_length;

                for (int j = 0; j < cmd_length; j++) {
                    fg_processes->processes[fg_processes->nb].command[j] = strdup(cmd[j]);
                }
                fg_processes->processes[fg_processes->nb].command[cmd_length] = NULL;

                fg_processes->nb++;
                pthread_mutex_unlock(&fg_processes->mutex);
            }
        }
    }

    // père

    // fermeture des pipes
    for (int i = 0; i < nb - 1; i++) {
        Close(pipes[i][0]);
        Close(pipes[i][1]);
    }

    // Problème : partie bloquante lorsqu'on fait un Ctrl+Z
    if (!l->bg) {
        for (int i = 0; i < fg_processes->nb; i++) {
            if (fg_processes->processes[i].state != SUSPENDED_STATE) {
                waitpid(pids[i], NULL, 0);
            }
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
    // les signaux
    Signal(SIGCHLD, handler_chld);
    Signal(SIGINT, handler_stop);
    Signal(SIGTSTP, handler_suspend);

    // initialisation des structures
    bg_processes = malloc(sizeof(ProcessesList));
    bg_processes->nb = 0;
    pthread_mutex_init(&bg_processes->mutex, NULL);

    fg_processes = malloc(sizeof(ProcessesList));
    fg_processes->nb = 0;
    pthread_mutex_init(&fg_processes->mutex, NULL);


    while (1) {
        struct cmdline *l;

        printf("shell> ");
        l = readcmd();

        /* If input stream closed, normal termination */
        if (!l) {
            // libération de la mémoire et arrêt du gestionnaire de sections critiques

            // arrière plan
            pthread_mutex_destroy(&bg_processes->mutex);

            for (int i = 0; i < bg_processes->nb; i++) {
                for (int j = 0; j < bg_processes->processes[i].cmd_size; j++) {
                    free(bg_processes->processes[i].command[j]);
                }

                free(bg_processes->processes[i].command);
            }

            free(bg_processes->processes);
            free(bg_processes);


            // premier plan
            pthread_mutex_destroy(&fg_processes->mutex);

            for (int i = 0; i < fg_processes->nb; i++) {
                for (int j = 0; j < fg_processes->processes[i].cmd_size; j++) {
                    free(fg_processes->processes[i].command[j]);
                }

                free(fg_processes->processes[i].command);
            }

            free(fg_processes->processes);
            free(fg_processes);

            printf("exit\n");
            exit(EXIT_SUCCESS);
        }

        if (l->err) {
            /* Syntax error, read another command */
            printf("error: %s\n", l->err);
            continue;
        }

        if (exec_shell_cmd(l) == 0) { // si ce n'est pas une commande intégrée au shell
            exec_cmd(l);
        }
    }
}
