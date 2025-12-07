#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     // for strcasecmp
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>     

#define PROTOCOL "CHLP/1.0"
#define BUF_SIZE 4096


ssize_t write_n(int fd, const void *buf, size_t n) {
    size_t left = n;
    const char *p = buf;
    while (left > 0) {
        ssize_t w = send(fd, p, left, 0);
        if (w <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        left -= w;
        p += w;
    }
    return n;
}


ssize_t read_n(int fd, void *buf, size_t n) {
    size_t left = n;
    char *p = buf;
    while (left > 0) {
        ssize_t r = recv(fd, p, left, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        } else if (r == 0) {
            break;
        }
        left -= r;
        p += r;
    }
    return n - left;
}


char *read_line(int fd) {
    char cbuf[1];
    size_t cap = 256, len = 0;
    char *s = malloc(cap);
    if (!s) return NULL;
    while (1) {
        ssize_t r = recv(fd, cbuf, 1, 0);
        if (r <= 0) {
            if (r == 0 && len == 0) { free(s); return NULL; }
            break;
        }
        char c = cbuf[0];
        if (c == '\n') break;
        if (len + 1 >= cap) { cap *= 2; s = realloc(s, cap); if (!s) return NULL; }
        s[len++] = c;
    }
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n')) len--;
    s[len] = '\0';
    return s;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s <host> <port> GET /path\n", argv[0]);
        fprintf(stderr, "  %s <host> <port> POST /path bodyfile\n", argv[0]);
        fprintf(stderr, "  %s <host> <port> ECHO /path bodyfile\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *method = argv[3];
    const char *resource = argv[4];

    char *body = NULL;
    size_t body_len = 0;

    if (strcasecmp(method, "POST") == 0 || strcasecmp(method, "ECHO") == 0) {
        if (argc < 6) {
            fprintf(stderr, "POST/ECHO requires a body file argument\n");
            return 1;
        }
        const char *bodyfile = argv[5];
        FILE *f = fopen(bodyfile, "rb");
        if (!f) { perror("fopen"); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) sz = 0;
        body_len = (size_t)sz;
        body = malloc(body_len + 1);
        if (!body) { fclose(f); return 1; }
        fread(body, 1, body_len, f);
        fclose(f);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);

    
    struct in_addr addr;
    if (inet_pton(AF_INET, host, &addr) <= 0) {
        struct hostent *h = gethostbyname(host);
        if (!h) {
            perror("gethostbyname");
            close(sock);
            return 1;
        }
        addr = *(struct in_addr *)h->h_addr;
    }
    serv.sin_addr = addr;

    if (connect(sock, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(sock);
        return 1;
    }

   
    char header[512];
    int hlen = snprintf(header, sizeof(header), "%s %s %s\nBody-Size: %zu\n\n", method, resource, PROTOCOL, body_len);
    if (write_n(sock, header, (size_t)hlen) != hlen) { perror("send header"); close(sock); return 1; }
    if (body_len > 0) {
        if (write_n(sock, body, body_len) != (ssize_t)body_len) { perror("send body"); close(sock); return 1; }
    }

    
    char *status_line = read_line(sock);
    if (!status_line) { fprintf(stderr, "no response\n"); close(sock); return 1; }
    printf("%s\n", status_line);
    free(status_line);

  
    ssize_t resp_body_size = 0;
    while (1) {
        char *h = read_line(sock);
        if (!h) break;
        if (strlen(h) == 0) { free(h); break; }
        printf("%s\n", h);
        char name[128], value[256];
        if (sscanf(h, " %127[^:]: %255[^\r\n]", name, value) == 2) {
            if (strcasecmp(name, "Body-Size") == 0) resp_body_size = atoll(value);
        }
        free(h);
    }
    printf("\n");

   
    if (resp_body_size > 0) {
        char *resp_body = malloc((size_t)resp_body_size + 1);
        if (!resp_body) { close(sock); return 1; }
        ssize_t got = read_n(sock, resp_body, (size_t)resp_body_size);
        if (got < 0) { perror("read"); free(resp_body); close(sock); return 1; }
        resp_body[got] = '\0';
        fwrite(resp_body, 1, got, stdout);
        printf("\n");
        free(resp_body);
    }

    if (body) free(body);
    close(sock);
    return 0;
}

