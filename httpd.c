#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: myhttpd\r\n"

void *accept_request(void *client);
void bad_request(int);
void cat(int, FILE *);
void cannot_execute(int);
void error_die(const char *);
void execute_cgi(int, const char *, const char *, const char *);
int get_line(int, char *, int);
void headers(int, const char *);
void not_found(int);
void serve_file(int, const char *);
int startup(u_short *);
void unimplemented(int);


//  处理监听到的 HTTP 请求
void *accept_request(void *from_client) {
    int client = *(int *) from_client;
    char buf[1024];
    int numchars;
    char method[255];
    char url[255];
    char path[512];
    size_t i, j;
    struct stat st;
    int cgi = 0;
    char *query_string = NULL;

    numchars = get_line(client, buf, sizeof(buf));
    // puts("1_get_line: "); puts(buf);// GET /post.html HTTP/1.1

    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        //提取其中的请求方式
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';
    // TODO: commit
    // printf("METHOD: %s\n", method);
    /* Compare S1 and S2, ignoring case. Return 0 when S1 equals S2 */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        // method != "GET" and method != "POST"
        unimplemented(client);
        return NULL;
    }

    // POST 转至cgi执行
    if (strcasecmp(method, "POST") == 0) cgi = 1;

    // fliter space
    i = 0;
    while (ISspace(buf[j]) && (j < sizeof(buf)))
        j++;

    // prase url from request buf
    while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    // TODO: commit
    // printf("URL: %s\n", url);

    //GET请求url可能会带有?,有查询参数
    if (strcasecmp(method, "GET") == 0) {

        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;

        /* 如果有?表明是动态请求, 开启cgi */
        if (*query_string == '?') {
            cgi = 1;
            *query_string = '\0';
            query_string++;
        }
        // TODO: commit
        // 将需要cgi处理的部分使用query_string接管，query_string部分使用\0填充，下一个位置以query_string开始
    }

    sprintf(path, "httpdocs%s", url);


    if (path[strlen(path) - 1] == '/') {
        strcat(path, "test.html");
    }

    if (stat(path, &st) == -1) {
        while ((numchars > 0) && strcmp("\n", buf))
            numchars = get_line(client, buf, sizeof(buf));

        not_found(client);
    } else {


        if ((st.st_mode & S_IFMT) == S_IFDIR)//S_IFDIR代表目录
            //如果请求参数为目录, 自动打开test.html
        {
            strcat(path, "/test.html");
        }

        //文件可执行
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
            //S_IXUSR:文件所有者具可执行权限
            //S_IXGRP:用户组具可执行权限
            //S_IXOTH:其他用户具可读取权限
            cgi = 1;

        if (!cgi)

            serve_file(client, path);
        else
            execute_cgi(client, path, method, query_string);

    }

    close(client);
    //printf("connection close....client: %d \n",client);
    return NULL;
}


void bad_request(int client) {
    char buf[1024];
    //发送400
    sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "<P>Your browser sent a bad request, ");
    send(client, buf, sizeof(buf), 0);
    sprintf(buf, "such as a POST without a Content-Length.\r\n");
    send(client, buf, sizeof(buf), 0);
}


void cat(int client, FILE *resource) {
    //发送文件的内容
    char buf[1024];
    fgets(buf, sizeof(buf), resource);
    while (!feof(resource)) {

        send(client, buf, strlen(buf), 0);
        fgets(buf, sizeof(buf), resource);
    }
}


void cannot_execute(int client) {
    char buf[1024];
    //发送500
    sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, strlen(buf), 0);
}


void error_die(const char *sc) {
    perror(sc);
    exit(1);
}


