#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include "messages.h"
#include <arpa/inet.h>

using namespace std;

#define MAX_BUFFER 4096
#define PORT 8080

// Helper to receive a serialized string
string recv_string(int socket) {
    uint32_t len;
    recv(socket, &len, sizeof(len), 0);  // Receive the length
    char buffer[len + 1];
    recv(socket, buffer, len, 0);  // Then receive the string
    buffer[len] = '\0';  // Null-terminate the string
    return string(buffer);
}

string hash_file (const string& path) {
    ifstream file("client_files/"+path, ios::binary);
    if (!file) {
        perror("Could not open file!");
        return "";
    }
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    char buffer[4096];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        SHA256_Update(&sha256, buffer, file.gcount());
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    stringstream hex_hash;
    hex_hash << hex << setfill('0');
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        hex_hash << hex << setw(2) << (int)hash[i];
    }
    return hex_hash.str();
}

vector<string> list_request(int clientSock) {
    Header header;
    header.type = LIST;
    header.size = 0;

    ListRequest req;
    req.header = header;

    // Send LIST request
    if (send(clientSock, &req, sizeof(req), 0) < 0) {
        perror("Could not send LIST request!");
        return {};
    }

    // Receive header
    ListResponse resp;
    if (recv(clientSock, &resp.header, sizeof(resp.header), 0) <= 0) {
        perror("Failed to receive header");
        return {};
    }

    // Receive the length of the combined string
    uint32_t combinedLength;
    if (recv(clientSock, &combinedLength, sizeof(combinedLength), 0) <= 0) {
        perror("Failed to receive combined string length");
        return {};
    }

    // Receive the combined string
    char buffer[combinedLength + 1];
    if (recv(clientSock, buffer, combinedLength, 0) <= 0) {
        perror("Failed to receive combined string");
        return {};
    }
    buffer[combinedLength] = '\0';

    vector<string> songs;
    string combinedFiles(buffer);
    size_t pos = 0;
    while ((pos = combinedFiles.find("\n")) != string::npos) {
        songs.push_back(combinedFiles.substr(0, pos));
        combinedFiles.erase(0, pos + 1);
    }

    return songs;
}

vector<string> diff_request(int clientSock) {
    Header header;
    header.type = DIFF;
    header.size = 0;

    DiffRequest req;
    req.header = header;

    string combinedFiles;
    string hashes;

    if (send(clientSock, &req, sizeof(req), 0) < 0) {
        perror("Could not send DIFF request!");
        return {};
    }

    // crawl local directory
    DIR *dir = opendir("client_files");
    if (dir == NULL) {
        perror("Could not open directory");
        return {};
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && req.fileCount < 100) {
        if (entry->d_type == DT_REG) {
            string fileName = string(entry->d_name);
            combinedFiles += fileName + "\n";
            hashes += hash_file(fileName) + "\n";
            req.fileCount++;
        }
    }
    closedir(dir);

    uint32_t combinedLength = combinedFiles.size();
    uint32_t combinedLengthHashes = hashes.size();
    printf("Your files: \n%s", combinedFiles.c_str());
    printf("Their hashes: \n%s", hashes.c_str());

    // Send buffer length and files to server
    if (send(clientSock, &combinedLength, sizeof(combinedLength), 0) < 0) {
        perror("Failed to send combined string length");
        return {};
    }
    if (send(clientSock, combinedFiles.c_str(), combinedLength, 0) < 0) {
        perror("Failed to send combined string");
        return {};
    }
    if (send(clientSock, &combinedLengthHashes, sizeof(combinedLengthHashes), 0) < 0) {
        perror("Failed to send combined hashes length");
        return {};
    }
    if (send(clientSock, hashes.c_str(), combinedLengthHashes, 0) < 0) {
        perror("Failed to send combined hashes");
        return {};
    }

    uint32_t recvLen;
    if (recv(clientSock, &recvLen, sizeof(recvLen), 0) <= 0) {
        perror("Failed to receive combined string length");
        return {};
    }

    char buffer[recvLen + 1];
    if (recv(clientSock, buffer, recvLen, 0) <= 0) {
        cout << "error is on client side" << endl;
        perror("Failed to receive combined string");
        return {};
    }
    buffer[recvLen] = '\0';

    vector<string> songs;
    string cfiles(buffer);
    size_t pos = 0;
    while ((pos = cfiles.find("\n")) != string::npos) {
        songs.push_back(cfiles.substr(0, pos));
        cfiles.erase(0, pos + 1);
    }

    return songs;
}

