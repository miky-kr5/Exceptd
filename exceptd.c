#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>

#include "daemonize.h"

#define PORT    9989
#define BACKLOG 42

static int server_fd;

/* Function prototypes. */
void * worker_thread(void * arg_ptr);

/* Argument definition for worker threads. */
struct thread_args{
  int    tid;
  int    client_fd;
  char * client_ip;
};

/* Clean up function for atexit(). */
void clean_up(){
  closelog();
}

/* Signal handler. */
void on_signal(int signal){
  close(server_fd);
}

int main(void){
  int                  client_fd, tid = 1;
  socklen_t            sin_size;
  struct sockaddr_in   server;
  struct sockaddr_in   client;
  struct thread_args * t_args;
  pthread_t            t;

  /* Convert the process into a daemon. */
  daemonize();
  openlog("exceptd", LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "Exception report daemon started.");
  
  /* Set up clean up method and signal handlers. */
  atexit(clean_up);
  signal(SIGINT, on_signal);

  /* Create the server socket. */
  if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ){  
    syslog(LOG_ERR, "Error creating server socket."); 
    exit(EXIT_FAILURE);
  }

  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  server.sin_addr.s_addr = INADDR_ANY;
  bzero(&(server.sin_zero), 8);

  if(bind(server_fd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1){
    syslog(LOG_ERR, "Error during bind().");
    exit(EXIT_FAILURE);
  }

  syslog(LOG_NOTICE, "Server socket open.");

  /* Start listening for clients. */
  if(listen(server_fd, BACKLOG) == -1){
    syslog(LOG_ERR, "Error during listen().");
    exit(EXIT_FAILURE);
  }

  while(1){
    /* Wait for clients to connect. */
    sin_size = sizeof(struct sockaddr_in);
    if((client_fd = accept(server_fd, (struct sockaddr *)&client, &sin_size)) == -1){
      syslog(LOG_ERR, "Error during accept() or SIGINT received."); 
      break;
    }
    syslog(LOG_NOTICE, "Client connected from: %s", inet_ntoa(client.sin_addr));

    /* Create arguments for a worker thread. */
    t_args = (struct thread_args *)malloc(sizeof(struct thread_args));
    if(t_args == NULL){
      syslog(LOG_ERR, "Could not allocate memory for thread arguments."); 
      close(client_fd);
      continue;
    }

    /* Fill the arguments. */
    t_args->client_ip = (char*)malloc(sizeof(char) * strlen(inet_ntoa(client.sin_addr)));
    if(t_args->client_ip != NULL){
      strcpy(t_args->client_ip, inet_ntoa(client.sin_addr));
    }else{
      syslog(LOG_ERR, "Could not allocate memory for the client ip.");
    }
    t_args->tid = tid;
    t_args->client_fd = client_fd;

    /* Launch worker thread. */
    if(t_args->client_ip != NULL){
      syslog(LOG_NOTICE, "Creating worker thread for client at: %s", t_args->client_ip);
    }
    pthread_create(&t, NULL, worker_thread, t_args);
  }

  /* Cleanly finish. */
  close(server_fd);
  syslog(LOG_NOTICE, "Exception report daemon terminated.");

  return EXIT_SUCCESS;
}

void * worker_thread(void *args_ptr){
  int num_bytes;
  char buffer[512];
  struct thread_args * args = (struct thread_args *)args_ptr;

  pthread_detach(pthread_self());

  /* Read from the client. */
  if((num_bytes = recv(args->client_fd, buffer, 511, 0)) == -1){
    /* On error log, clean up and exit. */
    if(args->client_ip != NULL){
      syslog(LOG_ERR, "Error receiving from client at: %s", args->client_ip);
    }else{
      syslog(LOG_ERR, "Error receiving from client.");
    }

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);
    pthread_exit(NULL);
  }

  /* Log the client message. */
  buffer[num_bytes] = '\0';

  if(args->client_ip != NULL){
    syslog(LOG_NOTICE, "Received the following message: %s - from client at: %s", buffer, args->client_ip);
  }else{
    syslog(LOG_ERR, "Received the following message: %s.", buffer);
  }

  /* Echo back to the client. */
  if(send(args->client_fd, buffer, strlen(buffer), 0) == -1){
    /* On error log, clean up and exit. */
    if(args->client_ip != NULL){
      syslog(LOG_ERR, "Error echoing to client at: %s", args->client_ip);
    }else{
      syslog(LOG_ERR, "Error echoing to client.");
    }

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);
    pthread_exit(NULL);
  }

  /* Clean up and exit. */
  close(args->client_fd);

  if(args->client_ip != NULL)
    free(args->client_ip);
  free(args);

  return NULL;
}
