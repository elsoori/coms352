#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include "encrypt-module.h"

/*
Authors: Matt Karmelich and Eranda Sooriyarachchi

This encryption driver uses 5 threads, the reader, the input counter, the output counter, the encryptor, and the writer
It drives an encrypt module to read a file, encrypt it, place the encrypted characters in an output file, and counts the input and output values
Also handles a reset of the encryption key
Runs with maximum concurrency

run using make encrypt

to recompile, run make clean and then make encrypt

run using ./encrypt *input file name* *output file name* *log file name*

*/


//buffer counts
int input_count, output_count;

//input and output buffers
char* input_buffer;
char* output_buffer;
//Declaration of semaphores used to indicate when thread should begin running
sem_t read_mutex, input_mutex, encrypt_mutex, output_mutex, write_mutex;
//Declation of semaphores used to indicate when a thread is complete
sem_t input_finished_mutex, encrypt_finished_mutex, output_finished_mutex, write_finished_mutex;

// input and output buffer sizes, respectively
int n, m;

pthread_mutex_t reader_lock; // locks reader for reset purposes
//Declation of semaphores used to indicate when a thread is complete for reset
sem_t input_signal, encrypt_signal, output_signal, write_signal;

// Reads each character of the file and puts it into the input buffer
void* readerThread() {
	char c;
	do {
		pthread_mutex_lock(&reader_lock); // locks the thread as the reader is functioning

		c = read_input(); // reads a character from the file

		input_buffer[input_count % n] = c; // puts character in each file, the modular operation makes the buffer a circular buffer

		// begins input counter and encryptor threads
		sem_post(&input_mutex);
		sem_post(&encrypt_mutex);
		// waits for input buffer slot to be free
		sem_wait(&input_finished_mutex);
		sem_wait(&encrypt_finished_mutex);

		// goes to next iteration of input buffer
		input_count++;

		pthread_mutex_unlock(&reader_lock);// unlocks the thread before the next iteration of the loop
	} while (c != EOF); // Loops until the end of file is reached
}

// reads each character in input buffer as they are entered and adds it to the count
void* inputCounterThread() {
	char c;
	do {
		sem_wait(&input_mutex); // wait for char to be put into buffer

		c = input_buffer[input_count % n]; // get char from circular buffer
		sem_post(&input_finished_mutex); // posts completion of input buffer use for reader
		sem_post(&input_signal); // signal completion of input bufferr use for reset

		if(c != EOF) count_input(c); // checks if EOF, if not, counts c
	} while (c != EOF); // Loops until the end of file is reached
}

// gets character from input, encrypts it, and places it into output
void* encryptorThread() {
	char c;
	do {
		sem_wait(&encrypt_mutex); // wait for char to be put into buffer

		c = input_buffer[input_count % n]; // get char from circular buffer
		sem_post(&encrypt_finished_mutex); // posts completion of input buffer use for reader

		output_buffer[output_count % m] = c != EOF ? (char) encrypt(c) : c; // will put encrypted character into output buffer if not EOF
		
		// begins output counter and encryptor threads
		sem_post(&output_mutex);
		sem_post(&write_mutex);

		// signal completion of input and output buffer use for reset
		sem_post(&encrypt_signal);

		// wait for free spot in output buffer
		sem_wait(&output_finished_mutex);
		sem_wait(&write_finished_mutex);

		// iterate to next step in output buffer
		output_count++;
	} while (c != EOF); // Loops until the end of file is reached
}

// reads output from output buffer and counts for logging
void* outputCounterThread(void* arg) {
	char c;
	do {
		sem_wait(&output_mutex); // wait for char to be put into buffer

		c = output_buffer[output_count % m]; // get char from output buffer
		sem_post(&output_finished_mutex); // post completion of output count use for encryptor
		
		sem_post(&output_signal); // post completion of output count use for reset

		if(c != EOF) count_output(c); // if not EOF, count output
	} while (c != EOF);  // Loops until the end of file is reached
}

