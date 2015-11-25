#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <sys/fcntl.h>


#define BUFF_SIZE 2048

/*
sends appropriate responce code depending on the situation
*/
int sendResponseCode(int socket, int status)
{

  static char* NFResponse= "HTTP/1.1 404 Not Found\nContent-type: text/html\n\n<html>\n<body>\n<h1>Not Found</h1>\n<p>The requested URL %s was not found on this server.</p>\n</body>\n</html>\n";
  static char* BRResponse =
    "HTTP/1.1 400 Bad Request\n"
    "Content-type: text/html"
    "\r\n\r\n"
    "<html>\n"
    " <body>\n"
    "  <h1>Bad Request</h1>\n"
    "  <p>This server did not understand your request.</p>\n"
    " </body>\n"
    "</html>\n";
  static char* forbidden = "HTTP/1.1 403 Forbidden\nContent-type: text/html\n\n<html>\n<body>\n<h1>Forbidden</h1>\n<p>Sorry.</p>\n</body>\n</html>\n";

  switch (status)
  {
    case 0: //send ok response code
            write(socket, NFResponse, strlen(NFResponse));
            return 1;
    case 1: //send bad request resp code
            write(socket, BRResponse, strlen(BRResponse));
            return 1;
     case 2: //forbidden reuest responce code
            write(socket, forbidden, strlen(forbidden));
            return 1;
    default: //wrong responce code...
            printf("Invalid status code:%d\n", status);
            break;
  }

  return -1;
}

// returns the host
char* getHostByBuff(char* buff){
  char* temp = strdup(buff);
  char* token = (char*) malloc(sizeof(buff));
  token = strtok(temp, "\n");
  token = strdup(strtok(NULL, "\n"));
  free(temp);
  token[strlen(token)-1] = 0; // to take out /r
  return token;
}

// returns the address
char* getAddrByBuff(char* buff){
  char* temp = strdup(buff);
  char* token = (char*) malloc(sizeof(buff));
  token = strdup(strtok(temp, "\n"));
  free(temp);
  token[strlen(token)-1] = 0; // to take out /r
  return token;
}

// returns the port number (default is 80)
int getPortByBuff(char* buff){
  return 80;
}

int retContentLen(char* buff) {
  char* clen = strstr(buff, "Content-Length:");
  if (clen != NULL) {
    clen = &clen[strlen("Content-Length: \0")];
    char num[20];
    bzero (num, 20);
    int l = 0;
    for (l; l<20; l++) {
      char next = clen[l];
      if ((next == '\n')||(next == '\r')) {
        break;
      }
      else {
        num[l] = next;
      }
    }
    return atoi(num);
  }
  return -1;
}

int lenOfHeader(char* buff) {
  char* space = strstr(buff, "\r\n\r\n\0");
  int i = space-buff;
  printf("HEADER: %d", i);
  return i;
}

int retChunkLen (char* buff) {
    char* chunk = strstr(buff, "\r\n\r\n");
    chunk = &chunk[strlen("\r\n\r\n\0")];
    char num[20];
    bzero (num, 20);
    int l = 0;
    for (l; l<20; l++) {
      char next = chunk[l];
      if ((next == '\n')||(next == '\r')) {
        break;
      }
      else {
        num[l] = next;
      }
    }
    // because in hex
    return (int)strtol(num, NULL, 16);
}

int connectHost(char* addr, char* host, int port, int clientSock) {

  char* temp = strdup(host);
  char* hostaddr = strtok(temp, " ");
  hostaddr = strtok(NULL, " ");
  char* hname = strdup(hostaddr);

  char* data = (char*) malloc(BUFF_SIZE);
  sprintf(data, "%s\n%s\n\n", addr, host);
  //printf("===REQUEST====\n%s\n========\n", data);

  int hostSocket = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in serv_addr;
  struct hostent* server;
  server = gethostbyname(hname);
  if (server == NULL) {
       printf("gethostbyname() failed on: %s\n", hname);
       return -1;
  }
  bzero((char*) &serv_addr, sizeof(serv_addr));
  bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  if (connect(hostSocket, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0) {
    printf("Error Connecting To server...\n");
    return -1;
  }

  if (write(hostSocket, data, strlen(data)) < 0) {
    printf("Error with sending request to server...");
    return -1;
  }

  char responce[BUFF_SIZE];
  bzero(responce, BUFF_SIZE);
  int n = read(hostSocket, responce, BUFF_SIZE);
  // printf("%s\n", responce);
  // for(;;);
  int contlen = retContentLen(responce);
  int chunklen = retChunkLen(responce);
  printf("Content-Length:%d\n", contlen);
  printf("chunklen:%d\n", chunklen);
  int head = lenOfHeader(responce);
  head = head + strlen("\r\n\r\n");
  head = n - head;
  if (contlen != -1) {
  contlen = contlen-head;
    while(n > 0) {
      write(clientSock, responce, n);
      bzero(responce, BUFF_SIZE);
      if (contlen > BUFF_SIZE) {
        n = read(hostSocket, responce, n);
        contlen = contlen-n;
      } else {
        n = read(hostSocket, responce, contlen);
      }
    }
  }
  else {
    chunklen = chunklen-head;
    while(n > 0) {
      write(clientSock, responce, n);
      bzero(responce, BUFF_SIZE);
      if (chunklen > BUFF_SIZE) {
        n = read(hostSocket, responce, BUFF_SIZE);
        chunklen = chunklen-n;
      } else {
        read(hostSocket, responce, chunklen);
        n=0;
        continue;
      }
    }
  }
  close(hostSocket);
  free(temp);
  free(hname);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Please provide a port number (Eg. 6553)\n");
    return -1;
  }
  // opening the main socket here
  int proxySock = socket(AF_INET, SOCK_STREAM, 0);
  if (proxySock<0) {
    printf("Error opening main socket. Exiting..\n");
    return -1;
  }
  int portno = atoi(argv[1]);
  printf("Using port number:%d\n", portno);

  struct sockaddr_in serv_addr, cli_addr;
  bzero((char*) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(portno);
  serv_addr.sin_addr.s_addr = INADDR_ANY; // This is where you would put the server addr

  if (bind(proxySock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0){
    printf("There was an error binding the socket to the port provided. Exiting...\n");
    return -1;
  }

  listen(proxySock, 5);
  int clientSock;
  unsigned int clilen;
  char buff[BUFF_SIZE];

  printf("Waiting for connections...\n");
  for(;;) {
    bzero((char*) &cli_addr, sizeof(cli_addr));

    clilen = sizeof(cli_addr);
    clientSock = accept(proxySock, (struct sockaddr*) &cli_addr, &clilen);
    // if error returns back to waiting for connections.
    if (clientSock < 0) {
      printf("Error on accepting connection.\n");
      continue;
    }
    bzero(buff, BUFF_SIZE);
    if (read(clientSock, buff, BUFF_SIZE) < 0) {
      printf("Error reading client request.\n");
      continue;
    }
    char* addr = getAddrByBuff(buff);
    char* host = getHostByBuff(buff);
    int port = getPortByBuff(buff);

    connectHost(addr, host, port, clientSock);
  }
}