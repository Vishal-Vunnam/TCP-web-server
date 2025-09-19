#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h> 
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#define BUFFER_SIZE 4096
#define TIMEOUT 10

// Global variables for graceful shutdown
static volatile sig_atomic_t running = 1;
static int server_file_descriptor_global = -1;

void sigchld_handler(int signal_number) {
    (void)signal_number;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

void sigint_handler(int signal_number) {
    (void)signal_number;
    printf("\n[SIGINT] Graceful shutdown initiated...\n");
    running = 0;
    
    if (server_file_descriptor_global != -1) {
        close(server_file_descriptor_global);
        server_file_descriptor_global = -1;
    }
}

int set_socket_timeout(int socket_file_descriptor, int timeout_seconds) {
    // This function configures the socket with a timeout
    // Any read after the socket closes will result in ERRNO
    struct timeval timeout_value;
    timeout_value.tv_sec = timeout_seconds;
    timeout_value.tv_usec = 0;
    
    if (setsockopt(socket_file_descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout_value, sizeof(timeout_value)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        return -1;
    }
    return 0;
}

void send_error_response(int client_file_descriptor, int status_code, const char *status_text, const char *body, bool keep_alive, const char *http_version) {
    char response[1024];
    int body_length = strlen(body);
    snprintf(response, sizeof(response),
        "%s %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "\r\n"
        "%s", http_version, status_code, status_text, body_length, keep_alive ? "keep-alive" : "close", body);
    
    ssize_t bytes_sent = send(client_file_descriptor, response, strlen(response), 0);
    if (bytes_sent < 0) {
        perror("Failed to send error response");
    }
}

void handle_get(int client_file_descriptor, const char *path, bool keep_alive, const char *http_version) {
    printf("Handling GET request for: %s\n", path);
    char full_path[512];

    snprintf(full_path, sizeof(full_path), "www%s", path);

    if (full_path[strlen(full_path) - 1] == '/' || (access(full_path, F_OK) == 0 && !access(full_path, R_OK))) {
        char index_html[512];
        snprintf(index_html, sizeof(index_html), "%sindex.html", full_path);
        if (access(index_html, F_OK) == 0) {
            strcpy(full_path, index_html);
        } else {
            snprintf(index_html, sizeof(index_html), "%sindex.htm", full_path);
            if (access(index_html, F_OK) == 0) {
                strcpy(full_path, index_html);
            }
        }
    } else {
        snprintf(full_path, sizeof(full_path), "www%s", path);
    }

    if (access(full_path, F_OK) == 0 && access(full_path, R_OK) != 0) {
        send_error_response(client_file_descriptor, 403, "Forbidden", "Forbidden", keep_alive, http_version);
        return;
    }

    FILE *file_pointer = fopen(full_path, "rb");
    if (!file_pointer) {
        const char *body = "404 Not Found";
        int body_length = strlen(body);
        char response[512];
        snprintf(response, sizeof(response),
            "%s 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n"
            "Connection: %s\r\n"
            "\r\n"
            "%s", http_version, body_length, keep_alive ? "keep-alive" : "close", body);
        
        ssize_t bytes_sent = send(client_file_descriptor, response, strlen(response), 0);
        if (bytes_sent < 0) {
            perror("Failed to send 404 response");
        }
        return;
    }

    fseek(file_pointer, 0, SEEK_END);
    long file_size = ftell(file_pointer);
    fseek(file_pointer, 0, SEEK_SET);

    // Make this into helper function?

    const char *content_type = "text/html";
    const char *file_extension = strrchr(full_path, '.');
    if (file_extension != NULL) {
        if (strcmp(file_extension, ".html") == 0 || strcmp(file_extension, ".htm") == 0) content_type = "text/html";
        else if (strcmp(file_extension, ".css") == 0) content_type = "text/css";
        else if (strcmp(file_extension, ".js") == 0) content_type = "application/javascript";
        else if (strcmp(file_extension, ".png") == 0) content_type = "image/png";
        else if (strcmp(file_extension, ".jpg") == 0 || strcmp(file_extension, ".jpeg") == 0) content_type = "image/jpeg";
        else if (strcmp(file_extension, ".gif") == 0) content_type = "image/gif";
        else if (strcmp(file_extension, ".ico") == 0) content_type = "image/x-icon"; 
        else if (strcmp(file_extension, ".txt") == 0) content_type = "text/plain"; 
        else {
            send_error_response(client_file_descriptor, 415, "Unsupported Media Type",
                                "File type not supported", keep_alive, http_version);
            fclose(file_pointer);
            return;
        }
    }

    char header[512];
    int header_length = snprintf(header, sizeof(header),
                              "%s 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: %s\r\n"
                              "\r\n",
                              http_version, content_type, file_size, keep_alive ? "keep-alive" : "close");
    
    ssize_t bytes_sent = send(client_file_descriptor, header, header_length, 0);
    if (bytes_sent < 0) {
        perror("Failed to send header");
        fclose(file_pointer);
        return;
    }

    char file_buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), file_pointer)) > 0) {
        bytes_sent = send(client_file_descriptor, file_buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send file data");
            break;
        }
    }

    fclose(file_pointer);
}


bool parse_connection_header(const char *request, bool *has_connection_header) {
    // this function parses the connection header, returns true if keep alive, false if close
    const char *connection_start = strcasestr(request, "Connection:");
    bool result = false;

    if (connection_start) {
        *has_connection_header = true;
        connection_start += strlen("Connection:");
        while (*connection_start == ' ' || *connection_start == '\t') {
            connection_start++;
        }

        if (strncasecmp(connection_start, "keep-alive", 10) == 0) {
            result = true;
        } else if (strncasecmp(connection_start, "close", 5) == 0) {
            result = false;
        }
    } else {
        *has_connection_header = false;
    }

    return result;
}

