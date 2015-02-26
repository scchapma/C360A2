/* CSC 360, Winter 2015
 * 
 * Assignment #2
 *
 * Stephen Chapman, V00190898
 *
 * Interstellar-space problem: 
 * 
 *  Input:   A user-determined number of n atoms
 *  Output:  The maximum number of HC2 radicals, based on a randomly generated split of the
 *           n atoms into m hydrogen atoms and (n-m) carbon atoms.
 *
 *  The program produces n threads, each representing an atom.
 *
 *  The program then allows these threads to determine how they combine into a set of
 *  radicals, each containing one hydrogen atom and two carbon atoms.
 *
 *  The production of radicals is controlled by a series of semaphores.  These semaphores
 *  are organized in a manner that prevents the starvation of any of the n threads.
 *
 *  After the maximum number of radicals has been produced, the blocked threads are released and
 *  the program terminates.
 *
 *  There are two options for program output.  If the user desires a detailed review of radical
 *  production, the output can be sent to a file.  Otherwise, the output can be piped to 
 *  a companion Python file (script_a1.py) that will output a summary and notify the user
 *  as to whether the program passed (no starvation) or failed (starvation).
 *
 *  This program for BONUS MARKS - Reimplementation of program using mutexes.
 */

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*-----------------------------------------------------------------------------
 ** 		STRUCT DEFINITIONS
 **-----------------------------------------------------------------------------
 */

typedef int bool;
#define true 1
#define false 0

/* Contents of user input string */
struct Stringtab{
    int sval;
    int max;
    char **stringval;
} stringtab;

enum {FINIT = 1, FGROW = 2};

/* Node struct for list of background functions */
typedef struct Customer {
    int id;
    int arrival_time;
    int service_time;
    int priority;
    int place_in_list;
    struct Customer *next;
} Customer;

/* Head for list of background functions */
Customer *bg_list = NULL;
Customer *customer_list = NULL;

/* File-scope variable: root node for the linked list */
Customer *queue = NULL;


/* -------------------------------------------------------------------------------------------
 **		FILE-SCOPE VARIABLES
 ** -------------------------------------------------------------------------------------------
 */

int debug = 0;


/* Random # below threshold indicates H; otherwise C. */
#define TRUE   1
#define FALSE  0


/* Global / shared variables */
pthread_mutex_t c_mutex, ch_mutex;
pthread_mutex_t mutex;

int num_threads;

/* -------------------------------------------------------------------------------------------
 ** 		ACCESSORY FUNCTIONS
 ** -------------------------------------------------------------------------------------------
 */

/* Dr. Zastre's code from SENG 265 */
/* If newline at end of string, then remove it.*/
void chomp(char *line) {
    int len;
    
    if (line == NULL) {
        return;
    }
    
    len = strlen(line);
    if (line[len-1] == '\n') {
        line[len-1] = '\0';
    }
    
    len = strlen(line);
    if (line[len-1] == '\r') {
        line[len-1] = '\0';
    }
}

/* Dr. Zastre's code from SENG 265 */
char *string_duplicator(char *input) {
    char *copy;
    assert (input != NULL);
    copy = (char *)malloc(sizeof(char) * strlen(input) + 1);
    if (copy == NULL) {
        fprintf(stderr, "error in string_duplicator");
        exit(1);
    }
    strncpy(copy, input, strlen(input)+1);
    return copy;
}

/* Dr. Zastre's code from SENG 265 */
void *emalloc(size_t n){
    void *p;
    p = malloc(n);
    if (p == NULL) {
        fprintf(stderr, "malloc of %lu bytes failed", n);
        exit(1);
    }
    return p;
}

/* Amended version of Dr. Zastre's "addname" code from SENG 265 */
int addstring(char *newstring){
    char **fp;
    
    if(stringtab.stringval == NULL){
        stringtab.stringval = (char **) emalloc(FINIT*sizeof(char *));
        stringtab.max = FINIT;
        stringtab.sval = 0;
    }else if(stringtab.sval >= stringtab.max){
        fp = (char **) realloc(stringtab.stringval, (FGROW*stringtab.max)*sizeof(char *));
        if(stringtab.stringval == NULL){
            return -1;
        }
        stringtab.max = FGROW*stringtab.max;
        stringtab.stringval = fp;
    }
    stringtab.stringval[stringtab.sval] = newstring;
    return stringtab.sval++;
}

int reset_string_array(){
    stringtab.sval = 0;
    stringtab.max = 0;
    stringtab.stringval = NULL;
    free(stringtab.stringval);
    return 0;
}

/* -------------------------------------------------------------------------------------------
 **              LINKED LIST FUNCTIONS
 ** -------------------------------------------------------------------------------------------
 */

/* Node constructor */
Customer *newitem (int count){
    Customer *newp;
    newp = (Customer *) emalloc(sizeof(Customer));
    
    //traverse string array to populate struct
    newp->id = atoi(*stringtab.stringval++);
    newp->arrival_time = atoi(*stringtab.stringval++);
    newp->service_time = atoi(*stringtab.stringval++);
    newp->priority = atoi(*stringtab.stringval++);
    newp->place_in_list = count;
    newp->next = NULL;
    
    reset_string_array();
    
    return newp;
}

/* Add new node to front of list */
Customer *addfront (Customer *listp, Customer *newp){
    newp->next = listp;
    return newp;
}

