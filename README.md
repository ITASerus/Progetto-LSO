# Laboratorio di Sistemi Operativi
Il seguente progetto è stato sviluppato ed implementato per l'esame di “Laboratorio di Sistemi Operativi” del Corso di Laurea in Informatica dell'Università di Napoli Federico II.
___
# Richiesta
La richiesta del progetto era di sviluppare un applicativo multithread client-server che offre un servizio di memorizzazione coppie (chiave, valore) in cui i processi client inviano comandi ai server disponibili, che li elaborano per restituire ai client il risultato desiderato. 
L'applicativo doveva essere non centralizzato e supportare una struttura multi server e multi client.
È possibile leggere maggiori dettagli sull'implementazione nella documentazione presente in questa Repository.
___
# Obiettivi del progetto
L'obiettivo di questo progetto è creare un applicativo client server con le seguenti caratteristiche:

- Utilizzo di I/O non bufferizzato
- Utilizzo corretto delle system call
- Comunicazione tramite socket TCP
- Assenza di race conditions tra i thread
- Assenza di deadlock tra la comunicazione server->server e client->server
___
# Tecnologie
L'applicativo è scritto interamente in C ed è compatibile con qualsiasi sistema operativo UNIX.
___
