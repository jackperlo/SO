#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>

/*-------------DEFINE di COSTANTI--------------*/
/* file eseguibili da cui i figli del master assorbiranno il codice */
#define TAXI "./Taxi"
#define SOURCE "./Source"  
/* path e numero di parametri del file di configurazione per variabili definite a tempo d'esecuzione */
#define SETTING_PATH "./settings"
#define NUM_PARAM 9

/*------------DICHIARAZIONE METODI------------*/
void init(); /* inizializzazione delle variabili */
void map_generator(); /* genera la matrice con annesse celle HOLES e celle SO_SOURCES */
void init_map(); /* inizializza la matrice vergine (tutte le celle a 1)*/
void assign_holes_cells(); /* metodo di supporto a map_generator(), assegna le celle invalide */
void assign_source_cells(); /* metodo di supporto a map_generator(), assegna le celle sorgenti */
int check_cell_2be_inaccessible(int x, int y); /* metodo di supporto a map_generator(). controlla se le 8 celle adiacenti a quella considerata sono tutte non inaccessibili */ 
void print_map(int isTerminal); /* stampa una vista della mappa durante l'esecuzione, e con isTerminal evidenzia le SO_TOP_CELLS celle con più frequenza di passaggio */
void source_processes_generator(); /* fork dei processi sorgenti */
void free_mat(); /* esegue la free di tutte le matrici allocate dinamicamente */
void execution(); /* esecuzione temporizzata del programma con stampa delle matrici */
void timed_print(int sig); /* stampa temporizzata della mappa ogni secondo */
void kill_sources(); /* elimina tutti i figli sources */
void print_map_specific(int** m, int isTerminal); /* stampa la mappa passata come parametro */

/*-------------COSTANTI GLOBALI-------------*/
int SO_TAXI; /* numero di taxi presenti nella sessione in esecuzione */
int SO_CAP_MIN; /* capacità minima assumibile da SO_CAP: determina il minimo numero che può assumere il valore che identifica il massimo numero di taxi che possono trovarsi in una cella contemporaneamente */
int SO_CAP_MAX; /* capacità massima assumibile da SO_CAP: determina il MASSIMO numero che può assumere il valore che identifica il massimo numero di taxi che possono trovarsi in una cella contemporaneamente */
int SO_TIMENSEC_MIN; /* valore minimo assumibile da SO_TIMESEC, che rappresenta il tempo di attraversamento di una cella della matrice */
int SO_TIMENSEC_MAX; /* valore MASSIMO assumibile da SO_TIMESEC, che rappresenta il tempo di attraversamento di una cella della matrice */

/*-------------VARIABILI GLOBALI-------------*/
int SO_HEIGHT, SO_WIDTH;
int **map; /* puntatore a matrice che determina la mappa in esecuzione */
int **SO_CAP; /* puntatore a matrice di capacità massima per ogni cella */
int **SO_TIMENSEC; /* puntatore a matrice dei tempi di attesa per ogni cella */
pid_t **SO_SOURCES_PID; /* puntatore a matrice contenente i PID dei processi SOURCES nella loro coordinata di riferimento */
int executing = 0; /* variabile BINARIA. 0-> Esecuzione Conclusa | 1-> In Esecuzione */
int seconds = 0; /* contiene i secondi da cui il programma è in esecuzionee */
int SO_DURATION; /* duarata in secondi del programma. Valore definito nel setting */

/* funzioni e struttura dati per lettura e gestione parametri su file */
typedef struct node {
	int value;
	char name[20];
	struct node * next;  
} node;
typedef node* param_list; /* conterrà la lista dei parametri passati tramite file di settings */
param_list listaParametri = NULL; /* lista che contiente i parametri letti dal file di settings */
param_list insert_exec_param_into_list(char name[], int value); /* inserisce i parametri d'esecuzione letti da settings in una lista concatenata */
void print_exec_param_list(); /* stampa la lista dei parametri d'esecuzione */
int search_4_exec_param(char nomeParam[]); /* ricerca il parametro pssatogli per nome nella lista dei parametri estratti dal file di settings. RITORNA 0 SE NON LO TROVA */
void free_param_list(param_list aus_list); /* eseguo la free dello spazio allocato con la malloc durante il riempimento della lista dei parametri */
int check_n_param_in_exec_list(); /* ritorna il numero di nodi(=parametri) presenti nella lista concatenata. Utile per controllare se ho esattamente gli N_PARAM richiesti */

