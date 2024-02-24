#include <stdio.h>
#include <unistd.h>

int main() {
    while (1) {
        printf("Le programme est au premier plan...\n");
        sleep(1); // Attendez 1 seconde avant de répéter
    }

    return 0;
}