//执行cgi动态解析
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string) {


    char buf[1024];
    int cgi_output[2];
    int cgi_input[2];

    pid_t pid;
    int status;

    int i;
    char c;

    int numchars = 1;
    int content_length = -1;
    //默认字符
    buf[0] = 'A';
    buf[1] = '\0';
    if (strcasecmp(method, "GET") == 0)

        while ((numchars > 0) && strcmp("\n", buf)) {
            numchars = get_line(client, buf, sizeof(buf));
            // puts("2_get_line: "); puts(buf);
        }
    else {

        numchars = get_line(client, buf, sizeof(buf));
        while ((numchars > 0) && strcmp("\n", buf)) {
            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));

            numchars = get_line(client, buf, sizeof(buf));
        }

        if (content_length == -1) {
            bad_request(client);
            return;
        }
    }


    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    // 无名管道通信 pipe
    if (pipe(cgi_output) < 0) {
        cannot_execute(client);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client);
        return;
    }

    if ((pid = fork()) < 0) {
        cannot_execute(client);
        return;
    }
    if (pid == 0)  /* 子进程: 运行CGI 脚本 */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        // 原本是指向标准输出文件描述符的1指向了管道cgi_output[1]
        // 使原本输出到显示器终端的字符串就打印到cgi_output[1]管道中
        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);


        close(cgi_output[0]);//关闭了cgi_output中的读通道
        close(cgi_input[1]);//关闭了cgi_input中的写通道


        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);

        if (strcasecmp(method, "GET") == 0) {
            //存储QUERY_STRING
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else {   /* POST */
            //存储CONTENT_LENGTH
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }

        // execl() 执行文件 emp: execl("/usr/bin/python3", "/usr/bin/python3", "./post.cgi", NULL);
        // execlp() 从PATH环境变量中查找文件并执行
        execlp("python3", "python3", path, NULL); // execlp(path, path, NULL); //执行CGI脚本
        exit(0);
    } else {
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0)

            for (i = 0; i < content_length; i++) {

                recv(client, &c, 1, 0);

                write(cgi_input[1], &c, 1);
            }



        //读取cgi脚本返回数据

        while (read(cgi_output[0], &c, 1) > 0)
            //发送给浏览器
        {
            send(client, &c, 1, 0);
        }

        //运行结束关闭
        close(cgi_output[0]);
        close(cgi_input[1]);


        waitpid(pid, &status, 0);
    }
}


// 解析一行http报文，返回buf（或报文）的长度
// 读取到的报头形如：
// GET /post.html HTTP/1.1
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);

        if (n > 0) {
            if (c == '\r') {

                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;

        } else
            c = '\n';
    }
    buf[i] = '\0';
    return (i);
}


void headers(int client, const char *filename) {

    char buf[1024];

    (void) filename;  /* could use filename to determine file type */
    //发送HTTP头
    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "\r\n");
    send(client, buf, strlen(buf), 0);

}


void not_found(int client) {
    char buf[1024];
    //返回404
    sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "your request because the resource specified\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "is unavailable or nonexistent.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}


//如果不是CGI文件，也就是静态文件，直接读取文件返回给请求的http客户端
void serve_file(int client, const char *filename) {
    FILE *resource = NULL;
    int numchars = 1;
    char buf[1024];
    buf[0] = 'A';
    buf[1] = '\0';
    while ((numchars > 0) && strcmp("\n", buf)) {
        numchars = get_line(client, buf, sizeof(buf));
    }

    //打开文件
    resource = fopen(filename, "r");
    if (resource == NULL)
        not_found(client);
    else {
        headers(client, filename);
        cat(client, resource);
    }
    fclose(resource);//关闭文件句柄
}

//启动服务端
int startup(u_short *port) {
    int httpd = 0, option;
    struct sockaddr_in name;
    //设置http socket
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("socket");//连接失败

    socklen_t optlen;
    optlen = sizeof(option);
    option = 1;
    setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR, (void *) &option, optlen);


    memset(&name, 0, sizeof(name));
    name.sin_family = AF_INET;
    name.sin_port = htons(*port);
    name.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(httpd, (struct sockaddr *) &name, sizeof(name)) < 0)
        error_die("bind");//绑定失败
    if (*port == 0)  /*动态分配一个端口 */
    {
        socklen_t namelen = sizeof(name);
        if (getsockname(httpd, (struct sockaddr *) &name, &namelen) == -1)
            error_die("getsockname");
        *port = ntohs(name.sin_port);
    }

    if (listen(httpd, 5) < 0)
        error_die("listen");
    return (httpd);
}

// unimplemented 方法未实现
void unimplemented(int client) {
    char buf[1024];
    //发送501说明相应方法没有实现
    sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, SERVER_STRING);
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "Content-Type: text/html\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<html><head><title>Method Not Implemented\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</title></head>\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "<body>HTTP request method not supported.\r\n");
    send(client, buf, strlen(buf), 0);
    sprintf(buf, "</body></html>\r\n");
    send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main() {
    int server_sock = -1;
    u_short port = 19722;
    int client_sock = -1;
    struct sockaddr_in client_name;
    socklen_t client_name_len = sizeof(client_name);
    pthread_t newthread;
    server_sock = startup(&port);

    printf("http server_sock is %d\n", server_sock);
    printf("http running on port %d\n", port);
    while (1) {

        client_sock = accept(server_sock,
                             (struct sockaddr *) &client_name,
                             &client_name_len);

        printf("New connection....  ip: %s , port: %d\n", inet_ntoa(client_name.sin_addr), ntohs(client_name.sin_port));
        if (client_sock == -1)
            error_die("accept");

        if (pthread_create(&newthread, NULL, accept_request, (void *) &client_sock) != 0)
            perror("pthread_create");

    }
    close(server_sock);

    return (0);
}