/* Add new node to end of list */
Customer *addend (Customer *listp, Customer *newp){
    Customer *p;
    if(listp == NULL){
        //listp = newp;
        return newp;
    }
    for (p=listp; p->next != NULL; p = p->next);
    p->next = newp;
    return listp;
}


/* Delete item at given pointer */
Customer *delitem (Customer *listp, Customer *targetp){
    Customer *p, *prev;
    
    prev = NULL;
    for (p = listp; p != NULL; p = p-> next){
        if (p == targetp){
            if (prev == NULL){
                listp = p->next;
            }else{
                prev->next = p->next;
            }
            free(p);
            return listp;
        }
        prev = p;
    }
    fprintf(stderr, "delitem: %d not in list", targetp->id);
    exit(1);
}

/* Free memory for all remaining nodes in list */
void freeall (Customer *listp) {
    Customer *next;
    
    for ( ; listp != NULL; listp = next){
        next = listp->next;
        free(listp);
    }
}

void print_list (Customer *listp){
    Customer *next;
    
    for ( ; listp != NULL; listp = next){
        next = listp->next;
        printf("Customer #%d.\n", listp->id);
    }
}

/* Traverse list and delete nodes that have terminated*/
/*
void check_bg_list(Customer *listp){
    
    for ( ; listp != NULL; listp = listp->next){
        int retVal = waitpid(listp->pid, &listp->status, WNOHANG);
        if (retVal == -1){
            perror("waitpid");
            exit(EXIT_FAILURE);
        }
        if(retVal > 0){
            printf("Background process terminated - pid: %d, command: %s.\n",
                   listp->pid, listp->name, WEXITSTATUS(listp->status));
            bg_list = delitem(bg_list, listp);
        }
    }
}
*/


/* -------------------------------------------------------------------------------------------
 ** 		MAIN EVENT FUNCTIONS
 ** -------------------------------------------------------------------------------------------
 */

/* tokenize files command-line argument and store file strings in dynamic array */
int parse_line(char* input){
    
    char *separator = ":,";
    char *basic_token = strtok(input, separator);
    char *token;
    
    while (basic_token != NULL){
        token = string_duplicator(basic_token);
        addstring(token);
        printf("basic_token: %s\n", basic_token);
        basic_token = strtok(NULL, separator);
    }
    
    if(stringtab.sval == 0){
        return 0;
    }
    
    addstring(NULL);	
    return 0;
}

/* Based on Dr. Zastre's code from SENG 265 */
void parse_file(char *filename){
    printf("Enter parse file.\n");
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    
    fp = fopen(filename, "r");
    if(fp == NULL){
        printf("Cannot located file %s\n", filename);
        exit(1);
    }
    
    int count = 1;
    
    //parse first line of input file
    if((read = getline(&line, &len, fp)) != -1){
        chomp(line);
        num_threads = atoi(line);
    }
    
    //parse remaining lines of input file
    while((read = getline(&line, &len, fp)) != -1){
        chomp(line);
        if(1){
            printf("Retrieved line of length %zu :\n", read);
            printf("%s\n", line);
        }
        
        /* process line */
        parse_line(line);
        Customer* new_customer = newitem(count);
        customer_list = addend(customer_list, new_customer);
        print_list(customer_list);
        count++;
    }
    
    if(line){
        free(line);
    }
    exit(0);
}


void init()
{
    fprintf(stdout, "\n");
    
    /*init mutexes*/
    int init_c_mutex = pthread_mutex_init(&c_mutex, NULL);
    int init_ch_mutex = pthread_mutex_init(&ch_mutex, NULL);
    int init_mutex = pthread_mutex_init(&mutex, NULL);
    
    /*check for nulls */
    if(init_c_mutex != 0 || init_ch_mutex != 0 || init_mutex != 0){
        fprintf(stdout, "Exiting - failed to initialize the semaphores.  Error: %d\n", errno);
        exit(1);
    }
    
}


/* Needed to pass legit copy of an integer argument to a pthread */
int *dupInt( int i )
{
	int *pi = (int *)malloc(sizeof(int));
	assert( pi != NULL);
	*pi = i;
	return pi;
}


int main(int argc, char *argv[])
{
	
	//local variables

	if ( argc != 2  ) {
		fprintf(stderr, "usage: PQS <file name>\n");
		exit(1);
	}

	//process file - argv[1]
    parse_file(argv[1]);
	//save # of threads
	//create LL of customer structs (
	
	init();
	
	//create threads
	//call main thread function - implement Wu's algorithm	

	/*
    for (i = 0; i < numAtoms; i++) {
		atom[i] = (pthread_t *)malloc(sizeof(pthread_t));
		if ( (double)rand()/(double)RAND_MAX < ATOM_THRESHOLD ) {
			hNum++;
			status = pthread_create (
					atom[i], NULL, hReady,
					(void *)dupInt(hNum)
				);
		} else {
			cNum++;
			status = pthread_create (

					atom[i], NULL, cReady,
					(void *)dupInt(cNum)
				);
		}
		if (status != 0) {
			fprintf(stderr, "Error creating atom thread\n");
			exit(1);
		}
	}
    */

	/* join threads */
    /*
	for (i=0; i<numAtoms; i++){
	  pthread_join(*atom[i], NULL);
	}
    */

	exit(0);
}