void handle_client_persistent(int client_file_descriptor) {
    // this function hangs until persistence ends or request is recieved
    // Once a request is recieved it starts timeout handle_timeout
    char buffer[BUFFER_SIZE] = {0};
    char request_copy[BUFFER_SIZE] = {0};
    
    while (running) {
        // Clear buffer for each request
        memset(buffer, 0, sizeof(buffer));
        memset(request_copy, 0, sizeof(request_copy));
        
        // Set timeout for this read
        set_socket_timeout(client_file_descriptor, TIMEOUT);
        
        ssize_t bytes_read = read(client_file_descriptor, buffer, sizeof(buffer) - 1);
        
        if (bytes_read == 0) {
            printf("Client closed connection (bytes_read=0)\n");
            break;
        }
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Connection timeout\n");
            } else {
                printf("Read error: %s\n", strerror(errno));
            }
            break;
        }
        
        buffer[bytes_read] = '\0';
        strcpy(request_copy, buffer); 
        printf("Received request:\n%s\n", buffer);

        // Parse request line first
        char *method = strtok(buffer, " \t\r\n");
        char *path = strtok(NULL, " \t\r\n");
        char *version = strtok(NULL, " \t\r\n");
        
        if (!method || !path || !version) {
            send_error_response(client_file_descriptor, 400, "Bad Request", "Bad Request", false, "HTTP/1.1");
            break;
        }
        
        if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
            send_error_response(client_file_descriptor, 505, "HTTP Version Not Supported", "HTTP Version Not Supported", false, version);
            break;
        }
        
        bool has_connection_header = false;
        bool keep_alive_from_header = parse_connection_header(request_copy, &has_connection_header);
        bool keep_alive;
        
        if (has_connection_header) {
            keep_alive = keep_alive_from_header;
        } else { 
            keep_alive = false; 
        }
        if (strcmp(method, "GET") != 0) {
            send_error_response(client_file_descriptor, 405, "Method Not Allowed", "Method Not Allowed", keep_alive, version);
            if (!keep_alive) break;
            continue;
        }
        
        handle_get(client_file_descriptor, path, keep_alive, version);
        
        if (!keep_alive) {
            printf("Connection: close requested, closing connection\n");
            break;
        }
        
        // Continues loop for keep-alive connections
    }
    
    shutdown(client_file_descriptor, SHUT_RDWR);
    close(client_file_descriptor);
    printf("Child (pid %d) connection handler exiting\n", getpid());
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port-number>\n", argv[0]);
        return 1;
    }

    int port_number = atoi(argv[1]);
    if (port_number <= 0 || port_number > 65535) {
        fprintf(stderr, "Invalid port number: %s\n", argv[1]);
        return 1;
    }

    // Set up signal handlers
    struct sigaction sigaction_child;
    sigaction_child.sa_handler = sigchld_handler;
    sigemptyset(&sigaction_child.sa_mask);
    sigaction_child.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sigaction_child, NULL) == -1) {
        perror("sigaction SIGCHLD");
        exit(EXIT_FAILURE);
    }


    // graceful exit variables
    struct sigaction sigaction_interrupt;
    sigaction_interrupt.sa_handler = sigint_handler;
    sigemptyset(&sigaction_interrupt.sa_mask);
    sigaction_interrupt.sa_flags = 0; 
    if (sigaction(SIGINT, &sigaction_interrupt, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }

    int server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server_file_descriptor < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // end of graceful exit variables

    server_file_descriptor_global = server_file_descriptor; // Store for signal handler (signal handler can access socket to exit gracefully)


    int option_value = 1;
    setsockopt(server_file_descriptor, SOL_SOCKET, SO_REUSEADDR, &option_value, sizeof(option_value));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_number);

    if (bind(server_file_descriptor, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_file_descriptor, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port_number);
    printf("Press Ctrl+C to shutdown gracefully.\n");
        
    while (running) {
        int client_file_descriptor = accept(server_file_descriptor, NULL, NULL);

        if (client_file_descriptor < 0) {
            if (errno == EINTR && !running) {
                printf("Accept interrupted during shutdown\n");
                break;
            }
            perror("accept");
            continue;
        }

        if (!running) {
            close(client_file_descriptor);
            break;
        }

        printf("New client connected\n");

        // child handles request, parent continues to wait
        pid_t process_id = fork();
        if (process_id < 0) {
            perror("fork failed");
            close(client_file_descriptor);
            continue;
        }

        if (process_id == 0) {
            // child -> close server socket and handle client
            close(server_file_descriptor);
            handle_client_persistent(client_file_descriptor);
            _exit(0);
        } else {
            // close parent client socket -> child has it
            close(client_file_descriptor); 
        }
    }
    printf("Shutting down server...\n");
    
    // check if server is open if not closes (for sig-int)
    if (server_file_descriptor_global != -1) {
        close(server_file_descriptor_global);
        server_file_descriptor_global = -1;
    }

    int status;
    pid_t child_process_id;
    int active_children = 0;
    sleep(1);
    
    while ((child_process_id = waitpid(-1, &status, WNOHANG)) > 0) {
        active_children++;
        printf("Child %d exited\n", child_process_id);
    }
    
    if (active_children > 0) {
        printf("Cleaned up %d child processes\n", active_children);
    }
    
    printf("Server shutdown complete.\n");
    return 0;
}