/*
-------------elenco dei parametri d'esecuzione e loro descrizione------------------
int SO_HOLES;  indica il numero di celle inaccessibili(da cui holes) all'interno della matrice
int SO_SOURCES;  indica il numero di celle sorgenti di possibili richieste. Come fossero le stazioni di servizio per i taxi 
int SO_TIMEOUT;  numero di secondi dopo i quali il processo viene abortito 
int SO_DURATION;  durata dell'esecuzione in secondi 
int SO_TOP_CELLS;  numero di celle più attraversate (es. 4 -> riferisco le prime 4 più attraversate) 
int SO_TOP_ROAD;  processo che ha fatto più strada 
int SO_TOP_LENGTH;  processo che ha impiegato più tempo in un viaggio 
int SO_TOP_REQ;  processo che ha completato più request di clienti 
int SO_TRIP_SUCCESS;  numero di viaggi eseguiti con successo, da stampare a fine dell'esecuzione 
int SO_TRIP_NOT_COMPLETED;  numero di viaggi ancora da eseguire o in itinere nel momento della fine dell'esecuzione 
int SO_TRIP_ABORTED;  numero di viaggi abortiti a causa del deadlock 
*/

int main(int argc, char *argv[]){
    char *s;
    
    /* estraggo e assegno le variabili d'ambiente globali che definiranno le dimensioni della matrice di gioco */
    s = getenv("SO_HEIGHT");
    SO_HEIGHT = atoi(s);
    s = getenv("SO_WIDTH");
    SO_WIDTH = atoi(s);
    if((SO_WIDTH <= 0) || (SO_HEIGHT <= 0)){
        fprintf(stderr, "PARAMETRI DI COMPILAZIONE INVALIDI. SO_WIDTH o SO_HEIGHT <= 0.\n");
		exit(EXIT_FAILURE);
    }

    /* CONFIGURAZIONE DELL'ESECUZIONE */
    init();
    map_generator();
    source_processes_generator();
    SO_DURATION = search_4_exec_param("SO_DURATION");

    /* START UFFICIALE DELL'ESECUZIONE */
    executing = 1; 
    execution();
    
    /* CONCLUSIONE DELL'ESECUZIONE */
    free_param_list(listaParametri);
    free_mat();
    return 0;
}

