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
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <strings.h>
#include <pthread.h>
#include <signal.h>

#define MAXBUFFER 200

// - STRUCTS -
//Struct contenente i nodi del ledger salvati sul server attuale
struct nodeList {
	char *key;
	char *value;
	struct nodeList *nextNode;
};

//Struct contenente le informazioni degli altri server a cui collegarsi per l'inoltro dei messaggi
struct serverParameters {
	char* ip;
	char* port;

	struct serverParameters *nextServer;
};

// - PROTOTIPI FUNZIONI PRIMARIE -
void startupConfig(char *fileName, int sockServer);
void *clientHandler(void *newSockFd);
void *serverHandler(void *parameters);
void commandHandler(char* command, int socket);
void signalHandler(int segnale);

// - PROTOTIPI FUNZIONI AUSILIARIE -
void stringSlicer(char* buffer, char *command, char *key, char *value);
void addNodeList(struct nodeList **head, char* newkey, char* newValue);
void addServerList(struct serverParameters **head, char* newIp, char* newPort);
void printList(struct nodeList *head, int where);
void corruptNode(struct nodeList *head, char *key, char *newValue);
char* findValue(struct nodeList *head, char *key);
int isStored(struct nodeList *head, char *key);
void destroyServerList(struct serverParameters *head);
void destroyNodeList(struct nodeList *head);

// - VARIABILI GLOBALI -
int *socketOthServer;
int serverNumber;
struct nodeList *head = NULL;
struct serverParameters *serverSockets = NULL;

// - MUTEX -
pthread_mutex_t commandHandlerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t clientHandlerMutex = PTHREAD_MUTEX_INITIALIZER;

