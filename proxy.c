#include <stdio.h>
#include "csapp.h"
#include <stdlib.h>

//Jiaxing Han
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 16777216 
#define MAX_OBJECT_SIZE 8388608 

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

//Cache node structure
//Entries: 
//size of content
//uri
//object to write to client
typedef struct cacheNode {
  size_t size;
  char uri[MAXLINE];
  char *object;
  struct cacheNode *next;
  struct cacheNode *prev;
} cacheNode;

//Cache structure
//Doubly-linked list
typedef struct cacheList {
  cacheNode *head;
  cacheNode *tail;
} cacheList;

//Proxy funcitons
void parse_uri(char *uri, char *hostname, char *filename, char *port);
void request_header(rio_t *rio, char *request, char *hostname, char *header_buf);
void doit(int connfd, cacheList *listPtr);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

//cache functions
cacheList *initCache();
cacheNode *newCacheNode(char *uri);
void insertHead(cacheList *listPtr, cacheNode *pointer);
int removeTail(cacheList *listPtr);
int updateList(cacheList *listPtr, cacheNode *ptr);
cacheNode *checkCache(cacheList *listPtr, char *uri);
cacheNode *findCache(cacheList *listPtr, char *uri);
size_t cacheSize(cacheList *listPtr);
void showHead(cacheList *listPtr);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  cacheList *listPtr;

  //initialize cache list
  listPtr = initCache();
    
  if (argc!= 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  
  Signal(SIGPIPE, SIG_IGN);
  listenfd = open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *) & clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd, listPtr);
    Close(connfd);
  }

  return 0;
}

void doit(int connfd, cacheList *listPtr){
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], filename[MAXLINE],port[MAXLINE], reqtoserver[MAXLINE];
  char header_buf[MAXLINE], receive_buf[MAXLINE];
  size_t receive_len, cache_total;
  rio_t rio, s_rio;
  int serverfd;

  //Read request from client
  rio_readinitb(&rio, connfd);
  rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);
  //Ignore non-GET request
  if(strcasecmp(method, "GET")){
    clienterror(connfd, method, "501", "Not implemented", "Only GET method is implemented");
    return;
  }
  //Change version to 1.0
  if(!strcasecmp(version, "HTTP/1.1"))
    strcpy(version, "HTTP/1.0");
  //Traverse cache list and look for the requesting one
  struct cacheNode *nodePtr = checkCache(listPtr, uri);
  if (nodePtr != NULL){
    printf("Request object in cache. Writing.\n");
    rio_writen(connfd, nodePtr->object, nodePtr->size);
    return;
  }
  parse_uri(uri, hostname, filename, port); //parse uri to get hostname, filename and port
  sprintf(reqtoserver, "%s %s %s\r\n", method, filename, version); // Request send to server
  request_header(&rio, reqtoserver, hostname, header_buf); //Get headers
  printf("Request headers: \n%s", reqtoserver);
  
  if ((serverfd = Open_clientfd(hostname, port)) < 0){
    printf("Failed to connect to server\n");
    return;
  }

  rio_readinitb(&s_rio, serverfd);
  rio_writen(serverfd, reqtoserver, strlen(reqtoserver)); // Write request to server

  int total_len = 0;
  nodePtr = newCacheNode(uri); //Initialize a cache node
  char *w_object = nodePtr->object;

  //Get response from server and write to client
  while ((receive_len = rio_readlineb(&s_rio, receive_buf, MAXLINE)) > 0){
    rio_writen(connfd, receive_buf, receive_len);
    memcpy(w_object, receive_buf, receive_len);
    w_object += receive_len;
    total_len += receive_len;
  }

  Close(serverfd);

  //Add the cache node to list if satisfies the size restrictions
  cache_total = cacheSize(listPtr);
  if(total_len < MAX_OBJECT_SIZE){
    while((cache_total + total_len) > MAX_CACHE_SIZE){
      removeTail(listPtr);
      cache_total = cacheSize(listPtr);
    }
    nodePtr->size = total_len;
    insertHead(listPtr, nodePtr);
  }
}

void request_header(rio_t *rio, char request[], char hostname[], char header_buf[]){
  char hoststr[MAXLINE];
  int count = 0;
  int pc = 0;
  int con = 0; 
  sprintf(hoststr, "Host: %s\r\n", hostname);

  while ((Rio_readlineb(rio, header_buf, MAXLINE)) > 0){
    count++;
    
    if (strcmp(header_buf, "\r\n") == 0){
      break;
    } 
    
    if (count == 1 && strstr(header_buf, "Host:") == NULL){
      strcat(request, hoststr); //If the first header is not host, add costumized HOST header
    }

    if (strstr(header_buf, "User-Agent:") != NULL){
      strcat(request, user_agent_hdr); //If encounters User-Agent, change to costumized one
      continue;
    }
    
    if (strncmp(header_buf, "Connection:", strlen("Connection")) == 0){
      strcat(request, "Connection: close\r\n"); //If encounters Connection, change to Close
      con = 1;
      continue;
    }

    if (strstr(header_buf, "Proxy-Connection:") != NULL){
      pc = 1; 
      continue; //Set a flag indicate there is proxy-connection, and skip this one
    }
    
    strcat(request, header_buf);
  }
  if (con == 0){
    strcat(request, "Connection: close\r\n");
  }
  strcat(request, "Proxy-Connection: close\r\n\r\n"); //Add this to the end of headers
  return;
}