void init(){
    int i, j;
    FILE *settings;
    /* conterranno nome e valore di ogni parametro definito in settings */
    char name[100];
    int value;
    int check_n_param_return_value = -1; /*conterrà il numero di nodi(=numero di parametri) presenti nella lista che legge i parametri da file di settings */
    
    /*-----------INIZIALIZZO LE MATRICI GLOBALI--------------*/
    map = (int **)malloc(SO_HEIGHT*sizeof(int *));
    if (map == NULL)
        return;
    for (i=0; i<SO_HEIGHT; i++){
        map[i] = malloc(SO_WIDTH*sizeof(int));
        if (map[i] == NULL)
        return;
    }

    SO_CAP = (int **)malloc(SO_HEIGHT*sizeof(int *));
    if (SO_CAP == NULL)
        return;
    for (i=0; i<SO_HEIGHT; i++){
        SO_CAP[i] = malloc(SO_WIDTH*sizeof(int));
        if (SO_CAP[i] == NULL)
        return;
    }

    SO_TIMENSEC = (int **)malloc(SO_HEIGHT*sizeof(int *));
    if (SO_TIMENSEC == NULL)
        return;
    for (i=0; i<SO_HEIGHT; i++){
        SO_TIMENSEC[i] = malloc(SO_WIDTH*sizeof(int));
        if (SO_TIMENSEC[i] == NULL)
        return;
    }

    SO_SOURCES_PID = (pid_t **)malloc(SO_HEIGHT*sizeof(pid_t *));
    if (SO_SOURCES_PID == NULL)
        return;
    for (i=0; i<SO_HEIGHT; i++){
        SO_SOURCES_PID[i] = malloc(SO_WIDTH*sizeof(pid_t));
        if (SO_SOURCES_PID[i] == NULL)
        return;
    }


    /*-----------LEGGO I PARAMETRI DI ESECUZIONE-------------*/
    /* inizializzo il random */
    srand(time(NULL));
    /* apro il file settings */
    if((settings = fopen(SETTING_PATH , "r")) == NULL){
        fprintf(stderr, "Errore durante l'apertura file Setting che contiene i parametri\n");
		exit(EXIT_FAILURE);
    }
    for(i=0; i<NUM_PARAM; i++){
        /* leggo ogni riga del file finché non raggiunge la fine EOF */
        while(fscanf(settings, "%s = %d\n", name, &value) != EOF)
            listaParametri = insert_exec_param_into_list(name, value);
    }

    /* check: ho tutti e soli i NUM_PARAM parametri d'esecuzione richiesti? */
    if((check_n_param_return_value = check_n_param_in_exec_list()) != NUM_PARAM){
        fprintf(stderr, "Master.c ERRORE nel numero di parametri presenti nel file di Settings.\nRichiesti:%d\nOttenuti:%d\n", NUM_PARAM, check_n_param_return_value);
		exit(EXIT_FAILURE);
    }

    /* chiudo il file settings */
    fclose(settings);

#ifdef DEBUG
    print_exec_param_list();
#endif

    for(i = 0; i < SO_HEIGHT; i++){
        for(j = 0; j < SO_WIDTH; j++){
            /* genera la matrice delle capacità per ogni cella, genera un valore casuale tra CAP_MIN e CAP_MAX */
            SO_CAP[i][j] = SO_CAP_MIN + rand() % (SO_CAP_MAX - (SO_CAP_MIN - 1));

            /* genera la matrice dei tempi di attesa per ogni cella, genera un valore casuale tra TIMENSEC_MIN e TIMENSEC_MAX */
            SO_TIMENSEC[i][j] = SO_TIMENSEC_MIN + rand() % (SO_TIMENSEC_MAX - (SO_TIMENSEC_MIN - 1));
        }
    }    

}

void map_generator(){
    init_map();
    assign_holes_cells();
    assign_source_cells();
}

void init_map(){
    int i, j;
    for (i = 0; i < SO_HEIGHT; i++){
        for (j = 0; j < SO_WIDTH; j++)
            map[i][j] = 1; /* rendo ogni cella vergine(no sorgente=2, no inaccessibile=0) */
    }
}

void assign_holes_cells(){
    int i, x, y, esito=0; /* valore restituito dalla check_cell_2be_inaccessible(): 0 -> cella non adatta ad essere inaccessibile per vincoli di progetto. 1 -> cella adatta ad essere inaccessibile */
    int SO_HOLES;
    srand(time(NULL)); /* inizializzo il random number generator */ 

    /* estraggo il parametro dalla lista dei parametri; errore se non lo trovo */
    
    if((SO_HOLES = search_4_exec_param("SO_HOLES")) == 0){
        fprintf(stderr, "Parametro SO_HOLES non trovato nella lista dei parametri!\n");
		exit(EXIT_FAILURE);
    }

    for (i = 0; i < SO_HOLES; i++){
        do{
        
            x = rand() % SO_HEIGHT; /* estrae un random tra 0 e (SO_HEIGHT-1) */  
            y = rand() % SO_WIDTH; /* estrae un random tra 0 e (SO_WIDTH-1) */    
            if(map[x][y] != 0) /* se la cella non è già segnata come inaccessibile */
                esito = check_cell_2be_inaccessible(x, y);
        }while(esito == 0); /* finché non trovo una cella adatta ad essere definita inaccessibile */
        map[x][y] = 0;  /* rendo effettivamente la cella inaccessibile */
        esito = 0; 
    }
}

