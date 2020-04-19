#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "err.h"

#define BUFFER_SIZE      1024

size_t scan_file(FILE *fp) {
  fseek(fp, 0L, SEEK_END);
  size_t sz = ftell(fp);
  rewind(fp);
  return sz;
}

int main(int argc, char *argv[]) {

  /* Kontrola argumentów ... */
  if (argc != 4) {
    fatal("Usage: %s host port", argv[0]);
  }

  // Adres połączenia
  size_t addr_len = strlen(argv[1]);
  size_t i;
  for (i = 0; i < addr_len && argv[1][i] != ':'; i++);
  if (i == addr_len) exit(1);
  argv[1][i] = '\0';
  i++;

  size_t port_len = addr_len - i;
  addr_len = i;
  char *port_str = malloc((port_len + 1) * sizeof(char));
  for (i = 0; i < port_len; i++)
    port_str[i] = argv[1][addr_len + i];
  port_str[port_len] = '\0';
  int port = atoi(port_str);
  printf("Server address: %s\nPort: %d\n", argv[1], port);

  // Plik ciasteczek
  FILE *fp = fopen(argv[2], "r");
  if (fp == NULL)
    syserr("file");
  size_t file_size = scan_file(fp);
  char line[BUFFER_SIZE];

  for (i = 0; i < file_size; i += BUFFER_SIZE) {
    memset(line, 0, BUFFER_SIZE);
    fread(line, 1, BUFFER_SIZE, fp);
    printf("%s", line);
  }
  printf("\n");
  fclose(fp);

  // Testowany adres HTTP
  addr_len = strlen(argv[3]);
  int slash_cnt = 2;
  for (i = 0; i < addr_len && slash_cnt != 0; i++) {
    if (argv[3][i] == '/')
      slash_cnt--;
  }
  if (slash_cnt != 0)
    syserr("bad address");

  size_t base = i;
  char *host = malloc((addr_len-base) * sizeof(char));

  for (; i < addr_len && argv[3][i] != '/'; i++)
    host[i-base] = argv[3][i];

  if (i == addr_len)
    syserr("bad address");
  host[i-base] = '\0';

  base = i;
  char *origin_form = malloc((addr_len-base+1) * sizeof(char));

  for (; i < addr_len; i++)
    origin_form[i-base] = argv[3][i];
  origin_form[i-base] = '\0';

  printf("host: %s\norigin_form: %s\n", host, origin_form);

  size_t request_size = sizeof(origin_form) + sizeof(host) + 80;
  char *request = malloc(request_size * sizeof(char));
  snprintf(request, request_size, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", origin_form, host);
  printf("Request: %s\n", request);
  free(origin_form);
  free(host);

  int rc, ret;
  int sock;
  struct addrinfo addr_hints, *addr_result;


  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    syserr("socket");
  }

  memset(&addr_hints, 0, sizeof(struct addrinfo));
  addr_hints.ai_flags = 0;
  addr_hints.ai_family = AF_INET;
  addr_hints.ai_socktype = SOCK_STREAM;
  addr_hints.ai_protocol = IPPROTO_TCP;

  rc = getaddrinfo(argv[1], port_str, &addr_hints, &addr_result);
  free(port_str);

  if (rc != 0) {
    fprintf(stderr, "rc=%d\n", rc);
    syserr("getaddrinfo: %s", gai_strerror(rc));
  }

  if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) != 0) {
    syserr("connect");
  }
  freeaddrinfo(addr_result);


  if (write(sock, request, strlen(request)) < 0)
    syserr("write socket – request");
  free(request);

  // write(sock, request, sizeof(request));

  for (;;) {
    printf("reading\n");
    memset(line, 0, sizeof(line));
    ret = read(sock, line, sizeof(line) - 1);
    if (ret == -1)
      syserr("read");
    else if (ret == 0)
      break;
    if (line[ret - 1] != '\n')
      line[ret++] = '\n';
    printf("%s\n", line);
  }


  // // wyślij nazwe pliku
  // if (write(sock, file_name, BUFFER_SIZE) < 0)
  //   syserr("write socket – file_name");

  // // wyślij plik
  // for (size_t i = 0; i < file_size; i += BUFFER_SIZE) {
  //   memset(line, 0, sizeof(line));
  //   fread(line, 1, BUFFER_SIZE, fp);
  //   if (write(sock, line, strlen(line)) < 0)
  //     syserr("write socket – file batch");
  // }
  // printf("WYSLANO\n");

  if (close(sock) < 0)
    syserr("close socket");
}