void parse_uri(char *uri, char *hostname, char *filename, char *port){
  char uri_temp[MAXLINE];
  memset(uri_temp, '\0', sizeof(uri_temp));
  strcpy(uri_temp, uri);
  char *titleptr = NULL;
  char *portptr = NULL;
  char *filenameptr = NULL;
  char *temp = strstr(uri_temp, "http://"); //Start parse after http://
  strcpy(port, "80"); //Default port to 80

  temp += 7;
  titleptr = temp;
  portptr = strstr(temp, ":"); //Locate the begining of port
  filenameptr = strstr(temp, "/"); //Locate the begining of filename

  if (filenameptr == NULL && portptr == NULL){ //No filename no port specified
    sscanf(titleptr, "%s", hostname);
    filename = "/";
  }else if (filenameptr != NULL && portptr == NULL){ //Only filename
    sscanf(titleptr, "%[^/]", hostname);
    sscanf(filenameptr, "%s", filename);
  }else if (filenameptr == NULL && portptr != NULL){ //Only port
    *portptr = ' ';
    sscanf(titleptr, "%s %s", hostname, port);
    filename = "/";
  }else if (filenameptr != NULL && portptr != NULL){ //Both filename and port
    sscanf(filenameptr, "%s", filename);
    *portptr = ' ';
    *filenameptr = '\0';
    sscanf(titleptr, "%s %s", hostname, port);
  }
  return; 
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{

  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>Proxy Server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "%sContent-type: text/html\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}
//
//
//
// Cache functions
//
cacheList *initCache(){
  struct cacheList *listPtr = (struct cacheList *)Malloc(sizeof(struct cacheList));
  listPtr->head = NULL;
  listPtr->tail = NULL;
  return listPtr;
}

cacheNode *newCacheNode(char *uri){
  cacheNode *newNode = (struct cacheNode *)Malloc(sizeof(struct cacheNode));
  memcpy(newNode->uri, uri, strlen(uri));
  newNode->object = (char *)Malloc(MAX_OBJECT_SIZE); 
  newNode->size = 0;
  return newNode;
}

void insertHead(cacheList *listPtr, cacheNode *pointer) {
  struct cacheNode *currentHead = listPtr->head;

  pointer->prev = NULL;
  listPtr->head = pointer;
  if (currentHead == NULL){
    pointer->next = NULL;
    listPtr->tail = pointer;//update tail pointer
  }else {
    pointer->next = currentHead;//connect the new node 
    pointer->prev = NULL;
    currentHead->prev = pointer;
  } 
}

int removeTail(cacheList *listPtr) {
  if (listPtr->head == NULL){//empty list
    return -1;
  }
  
  struct cacheNode *currentTail = listPtr->tail;
  //int value = currentTail->value;//get return value

  if(listPtr->head->next == NULL){//if only one element in the list 
    listPtr->head = NULL;//update both head and tail to be NULL
    listPtr->tail = NULL;
  }else {
    listPtr->tail = currentTail->prev;//disconnect everything properly
    listPtr->tail->next = NULL;
    currentTail->prev = NULL;
  }

  Free(currentTail->object);
  Free(currentTail); //free memory space 
  return 1;
}

int updateList(cacheList *listPtr, cacheNode *ptr){
  if (listPtr->head == NULL){
    printf("Empty cache\n");
    return 0;
  }

  if (listPtr->head == ptr)
    return 1;

  cacheNode *prevptr = ptr->prev;
  cacheNode *nextptr = ptr->next;

  ptr->prev = NULL;
  prevptr->next = nextptr;

  if (nextptr)
    nextptr->prev = prevptr;

  ptr->next = listPtr->head;
  listPtr->head->prev = ptr;

  listPtr->head = ptr;
  listPtr->tail = prevptr;
  return 1;
}

cacheNode *checkCache(cacheList *listPtr, char *uri){
  struct cacheNode *target = findCache(listPtr, uri);
  if (target == NULL){
    printf("Not cached. Sending request to server.\n");
    return NULL;
  }
  updateList(listPtr, target);
  return target;
}

cacheNode *findCache(cacheList *listPtr, char *uri){
  struct cacheNode *ptr = listPtr->head;
  while (ptr){
    if (strcmp(ptr->uri, uri) == 0){
      return ptr;
    }
    ptr = ptr->next;
  }
  return NULL;
}

size_t cacheSize(cacheList *listPtr){
  struct cacheNode *ptr = listPtr->head;
  size_t sum = 0;
  while (ptr){
    sum += ptr->size;
    ptr = ptr->next;
  }
  printf("Sum of cache: %d\n", (int)sum);
  return sum;
}