void assign_source_cells(){
    int i, x, y;
    int SO_SOURCES;
    srand(time(NULL)); /* inizializzo il random number generator */ 
    
    /* estraggo il parametro dalla lista dei parametri; errore se non lo trovo. */
    if((SO_SOURCES = search_4_exec_param("SO_SOURCES")) == 0){
        fprintf(stderr, "Parametro SO_SOURCES non trovato nella lista dei parametri!\n");
		exit(EXIT_FAILURE);
    }

    /*dprintf(1,"%d",SO_SOURCES);*/
    
    for (i = 0; i < SO_SOURCES; i++){
        do{
            x = rand() % SO_HEIGHT; /* estrae un random tra 0 e (SO_HEIGHT-1) */
            y = rand() % SO_WIDTH; /* estrae un random tra 0 e (SO_WIDTH-1) */
            if(map[x][y] != 0) /* se la cella non è inaccessibile */
                map[x][y] = 2; /* assegno la cella come SOURCE */
        }while(map[x][y] != 2); /* finché la cella che sto considerando non viene marcata come sorgente */
    }
}

void print_map(int isTerminal){
    /* indici per ciclare */
    int i, k;

    /* cicla per tutti gli elementi della mappa */
    for(i = 0; i < SO_HEIGHT; i++){
        for(k = 0; k < SO_WIDTH; k++){
            switch (map[i][k])
            {
            /* CASO 0: cella invalida, quadratino nero */
            case 0:
                printf("|X");
                break;
            /* CASO 1: cella di passaggio valida, non sorgente, quadratino bianco */
            case 1:
                printf("|_");
                break;
            /* CASO 2: cella sorgente, quadratino striato se stiamo stampando l'ultima mappa, altrimenti stampo una cella generica bianca*/
            case 2:
                if(isTerminal)
                    printf("|Z");
                else
                    printf("|_");
                break;
            /* DEFAULT: errore o TOP_CELL se stiamo stampando l'ultima mappa, quadratino doppio */
            default:
                if(isTerminal)
                    printf("|L");
                else
                    printf("E");
                break;
            }
        }
        /* nuova linea dopo aver finito di stampare le celle della linea i della matrice */
        printf("|\n");
    }
}

void source_processes_generator(){
    int x,y; /* coordinate di map al quale si trova il processo Source che viene forkato */
    char *source_args[6]; /* array di stringhe da passare al processo Source */
    char aus[50];

    /* variabili per settaggio della shared memory */
    int a = 30;
    int *sh_map;
    int memd;
    key_t key;

    /* INIZIALIZZO LA SHARED MEMORY PER MAP *//*
    if ((key = ftok(".", 'b')) == -1)
    {
        perror("non esiste il file per la shared memory");
        exit(-1);
    }
    if((memd = shmget(key, sizeof(int), 0666 | IPC_CREAT)) == -1) 
        fprintf(stderr, "\n%s: %d. Errore nella creazione della memoria condivisa\n", __FILE__, __LINE__);
    sh_map =  shmat(memd, 0, 0);
    if(sh_map == (int)(-1))
        fprintf(stderr, "\n%s: %d. Impossibile agganciare la memoria condivisa \n", __FILE__, __LINE__);

    sh_map = a;
    printf("sh_map = %p\n", (void *) &sh_map);*/
    /*print_map_specific(sh_map, 0);*/


    /* CREO I PROCESSI */
    for (x = 0; x < SO_HEIGHT; x++){
        for (y = 0; y < SO_WIDTH; y++){
            if(map[x][y] == 2){
                switch(SO_SOURCES_PID[x][y] = fork()){
                    case -1:
                        /* errore nella fork */
                        fprintf(stderr,"\nFORK Error #%03d: %s\n", errno, strerror(errno));
                        exit(EXIT_FAILURE);
                        break;
                    
                    /* caso PROCESSO FIGLIO */ 
                    case 0:

                        source_args[0] = malloc(100 * sizeof(char));
                        sprintf(aus, "%s", "Source");
                        strcpy(source_args[0], aus);

                        source_args[1] = malloc(10 * sizeof(char));
                        sprintf(aus, "%d", x);
                        strcpy(source_args[1], aus);

                        source_args[2] = malloc(10 * sizeof(char));
                        sprintf(aus, "%d", y);
                        strcpy(source_args[2], aus);

                        source_args[3] = malloc(10 * sizeof(char));
                        sprintf(aus, "%d", SO_HEIGHT);
                        strcpy(source_args[3], aus);

                        source_args[4] = malloc(10 * sizeof(char));
                        sprintf(aus, "%d", SO_WIDTH);
                        strcpy(source_args[4], aus);

                        source_args[5] = malloc(100 * sizeof(char));
                        sprintf(aus, "%d", memd);
                        strcpy(source_args[5], aus);

                        source_args[6] = NULL;

                        execvp(SOURCE, source_args);

                        /* ERRORE ESECUZIONE DELLA EXECVP */
                        fprintf(stderr, "\n%s: %d. EXECVP Error #%03d: %s\n", __FILE__, __LINE__, errno, strerror(errno));
	                    exit(EXIT_FAILURE);
                        break;
                    
                    /* caso PROCESSO PADRE */
                    default:
                        break;
                }
            }
        }
    }
}

