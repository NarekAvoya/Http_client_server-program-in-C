#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

#define BACKLOG 10
#define BUF_SIZE 4096
#define PROTOCOL "CHLP/1.0"
#define WWW_DIR "www"
#define UPLOADS_DIR "uploads"

typedef struct {
    int client_fd;
    struct sockaddr_in addr;
} client_info_t;


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
    char buf[1];
    size_t cap = 256;
    size_t len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;

    while (1) {
        ssize_t r = recv(fd, buf, 1, 0);
        if (r <= 0) {
            if (r == 0 && len == 0) { free(out); return NULL; }
            break;
        }
        char c = buf[0];
        if (c == '\n') break;
        if (len + 1 >= cap) {
            cap *= 2;
            out = realloc(out, cap);
            if (!out) return NULL;
        }
        out[len++] = c;
    }
   
    while (len > 0 && (out[len-1] == '\r' || out[len-1] == '\n')) len--;
    out[len] = '\0';
    return out;
}


int sanitize_resource(const char *res) {
    if (!res || res[0] != '/') return -1;
    
    if (strstr(res, "..")) return -1;
    return 0;
}


char *build_fs_path(const char *resource) {
    size_t len = strlen(WWW_DIR) + strlen(resource) + 2;
    char *p = malloc(len);
    if (!p) return NULL;
    snprintf(p, len, "%s%s", WWW_DIR, resource); '/'
	    return p;
}


int send_response(int client_fd, const char *status_code_msg, const char *body, size_t body_len) {
    char header[256];
    int hdr_len = snprintf(header, sizeof(header), "%s %s\nBody-Size: %zu\n\n", PROTOCOL, status_code_msg, body_len);
    if (hdr_len < 0) return -1;
    if (write_n(client_fd, header, (size_t)hdr_len) != hdr_len) return -1;
    if (body_len > 0 && write_n(client_fd, body, body_len) != (ssize_t)body_len) return -1;
    return 0;
}

void handle_get(int client_fd, const char *resource) {
    if (sanitize_resource(resource) < 0) {
        send_response(client_fd, "400 Bad Request", NULL, 0);
        return;
    }
    char *path = build_fs_path(resource);
    if (!path) { send_response(client_fd, "500 Internal Server Error", NULL, 0); return;}
    FILE *f = fopen(path, "rb");
    free(path);
    if (!f) {
        send_response(client_fd, "404 Not Found", NULL, 0);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) sz = 0;
    char *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); send_response(client_fd, "500 Internal Server Error", NULL, 0); return; }
    size_t read = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    send_response(client_fd, "200 OK", buf, read);
    free(buf);
}


void handle_post(int client_fd, const char *body, size_t body_len) {
   
    mkdir(UPLOADS_DIR, 0755);
    char filename[256];
    srand((unsigned)time(NULL) ^ getpid());
    int r = rand() % 100000;
    snprintf(filename, sizeof(filename), "%s/post_%ld_%d.txt", UPLOADS_DIR, time(NULL), r);
    FILE *f = fopen(filename, "wb");
    if (!f) {
        send_response(client_fd, "500 Internal Server Error", NULL, 0);
        return;
    }
    if (body_len > 0) fwrite(body, 1, body_len, f);
    fclose(f);
    send_response(client_fd, "200 OK", NULL, 0);
}


void handle_echo(int client_fd, const char *body, size_t body_len) {
    send_response(client_fd, "200 OK", body, body_len);
}

void *client_thread(void *arg) {
    client_info_t *ci = (client_info_t *)arg;
    int fd = ci->client_fd;
    char addrbuf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ci->addr.sin_addr, addrbuf, sizeof(addrbuf));
    printf("[+] Connection from %s:%d (fd=%d)\n", addrbuf, ntohs(ci->addr.sin_port), fd);
    free(ci);

    
    char *line = read_line(fd);
    if (!line) { close(fd); return NULL; }
   
    char method[32], resource[256], version[64];
    if (sscanf(line, "%31s %255s %63s", method, resource, version) < 3) {
        free(line);
        send_response(fd, "400 Bad Request", NULL, 0);
        close(fd);
        return NULL;
    }
    free(line);

    
    ssize_t body_size = 0;
    while (1) {
        char *h = read_line(fd);
        if (!h) { close(fd); return NULL; }
        if (strlen(h) == 0) { free(h); break; } 
            "Header-Name: value"
        char name[128], value[256];
        if (sscanf(h, " %127[^:]: %255[^\r\n]", name, value) == 2) {
            if (strcasecmp(name, "Body-Size") == 0) {
                body_size = atoll(value);
            }
        }
        free(h);
    }

    
    char *body = NULL;
    if (body_size > 0) {
        body = malloc((size_t)body_size + 1);
        if (!body) {
            send_response(fd, "500 Internal Server Error", NULL, 0);
            close(fd);
            return NULL;
        }
        ssize_t got = read_n(fd, body, (size_t)body_size);
        if (got < 0) {
            free(body);
            close(fd);
            return NULL;
        }
        body[body_size] = '\0';
    }

    
    if (strcasecmp(method, "GET") == 0) {
        handle_get(fd, resource);
    } else if (strcasecmp(method, "POST") == 0) {
        handle_post(fd, body, body_size);
    } else if (strcasecmp(method, "ECHO") == 0) {
        handle_echo(fd, body, body_size);
    } else {
        send_response(fd, "501 Not Implemented", NULL, 0);
    }

    if (body) free(body);
    close(fd);
    printf("[-] Connection closed (fd=%d)\n", fd);
    return NULL;
}

int main(int argc, char **argv) {
    int port = 8080;
    if (argc >= 2) port = atoi(argv[1]);
    if (port <= 0) port = 8080;

    mkdir(WWW_DIR, 0755); 
    mkdir(UPLOADS_DIR, 0755);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv;
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = INADDR_ANY;
    serv.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv, sizeof(serv)) < 0) { perror("bind"); close(sockfd); return 1; }
    if (listen(sockfd, BACKLOG) < 0) { perror("listen"); close(sockfd); return 1; }

    printf("Server listening on port %d (serving ./%s)\n", port, WWW_DIR);

    while (1) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int client_fd = accept(sockfd, (struct sockaddr *)&cli, &cli_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        client_info_t *ci = malloc(sizeof(client_info_t));
        if (!ci) { close(client_fd); continue; }
        ci->client_fd = client_fd;
        ci->addr = cli;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, ci);
        pthread_detach(tid);
    }

    close(sockfd);
    return 0;
}

