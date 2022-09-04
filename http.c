#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    //Store full request in temp file before parsing.
    FILE *request = tmpfile();
    if (request == NULL) {
        perror("tmpfile");
        return 1;
    }

    // Disable the usual stdio buffering
    if (setvbuf(request, NULL, _IONBF, 0) != 0) {
        perror("setvbuf");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        return 1;
    }

    // stdio FILE * gives us 'fgets()' to easily read line by line
    int sock_fd_copy = dup(fd);
    if (sock_fd_copy == -1) {
        perror("dup");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        return 1;
    }
    FILE *socket_stream = fdopen(sock_fd_copy, "r");
    if (socket_stream == NULL) {
        perror("fdopen");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        close(sock_fd_copy);
        return 1;
    }
    
    // Disable the usual stdio buffering
    if (setvbuf(socket_stream, NULL, _IONBF, 0) != 0) {
        perror("setvbuf");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        if (fclose(socket_stream) != 0){ //also closes underlying sock_fd_copy
            perror("fclose socket stream");
            return 1;
        } 
        return 1;
    }

    // Keep consuming lines until we find an empty line
    // This marks the end of the response headers and beginning of actual content
    char buf[BUFSIZE];
    while (fgets(buf, BUFSIZE, socket_stream) != NULL) {
        if (fwrite(buf, sizeof(char), strlen(buf), request) != strlen(buf)){
            fprintf(stderr, "Error writing request to file\n");
            if (fclose(request) != 0){
                perror("fclose request");
                return 1;
            }
            if (fclose(socket_stream) != 0){
                perror("fclose socket stream");
                return 1;
            }
            return 1;
        }
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
    }

    if (fclose(socket_stream) != 0) {
        perror("fclose socket stream");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        return 1;
    }
    
    rewind(request); //no possible error checking
 
    if (fgets(buf, BUFSIZE, request) == NULL){
        fprintf(stderr, "fgets failed to get first line\n");
        if (fclose(request) != 0){
            perror("fclose request");
            return 1;
        }
        return 1;
    }

    //File no longer needed
    if (fclose(request) != 0){
        perror("fclose request");
        return 1;
    }

    char *token = strtok(buf, " ");

    //Check if first token is GET
    if (token == NULL || strcmp(token, "GET") != 0){
        fprintf(stderr, "Bad Request no GET\n");
        return 1;
    }

    token = strtok(NULL, " ");
    //Check if second token is valid
    if (token == NULL){
        fprintf(stderr, "Bad Request second token invalid\n");
        return 1;
    }

    strcpy(resource_name, token); //No possible error checking

    return 0;
}

int write_http_response(int fd, const char *resource_path) {

    char response[BUFSIZE];

    struct stat stat_buf;
    if (stat(resource_path, &stat_buf) == -1) { //File not found
        
        if (sprintf(response, "%s", "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n") < 0){
            fprintf(stderr, "error sprintf to send 404 response\n");
            return 1;
        }

        //Send header
        if (write(fd, response, strlen(response)) == -1){
            perror("write");
            return 1;
        }

    } else {
        int file_size = stat_buf.st_size;
    
        char *dot = strchr(resource_path, '.');
        if (dot == NULL){
            fprintf(stderr, "error finding dot that marks extension\n");
            return 1;
        }

        int dot_idx; //index to slice to get extension
        dot_idx = (int)(dot - resource_path);

        char ext[5];
        strcpy(ext, resource_path+dot_idx);
        
        const char *mime_ext = get_mime_type(ext);    
        if (mime_ext == NULL){
            fprintf(stderr, "error get_mime_type\n");
            return 1;
        }    
        
        char* format = "HTTP/1.0 200 OK\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n";

        if (sprintf(response, format, mime_ext, file_size) < 0){
            printf("sprintf error\n");
            return 1;   
        }

        //Send header
        if (write(fd, response, strlen(response)) == -1){
            perror("write");
            return 1;
        }

        //Send file in chunks
        int resource_fd = open(resource_path, O_RDONLY);
        if (resource_fd == -1){
            perror("open");
            return 1;
        }

        int bytes_read;
        while ((bytes_read = read(resource_fd, response, BUFSIZE)) > 0){
            if (write(fd, response, bytes_read) == -1) {
                perror("write");
                close(resource_fd);
                return 1;
            }
        }

        if (bytes_read == -1) {
            perror("read");
            close(resource_fd);
            return 1;
        }

        if (close(resource_fd) != 0) {
            perror("close");
            return 1;
        }
    }   
    
    return 0;
}
