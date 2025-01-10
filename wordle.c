#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <wait.h>
#include <stdbool.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define MAXBUFFER 5

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char ** words;
pthread_mutex_t mutex_guess = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_win = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_loss = PTHREAD_MUTEX_INITIALIZER;
char** dictionary_words;
int num_used = 1;
bool shutdown_server = false;
int num_words = 0;

typedef struct{
	int sd;
    short guesses_remain;
    char* hidden_word;
} Info;

int find(char* buffer){
    //Find if the 5 character word is a recognized wordle word
    for(char** ptr = dictionary_words; *ptr; ptr++){
        if(strcmp(*ptr, buffer) == 0){ return 1;}
    }
    return 0;
}

//Print out server shutdown messages and free memory
void sigusr1_handler(int signum) {
    printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
    printf("MAIN: Wordle server shutting down...");
    int temp = 0;
    while(temp < num_words){
        free(*(dictionary_words + temp));
        temp++;
    }
    free(dictionary_words);
    printf("\n");
    shutdown_server = true;
}

void* work(void* args){
    Info* info = args;
    char* buffer = calloc(MAXBUFFER + 1, sizeof(char));
    int reply_size = sizeof(char) + sizeof(short) + 6;
    void* reply = calloc(1, reply_size);
    int n = 1;
    do{
        printf("THREAD %lu: waiting for guess\n", pthread_self());
        n = recv(info->sd, buffer, MAXBUFFER, 0);
        if ( n == -1 ){
            perror( "recv() failed" );
        }else if ( n == 0 ){
            printf("THREAD %lu: client gave up; closing TCP connection...\n", pthread_self());
            printf("THREAD %lu: game over; word was %s!\n", pthread_self(), info->hidden_word);
            pthread_mutex_lock(&mutex_loss);
            total_losses++;
            pthread_mutex_unlock(&mutex_loss);
        }else{
            int win = 1;
            *(buffer + n) = '\0';
            printf("THREAD %lu: rcvd guess: %s\n", pthread_self(), buffer);
            int i;
            for(i = 0; i < n; i++){
                *(buffer + i) = toupper(*(buffer + i));
            }
            char valid_guess = 'N';
            char* result = calloc(6, sizeof(char));
            for(i = 0; i < 5; i++){*(result + i) = '?';}
            if(n == 5 && find(buffer)){ //If word is a valid guess
                pthread_mutex_lock(&mutex_guess);
                total_guesses++;
                pthread_mutex_unlock(&mutex_guess);
                valid_guess = 'Y';
                info->guesses_remain--;
                int i;
                for(i = 0; i < 5; i++){ //Check how many spots are correct
                    if(*(buffer + i) == *(info->hidden_word + i)){
                        *(result + i) = *(buffer + i);
                    }else{
                        win = 0;
                        int j;
                        for(j = 0; j < 5; j++){ //Check if right letter wrong place
                            if(*(info->hidden_word + j) == *(buffer + i)){
                                *(result + i) = tolower(*(buffer + i));
                                break;
                            }
                        }
                        if(j == 5){ //guessed letter does not exist anywhere
                            *(result + i) = '-';
                        }
                    }
                }
                *(result + 5) = '\0';
                int num = info->guesses_remain;
                if (num == 1){
                    printf("THREAD %lu: sending reply: %s (%d guess left)\n", pthread_self(), result, num);
                }else{
                    printf("THREAD %lu: sending reply: %s (%d guesses left)\n", pthread_self(), result, num);
                }
            }else{ //If word is not a valid guess
                win = 0;
                int num = info->guesses_remain;
                if (num == 1){
                    printf("THREAD %lu: invalid guess; sending reply: %s (%d guess left)\n", pthread_self(), result, num);
                }else{
                    printf("THREAD %lu: invalid guess; sending reply: %s (%d guesses left)\n", pthread_self(), result, num);
                }
            }
            *(char *) reply = valid_guess;
            *(short *)(reply + sizeof(char)) = htons(info->guesses_remain);
            strcpy(reply + sizeof(char) + sizeof(short), result);
            n = send(info->sd, reply, 8, 0);
            free(result);
            if(info->guesses_remain == 0){ //Game over client lost
                printf("THREAD %lu: game over; word was %s!\n", pthread_self(), info->hidden_word);
                pthread_mutex_lock(&mutex_loss);
                total_losses++;
                pthread_mutex_unlock(&mutex_loss);
                n = 0;
                break;
            }
            if(win == 1){ //Game over client won
                printf("THREAD %lu: game over; word was %s!\n", pthread_self(), info->hidden_word);
                pthread_mutex_lock(&mutex_win);
                total_wins++;
                pthread_mutex_unlock(&mutex_win);
                n = 0;
                break;               
            }

        }

    }while(n > 0);
    close(info->sd);
    free(reply);
    free(buffer);
    free(info->hidden_word);
    free(info); 
    return EXIT_SUCCESS;
}


