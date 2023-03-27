#include "Server.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>


/*
@function:          the construction
@params:            int port -- sever port
                    const char* workPath -- change path for work
@return:            none
@be used in:        main.cpp
*/
Server::Server(int port, const char* workPath)
{
    //initialize server port
    this->port = port;
    
    //change path for work
    int ret = chdir(workPath);
    if (ret == -1)
    {
        perror("chdir error");
        exit(1);
    }
}

/*
@function:          create and initial a socket to listen
@params:            none
@return:            int lfd -- fd to listen
@be used in:        void Server::epollRun()
*/
int Server::initListenFd()
{
    //initial lfd
    this-> lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) 
    {
        perror("socket error");
        exit(1);
    }

    // create server address
    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = htonl(INADDR_ANY);

    // set port reuse
    int flag = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

    //bind lfd with server address
    int ret = bind(lfd, (struct sockaddr*)&serv, sizeof(serv));
    if (ret == -1) 
    {
        perror("bind error");
        exit(1);
    }

    // set listen
    ret = listen(lfd, 64);
    if (ret == -1) 
    {
        perror("listen error");
        exit(1);
    }

    // add lfd to epoll tree
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1) 
    {
        perror("epoll_ctl add lfd error");
        exit(1);
    }

    //return lfd
    return lfd;
}

/*
@function:          accept client connection
@params:            none
@return:            void
@be used in:        void Server::epollRun()
*/
void Server::acceptClient()
{
    //accept client connection
    struct sockaddr_in client;
    socklen_t len = sizeof(client);
    int cfd = accept(lfd, (struct sockaddr*)&client, &len);
    if (cfd == -1) 
    {
        perror("accept error");
        exit(1);
    }

    // print client imformation
    char ip[64] = { 0 };
    printf("New Client IP: %s, Port: %d, cfd = %d\n",
        inet_ntop(AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip)),
        ntohs(client.sin_port), cfd);

    // set cfd nonblock
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);

    // add cfd to epoll tree
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if (ret == -1) 
    {
        perror("epoll_ctl add cfd error");
        exit(1);
    }
}

/*
@function:          disconnect
@params:            int cfd -- disconnect cfd with server
@return:            void
@be used in:        void Server::dealRead(int cfd)
*/
void Server::disconnect(int cfd)
{
    //delete cfd from epoll tree
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret == -1) 
    {
        perror("epoll_ctl del cfd error");
        exit(1);
    }

    //close cfd
    close(cfd);
}

/*
@function:          create epoll tree and run
@params:            none
@return:            void
@be used in:        main.cpp
*/
void Server::epollRun()
{
    //create epoll tree
    int MAXSIZE = 2000;
    this->epfd = epoll_create(MAXSIZE);
    if (epfd == -1) {
        perror("epoll_create error");
        exit(1);
    }

    //initialize lfd to listen
    this->lfd = initListenFd();

    //core check node on tree
    struct epoll_event all[MAXSIZE];
    while (1) {
        int ret = epoll_wait(epfd, all, MAXSIZE, 0);
        if (ret == -1) 
        {
            perror("epoll_wait error");
            exit(1);
        }

        // check node
        for (int i = 0; i < ret; ++i)
        {
            
            //only deal read event
            struct epoll_event* pev = &all[i];
            if (!(pev->events & EPOLLIN)) 
            {
                continue;
            }

            //accept client connect
            if (pev->data.fd == lfd) 
            {
                acceptClient();
            }

            //deal read event
            else 
            {
                printf("======================before do read, ret = %d\n", ret);
                dealRead(pev->data.fd);
                printf("======================after do read\n");
            }
        }
    }
}