void pull_request(int clientSock) {
    Header header;
    header.type = PULL;
    header.size = 0;

    DiffRequest req;
    req.header = header;

    string combinedFiles;
    string hashes;

    if (send(clientSock, &req, sizeof(req), 0) < 0) {
        perror("Could not send PULL request!");
        return;
    }

    // crawl local directory
    DIR *dir = opendir("client_files");
    if (dir == NULL) {
        perror("Could not open directory");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && req.fileCount < 100) {
        if (entry->d_type == DT_REG) {
            string fileName = string(entry->d_name);
            combinedFiles += fileName + "\n";
            hashes += hash_file(fileName) + "\n";
            req.fileCount++;
        }
    }
    closedir(dir);

    uint32_t combinedLength = combinedFiles.size();
    uint32_t combinedLengthHashes = hashes.size();
    printf("Your files: \n%s", combinedFiles.c_str());
    printf("Their hashes: \n%s", hashes.c_str());

    // Send buffer length and files to server
    if (send(clientSock, &combinedLength, sizeof(combinedLength), 0) < 0) {
        perror("Failed to send combined string length");
        return;
    }
    if (send(clientSock, combinedFiles.c_str(), combinedLength, 0) < 0) {
        perror("Failed to send combined string");
        return;
    }
    if (send(clientSock, &combinedLengthHashes, sizeof(combinedLengthHashes), 0) < 0) {
        perror("Failed to send combined hashes length");
        return;
    }
    if (send(clientSock, hashes.c_str(), combinedLengthHashes, 0) < 0) {
        perror("Failed to send combined hashes");
        return;
    }

    DiffResponse diffResp;
    if (recv(clientSock, &diffResp.header, sizeof(diffResp.header), 0) <= 0) {
        perror("Failed to receive diff header");
        return;
    }

    // Receive the list of different files
    uint32_t diffLength;
    if (recv(clientSock, &diffLength, sizeof(diffLength), 0) <= 0) {
        perror("Failed to receive diff string length");
        return;
    }

    char diffBuffer[diffLength + 1];
    if (recv(clientSock, diffBuffer, diffLength, 0) <= 0) {
        perror("Failed to receive diff string");
        return;
    }
    diffBuffer[diffLength] = '\0';

    // Print the list of files that are different
    printf("Different files:\n%s", diffBuffer);
}

void leave_request(int clientSock) {
    Header header;
    header.type = LEAVE;
    header.size = 0;

    LeaveRequest req;
    req.header = header;

    if (send(clientSock, &req, sizeof(req), 0) < 0) {
        perror("Could not send LEAVE request!");
    }
    printf("Sent LEAVE request.\n");
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Connect to the server
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client_socket);
        exit(1);
    }

    printf("Connected to the server.\n");

    while (1) {
        string input;
        printf("Enter command (LIST, DIFF, PULL, LEAVE): \n");
        cin >> input;

        vector<string> songs;

        if (input == "LIST") {
            songs = list_request(client_socket);
            for (const string &song : songs) {
                printf("%s\n", song.c_str());
            }
        }
        else if (input == "DIFF") {
            songs = diff_request(client_socket);
            for (const string &song : songs) {
                printf("%s\n", song.c_str());
            }
        }
        else if (input == "PULL") {
            // Add code to handle PULL request
        }
        else if (input == "LEAVE") {
            leave_request(client_socket);
            close(client_socket);
            printf("Connection closed.\n");
            return 0;
        }
        else {
            printf("Invalid input!\n");
        }
    }

    return 0;
}