param_list insert_exec_param_into_list(char name[], int value){
    param_list new_elem;
    int esito = 1; /* controllo che non si stiano inserendo parametri doppi */
    esito = search_4_exec_param(name);

    if(esito == 0){ /* se non ancora presente nella lista */
        /* genero un nuovo nodo(name,value) della lista e lo inserisco all'interno della stessa */
        new_elem = malloc(sizeof(*new_elem));
        new_elem->value = value;
        strcpy(new_elem->name, name);
        new_elem->next = listaParametri;
        return new_elem;
    }else{
        fprintf(stderr, "\n%s: %d. PARAMETRO DUPLICATO nel file di SETTING.\nNome del parametro doppio: %s\n", __FILE__, __LINE__, name);
        exit(EXIT_FAILURE);
    }
}

void print_exec_param_list(){
    /* stampa dei nodi contenuti nella lista */
    param_list aus_param_list = listaParametri;
	if (aus_param_list == NULL) {
		printf("Empty EXECUTION PARAM LIST\n");
		return;
	}
	for(; aus_param_list!=NULL; aus_param_list = aus_param_list->next) {
		printf("\nNOME: %s ---- VALORE: %d", aus_param_list->name, aus_param_list->value);
	}
	printf("\n");
}

int search_4_exec_param(char nomeParam[]){
    param_list aus_param_list = listaParametri;
	for(; aus_param_list!=NULL; aus_param_list = aus_param_list->next) {
        if (strcmp(aus_param_list->name,nomeParam) == 0){
            return aus_param_list->value;
        }	
    }
	return 0;
}

int check_n_param_in_exec_list(){
    int n_elem = 0;
    param_list aus_param_list = listaParametri;
	for(; aus_param_list!=NULL; aus_param_list = aus_param_list->next) {
        n_elem++;	
    }
	return n_elem;
}

int check_cell_2be_inaccessible(int x, int y){ 
    int esito = 1; /* assumo che sia possibile rendere la cella inaccessibile */
    /* devo controllare che tutte le celle X siano (!= 0), dove 0 => cella inaccessibile
        X X X
        X o X
        X X X
    */

    if((x-1) >= 0){ /* check sull'INTERA RIGA SOPRA alla cella considerata, se non fuori dai bordi */
        if((y-1) >= 0){
            if(map[x-1][y-1] == 0) /* è già inaccessibile => CELLA NON UTILE */
                esito = 0;
        }

        if(map[x-1][y] == 0)
            esito = 0;

        if((y+1) < SO_WIDTH){
            if(map[x-1][y+1] == 0)
                esito = 0;
        }
    }

    if((esito == 1) && ((x+1) < SO_HEIGHT)){ /* check sull'INTERA RIGA SOTTO, SE la RIGA SOPRA era OK(esito rimasto 1)*/
        if((y-1) >= 0){
            if(map[x+1][y-1] == 0)
                esito = 0;
        }

        if(map[x+1][y] == 0)
            esito = 0;

        if((y+1) < SO_WIDTH){
            if(map[x+1][y+1] == 0)
                esito = 0;
        }
    }

    if((esito == 1) && ((y-1) >= 0)){ /* CELLA a SINISTRA */
        if(map[x][y-1] == 0)
            esito = 0;
    }

    if((esito == 1) && ((y+1) < SO_WIDTH)){ /* CELLA a DESTRA */
        if(map[x][y+1] == 0)
            esito = 0;
    } 

    return esito;
}