int wordle_server( int argc, char ** argv ){
    //Signal to terminate the server
    if (signal(SIGUSR1, sigusr1_handler) == SIG_ERR) {
        perror("Error setting signal handler");
        return EXIT_FAILURE;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    srand(atoi(*(argv + 2)));
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    if (sd == -1)
    {
        perror("socket() failed: ");
        return EXIT_FAILURE;
    }
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    int port_num = atoi(*(argv + 1));
    server.sin_port = htons(port_num);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sd, (struct sockaddr *) &server, sizeof(server)) < 0)
    {
        perror("bind() failed: ");
        return EXIT_FAILURE;
    }
    socklen_t length;
    struct sockaddr_in client;
    length = sizeof(client);
    if (listen(sd, 5) < 0)
    {
        perror("listen() failed: ");
        return EXIT_FAILURE;
    }
    //Read in all the valid wordle words
    FILE* file = fopen(*(argv + 3), "r");
    num_words = atoi(*(argv + 4));
    dictionary_words = calloc((num_words + 1), sizeof(char*));
    char* line = calloc(7, sizeof(char));
    int count = 0;
    while(fgets(line, 7, file) && count < num_words){
        if (*(line + 5) == '\n') {
            *(line + 5) = '\0';
        }
        int i;
        for(i = 0; i < 5; i++){ *(line + i) = toupper(*(line + i));}
        *(dictionary_words + count) = calloc(6, sizeof(char));
        strncpy(*(dictionary_words + count), line, 6);
        count++;
    }
    fclose(file);
    free(line);
    *(dictionary_words + count) = NULL;

    printf("MAIN: opened %s (%d words)\n", *(argv + 3), num_words);
    printf("MAIN: Wordle server listening on port {%d}\n", port_num);
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    while(!shutdown_server){
        //Select used for ensuring no memory leaks by server getting stuck on blocking recv vall
        fd_set set;
        FD_ZERO(&set);
        FD_SET(sd, &set);
        int ready = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
        if (ready == -1) {
            continue;
        } else if (ready == 0) {
            //Gets called everytime timeout has run out
            if(shutdown_server){
                break;
            }
        }else{
            int newsd = accept(sd, (struct sockaddr *) &client, &length);
            printf("MAIN: rcvd incoming connection request\n");
            Info* info = calloc(1, sizeof(Info));
            info->sd = newsd;
            info->guesses_remain = 6;
            info->hidden_word = calloc(6, sizeof(char));
            int index = rand() % num_words;
            strcpy(info->hidden_word, *(dictionary_words + index));
            *(words + num_used - 1) = calloc(6, sizeof(char));
            strcpy(*(words + num_used - 1), *(dictionary_words + index));
            num_used++;
            words = (char**) realloc(words, num_used * sizeof(char*));
            *(words + num_used - 1) = NULL;
            pthread_t tid;
            pthread_create(&tid, NULL, &work, info);
            pthread_detach(tid);
        }

    }
    close(sd);
    return EXIT_SUCCESS;
}
