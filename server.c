#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#define BUFFER_SIZE 4096

// function to kill unused child processes
void sigchld_handler(int signo) {
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_get(int client_fd, const char *path) {
    printf("Handling GET request for: %s\n", path);
    char fullpath[512];

    if (strcmp(path, "/") == 0) {
        snprintf(fullpath, sizeof(fullpath), "www/index.html");
    } else {
        snprintf(fullpath, sizeof(fullpath), "www%s", path);
    }

    FILE *fp = fopen(fullpath, "rb");
    if (!fp) {
        const char *not_found =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 13\r\n"
            "Connection: close\r\n"
            "\r\n"
            "404 Not Found";
        send(client_fd, not_found, strlen(not_found), 0);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *content_type = "text/html";
    const char *ext = strrchr(fullpath, '.');
    if (ext != NULL) {
        if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) content_type = "text/html";
        else if (strcmp(ext, ".css") == 0) content_type = "text/css";
        else if (strcmp(ext, ".js") == 0) content_type = "application/javascript";
        else if (strcmp(ext, ".png") == 0) content_type = "image/png";
        else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(ext, ".gif") == 0) content_type = "image/gif";
        else if (strcmp(ext, ".ico") == 0) content_type = "image/x-icon"; 
        else if (strcmp(ext, ".txt") ==0) content_type = "text/plain"; 

        else content_type = "application/octet-stream";
    }

    char header[512];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              content_type, file_size);
    send(client_fd, header, header_len, 0);

    char file_buffer[BUFFER_SIZE];
    size_t nread;
    while ((nread = fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
        ssize_t sent = send(client_fd, file_buffer, nread, 0);
        if (sent < 0) {
            perror("Failed to send file data");
            break;
        }
    }

    fclose(fp);
}

void handle_request(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) return;
    buffer[n] = '\0';

    printf("Received request:\n%s\n", buffer);

    char *method = strtok(buffer, " \t\r\n");
    char *path = strtok(NULL, " \t\r\n");
    char *version = strtok(NULL, " \t\r\n");

    char response[256]; 

    if (method && path && version) {
        if (strcmp(version, "HTTP/1.1") != 0 || strcmp(version, "HTTP/1.0") != 0) {
            snprintf(response, sizeof(response),
             *response =
                "%s 505 HTTP Version Not Supported\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 25\r\n"
                "Connection: close\r\n"
                "\r\n"
                "HTTP Version Not Supported", version);
            send(client_fd, response, strlen(response), 0);
        } else if (strcmp(method, "GET") != 0) {
            const char *response =
                "%s 405 Method Not Allowed\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: 18\r\n"
                "Connection: close\r\n"
                "\r\n"
                "Method Not Allowed";
            send(client_fd, response, strlen(response), 0);
        } else {
            handle_get(client_fd, path);
        }
    } else {
        const char *response =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 11\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Bad Request";
        send(client_fd, response, strlen(response), 0);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port-number>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return 1;
    }

    // Reap child processes automatically
    signal(SIGCHLD, sigchld_handler);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        // fork to accept new requests. 
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            // Child process
            close(server_fd);
            handle_request(client_fd);
            close(client_fd);
            exit(0);
        } else {
            close(client_fd); 
        }
    }

    close(server_fd);
    return 0;
}
