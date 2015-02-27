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
Customer *customer_queue = NULL;


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
    
    reset_string_array();
    
    return newp;
}

/* Node contructor for queue */
Customer *new_queue_node (Customer *oldnode){
    Customer *newp;
    newp = (Customer *) emalloc(sizeof(Customer));
    
    newp->id = oldnode->id;
    newp->arrival_time = oldnode->arrival_time;
    newp->service_time = oldnode->service_time;
    newp->priority = oldnode->priority;
    newp->place_in_list = oldnode->place_in_list;
    newp->next = NULL;
    
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
        return newp;
    }
    for (p=listp; p->next != NULL; p = p->next);
    p->next = newp;
    return listp;
}

int higher_priority(Customer *p, Customer *newp){
    //compare two structs
    //if newp priority > p priority, return 1
    if(newp->priority > p->priority){
        return 1;
    }else if(newp->priority == p->priority){
        if(newp->arrival_time < p->arrival_time){
            return 1;
        }else if(newp->arrival_time == p->arrival_time){
            if(newp->service_time < p->service_time){
                return 1;
            }else if(newp->service_time == p->service_time){
                if(newp->place_in_list < p->place_in_list){
                    return 1;
                }
            }
        }
    }
    
    return 0;
}

/* Add item at given pointer */
Customer *additem (Customer *listp, Customer *newp){
    
    Customer *p, *prev;
    prev = NULL;
    
    if(!listp){
        listp = newp;
        return listp;
    }
    
    for (p = listp; p != NULL; p = p-> next){
        if (higher_priority(p, newp)){
            if (prev == NULL){
                newp->next = p;
                listp = newp;
            }else{
                newp->next = p;
                prev->next = newp;
            }
            return listp;
        }
        prev = p;
    }
    prev->next = newp;
    return listp;
}

Customer *deletehead (Customer *listp){
    listp = listp->next;
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

void print_list2(Customer * queuep){
    Customer *p = queuep;
    while (p != NULL){
        printf("Listed customer %d.\n", p->id);
        p = p->next;
    }
    //printf("End list.\n");
}



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
        //printf("basic_token: %s\n", basic_token);
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
    //printf("Enter parse file.\n");
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
        
        /* process line */
        parse_line(line);
        Customer* new_customer = newitem(count);
        customer_list = addend(customer_list, new_customer);
        //print_list(customer_list);
    }
    
    if(line){
        free(line);
    }
    return;
}


void init()
{
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
    Customer *node = new_queue_node(customer_node);
    
    pthread_mutex_lock(&service_mutex);
    if (clerk_is_idle && !customer_queue){
    //if (clerk_is_idle && waiting_customers == 0){
        clerk_is_idle = 0;
        pthread_mutex_unlock(&service_mutex);
        printf("Customer %d returning from request service if loop.\n", node->id);
        return;
    }
    
    pthread_mutex_lock(&queue_mutex);
    
    //add customers to list
    waiting_customers++;
    printf("Customer %2d waits for the finish of customer __.\n", node->id);
    customer_queue = additem(customer_queue, node);
    //print_list2(customer_queue);
    pthread_mutex_unlock(&queue_mutex);
    
    //if clerk is busy or node is not head of list, wait
    while (!clerk_is_idle || (node->id != customer_queue->id)){
        //printf("Enter while loop - element %d.\n", node->id);
        pthread_cond_wait(&service_convar, &service_mutex);
    }
    
    //delete head from list
    pthread_mutex_lock(&queue_mutex);
    clerk_is_idle = 0;
    customer_queue = deletehead(customer_queue);
    waiting_customers--;
    printf("Customer %d returning from request service.\n", node->id);
    pthread_mutex_unlock(&queue_mutex);
}

void *process_thread(void *customer_node){
    Customer *node = (Customer *) customer_node;
    
    //implement Wu's Algorithm here for each thread...
    
    //sleep until arrival time
    int arrival_sleep_time = SLEEP_FACTOR*(node->arrival_time);
    usleep(arrival_sleep_time);
    printf("Customer %d arrives: arrival time(), service time(), priority (%2d).\n", node->id, node->priority);
    
    //request service
    request_service(node);
    
    pthread_mutex_unlock(&service_mutex);
    //sleep for service time
    printf("The clerk starts serving customer %2d at time ().\n", node->id);
    int service_sleep_time = SLEEP_FACTOR*(node->service_time);
    usleep(service_sleep_time);
    
    //release service
    pthread_mutex_lock(&service_mutex);
    clerk_is_idle = 1;
    printf("The clerk finishes the service to customer %2d at time ().\n", node->id);
    pthread_cond_broadcast(&service_convar);
    pthread_mutex_unlock(&service_mutex);
    
    return((void *) 0);
}

int create_customer_threads(int count){
    
    pthread_t customer_thread[count];
    
    int i, j, status, status_join;
    
    for (i = 0; i < count; i++) {
        status = pthread_create(&customer_thread[i], NULL, process_thread, customer_list);
        //printf("Created thread #%d.\n", i+1);
        
        if (status != 0) {
            fprintf(stderr, "Error creating customer thread\n");
            exit(1);
        }
        customer_list = customer_list->next;
    }
    
    for(j=0; j < count; j++)
    {
        status_join = pthread_join(customer_thread[j], NULL);
        
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
	
	exit(0);
}