// reads output buffer and places it into output file
void* writerThread(void* arg) {
	char c;
	do {
		sem_wait(&write_mutex); // wait for char to be put into buffer

		c = output_buffer[output_count % m]; // get encrypted character from output buffer
		
		// indicate completeion of use of output count for encryptor and writer
		sem_post(&write_finished_mutex);
		sem_post(&write_signal);
		
		if(c != EOF) write_output(c); // if not EOF, write character into output file

	} while (c != EOF);  // Loops until the end of file is reached
}

// when reset is called, locks reader, waits for other threads to catch up, resets buffr counter and logs result for current key
void reset_requested() {
	pthread_mutex_lock(&reader_lock);
	sem_wait(&input_signal);
	sem_wait(&encrypt_signal);
	sem_wait(&output_signal);
	sem_wait(&write_signal);
	input_count = 0;
	output_count = 0;
	log_counts();
}

// when called, unlocks reader
void reset_finished() {
	pthread_mutex_unlock(&reader_lock);
}


// main driver, takes in 3 files, input, output, and log, encrypts the input file placing the encryption inot the output file, and logging the result
int main(int argc, char *argv[]) {

	// checks for exactly 4 arguments
	if (argc != 4) {
		printf("Error: Must include an input file, an output file, and a log file in arguments\n");
		exit(1);
	}

	// initializes files for encrypt module
	init(argv[1], argv[2], argv[3]);

	// loops until recieving valid buffer input
	do {
		printf("\nEnter buffer size for the input: ");
		scanf("%d", &n);
		if (n < 1) printf("\nInvalid buffer, must be greater than 0\n");
	} while(n < 1);
	
	do {
		printf("Enter buffer size for the output: ");
		scanf("%d", &m);
		if (m < 1) printf("\nInvalid buffer, must be greater than 0\n");
	} while(m < 1);

	//allocates buffer size
	input_buffer = malloc(sizeof(char) * n);
	output_buffer = malloc(sizeof(char) * m); 

	// keeps track of buffer count
	input_count = 0;
	output_count = 0;

	// initializes semophores
	sem_init(&read_mutex, 0, 0);
	sem_init(&input_mutex, 0, 0);
	sem_init(&encrypt_mutex, 0, 0);
	sem_init(&output_mutex, 0, 0);
	sem_init(&write_mutex, 0, 0);
	sem_init(&input_finished_mutex, 0, 0);
	sem_init(&encrypt_finished_mutex, 0, 0);
	sem_init(&output_finished_mutex, 0, 0);
	sem_init(&write_finished_mutex, 0, 0);

	// initializes threads
	pthread_t reader, input_counter, encryptor, output_counter, writer;

	// initializes reader mutex
	pthread_mutex_init(&reader_lock, NULL);
	
	//creates threads
	pthread_create(&reader, NULL, readerThread, NULL);
	pthread_create(&input_counter, NULL, inputCounterThread, NULL);
	pthread_create(&encryptor, NULL, encryptorThread, NULL);
	pthread_create(&output_counter, NULL, outputCounterThread, NULL);
	pthread_create(&writer, NULL, writerThread, NULL);

	// runs threads
	pthread_join(reader, NULL);
	pthread_join(input_counter, NULL);
	pthread_join(encryptor, NULL);
	pthread_join(output_counter, NULL);
	pthread_join(writer, NULL);

	// destroys reader lock
	pthread_mutex_destroy(&reader_lock);

	// destroys all semophores
	sem_destroy(&read_mutex);
	sem_destroy(&input_mutex);
	sem_destroy(&encrypt_mutex);
	sem_destroy(&output_mutex);
	sem_destroy(&write_mutex);
	sem_destroy(&input_finished_mutex);
	sem_destroy(&encrypt_finished_mutex);
	sem_destroy(&output_finished_mutex);
	sem_destroy(&write_finished_mutex);

	printf("\nEnd of file reached.\n"); 
	log_counts();
}
