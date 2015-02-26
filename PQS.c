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
#include <errno.h>


/* Random # below threshold indicates H; otherwise C. */
#define TRUE   1
#define FALSE  0


/* Global / shared variables */
pthread_mutex_t c_mutex, ch_mutex;
pthread_mutex_t mutex;


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

/* Determine target number of molecules based on number of H atoms and C atoms */
int compute_max_molecules (int hNum, int cNum)
{
	int max_possible_molecules = cNum/2;
	/*take min of theoretical max (given #C) and #H*/
	if(max_possible_molecules > hNum) max_molecules = hNum;
	else max_molecules = max_possible_molecules;
	//fprintf(stdout, "Max molecules: %d\n\n", max_molecules);
	return max_molecules;
}

void printMessage(int atom, int type)
{
  	char type_char; 
  	if(type == HYDROGEN) type_char = 'H';
  	else type_char = 'C';

  	pthread_mutex_lock(&mutex);
  	fprintf(stdout, "A ethynyl radical was made by actions of %c%d.\n", type_char, atom);
  	fprintf(stdout, "Radical composition: H%d, C%d, C%d.\n", h_buffer[0], c_buffer[0], c_buffer[1]);
  	fprintf(stdout, "Molecule #%d.\n\n", radical_counter);
  	pthread_mutex_unlock(&mutex);
}

/*make calls to release remaining threads */
void terminate ()
{
    fprintf(stdout, "Actual molecules = Target molecules.\n");
    fprintf(stdout, "Terminate program - release all blocked threads..\n");
    terminate = TRUE;
    int h_remaining = hNum - radical_counter;
    int c_remaining = cNum - (2*radical_counter);
    int i;
    for (i=0; i<h_remaining; i++){
        sem_post(&h_sem);
    }
    for (i=0; i<c_remaining; i++){
        pthread_mutex_unlock(&c_mutex);
        sem_post(&c_sem);
    }
    
}

/* Create a radical and reset buffers for next radical */
void makeRadical(int atom, int type)
{
    radical_counter++;
    /*printMessage(atom, type);*/
  
    char type_char;
    if(type == HYDROGEN) type_char = 'H';
    else type_char = 'C';

    pthread_mutex_lock(&mutex);
    fprintf(stdout, "\nA ethynyl radical was made by actions of %c%d.\n", type_char, atom);
    fprintf(stdout, "Molecule #%d - radical composition: H%d, C%d, C%d.\n\n", radical_counter, h_buffer[0], c_buffer[0], c_buffer[1]);
    pthread_mutex_unlock(&mutex);

    /*reset buffers*/
    h_buffer[0] = c_buffer[0] = c_buffer[1] = 0;
    
    /*reset semaphores*/
    if(type == HYDROGEN){
        sem_post(&c_molecule_complete);
        sem_post(&c_molecule_complete);
    }else{
        sem_post(&c_molecule_complete);
        sem_post(&h_molecule_complete);
    }

    /*track max computations */
    max_molecules = compute_max_molecules(hNum, cNum);

    if(radical_counter==final_max_molecules) terminate();
}

/* Allow H atom into buffer and block all others until buffer free */
void *hReady( void *arg )
{
	int id = *((int *)arg);
	pthread_mutex_lock(&mutex);
	printf("hydrogen %d is alive\n", id);
	pthread_mutex_unlock(&mutex);

	/* only one H atom can enter here at a time */
    sem_wait(&h_sem);

	/* need to block C atoms as well when adding H atom to buffer */
    pthread_mutex_lock(&ch_mutex);
	h_in = h_in % HYDROGEN_SIZE;
	h_buffer[0] = id;
	pthread_mutex_lock(&mutex);
  	fprintf(stdout, "H%d in buffer.\n", id);
	pthread_mutex_unlock(&mutex);
	h_in++;
		
	if(terminate){
	  /*pthread_mutex_lock(&mutex);*/
	  printf("terminate - return h%d\n", id);
	  /*pthread_mutex_unlock(&mutex);*/
          exit(1);
	}
	
	/*if carbon buffer full, call makeRadical*/
	if(c_buffer[0] && c_buffer[1]){
	  makeRadical(id, HYDROGEN);
	  pthread_mutex_unlock(&ch_mutex);
    /*otherwise, wait for a C atom to complete the radical*/
	}else{
	  pthread_mutex_unlock(&ch_mutex);
	  sem_wait(&h_molecule_complete);		
	}
	
	pthread_mutex_lock(&mutex);
	printf("return h%d\n", id);
	pthread_mutex_unlock(&mutex);

	sem_post(&h_sem); 

	return arg;   
}


void *cReady( void *arg )
{
	int id = *((int *)arg);
	pthread_mutex_lock(&mutex);
	printf("carbon %d is alive\n", id);
	pthread_mutex_unlock(&mutex);
	
	/*Two C atoms can enter here */
    	sem_wait(&c_sem);
	
    	pthread_mutex_lock(&c_mutex);
	if(terminate){  
	  printf("terminate - return c%d\n", id);
          exit(1);
	}
	pthread_mutex_unlock(&c_mutex);

	/* But only one C atom at a time here */
    	pthread_mutex_lock(&c_mutex);

	/* Need to block H atoms as well when adding C atom to buffer */
	pthread_mutex_lock(&ch_mutex);
    
	/*if hydrogen buffer and other carbon buffer full, call makeRadical*/
	if(h_buffer[0] && (c_buffer[0] || c_buffer[1])){
	  if (!c_buffer[0]) c_buffer[0] = id;
	  else c_buffer[1] = id;
	  fprintf(stdout, "C%d in buffer.\n", id);
	  makeRadical(id, CARBON);
	  pthread_mutex_unlock(&ch_mutex);
	  pthread_mutex_unlock(&c_mutex); 
	/*otherwise, add carbon atom to carbon buffer and wait for another atom to call makeRadical*/
	}else{
	  if (!c_buffer[0]){ 
		c_buffer[0] = id;
		fprintf(stdout, "C%d in buffer.\n", id);
	  }
	  else {
		c_buffer[1] = id;
		fprintf(stdout, "C%d in buffer.\n", id);
	  }
	  pthread_mutex_unlock(&ch_mutex);
	  pthread_mutex_unlock(&c_mutex);
	  sem_wait(&c_molecule_complete);
	}

	pthread_mutex_lock(&mutex);
	printf("return c%d\n", id);
	pthread_mutex_unlock(&mutex);

	sem_post(&c_sem);

	return arg;
}

int main(int argc, char *argv[])
{
	
	//local variables

	if ( argc != 2  ) {
		fprintf(stderr, "usage: PQS <file name>\n");
		exit(1);
	}

	//process file - argv[1]
	//save # of threads
	//create LL of customer structs (
	
	init();
	
	//create threads
	//call main thread function - implement Wu's algorithm	

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

	/* join threads */
	for (i=0; i<numAtoms; i++){
	  pthread_join(*atom[i], NULL);
	}

	exit(0);
}

