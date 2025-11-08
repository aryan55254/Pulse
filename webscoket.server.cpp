#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_size 4096

int main()
{
    // initizing variables
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // asking os to create a socket with tcp connection
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    // asks os to enable reuse address and reuse prot on the socket server_fd we created
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    // assign address family port and ip address on which our computer will listen to
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // bind the address to our socket
    // struct sockaddr * thing is becuase bind func can't directly accept the pointer to address variable
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    // start listening on our socket
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    std::cout << "Server listening on port " << PORT << "..." << std::endl;

    while (true)
    {
        // we wait for a clinet to connect after it conenct we return a new socket the client_socket while our server_fd keep listening for new connections
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("accept");
            continue;
        }
        // basically storing the ip of client in human readable format;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "new connection from " << client_ip << std::endl;

        char buffer[1024] = {0};
        recv(client_socket, buffer, 1024, 0);
        std::cout << "Client sent:\n"
                  << buffer << std::endl;

        const char *http_response = "HTTP/1.1 200OK\r\nContent-Type : text/plain\r\n\r\nHello , client!\r\n";
        send(client_socket, http_response, strlen(http_response), 0);

        std::cout << "Sent 'Hello'. Closing connection." << std::endl;
        close(client_socket);
    }
    close(server_fd);
    return 0;
}