void free_param_list(param_list aus_list){
    /* eseguo la free dello spazio allocato per i nodi della lista */
	if (aus_list == NULL) {
		return;
	}
    
	free_param_list(aus_list->next);
	free(aus_list);
}

void free_mat(){
    int i;
    int *currentIntPtr;
    pid_t *currentPid_tPtr;
    /* free map 2d array */
    for (i = 0; i < SO_HEIGHT; i++){
        currentIntPtr = map[i];
        free(currentIntPtr);

        currentIntPtr = SO_CAP[i];
        free(currentIntPtr);

        currentIntPtr = SO_TIMENSEC[i];
        free(currentIntPtr);

        currentIntPtr = SO_SOURCES_PID[i];
        free(currentIntPtr);
    }
}

void execution(){
    /* associa l'handler dell'alarm */
    if (signal(SIGALRM, timed_print)==SIG_ERR) {
        fprintf(stderr,"\n%s: %d.SIGALARM Error\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }else{
        /* faccio un alarm di un secondo per far iniziare il ciclo di stampe ogni secondo */
        alarm(1);
        while(seconds < SO_DURATION){
            /* aspetta che finisca il tempo di esecuzione DA MODIFICARE */

            /* ATTESA ATTIVA (?) */
            /* DA TOGLIERE */
            if(seconds == 3)
                kill_sources();
        }
        /* esecuzione terminata */
        executing = 0;
        printf("\n\n--------------------------------------\nFine: %d secondi di simulazione\n", seconds);
        print_map(1);
    }
}

void timed_print(int sig){
    /* se sto eseguendo la simulazione il ciclo continua in loop incrementando i secondi passati dall'inizio */
    if(executing){
        seconds++;
        printf("\n\n--------------------------------------\nSecondo: %d\nInvalidi: %d\nTaxi: %d\n", seconds, 5, 5);
        print_map(0);
        alarm(1);
    }
}

void kill_sources(){
    int i,j;

    for(i=0; i<SO_HEIGHT; i++){
        for(j=0; j<SO_WIDTH; j++){
            if(SO_SOURCES_PID[i][j] > 0)
                kill(SO_SOURCES_PID[i][j], SIGQUIT);
        }
    }
}

void print_map_specific(int** m, int isTerminal){
    /* indici per ciclare */
    int i, k;
    printf("value = %p",(void *) &m);
    printf("Specific map:\n");
    /* cicla per tutti gli elementi della mappa */
    for(i = 0; i < SO_HEIGHT; i++){
        for(k = 0; k < SO_WIDTH; k++){
            switch (m[i][k])
            {
            /* CASO 0: cella invalida, quadratino nero */
            case 0:
                printf("|X");
                break;
            /* CASO 1: cella di passaggio valida, non sorgente, quadratino bianco */
            case 1:
                printf("|_");
                break;
            /* CASO 2: cella sorgente, quadratino striato se stiamo stampando l'ultima mappa, altrimenti stampo una cella generica bianca*/
            case 2:
                if(isTerminal)
                    printf("|Z");
                else
                    printf("|_");
                break;
            /* DEFAULT: errore o TOP_CELL se stiamo stampando l'ultima mappa, quadratino doppio */
            default:
                if(isTerminal)
                    printf("|L");
                else
                    printf("E");
                break;
            }
        }
        /* nuova linea dopo aver finito di stampare le celle della linea i della matrice */
        printf("|\n");
    }
}