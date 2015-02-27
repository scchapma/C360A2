/* CSC 360, Winter 2015
 * 
 * Assignment #2
 *
 * Stephen Chapman, V00190898
 *
 *
 */

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

/*-----------------------------------------------------------------------------
 ** 		STRUCT DEFINITIONS
 **-----------------------------------------------------------------------------
 */

typedef int bool;
#define true 1
#define false 0

#define SLEEP_FACTOR 100000

/* clerk flag */
int clerk_is_idle = 1;
int waiting_customers = 0;

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
int count = 0;


/* Random # below threshold indicates H; otherwise C. */
#define TRUE   1
#define FALSE  0


/* Global / shared variables */
pthread_mutex_t queue_mutex, service_mutex;
pthread_cond_t service_convar;

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
    printf("New item: id:%d ; arrival:%d ; service:%d ; priority:%d ;count: %d\n",
           newp->id, newp->arrival_time, newp->service_time, newp->priority, newp->place_in_list);
    
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

/* tokenize each line of input file (i.e., each customer) and store file strings in dynamic array */
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
    
    //count = 1;
    
    //parse first line of input file
    if((read = getline(&line, &len, fp)) != -1){
        chomp(line);
        num_threads = atoi(line);
    }
    
    //parse remaining lines of input file
    while((read = getline(&line, &len, fp)) != -1){
        count++;
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
        //count++;
    }
    
    if(line){
        free(line);
    }
    return;
}


void init()
{
    fprintf(stdout, "Enter init.\n");
    
    /*init mutexes*/
    int init_queue_mutex = pthread_mutex_init(&queue_mutex, NULL);
    int init_service_mutex = pthread_mutex_init(&service_mutex, NULL);
    int init_service_convar = pthread_cond_init(&service_convar, NULL);
    
    /*check for nulls */
    if(init_queue_mutex != 0 || init_service_mutex != 0 || init_service_convar != 0){
        fprintf(stdout, "Exiting - failed to initialize the mutexes/convars.  Error: %d\n", errno);
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

void request_service(Customer * customer_node){
    pthread_mutex_lock(&service_mutex);
    if (clerk_is_idle && waiting_customers == 0){
        clerk_is_idle = 0;
        
        printf("Customer %d being served.\n", customer_node->id);
        return;
    }else{
        printf("Customer %d waiting.\n", customer_node->id);
        pthread_cond_wait(&service_convar, &service_mutex);
        printf("Customer %d being served.\n", customer_node->id);
    }
    //queue_mutex_lock;
}

void *process_thread(void *customer_node){
    Customer *node = (Customer *) customer_node;
    printf("New node: id:%d ; arrival:%d ; service:%d ; priority:%d ;count: %d\n",
           node->id, node->arrival_time, node->service_time, node->priority, node->place_in_list);
    
    //implement Wu's Algorithm here for each thread...
    
    //sleep until arrival time
    int arrival_sleep_time = SLEEP_FACTOR*(node->arrival_time);
    usleep(arrival_sleep_time);
    
    //request service
    request_service (customer_node);
    
    pthread_mutex_unlock(&service_mutex);
    //sleep for service time
    int service_sleep_time = SLEEP_FACTOR*(node->service_time);
    usleep(service_sleep_time);
    
    //release service
    clerk_is_idle = 1;
    pthread_cond_signal(&service_convar);
    //pthread_mutex_unlock(&service_mutex);
    
    return((void *) 0);
}

int create_customer_threads(int count){
    
    pthread_t customer_thread[count];
    
    int i, j, status, status_join;
    
    for (i = 0; i < count; i++) {
        //customer_thread[i] = (pthread_t *)malloc(sizeof(pthread_t));
        status = pthread_create(&customer_thread[i], NULL, process_thread, customer_list);
        printf("Created thread #%d.\n", i+1);
        
        if (status != 0) {
            fprintf(stderr, "Error creating customer thread\n");
            exit(1);
        }
        customer_list = customer_list->next;
    }
    
    for(j=0; j < count; j++)
    {
        status_join = pthread_join(customer_thread[j], NULL);
        printf("Joined thread #%d.\n", j+1);
        
        if (status_join != 0) {
            fprintf(stderr, "Error joining customer thread\n");
            exit(1);
        }
    }
    
    return 0;
}


int main(int argc, char *argv[])
{
	
	if ( argc != 2  ) {
		fprintf(stderr, "usage: PQS <file name>\n");
		exit(1);
	}
    
	//process file - create list of customer structs and determine # of customer threads
    parse_file(argv[1]);
	init();
	create_customer_threads(count);
	

	/* join threads */
    /*
	for (i=0; i<numAtoms; i++){
	  pthread_join(*atom[i], NULL);
	}
    */

	exit(0);
}