/*
@function:          deal with read event
@params:            int cfd -- fd of client
@return:            void
@be used in:        void Server::epollRun()
*/
void Server::dealRead(int cfd)
{
    char line[1024] = { 0 };
    int len = getLine(cfd, line, sizeof(line));
    if (len == 0) {
        printf("client disconnection...\n");
        disconnect(cfd);
    }
    else 
    {
        printf("============= The Head ============\n");
        printf("request line data: %s", line);
        while (1) 
        {
            char buf[1024] = { 0 };
            len = getLine(cfd, buf, sizeof(buf));
            if (buf[0] == '\n') 
            {
                break;
            }
            else if (len == -1)
            {
                break;
            }
        }
        printf("============= The End ============\n");
    }

    // check GET request
    if (strncasecmp("get", line, 3) == 0) {   
        
        // deal with http request
        httpRequest(line, cfd);

        //disconnect
        disconnect(cfd);
    }
}

/*
@function:          deal with http request
@params:            const char* request --
                    int cfd --
@return:            void
@be used in:        void Server::dealRead(int cfd)
*/
void Server::httpRequest(const char* request, int cfd)
{
    // split http request line
    char method[12], path[1024], protocol[12];
    sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
    printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);

    
    //decode chinese
    decodeStr(path, path);

    //delete / from path to get file name
    char* file = path + 1;

    // appoint default content
    if (strcmp(path, "/") == 0) 
    {
        
        // the value of file is work path
        file = "./";
    }

    // check file
    struct stat st;
    int ret = stat(file, &st);
    if (ret == -1) 
    {
        sendError(cfd, 404, "Not Found", "NO such file or direntry");
        return;
    }

    // for direction
    if (S_ISDIR(st.st_mode)) 
    {  	 
        
        //send respond head
        sendRespondHead(cfd, 200, "OK", getFileType(".html"), -1);
        
        //send direction 
        sendDir(cfd, file);
    }

    //for file
    else if (S_ISREG(st.st_mode)) 
    {         
        
        //send respond head 
        sendRespondHead(cfd, 200, "OK", getFileType(file), st.st_size);
        
        // send file
        sendFile(cfd, file);
    }
}

/*
@function:          send direction
@params:            int cfd -- fd of client
                    const char* dirname -- direction name
@return:            void
@be used in:        void Server::httpRequest(const char* request, int cfd)
*/
void Server::sendDir(int cfd, const char* dirname)
{
    int i, ret;
    char buf[4094] = { 0 };
    sprintf(buf, "<html><head><title>direction: %s</title></head>", dirname);
    sprintf(buf + strlen(buf), "<body><h1>Now direction: %s</h1><table>", dirname);
    char enstr[1024] = { 0 };
    char path[1024] = { 0 };
    struct dirent** ptr;
    int num = scandir(dirname, &ptr, NULL, alphasort);
    for (int i = 0; i < num; ++i) 
    {
        char* name = ptr[i]->d_name;
        sprintf(path, "%s/%s", dirname, name);
        printf("path = %s ===================\n", path);
        struct stat st;
        stat(path, &st);

        // encode
        encodeStr(enstr, sizeof(enstr), name);

        // for file
        if (S_ISREG(st.st_mode)) {
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                enstr, name, (long)st.st_size);
        }

        //for direction
        else if (S_ISDIR(st.st_mode)) 
        {	      
            sprintf(buf + strlen(buf),
                "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                enstr, name, (long)st.st_size);
        }
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1) 
        {
            if (errno == EAGAIN) 
            {
                perror("send error:");
                continue;
            }
            else if (errno == EINTR) 
            {
                perror("send error:");
                continue;
            }
            else 
            {
                perror("send error:");
                exit(1);
            }
        }
        memset(buf, 0, sizeof(buf));
    }
    sprintf(buf + strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    printf("dir message send OK!!!!\n");
}

/*
@function:          send file
@params:            int cfd -- fd of client
                    const char* filename -- file name
@return:            void
@be used in:        void Server::httpRequest(const char* request, int cfd)
*/
void Server::sendFile(int cfd, const char* filename)
{
    // open file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) 
    {
        sendError(cfd, 404, "Not Found", "NO such file or direntry");
        exit(1);
    }

    // read file
    char buf[4096] = { 0 };
    int len = 0, ret = 0;
    while ((len = read(fd, buf, sizeof(buf))) > 0) 
    {
        // send data
        ret = send(cfd, buf, len, 0);
        if (ret == -1) 
        {
            if (errno == EAGAIN) 
            {
                perror("send error:");
                continue;
            }
            else if (errno == EINTR) 
            {
                perror("send error:");
                continue;
            }
            else 
            {
                perror("send error:");
                exit(1);
            }
        }
    }
    if (len == -1) 
    {
        perror("read file error");
        exit(1);
    }

    //close fd
    close(fd);
}

