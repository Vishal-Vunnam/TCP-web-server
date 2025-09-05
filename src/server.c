#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

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

    int server_fd, new_socket; 
    ssize_t valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    socklen_t addrlen = sizeof(address);
    char buffer[1024] = { 0 };
    char *hello = "Hello from server\n";

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Server listening on port %d...\n", port);

    // Accept client
    if ((new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen)) < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
    }
    printf("Client connected!\n");

    // Read from client
    valread = read(new_socket, buffer, sizeof(buffer) - 1);
    if (valread < 0) {
        perror("read failed");
    } else {
        buffer[valread] = '\0'; // Null-terminate
        printf("Received: %s\n", buffer);
    }

    buffer[strcspn(buffer, "\r\n")] = '\0';

    char *method = strtok(buffer, " \t\n");
    char *path = strtok(NULL, " \t\n");
    char *version = strtok(NULL, " \t\n");

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);
    printf("Version: %s\n", version);

    if(strcmp(method, "GET") == 0 ){ 
        printf("Method: GET");
    }
    else if (strcmp(method, "POST") == 0) { 
        print("Method POST");
    }
    // else if (strcmp(method, "PO")); 

send(new_socket, hello, strlen(hello), 0);
printf("Hello message sent\n");


    close(new_socket);
    close(server_fd);
    return 0; 
}
