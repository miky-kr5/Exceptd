#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mysql.h>

#define PORT        9989
#define BACKLOG     42
#define BUFFER_SIZE 8192

static int             server_fd;
static MYSQL *         db_conn;
static sem_t           db_sem;

/* MySQL connection parameters. */
const char * mysql_server = "localhost";
const char * mysql_user   = "ocr";
const char * mysql_passwd = "$ocr&user*.";
const char * mysql_db     = "ocr_page";
const char * query_header = "INSERT INTO exception(date, exception, message) VALUES(\"";

/* Function prototypes. */
void * worker_thread(void * arg_ptr);
void * ping_thread(void * unused);

/* Argument definition for worker threads. */
struct thread_args{
  int    tid;
  int    client_fd;
  char * client_ip;
};

/* Clean up function for atexit(). */
void clean_up(){
  closelog();
  sem_wait(&db_sem);{
    mysql_close(db_conn);
  }sem_post(&db_sem);
  mysql_library_end();
}

/* Signal handler. */
void on_signal(int signal){
  close(server_fd);
}

int main(void){
  my_bool              reconnect = 1;
  int                  client_fd, tid = 1;
  socklen_t            sin_size;
  struct sockaddr_in   server;
  struct sockaddr_in   client;
  struct thread_args * t_args;
  pthread_t            t, ping;

  /* Convert the process into a daemon. */
  daemon(1, 1);

  /* Set up the system log. */
  openlog("exceptd", LOG_PID, LOG_DAEMON);
  syslog(LOG_NOTICE, "Exception report daemon started.");
  
  /* Set up clean up method and signal handlers. */
  atexit(clean_up);
  signal(SIGINT, on_signal);

  /* Connect to MySQL. */
  if(mysql_library_init(0, NULL, NULL)) {
    syslog(LOG_ERR, "could not initialize the MySQL library.");
    exit(EXIT_FAILURE);
  }
  db_conn = mysql_init(NULL);
  mysql_options(db_conn, MYSQL_OPT_RECONNECT, &reconnect);

  if(!mysql_real_connect(db_conn, mysql_server, mysql_user, mysql_passwd, mysql_db, 0, NULL, 0)) {
    syslog(LOG_ERR, "Could not connect to MySQL: %s", mysql_error(db_conn));
    exit(EXIT_FAILURE);
  }

  /* Create the server socket. */
  if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ){  
    syslog(LOG_ERR, "Error creating server socket."); 
    exit(EXIT_FAILURE);
  }

  server.sin_family = AF_INET;
  server.sin_port = htons(PORT);
  server.sin_addr.s_addr = INADDR_ANY;
  bzero(&(server.sin_zero), 8);

  /* Bind a file descriptor to the socket. */
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

  /* Initialize the database semaphore. */
  sem_init(&db_sem, 0, 1);

  /* Launch the keep alive thread. */
  pthread_create(&ping, NULL, ping_thread, NULL);

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
    pthread_detach(t);
  }

  /* Cleanly finish. */
  close(server_fd);

  pthread_cancel(ping);
  pthread_join(ping, NULL);

  syslog(LOG_NOTICE, "Exception report daemon terminated.");

  return EXIT_SUCCESS;
}

void * worker_thread(void *args_ptr){
  int                  num_bytes, len, i;
  char                 buffer[BUFFER_SIZE], query[BUFFER_SIZE + 512];
  char *               save_ptr;
  char *               token;
  MYSQL_RES *          query_result;
  struct thread_args * args = (struct thread_args *)args_ptr;

  /* Initialize the thread. */
  if(pthread_detach(pthread_self()) != 0){
    /* If the thread could not detach itself a memory leak is bound to happen,
       so we finish the daemon to avoid problems. */
    syslog(LOG_ERR, "The thread of id %d could not detach itself.", args->tid);
    syslog(LOG_ERR, "Stopping daemon.");

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);

    kill(getpid(), SIGINT);
    pthread_exit(NULL);
  }

  memset(buffer, '\0', BUFFER_SIZE * sizeof(char));

  /* Read from the client. */
  if((num_bytes = recv(args->client_fd, buffer, BUFFER_SIZE - 1, 0)) == -1){
    /* On error clean up and exit. */
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

  buffer[num_bytes] = '\0';

  /* Parse the received message. */
  memset(query, '\0', (BUFFER_SIZE + 512) * sizeof(char));

  /* Copy the header. */
  strncpy(query, query_header, strlen(query_header));
  len = strlen(query);

  /* Parse the date. */
  token = strtok_r(buffer, "!", &save_ptr);
  if(token != NULL){
    strncpy(&query[len], token, strlen(token));
    len = strlen(query);
    strncpy(&query[len], "\", \"", strlen("\", \""));
    len = strlen(query);
  }else{
    /* The received message has a wrong format. */
    syslog(LOG_ERR, "Badly formatted message received in thread %d.", args->tid);

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);

    pthread_exit(NULL);
  }

  /* Parse the exception name. */
  token = strtok_r(NULL, "!", &save_ptr);
  if(token != NULL){
    strncpy(&query[len], token, strlen(token));
    len = strlen(query);
    strncpy(&query[len], "\", \"", strlen("\", \""));
    len = strlen(query);
  }else{
    /* The received message has a wrong format. */
    syslog(LOG_ERR, "Badly formatted message received in thread %d.", args->tid);

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);

    pthread_exit(NULL);
  }

  /* Parse the stack trace. */
  token = strtok_r(NULL, "!", &save_ptr);
  if(token != NULL){
    strncpy(&query[len], token, strlen(token));
    len = strlen(query);
    strncpy(&query[len], "\");", strlen("\");"));
    len = strlen(query);
  }else{
    /* The received message has a wrong format. */
    syslog(LOG_ERR, "Badly formatted message received in thread %d.", args->tid);

    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);

    pthread_exit(NULL);
  }

  /* Replace all '#' for new lines and all '*' for tabs. */
  for(i = 0; query[i]; i++){
    if(query[i] == '#'){
      query[i] = '\n';
    }else if(query[i] == '*'){
      query[i] = '\t';
    }
  }

  /* Execute the query. */
  if(mysql_query(db_conn, query)){
    syslog(LOG_ERR, "Failed executing query: %s", query);
    syslog(LOG_ERR, "MySQL Output: %s", mysql_error(db_conn));
    
    close(args->client_fd);
    if(args->client_ip != NULL)
      free(args->client_ip);
    free(args);

    pthread_exit(NULL);
  }

  /* Clean up the query's result to avoid a memory leak. */
  sem_wait(&db_sem);{
    query_result = mysql_use_result(db_conn);
    mysql_free_result(query_result);
  }sem_post(&db_sem);

  /* Clean up and exit. */
  syslog(LOG_NOTICE, "Thread of id %d finished.", args->tid);

  close(args->client_fd);
  if(args->client_ip != NULL)
    free(args->client_ip);
  free(args);

  return NULL;
}

void * ping_thread(void* unused){
  unsigned long m_tid_before;
  unsigned long m_tid_after;

  while(1){
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    /* Ping the MySQL server and log reconnections.*/
    sem_wait(&db_sem);{ 
      m_tid_before = mysql_thread_id(db_conn);
      mysql_ping(db_conn);
      m_tid_after = mysql_thread_id(db_conn);

      if(m_tid_before != m_tid_after){
	syslog(LOG_NOTICE, "MySQL reconnected.");
      }
    }sem_post(&db_sem);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    /* Wait 10 minutes before pinging again. */
    sleep(600);
  }

  return NULL;
}