/*
@function:          send respond head
@params:            int cfd -- fd of client
                    int no -- number
                    const char* desp -- description
                    const char* type -- file type
                    long len -- file length
@return:            void
@be used in:        void Server::httpRequest(const char* request, int cfd)
*/
void Server::sendRespondHead(int cfd, int no, const char* desp, const char* type, long len)
{
    //send head
    char buf[1024] = { 0 }; 
    sprintf(buf, "http/1.1 %d %s\r\n", no, desp);
    send(cfd, buf, strlen(buf), 0);
    
    //send type and length of file
    sprintf(buf, "Content-Type:%s\r\n", type);
    sprintf(buf + strlen(buf), "Content-Length:%ld\r\n", len);
    send(cfd, buf, strlen(buf), 0);
    
    //send flag of end 
    send(cfd, "\r\n", 2, 0);
}

/*
@function:          send error
@params:            int cfd -- fd of client
                    int statuss --
                    char* title --
                    char* text --
@return:            void
@be used in:        void Server::httpRequest(const char* request, int cfd)
                    void Server::sendFile(int cfd, const char* filename)
*/
void Server::sendError(int cfd, int status, char* title, char* text)
{
    char buf[4096] = { 0 };
    sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
    sprintf(buf + strlen(buf), "Content-Type:%s\r\n", "text/html");
    sprintf(buf + strlen(buf), "Content-Length:%d\r\n", -1);
    sprintf(buf + strlen(buf), "Connection: close\r\n");
    send(cfd, buf, strlen(buf), 0);
    send(cfd, "\r\n", 2, 0);
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
    sprintf(buf + strlen(buf), "<body bgcolor=\"#cc99cc\"><h2 align=\"center\">%d %s</h4>\n", status, title);
    sprintf(buf + strlen(buf), "%s\n", text);
    sprintf(buf + strlen(buf), "<hr>\n</body>\n</html>\n");
    send(cfd, buf, strlen(buf), 0);
    return;
}

/*
@function:          get a line
@params:            int sock -- fd to read
                    char* buf -- to receive data
                    int size -- buffer size
@return:            int i -- number of get characters in a line 
@be used in:        void Server::dealRead(int cfd)
*/
int Server::getLine(int sock, char* buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n')) {
                    recv(sock, &c, 1, 0);
                }
                else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return i;
}

/*
@function:          get type by file name
@params:            const char* name -- file name
@return:            const char* -- file type
@be used in:        void Server::httpRequest(const char* request, int cfd)
*/
const char* Server::getFileType(const char* name)
{
    const char* dot;
    dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

 /*
 @function:         encode
 @params:           char* to -- result
                    int tosize -- size of result
                    char* from -- to encode
 @return:           void
 @be used in:       void Server::sendDir(int cfd, const char* dirname)
 */
void Server::encodeStr(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {
            *to = *from;
            ++to;
            ++tolen;
        }
        else {
            sprintf(to, "%%%02x", (int)*from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}

/*
@function:          decode
@params:            char* to -- result
                    char* from -- to decode
@return:            void
@be used in:        void Server::httpRequest(const char* request, int cfd)
*/
void Server::decodeStr(char* to, char* from)
{
    for (; *from != '\0'; ++to, ++from) {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {
            *to = hex2dec(from[1]) * 16 + hex2dec(from[2]);
            from += 2;
        }
        else {
            *to = *from;
        }
    }
    *to = '\0';
}

/*
@function:          convert hex to dec
@params:            char c -- to convert
@return:            int -- result of convertion
@be used in:        void Server::decodeStr(char* to, char* from)
*/
int Server::hex2dec(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return 0;
}
