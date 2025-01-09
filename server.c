/*HTTP Server written in C by San Ramzi*/
/* gcc server.c -lpthread */
/* ./server -p <port> -d <directory> -u <users_file> */

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>

/* Definitions */
#define DEFAULT_BUFLEN 1024
#define PORT 6467

void PANIC(char* msg);
#define PANIC(msg)  { perror(msg); exit(-1); }
//User struct
typedef struct {
    char username[50];
    char password[50];
} User;
User users[100];
int user_count = 0;

//Main Directory and Users File
char main_directory[PATH_MAX];
char users_file[PATH_MAX];

void load_users(const char* password_file);
int authenticate_user(const char* username, const char* password);
long get_file_size(const char* filename);
void list_file(int client);
void delete_file(int client, char filename[DEFAULT_BUFLEN]);
void put_file(int client, const char* filename);
void get_file(int client, const char* filename);
void* Child(void* arg);

//Main function
int main(int argc, char *argv[])
    {
    int sd,opt,optval;
    struct sockaddr_in addr;
    unsigned short port=0;
    //Get CLI Arguments
    while ((opt = getopt(argc, argv, "p:d:u:")) != -1) {
        switch (opt) {
            case 'p':
                port=atoi(optarg);
                break;
            case 'd':
                strncpy(main_directory,optarg,PATH_MAX);
                break;
            case 'u':
                strncpy(users_file,optarg,PATH_MAX);
                break;
            default:
                printf("usage: %s -p <port> -d <directory> -u <users_file>\n",argv[0]);
                break;
        }
    }
    //If directory or users file is missing exit.
    if (main_directory[0] == '\0' || users_file[0] == '\0') {
        printf("Missing required arguments. Usage: %s -d directory -p port -u password_file\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if ( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
        PANIC("Socket");
    addr.sin_family = AF_INET;
    //Load users from file
    load_users(users_file);
    //If port is not given by user in CLI then set the default port
    if ( port > 0 )
                addr.sin_port = htons(port);
    else
                addr.sin_port = htons(PORT);

    addr.sin_addr.s_addr = INADDR_ANY;

   // set SO_REUSEADDR on a socket to true (1):
   optval = 1;
   setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);


    if ( bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
        PANIC("Bind");
    if ( listen(sd, SOMAXCONN) != 0 )
        PANIC("Listen");

    printf("System ready on port %d\n",ntohs(addr.sin_port));
    while (1)
    {
        int client, addr_size = sizeof(addr);
        pthread_t child;

        client = accept(sd, (struct sockaddr*)&addr, &addr_size);
        printf("Connected: %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
        if ( pthread_create(&child, NULL, Child, &client) != 0 )
            perror("Thread creation");
        else
            pthread_detach(child);  /* disassociate from parent */
    }
    return 0;
}


//Loading users and passwords from file
void load_users(const char *password_file) {
    FILE *file = fopen(password_file, "r");
    if (!file) {
        printf("Failed to open password file\n");
        exit(EXIT_FAILURE);
    }

    char line[DEFAULT_BUFLEN];
    while (fgets(line, sizeof(line), file)) {
        char *username = strtok(line, ":");
        char *password = strtok(NULL, "\n");
        if (username && password) {
            strncpy(users[user_count].username, username, sizeof(users[user_count].username));
            strncpy(users[user_count].password, password, sizeof(users[user_count].password));
            user_count++;
        }
    }
    fclose(file);
}
//Authenticate user function
int authenticate_user(const char *username, const char *password) {
    char line[DEFAULT_BUFLEN], file_user[DEFAULT_BUFLEN], file_pass[DEFAULT_BUFLEN];
    FILE *file = fopen(users_file, "r");

    if (!file) {
        perror("Failed to open users file");
        return 0;
    }

    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%[^:]:%s", file_user, file_pass) == 2) {
            if (strcmp(username, file_user) == 0 && strcmp(password, file_pass) == 0) {
                fclose(file);
                return 1;
            }
        }
    }

    fclose(file);
    return 0;
}
//Get file size function
long get_file_size(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return -1;
    }
    fseek(file, 0, SEEK_END);  
    long size = ftell(file);   
    fclose(file);              
    return size;
}
//List function (lists all files in directory)
void list_files(int client){
    DIR *dir = opendir(main_directory);
    struct dirent *entry;
    char current_file[DEFAULT_BUFLEN];

    //Read the directory and list files
    while ((entry = readdir(dir)) != NULL) {
        memset(current_file, 0, sizeof(current_file));
        //Add file name to the list except . and ..
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            //Get size in bytes
            char file_path[PATH_MAX];
            memset(file_path,0,sizeof(file_path));
            sprintf(file_path,"%s/%s",main_directory,entry->d_name);
            long size = get_file_size(file_path);
            //Add file name and size to the list
            sprintf(current_file,"%s %ld\n",entry->d_name,size);
            //Write to client
            if(strlen(current_file) > 0){
            write(client, current_file,sizeof(current_file));
            }
        }
    }
    closedir(dir);
    write(client,".\n",3);
}
//DEL function (delete file)
void delete_file(int client, char filename[DEFAULT_BUFLEN]){
    // Ensure the user provides a filename
    if (strlen(filename) == 0) {
        write(client, "No file specified for deletion.\n", 33);
        return;
    }
    // Create the full path of the file to delete
    char file_path[PATH_MAX],output[DEFAULT_BUFLEN];
    memset(file_path,0,sizeof(file_path));
    memset(output,0,sizeof(output));
    sprintf(file_path,"%s/%s",main_directory,filename);
    // Check if the file exists
    FILE *file = fopen(file_path, "r");
    if (!file) {
        sprintf(output,"404 File %s is not on the server.\n",filename);
        write(client, output, sizeof(output));
        return;
    }
    fclose(file);
    // Try to delete the file
    if (remove(file_path) == 0) {
        sprintf(output,"200 File %s deleted.\n",filename);
        write(client, output, sizeof(output));
    }
    else{
        write(client,"Failed to delete file.\n",24);
    }
}
//PUT function (create file and write to it)
void put_file(int client, const char *filename) {
    char file_path[PATH_MAX];
    sprintf(file_path,"%s/%s", main_directory, filename);
    FILE *file = fopen(file_path, "w+");
    if (!file) {
        write(client, "400 File cannot save on server side.\n", 38);
        return;
    }
    long total_bytes = 0;
    char buffer[DEFAULT_BUFLEN];
    while (1) {
        memset(buffer, 0, DEFAULT_BUFLEN);
        //Read from client input
        read(client, buffer, DEFAULT_BUFLEN);
        //Check if the current line is a single period (indicating end of file)
        if (strcmp(buffer, ".\n") == 0) {
            break;
        }
        //Write the received data to the file
        size_t bytes_written = fwrite(buffer, sizeof(char), strlen(buffer), file);
        total_bytes += bytes_written;
    }
    //Close file
    fclose(file);
    // Send confirmation to the client
    char response[DEFAULT_BUFLEN];
    memset(response,0,DEFAULT_BUFLEN);
    sprintf(response,"200 %ld Byte %s file retrieved by server and was saved.\n", total_bytes, filename);
    write(client, response, sizeof(response));
}
//GET function (send file to client)
void get_file(int client, const char *filename) {
    char file_path[PATH_MAX];
    sprintf(file_path,"%s/%s", main_directory, filename); 
    FILE *file = fopen(file_path, "rb"); 
    if (!file) {
        // If the file does not exist or cannot be opened, send a 404 error message
        char error_message[DEFAULT_BUFLEN];
        sprintf(error_message,"404 File %s not found.\n", filename);
        write(client, error_message, strlen(error_message));
        return;
    }
    char line[DEFAULT_BUFLEN];
    // Send the full file content to the client for saving
    while (fgets(line, sizeof(line), file)) {
        write(client, line, strlen(line));
    }
    fclose(file);
    // Send a final marker to indicate the file transfer is complete
    write(client, ".\n", 4);
}
//Client function (child thread)
void* Child(void* arg)
{   char line[DEFAULT_BUFLEN];
    int bytes_read;
    int client = *(int *)arg;
    int auth = 0;
    char command[DEFAULT_BUFLEN],arg_a[DEFAULT_BUFLEN],arg_b[DEFAULT_BUFLEN];
    do
    {
        memset(line, 0, DEFAULT_BUFLEN);
        memset(arg_a, 0, DEFAULT_BUFLEN);
        memset(arg_b, 0, DEFAULT_BUFLEN);
        read(client, line, DEFAULT_BUFLEN);
        //Get user input
        char *token = strtok(line, " ");
        if (token != NULL) {
            strcpy(command, token);
            token = strtok(NULL, " ");
            if (token != NULL) strcpy(arg_a, token);
            token = strtok(NULL, " ");
            if (token != NULL) strcpy(arg_b, token);
        }
        command[strcspn(command,"\n")] = 0;
        arg_a[strcspn(arg_a,"\n")] = 0;
        arg_b[strcspn(arg_b,"\n")] = 0;

        //Server: Output parsed command and arguments
        printf("Command: %s, Arg1: %s, Arg2: %s\n", command, arg_a, arg_b);
        
        if(strcmp(command, "USER") == 0){
            if(auth == 1){
                write(client,"User already authenticated.\n",29);
                continue;
            }
            else if(authenticate_user(arg_a,arg_b)){
                write(client,"200 User authenticated.\n",25);
                auth = 1;
                continue;
            }
            else{
                write(client,"400 Authentication failed.\n",28);
                continue;
            }
        }
        else if(strcmp(command, "LIST") == 0){
            if(auth != 1){
                write(client,"User not authenticated. Use USER <username> <password>\n",56);
                continue;
            }
            else{
                list_files(client);
                continue;
            }
        }
        else if(strcmp(command,"GET") == 0){
            if(auth != 1){
                write(client,"User not authenticated. Use USER <username> <password>\n",56);
                continue;
            }
            else{
                if(strlen(arg_a) == 0){
                    write(client,"Please provide a filename.\n",28);
                }
                else{
                    get_file(client,arg_a);
                    continue;
                }
            }
        }
        else if(strcmp(command,"PUT") == 0){
            if(auth != 1){
                write(client,"User not authenticated. Use USER <username> <password>\n",56);
                continue;
            }
            else{
                put_file(client,arg_a);
                continue;
            }
        }
        else if(strcmp(command,"DEL") == 0){
            if(auth != 1){
                write(client,"User not authenticated. Use USER <username> <password>\n",56);
                continue;
            }
            else{
                delete_file(client,arg_a);
                continue;
            }
        }
        else if(strcmp(command,"QUIT") == 0){
            write(client,"Goodbye!\n",10);
            break;
        }
        else {
            write(client,"Unknown command.\nUSER <user_name> <password>\nLIST\nDEL <file_name>\nPUT <file_name>\nGET <file_name>\nQUIT\n",104);
            continue;
        }
    } while (1);
    close(client);
    return arg;
}