// - MAIN -
int main (int args, char *argv[]) {
	signal(SIGINT, signalHandler); // Listener segnale SIGINT
	signal(SIGTERM, signalHandler); // Listener segnale SIGTERM

	system("clear"); // Pulisce il terminale dai vecchi comandi

	// Dichiarazione variabili locali
	int sockFd, newsockFd, checkValue;
	struct sockaddr_in serverAddress, clientAddress;
	socklen_t addrSize = sizeof(clientAddress);
	char *buf = malloc(sizeof(char)*MAXBUFFER);

	// Verifica corretto inserimento parametri di input
	if(args < 3 || args > 3 ) {
		write(STDOUT_FILENO, "[-] ERRORE! Numero di parametri errato.\n", 40), exit(-1);
	} else if (argv[1] == NULL || argv[2] == NULL || (strlen(argv[2]) > 4)) write(STDOUT_FILENO, "[-] ERRORE! Ricompilare indicando il file di configurazione e la porta da utilizzare.\n", 86), exit(-1);

	// Creazione socket per la connessione server-client
	if ((sockFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) perror("[-] ERRORE Socket!"), exit(-1);
	write(STDOUT_FILENO, "[+] Server socket creato correttamente!\n", 40);

	// Settaggio parametri struttura sockaddr_in
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(atoi(argv[2]));
	serverAddress.sin_addr.s_addr = inet_addr("127.000.000.001");

	// Assegnamento nome al socket
	if ((checkValue = bind(sockFd, (struct sockaddr *) &serverAddress, sizeof(serverAddress))) == -1) perror("[-] ERRORE Bind!"), exit(-1);
	sprintf(buf, "[+] Connesso correttamente alla porta %d\n", atoi(argv[2]));
	write(STDOUT_FILENO, buf, strlen(buf));
	bzero(buf, MAXBUFFER);

	// Socket in ascolto
	if (listen(sockFd, 10) == -1) perror("[-] ERRORE Listen!"), exit(-1);
	write(STDOUT_FILENO, "\nServer pronto...\n\nIn attesa di collegamento con gli altri server...\n\n", 70);
	
	startupConfig(argv[1], sockFd); // Connessione (in fase di avvio) tra i vari server

	write(STDOUT_FILENO, "\n-----------\nIn attesa di un client...\n\n", 40);
	while (1) {
		// Accettazione connessioni in ingresso dai client
		if ((newsockFd = accept(sockFd, (struct sockaddr *) &clientAddress, &addrSize)) == -1) perror("[-] ERRORE Accept!"), exit(-1);
		write(newsockFd, "ACCETTATO\0", 10);
		
		pthread_t tid;
		pthread_create(&tid, NULL, clientHandler, &newsockFd);
		pthread_join(tid, NULL);

		if ((checkValue = close(newsockFd)) == -1) perror("[-] ERRORE Close!"), exit(-1);
	}

	// Chiusura socket
	if ((checkValue = close(sockFd)) == -1) perror("[-] ERRORE Close!"), exit(-1);

	// Deallocazione memoria
	free(buf);
	
	return 0;
}

/************************************************************************************************************************
*                                                                                                                       *
*                                                  FUNZIONI PRIMARIE                                                    *
*                                                                                                                       *
*************************************************************************************************************************/

/* 
- FUNZIONE STARTUPCONFIG -
Si occupa della fase di startup dei server, analizzando il file di configurazione contenente le informazioni dei server a cui collegarsi, 
creando nuovi thread per la connessione agli altri server e gestendo le richieste di connessione in entrata.
-- Paramentri:
- fileName: Nome del file di configurazione contenente le informazioni degli altri server.
- sockServer: File descriptor del socket tramite il quale accettare le nuove connessioni.
-- Valore di ritorno: Nessuno.
*/
void startupConfig(char *fileName, int sockServer) {
	int configfd; //File descriptor del file di configurazione
	char *buf = malloc(sizeof(char)*MAXBUFFER);
	
	if ((configfd = open(fileName, O_RDONLY)) == -1) perror("[-] ERRORE Open!"), exit(-1);
	off_t endPosition = lseek(configfd, 0, SEEK_END);
	serverNumber = endPosition/21; //Il numero dei server è dato dividendo i byte totali contenuti nel file per 21 in quanto ogni riga, rappresentante un server, contiene esattamente 21 caratteri
	off_t currentPosition = lseek(configfd, 0, SEEK_SET);

	char ip[16];
	char port[5];

	//Ciclo che analizza il file di configurazione riga per riga gestendo ogni server opportunamente
	while(currentPosition < endPosition) {
		read(configfd, buf, 20);
		
		//Salva nella stringa 'ip' l'ip del server alla riga del file corrente
		for(int i=0; i<15; i++) {
			ip[i] = buf[i];
		}
		ip[15] = '\0';

		//Salva nella stringa 'port' la porta del server alla riga del file corrente
		for(int j=16; j<20; j++) {
			port[j-16] = buf[j];
		}
		port[4] = '\0';

		currentPosition = lseek(configfd, 1, SEEK_CUR);

		addServerList(&serverSockets, ip, port); //Aggiunge alla lista dei server a cui il server chiamante dovrà collegarsi, il server appena "estrapolato" dal file di configurazione
		
		bzero(buf, MAXBUFFER);
	}

	//Per ogni server salvato, viene aperto il thread apposito
	struct serverParameters *refServer = serverSockets;
	while(refServer != NULL) {
		pthread_t serverTid;

		pthread_create(&serverTid, NULL, serverHandler, (void *)refServer);

		refServer = refServer->nextServer;
	}

	socketOthServer = malloc(sizeof(int)*serverNumber);
	struct sockaddr_in serverClientAddress;
	socklen_t addrSize = sizeof(serverClientAddress);

	int newsockFd;
	int serverConnected = 0;

	bzero(buf, MAXBUFFER); 

	//Ciclo che si mette in attesa di richiesta di connessione da parte di tutti i server
	while(serverConnected <= serverNumber) {
		if ((newsockFd = accept(sockServer, (struct sockaddr *) &serverClientAddress, &addrSize)) == -1) {
			perror("[-] ERRORE Accept!");
			exit(-1);
		} else {
			//Ogni connessione in entrata viene analizzata ed effettivamente accettata solamente se viene ricevuta la stringa 'SERVER' che evita che un client riesca ad inviare un comando prima del docuto
			read(newsockFd, buf, MAXBUFFER);
			if(strcmp("SERVER", buf) != 0) {
				write(STDOUT_FILENO, "[-] Connessione in entrata rifiutata.\n", 38);
				write(newsockFd, "RIFIUTATO\0", 10);
				close(newsockFd);
			} else {
				//Se la connessione viene accettata, il socket corrispondente viene salvato nell'array socketOthServer per utilizzi futuri
				socketOthServer[serverConnected] = newsockFd;
				serverConnected++;

				sprintf(buf, "[+] Connessione in entrata accettata (%d/%d)\n", serverConnected, serverNumber+1);
				write(STDOUT_FILENO, buf, strlen(buf));
			}
		}
		bzero(buf, MAXBUFFER); 
	}

	write(STDOUT_FILENO, "[+] Collegamento con tutti i server effettuato con successo.\n", 61);

	free(buf);
	close(configfd);
}

/* 
- FUNZIONE CLIENTHANDLER -
Gestisce l'intera sessione tra client e server eseguendo i vari comandi e inoltrandoli agli altri server quando necessario.
-- Paramentri:
- newSockFd: Il socket file descriptor col quale vengono effettuati gli scambi di messaggio tra il client e il server al quale si è connesso.
-- Valore di ritorno: Nessuno.
*/
void *clientHandler(void *newSockFd) {
	pthread_mutex_lock(&clientHandlerMutex);

	int sockFd = *(int *)newSockFd;

	char *buf = malloc(sizeof(char)*MAXBUFFER); //Buffer di servizio per la comunicazione dei messaggi
	char *command = malloc(sizeof(char)*MAXBUFFER); //Buffer contenente il comando arrivato dal client
	bzero(buf, MAXBUFFER);
	
	read(sockFd, command, MAXBUFFER);

	//Gestisco il comando per il server attuale
	char *commandType = malloc(sizeof(char)*MAXBUFFER);
	char *key = malloc(sizeof(char)*MAXBUFFER);
	char *value = malloc(sizeof(char)*MAXBUFFER);

	stringSlicer(command, commandType, key, value); //Il comando viene suddiviso nelle opportune sottostringhe
	
	if(strcmp(commandType, "store") == 0) { //Comando 'store' arrivato
		if(isStored(head, key) == 0) { //Controlla se esiste già nel ledger una coppia con chiave uguale a quella che il client ha chiesto di memorizzate
			addNodeList(&head, key, value);

			sprintf(buf, "[+] Coppia (%s,%s) memorizzata.\n", key, value);
			write(STDOUT_FILENO, buf, strlen(buf));
			write(sockFd, buf, strlen(buf));

			//Inoltra il comando a tutti gli altri server
			for(int i=0; i<=serverNumber; i++) {
				write(socketOthServer[i], command, strlen(command));
			}
		} else {
			sprintf(buf, "[-] Chiave '%s' gia' presente nel ledger.\n", key);
			write(STDOUT_FILENO, buf, strlen(buf));
			write(sockFd, buf, strlen(buf));
		}
		bzero(buf, MAXBUFFER);
	} else if(strcmp(commandType, "corrupt") == 0) { //Comando 'corrupt' arrivato
		if(isStored(head, key) == 0) { //Controlla se esiste nel ledger una coppia con chiave uguale a quella che il client ha chiesto di corrompere
			sprintf(buf, "[-] Chiave '%s' non presente nel ledger.\n", key);
			write(STDOUT_FILENO, buf, strlen(buf));
			write(sockFd, buf, strlen(buf));
		} else {
			corruptNode(head, key, value);

			write(STDOUT_FILENO, "[+] Coppia corrotta\n", 20);
			write(sockFd, "[+] Coppia corrotta\n", 20);
		}
	} else if (strcmp(commandType, "search") == 0) { //Comando 'search' arrivato
		if(isStored(head, key) == 0) {  //Controlla se esiste nel ledger una coppia con chiave uguale a quella che il client ha chiesto di cercare
			sprintf(buf, "[-] Chiave %s assente.\n", key);
			write(STDOUT_FILENO, buf, strlen(buf));
			write(sockFd,  buf, strlen(buf));
		} else {
			//Viene generata opportunamente una stringa contenente solo la chiave e il valore della coppia da cercare
			char* node = malloc(sizeof(char)*MAXBUFFER);
			strcat(node, "(");
			strcat(node, key);
			strcat(node, ",");
			strcat(node, findValue(head, key));
			strcat(node, ")");

			int ledgerCorrupted = 1;
			//Inoltra il comando a tutti gli altri server
			for(int i=0; i<=serverNumber; i++) {
				write(socketOthServer[i], command, strlen(command));
				read(socketOthServer[i], buf, MAXBUFFER); //Il server avrà restituito una stringa del tipo '(chiave,valore)' con i dati contenuti in esso
				
				if(strcmp(node, buf) != 0) { //Per ogni coppia che gli altri server reinviano al server inoltrante, viene controllata la validità
					ledgerCorrupted = 0;
					break; //Se una copppia ritornata dal server è diversa da quella attuale, allora tale coppia è stata corrotta
				}
				bzero(buf, MAXBUFFER);
			}

			//Se la coppia è stata corrotta, viene mostrato il messaggio 'Ledger Corrotto', altrimenti viene mostrata la coppia trovata
			if(ledgerCorrupted == 0) {
				write(sockFd, "Ledger Corrotto.\n", 18);
			} else {
				strcat(node, "\n");
				write(sockFd, node, strlen(node));
			}
			free(node);
		}
	} else {
		write(STDOUT_FILENO, "* ELENCO COPPIE *\n", 18);
		printList(head, STDOUT_FILENO);
		printList(head, sockFd);
	}

	free(buf);
	free(commandType);
	free(key);
	free(value);

	pthread_mutex_unlock(&clientHandlerMutex);
	pthread_exit(NULL);
}

/* 
- FUNZIONE SERVERHANDLER -
Gestisce le connessioni con gli altri server analizzando i comandi inoltrati da parte di altri server
-- Paramentri:
- parameters: Nodo della linked list serverParameters contenente le informazioni del server a cui collegarsi
-- Valore di ritorno: Nessuno.
*/
void *serverHandler(void *parameters) {
	struct serverParameters *server = (struct serverParameters *) parameters;

	char *buf = malloc(sizeof(char)*MAXBUFFER);

	int sockFd;
	struct sockaddr_in serverAddr;

	//Preparazione dell'indirizzo del socket
	memset(&serverAddr, '\0', sizeof(serverAddr)); //Inizializza la struttura di tipo sockaddr_in a valori "nulli"
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(atoi(server->port)); 
	serverAddr.sin_addr.s_addr = inet_addr(server->ip);

	//Ciclo che si ripete fin quando la connessione col server destinatario non è andata a buon fine
	int checkValue;
	do {
		if ((sockFd = socket(AF_INET, SOCK_STREAM, 0)) == -1) perror("[-] ERRORE Socket!"), exit(-1);

		//Connessione del socket al server
		if((checkValue = connect(sockFd, (struct sockaddr*) &serverAddr, sizeof(serverAddr))) != 0) {
			close(sockFd);
		}
	} while (checkValue != 0);

	write(sockFd, "SERVER", 6); //Messaggio inviato per avvisare il server a cui ci si sta collegando che si è uno dei server "autorizzati" alla connessione

	int nBytes;
	while(1) {
		bzero(buf, MAXBUFFER);
		nBytes = read(sockFd, buf, MAXBUFFER); //Legge il comando arrivato da uno degli altri server

		if(nBytes > 0) {
			if (strcmp(buf, "Server chiuso") == 0) { //Se il comando è 'Server chiuso' allora uno dei server è stato chiuso (volutamente o meno) ed è necessario chiudere anche quello attuale
				write(STDOUT_FILENO, "[+] Processo terminato dall'utente.\n", 36);
				close(sockFd);
				exit(-1);
			}
	
			commandHandler(buf, sockFd); //Se non è stata richiesta la chiusura del server, viene gestito il comando effettivo
		}
	}
	
	pthread_exit(NULL);
}

/* 
- FUNZIONE COMMANDHANDLER -
Gestisce il comando inoltrato da parte di un altro server.
-- Paramentri:
- command: Stringa contenente l'intero comando inoltrato da un altro server. Della forma comando([valore][,][valore]).
- socket: Socket descriptor utilizzato per rispondere al server da cui è arrivato il comando (necessario per il comando 'search').
-- Valore di ritorno: Nessuno.
*/
void commandHandler(char* command, int socket) {
	pthread_mutex_lock(&commandHandlerMutex);

	char *buf = malloc(sizeof(char)*MAXBUFFER);
	char *commandType = malloc(sizeof(char)*MAXBUFFER);
	char *key = malloc(sizeof(char)*MAXBUFFER);
	char *value = malloc(sizeof(char)*MAXBUFFER);

	stringSlicer(command, commandType, key, value); //Il comando viene suddiviso nelle opportune sottostringhe

	//Essendo inoltrato da un altro server, il comando potrà essere solo 'store' e 'search', inutile quindi gestire anche 'list' e 'corrupt' 
	if(strcmp(commandType, "store") == 0) {
		addNodeList(&head, key, value); //Aggiunge direttamente la nuova coppia alla lista visto che i controlli opportuni sono stati fatti a monte dal server inoltrante
	} else if (strcmp(commandType, "search") == 0) {
		//Viene reinviata al server inoltrante la coppia chiave-valore presente sul server attuale (che non necessariamente corrisponderà a quella salvata sul server inoltrante)
		strcpy(value, findValue(head, key));
		sprintf(buf, "(%s,%s)", key, value);
		
		write(socket, buf, strlen(buf));
	}

	free(buf);
	free(commandType);
	free(key);
	free(value);

	pthread_mutex_unlock(&commandHandlerMutex);
}

/* 
- FUNZIONE SIGNALHANDLER -
Gestisce l'interruzione di uno dei server terminando automaticamente la sessione di tutti i server collegati.
-- Paramentri:
- segnale: Variabile intera rappresentante in segnale arrivato.
-- Valore di ritorno: Nessuno.
*/
void signalHandler(int segnale) {
	if (segnale == SIGINT || segnale == SIGTERM) {
		write(STDOUT_FILENO, "[+] Processo terminato dall'utente.\n", 36);
		for(int i = 0; i <= serverNumber; i++) {
			write(socketOthServer[i], "Server chiuso", 15);
			close(socketOthServer[i]);
		}
		free(socketOthServer);

		destroyNodeList(head); //Dealloca i nodi contenuti nel ledger
		head = NULL;

		destroyServerList(serverSockets); //Dealloca i riferimenti agli altri server
		serverSockets = NULL;

		exit(-1);
	}
}


/************************************************************************************************************************
*                                                                                                                       *
*                                                  FUNZIONI AUSILIARIE                                                  *
*                                                                                                                       *
*************************************************************************************************************************/

/* 
- FUNZIONE STRINGSLICER- 
Data come parametro una stringa contenente il comando e gli eventuali parametri, stringSlicer suddivide tale stringa in modo opportuno.
-- Paramentri:
- buffer: Stringa contenente l'intero comando arrivato dal client. Della forma comando([valore][,][valore]).
- command: Sottostringa di buffer contenente, in output, il comando.
- key: Sottostringa di buffer contenente, in output, la chiave (può essere vuota).
- value: Sottostringa di buffer contenente, in output, il valore (può essere vuota).
-- Valore di ritorno: command, key e value possono essere considerati come valori di ritorno "multipli".
*/
void stringSlicer(char *buffer, char *command, char *key, char *value) {
	int bufferIndex = 0; //Indice che tiene traccia del punto in cui si è arrivati ad analizzare il buffer contenente l'intero comando da suddividere in sottostringhe
	int fieldIndex = 0; //Indice di servizio che serve a copiare la sottostringa interessata carattere per carattere

	//Ciclo che separa memorizza in 'command' la parte della stringa 'buffer' corrispondente al comando
	while(buffer[bufferIndex] != '(') { 
		command[fieldIndex] = buffer[bufferIndex];

		bufferIndex = bufferIndex + 1;
		fieldIndex = fieldIndex + 1;
	}
	command[fieldIndex] = '\0';

	fieldIndex = 0;
	bufferIndex = bufferIndex +1;

	//Analisi della presenza di parametri
	if((strcmp(command, "store") == 0) || (strcmp(command, "corrupt") == 0) || (strcmp(command, "search") == 0)) { //I comandi 'store', 'corrupt' e 'search' prevedono almeno un parametro (key)
		//Ciclo che memorizza in 'key' la parte della stringa 'buffer' corrispondente alla chiave della coppia
		while(buffer[bufferIndex] != ',' && buffer[bufferIndex] != ')') {
			key[fieldIndex] = buffer[bufferIndex];

			bufferIndex = bufferIndex + 1;
			fieldIndex = fieldIndex + 1;
		}
		key[fieldIndex] = '\0';

		fieldIndex = 0;
		bufferIndex = bufferIndex +1;

		if((strcmp(command, "store") == 0) || (strcmp(command, "corrupt") == 0)) { //I comandi 'store' e 'corrupt' prevedono un ulteriore parametro (value)
			//Ciclo che memorizza in 'value' la parte della stringa 'buffer' corrispondente al valore della coppia
			while(buffer[bufferIndex] != ')') {
				value[fieldIndex] = buffer[bufferIndex];

				bufferIndex = bufferIndex + 1;
				fieldIndex = fieldIndex + 1;
			}
			value[fieldIndex] = '\0';
		}
	}
}

/* 
- FUNZIONE ADDNODELIST -
Aggiunge una nuova coppia chiave-valore alla linked list contenente i nodi del ledger attualmente memorizzati.
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
- newKey: La chiave del nuovo nodo da aggiungere nel ledger.
- newValue: Il valore del nuovo nodo da aggiungere nel ledger.
-- Valore di ritorno: nessuno
*/
void addNodeList(struct nodeList **head, char* newKey, char* newValue) {
	struct nodeList *newNode = (struct nodeList *) malloc(sizeof(struct nodeList));

	newNode->key = malloc(sizeof(char)*MAXBUFFER);
	newNode->value = malloc(sizeof(char)*MAXBUFFER);
	newNode->nextNode = NULL;

	strcpy(newNode->key, newKey);
	strcpy(newNode->value, newValue);
	
	struct nodeList *ref = *head;

	if (*head == NULL) {
		*head = newNode;
		return;
	}

	while (ref->nextNode != NULL)
		ref = ref->nextNode;

	ref->nextNode = newNode;
}

/* 
- FUNZIONE ADDSERVERLIST -
Aggiunge alla linked list 'serverParameters' l'ip e la porta di un server alla quale l'esecutore dovrà collegarsi.
-- Paramentri:
- head: Linked list contenente le informazioni dei server.
- newIp: Ip del nuovo server da aggiungere alla lista.
- newPort: Porta del nuovo server da aggiungere alla lista.
-- Valore di ritorno: Nessuno
*/
void addServerList(struct serverParameters **head, char* newIp, char* newPort) {
	struct serverParameters *newServer = (struct serverParameters *) malloc(sizeof(struct serverParameters));

	newServer->ip = malloc(sizeof(char)*MAXBUFFER);
	newServer->port = malloc(sizeof(char)*MAXBUFFER);
	newServer->nextServer = NULL;

	strcpy(newServer->ip, newIp);
	strcpy(newServer->port, newPort);
	
	struct serverParameters *ref = *head;

	if (*head == NULL) {
		*head = newServer;
		return;
	}

	while (ref->nextServer != NULL)
		ref = ref->nextServer;

	ref->nextServer = newServer;
}

/* 
- FUNZIONE PRINTLIST -
Stampa tutte le coppie memorizzate nel ledger in ordine di memorizzazione sul file descriptor indicato come parametro.
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
- where: File descriptor sul quale si vuole stampare (es STDOUT_FILENO, un socket).
-- Valore di ritorno: Nessuno
*/
void printList(struct nodeList *head, int where) {
	char buffer[MAXBUFFER];
	struct nodeList *ref = head;

	while (ref != NULL) {
		bzero(buffer, MAXBUFFER);
		sprintf(buffer, "Key: %s - Value: %s\n", ref->key, ref->value);
		write(where, buffer, strlen(buffer));
		ref = ref->nextNode;
	}
}

/* 
- FUNZIONE CORRUPTNODE - 
Data una linked list, una chiave ed un valore, la funzione corruptNode cambia il valore della coppia che ha 
la chiave corrispondente a quella passata come parametro (key) con il valore passato come parametro (newValue).
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
- key: La chiave della coppia che si vuole "corrompere".
- newValue: Il valore che la coppia assumera' dopo il comando corruptNode.
-- Valore di ritorno: Nessuno.
*/
void corruptNode(struct nodeList *head, char *key, char *newValue) {
	struct nodeList *ref = head;

	while (ref != NULL) {
		if(strcmp(ref->key, key) == 0) { 
			strcpy(ref->value, newValue);
			break;
		}
		ref = ref->nextNode;
	}
}

/* 
- FUNZIONE FINDVALUE - 
Data una linked list e una chiave, la funzione findNode cerca una coppia che ha la chiave corrispopndente
a quella passata come parametro (key) e ne restituisce il valore (se presente).
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
- key: La chiave della coppia di cui si vuole cercare il valore.
-- Valore di ritorno: Una stringa contenente il valore della coppia se è presente una coppia con chiave corrispondenrte a quella passata come parametro, NULL altrimenti.
*/
char* findValue(struct nodeList *head, char *key) {
	struct nodeList *ref = head;

	while (ref != NULL) {
		if(strcmp(ref->key, key) == 0) {
			return ref->value;
		}
		ref = ref->nextNode;
	}

	return NULL;
}

/* 
- FUNZIONE ISSTORED -
Data come parametro una linked list e una chiave, la funzione isStored verifica se nella linked list è presente una coppia che ha la chiave corrispondente a quella passata come parametro (key).
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
- key: La chiave della coppia di cui si vuole verificare la presenza nella linked list.
-- Valore di ritorno: 1 se esiste una coppia con chiave corrispondente a quella passata alla funzione, 0 altrimenti.
*/
int isStored(struct nodeList *head, char *key) {
	struct nodeList *ref = head;
	int isFound = 0;

	while (ref != NULL) {
		if(strcmp(ref->key, key) == 0) {
			isFound = 1;
			break;
		}
		ref = ref->nextNode;
	}

	return isFound;
}

/* 
- FUNZIONE DESTROYNODELIST -
Dealloca tutti le coppie chiave-valore memorizzati nel server chiamante.
-- Paramentri:
- head: Linked list contenente le coppie chiave-valore memorizzate nel ledger.
-- Valore di ritorno: Nessuno.
*/
void destroyNodeList(struct nodeList *head) {
	if (head == NULL) return;
	struct nodeList *temp;

	temp = head->nextNode;
	free(head->key);
	free(head->value);
	free(head);
	
	destroyNodeList(temp);
}

/* 
- FUNZIONE DESTROYSERVERLIST -
Dealloca tutte le informazioni degli altri server collegati al server chiamante.
-- Paramentri:
- head: Linked list contenente le informazioni degli altri server collegati al server chiamante.
-- Valore di ritorno: Nessuno.
*/
void destroyServerList(struct serverParameters *head) {
	if (head == NULL) return;
	struct serverParameters *temp;

	temp = head->nextServer;
	free(head->ip);
	free(head->port);
	free(head);
	
	destroyServerList(temp);
}