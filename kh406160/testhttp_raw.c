#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "err.h"

#define BUFFER_SIZE    5123

typedef struct {
  int sock;

  size_t ind;
  size_t end;
  char *line;

  int is_chunked;
  char **cookies;
  size_t cookies_max;
  size_t cookies_num;
  size_t sum;
} reader_t;

size_t scan_file(FILE *fp) {
  fseek(fp, 0L, SEEK_END);
  size_t sz = ftell(fp);
  rewind(fp);
  return sz;
}

size_t left(reader_t *r) {
  return r->end - r->ind + 1;
}

int read_socket(reader_t *r) {
  memset(r->line, 0, BUFFER_SIZE);
  int ret = read(r->sock, r->line, BUFFER_SIZE - 1);
  if (ret < 0)
    syserr("read");
  r->ind = 0;
  r->end = (size_t)(ret - 1);
  return ret;
}

size_t next(reader_t *r) {
  r->ind++;
  if (r->ind > r->end)
    read_socket(r);
  return 1;
}

size_t go_until(reader_t *r, char end_char) {
  size_t res = 0;
  while (r->line[r->ind] != end_char)
    res += next(r);
  return res;
}

size_t skip_amount(reader_t *from, size_t amount) {
  size_t skipped = 0;
  while (skipped++ != amount)
    next(from);
  return amount;
}

size_t copy_until(reader_t *from, char *to, char end_char) {
  size_t copied = 0;
  while (from->line[from->ind] != end_char) {
    to[copied++] = from->line[from->ind];
    next(from);
  }
  to[copied] = '\0';
  return copied;
}

size_t next_line(reader_t *r) {
  size_t distance = go_until(r, '\n');
  return next(r) + distance;
}

char current_char(reader_t *r) {
  if (r->ind > r->end)
    read_socket(r);
  return r->line[r->ind];
}

int parse_status(reader_t *r) {
  char stat_str[40];
  memset(stat_str, 0, 40);

  read_socket(r);
  go_until(r, ' ');
  next(r);
  copy_until(r, stat_str, '\r');
  next_line(r);

  if (strcmp("200 OK", stat_str) == 0)
    return 1;

  printf("%s\n", stat_str);
  return 0;
}

int read_field(reader_t *r) {
  if (current_char(r) == '\r') {
    next(r);
    if (current_char(r) == '\n') //raczej nie zajdzie sytuacja przeciwna
      return 0;
  }
  return 1;
}

void get_cookie(reader_t *r) {
  size_t i = 0;
  if (r->cookies_num == r->cookies_max) {
    r->cookies_max *= 2;
    r->cookies = realloc(r->cookies, r->cookies_max * sizeof(char));
  }
  size_t cookie_size = BUFFER_SIZE;
  r->cookies[r->cookies_num] = malloc(cookie_size * sizeof(char));
  while (current_char(r) != '\r' && current_char(r) != ';') {
    if (i + 2 >= cookie_size) {
      cookie_size *= 2;
      r->cookies[r->cookies_num] = realloc(r->cookies[r->cookies_num], cookie_size * sizeof(char));
    }
    r->cookies[r->cookies_num][i++] = current_char(r);
    next(r);
  }
  r->cookies[r->cookies_num][i] = '\0';
  r->cookies_num++;
}

void parse_header(reader_t *r) {
  char token[BUFFER_SIZE], value[BUFFER_SIZE];
  memset(token, 0, BUFFER_SIZE);
  memset(value, 0, BUFFER_SIZE);
  while(read_field(r)) {
    copy_until(r, token, ':');
    next(r); // usuń ':'
    next(r); // usuń ' '
    if (strcmp(token, "Transfer-Encoding") == 0) {
      copy_until(r, value, '\r');
      if (strcmp(value, "chunked") == 0)
        r->is_chunked = 1;
    }
    else if (strcmp(token, "Set-Cookie") == 0) {
      get_cookie(r);
    }
    next_line(r);
  }
  next_line(r);
}

size_t parse_not_chunked(reader_t *r) {
  r->sum = left(r);
  while (left(r) > 0) {
    read_socket(r);
    r->sum += left(r);
  }
  return r->sum;
}

size_t get_chunk_size(reader_t *r) {
  char line[20];
  char *endPtr;
  memset(line, 0, 20);
  copy_until(r, line, '\r');
  next_line(r);
  return strtol(line, &endPtr, 16);
}

size_t parse_chunked(reader_t *r) {
  size_t chunk_size = get_chunk_size(r);
  while (chunk_size != 0 ) {
    r->sum += chunk_size;
    skip_amount(r, chunk_size);
    next_line(r);

    chunk_size = get_chunk_size(r);
  }

  return r->sum;
}

void report(reader_t *r) {
  printf("%ld\n", r->cookies_num);
  for (size_t i = 0; i < r->cookies_num; i++)
    printf("%s\n", r->cookies[i]);
  printf("Dlugosc zasobu: %ld\n", r->sum);
}

int main(int argc, char *argv[]) {

  /****************************** Kontrola argumentów ********************************/
  if (argc != 4)
    fatal("Usage: %s host port", argv[0]);

  /****************************** Adres połączenia ***********************************/
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

  /***************************** Plik ciasteczek *************************************/
  FILE *fp = fopen(argv[2], "r");
  if (fp == NULL)
    syserr("file");
  size_t file_size = scan_file(fp);
  char *cookies = malloc(file_size * 2 * sizeof(char));
  char line[BUFFER_SIZE];

  size_t cookie_it = 0, j, read_amount;
  for (i = 0; i < file_size; i += BUFFER_SIZE) {
    memset(line, 0, BUFFER_SIZE);
    read_amount = fread(line, 1, BUFFER_SIZE, fp);
    for (j = 0; j < read_amount; j++) {
      if (line[j] == '\n') {
        cookies[cookie_it++] = ';';
        cookies[cookie_it++] = ' ';
      } else {
        cookies[cookie_it++] = line[i+j];
      }
    }
  }
  fclose(fp);

  /**************************** Testowany adres HTTP *********************************/
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

  /************************************* REQUEST *************************************/

  size_t request_size = strlen(origin_form) + strlen(host) + file_size * 2 + 80;
  char *request = malloc(request_size * sizeof(char));
  snprintf(request, request_size, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nCookie: %s\r\n\r\n", origin_form, host, cookies);
  free(cookies);
  free(host);
  free(origin_form);

  /************************************ Połączenie ***********************************/

  int rc;
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

  /******************************* Wysłanie rządania *********************************/

  if (write(sock, request, strlen(request)) < 0)
    syserr("write socket – request");
  free(request);

  /****************************** Odebranie odpowiedzi *******************************/
  reader_t *r = malloc(sizeof(reader_t));
  r->line = line;
  r->sock = sock;
  r->is_chunked = 0;
  r->cookies_num = 0;
  r->cookies_max = 100;
  r->cookies = malloc(r->cookies_max * sizeof(char*));
  r->sum = 0;

  if (parse_status(r)) {
    parse_header(r);
    if (r->is_chunked)
      parse_chunked(r);
    else
      parse_not_chunked(r);
    report(r);
  }

  /***************************** Zakończenie połączenia ******************************/
  for (i = 0; i < r->cookies_num; i++)
    free(r->cookies[i]);
  free(r->cookies);
  free(r);
  if (close(sock) < 0)
    syserr("close socket");
}