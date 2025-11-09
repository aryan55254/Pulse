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

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 8080
#define BUFFER_size 4096

// helper functions
// encodes raw bytes into a base64 string
std::string base64_encode(const unsigned char *input, int length)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);

    BUF_MEM *mem_ptr;
    BIO_get_mem_ptr(b64, &mem_ptr);
    std::string encoded(mem_ptr->data, mem_ptr->length);

    BIO_free_all(b64);
    return encoded;
}

// add all key value pairs in a hashmap
std::map<std::string, std::string> parse_http_request(char *buffer)
{
    std::map<std::string, std::string> headers;
    std::istringstream request(buffer);
    std::string line;

    std::getline(request, line);

    while (std::getline(request, line) && line != "\r")
    {
        std::size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos)
        {
            std::string header_name = line.substr(0, colon_pos);
            std::string header_value = line.substr(colon_pos + 2);
            if (!header_value.empty() && header_value.back() == '\r')
            {
                header_value.pop_back();
            }
            headers[header_name] = header_value;
        }
    }
    return headers;
}

// fucntion to add the parsed client key to the magic key and create the websocket protocol accepting key
std::string generate_websocket_accept_key(const std::string &client_key)
{
    const std::string magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + magic_string;

    unsigned char sha1_hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char *>(combined.c_str()), combined.length(), sha1_hash);
    return base64_encode(sha1_hash, SHA_DIGEST_LENGTH);
}

int main()
{
    // initializing variables
    int server_fd, client_socket;
    struct sockaddr_in address;
    struct sockaddr_in client_addr;
    int opt = 1;
    int addrlen = sizeof(address);
    int client_addrlen = sizeof(client_addr);

    // asking os to create a socket with tcp connection
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    // asks os to enable reuse address and reuse port on the socket server_fd we created
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
    // assign address family port and ip address on which our computer will listen to
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // bind the address to our socket
    // struct sockaddr * thing is because bind func can't directly accept the pointer to address variable
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
        // we wait for a client to connect after it connect we return a new socket the client_socket while our server_fd keep listening for new connections
        if ((client_socket = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addrlen)) < 0)
        {
            perror("accept");
            continue;
        }
        // basically storing the ip of client in human readable format;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        std::cout << "new connection from " << client_ip << std::endl;

        char buffer[BUFFER_size] = {0};
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_size - 1, 0);
        if (bytes_received < 0)
        {
            perror("recv failed");
            close(client_socket);
            continue;
        }
        else if (bytes_received == 0)
        {
            std::cout << "Client disconnected before sending data" << std::endl;
            close(client_socket);
            continue;
        }

        std::cout << "--- Client Handshake Request ---" << std::endl;
        std::cout << buffer << std::endl;
        std::cout << "--------------------------------" << std::endl;

        // parse http requests

        std::map<std::string, std::string> headers = parse_http_request(buffer);

        // check for ws headers
        if (headers.find("Upgrade") == headers.end() || headers["Upgrade"] != "websocket" || headers.find("Sec-WebSocket-Key") == headers.end() ||
            headers.find("Sec-WebSocket-Version") == headers.end() || headers["Sec-WebSocket-Version"] != "13")
        {
            std::cerr << "Invalid HTTP request (not a WebSocket upgrade)" << std::endl;
            const char *http_response = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send(client_socket, http_response, strlen(http_response), 0);
            close(client_socket);
            continue;
        }
        std::string client_key = headers["Sec-WebSocket-Key"];
        std::string accept_key = generate_websocket_accept_key(client_key);
        std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " +
            accept_key + "\r\n"
                         "\r\n";

        std::cout << "--- Server Handshake Response ---" << std::endl;
        std::cout << response << std::endl;
        std::cout << "---------------------------------" << std::endl;

        ssize_t bytes_sent = send(client_socket, response.c_str(), response.length(), 0);
        if (bytes_sent < 0)
        {
            perror("Handshake send failed");
            close(client_socket);
            continue;
        }
        std::cout << "Handshake successful. WebSocket connection established." << std::endl;
        std::cout << "Waiting for WebSocket frames..." << std::endl;

        while (true)
        {
            char frame_buffer[BUFFER_size] = {0};
            bytes_received = recv(client_socket, frame_buffer, BUFFER_size - 1, 0);
            if (bytes_received <= 0)
            {
                std::cout << "Client disconnected." << std::endl;
                break; // Exit this inner loop
            }
            std::cout << "Received " << bytes_received << " bytes of raw frame data." << std::endl;
        }
        close(client_socket);
        std::cout << "Connection closed." << std::endl;
    }
    close(server_fd);
    return 0;
}