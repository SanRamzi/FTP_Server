# FTP_Server
FTP server written in C <br>
Compile : `gcc server.c -o server -lpthread` <br>
Execute : `./server -p <port_number> -d <directory_name> -u <users_file>` <br>
Connect from terminal with `nc localhost <port_number>`
