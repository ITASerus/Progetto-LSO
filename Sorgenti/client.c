/************************************************************************************************************************
*                                                                                                                       *
*                                                 SIMPLE PUBLIC LEDGER                                                  *
*                                                      (2018/2019)                                                      *
*                                                                                                                       *
*                                            Ernesto De Crecchio (N86001596)                                            *
*                                                                                                                       *
*************************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/uio.h>
#include <strings.h>
#include <arpa/inet.h>

#define MAXBUFFER 200

int main (int args, char *argv[]) {
	system("clear");

	char buffer[MAXBUFFER];

	char *ipServer = argv[1];
	char *port = argv[2];
	char *command = argv[3];

	//Controllo parametri
	if ( args == 6 && strcmp(command, "store") == 0) { //Controllo validità comando 'store' (richiede 2 parametri)
		write(STDOUT_FILENO, "* Comando STORE *\n", 18);

		sprintf(buffer, "Chiave: %s\nValore: %s\n\n", argv[4], argv[5]);
		write(STDOUT_FILENO, buffer, strlen(buffer));

		bzero(buffer, MAXBUFFER);

		sprintf(buffer, "store(%s,%s)", argv[4], argv[5]);
	} else if ( args == 6 && strcmp(command, "corrupt") == 0) { //Controllo validità comando 'corrupt' (richiede 2 parametri)
		write(STDOUT_FILENO, "* Comando CORRUPT *\n", 20);

		sprintf(buffer, "Chiave: %s\nNuovo Valore: %s\n\n", argv[4], argv[5]);
		write(STDOUT_FILENO, buffer, strlen(buffer));

		bzero(buffer, MAXBUFFER);

		sprintf(buffer, "corrupt(%s,%s)", argv[4], argv[5]);
	} else if (args == 5 && strcmp(command, "search") == 0) { //Controllo validità comando 'search' (richiede 1 parametro)
		write(STDOUT_FILENO, "* Comando SEARCH *\n", 19);

		sprintf(buffer, "Chiave: %s\n\n", argv[4]);
		write(STDOUT_FILENO, buffer, strlen(buffer));
	
		bzero(buffer, MAXBUFFER);

		sprintf(buffer, "search(%s)", argv[4]);
	} else if (args == 4 && strcmp(command, "list") == 0) { //Controllo validità comando 'list' (richiede 0 parametri)
		write(STDOUT_FILENO, "* Comando LIST *\n", 17);

		sprintf(buffer, "list()");
	} else {
		write(STDOUT_FILENO, "[-] ERRORE! Comando non trovato o parametri errati.\n", 52);
		exit(-1);
	}

	///// INIZIALIZZAZIONE SOCKET /////
	int sockFd; // Socket file descriptor
	struct sockaddr_in serverAddr;

	//Apertura del socket client
	if((sockFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
		perror("[-] ERRORE Socket!"), exit(-1);

	//Preparazione dell'indirizzo del socket
	memset(&serverAddr, '\0', sizeof(serverAddr)); //Inizializza la struttura di tipo sockaddr_in a valori "nulli"
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(atoi(port)); 
	serverAddr.sin_addr.s_addr = inet_addr(ipServer);

	//Connessione del socket al server
	if(connect(sockFd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) < 0) {
		perror("[-] ERRORE Connect!"), exit(-1);
	} else {
		write(STDOUT_FILENO, "\n[+] Connessione stabilita.\n", 28);
	}

	write(sockFd, buffer, strlen(buffer));

	read(sockFd, buffer, MAXBUFFER -1);
	if(strcmp("RIFIUTATO", buffer) == 0) {	
		write(STDOUT_FILENO, "[-] ATTENZIONE! Il server è ancora in fase di startup.\n", 56);
		exit(-1);
	}

	write(STDOUT_FILENO, "\n-----------------------------------\n\n\n* RISULTATO *\n", 53);
	bzero(buffer, MAXBUFFER);
	while(read(sockFd, buffer, MAXBUFFER -1) > 0) {
		write(STDOUT_FILENO, buffer, strlen(buffer));
		bzero(buffer, MAXBUFFER);
	}
	
	close(sockFd);
}