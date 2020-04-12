/*****************************************************************************/
/* 文件名:    httpd.c                                                        */
/* 描  述:    轻量HTTP服务器                                                  */
/* 创  建:    2020-04-12 changzehai                                          */
/* 更  新:    无                                                             */
/* Copyright 1998 - 2020 CZH. All Rights Reserved                            */
/*****************************************************************************/
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


/*-----------------------------------*/
/* 宏定义                            */
/*-----------------------------------*/
#define SERVER_STRING "Server: httpd/1.0.0\r\n" /* 定义http server名称 */

/*-----------------------------------*/
/* 数据结构定义                       */
/*-----------------------------------*/
/* HTTP请求行数据结构定义 */
typedef struct __HTTP_REQUEST_LINE_DATA_T_
{
    char method[10];        /* 请求方法                */
    char path[255];         /* 请求资源路径            */
    char query_string[255]; /* 查询参数， 仅GET命令有效 */
    int  cgi;               /* 是否需要执行CGI程序标志  */
} http_request_line_data_t;

/* HTTP请求数据结构定义 */
typedef struct __HTTP_REQUEST_DATA_T_
{
    http_request_line_data_t req_line_data;  /* 请求行数据    */
    int content_length;                      /* 请求体数据长度 */
} http_request_data_t;

/*-----------------------------------*/
/* 函数声明                          */
/*-----------------------------------*/
/* 记录错误信息并关闭服务器程序 */
static void httpd_error_exit(const char *error); 

/* 创建TCP服务监听 */
static int httpd_server_startup(void);

/* 获取一行HTTP报文 */
static int httpd_get_line_message(int sock, char *buf, int size);

/* 解析HTTP报文的请求行 */
static void httpd_request_line_analyze(int client_sock, http_request_line_data_t *req_line_data);

/* 解析HTTP报文的请求头 */
static void httpd_request_header_analyze(int client_sock, const char *method, int *content_length);

/* 返回请求方法错误信息给客户端 */
static void httpd_request_method_error(int client);

/* 返回请求资源路径错误给客户端 */
static void httpd_request_path_error(int client);

/* 返回不能执行CGI程序错误给客户端 */
static void httpd_request_cannot_execute_error(int client);

/* 返回HTTP坏请求错误(content_lenght有误) */
static void httpd_request_bad_error(int client);

/* 检查并处理HTTP请求错误 */
static int  httpd_request_error_deal(int client, http_request_data_t *h_data);

/* 发送回复报文头 */
static void httpd_response_header(int client);

/* 返回静态请求文件给客户端 */
static void httpd_send_file(int client, const char *filename);

/* 执行CGI程序处理HTTP请求，并将处理结果发送回客户端 */
static void httpd_execute_cgi(int client, http_request_data_t *h_data);

/* 处理客户端请求 */
static void *httpd_accept_client_request(void *from_client);


/*****************************************************************************
 * 函  数:    httpd_error_exit
 * 功  能:    记录错误信息并关闭服务器程序
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_error_exit(const char *error)
{
    perror(error);
    exit(1);
}


/*****************************************************************************
 * 函  数:    httpd_server_startup
 * 功  能:    创建TCP服务监听
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static int httpd_server_startup(void)
{
    int server_sock = -1;
    int on = 1;
    struct sockaddr_in server_addr;

    server_sock = socket(PF_INET, SOCK_STREAM, 0);
    if (-1 == server_sock)
    {
        httpd_error_exit("socket failed");
    }

    memset(&server_addr, 0x00, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8000);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    {
        httpd_error_exit("setsockopt failed");
    }

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        httpd_error_exit("bind failed");
    }

    if (listen(server_sock, 5) < 0)
    {
        httpd_error_exit("listen failed");
    }

    return (server_sock);
}


/*****************************************************************************
 * 函  数:    httpd_get_line_message
 * 功  能:    获取一行HTTP报文
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static int httpd_get_line_message(int sock, char *buf, int size)
{
    int i = 0;
    int n = 0;
    char c = '\0';

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if ((n > 0) && (c == '\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
                
            }

            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }

    buf[i] = '\0';
    
    return(i);
}

/*****************************************************************************
 * 函  数:    httpd_request_line_analyze
 * 功  能:    解析HTTP报文的请求行
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_line_analyze(int client_sock, http_request_line_data_t *req_line_data)
{

    char buf[1024] = {0};
    char url[255] = {0};
    char *query_string = NULL;
    size_t i = 0;
    size_t j = 0;

    i = 0;
    j = 0;

    httpd_get_line_message(client_sock, buf, sizeof(buf));

    while (!isspace((int)buf[j]) && (i < (sizeof(req_line_data->method) - 1)))
    {
        req_line_data->method[i] = buf[j];
        i++;
        j++;
    }
    req_line_data->method[i] = '\0';

    i = 0;
    /* 将method后面的后边的空白字符略过 */
    while(isspace((int)buf[j]) && (j < sizeof(buf)))
    {
        j++;
    }

    /* 继续读取request-URL */
    while (!isspace((int)buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
    {
        url[i] = buf[j];
        i++; 
        j++;
    }
    url[i] = '\0';

    if (0 == strcasecmp(req_line_data->method, "GET"))
    {
        query_string = url;
        
        while ((*query_string != '?') && (*query_string != '\0'))
        {
            query_string++;
        }

        if ('?' == *query_string)
        {
            *query_string = '\0';
            query_string++;
            sprintf(req_line_data->query_string, "%s", query_string);
            req_line_data->cgi = 1;
        }
        
    }
    else if (0 == strcasecmp(req_line_data->method, "POST"))
    {
        req_line_data->cgi = 1;
    }
    else
    {
        /* code */
    }
    

    /* url中的路径格式化到path */
    sprintf(req_line_data->path, "htdocs%s", url);
   
    /* 如果path只是一个目录，默认设置为首页index.html */
    if (req_line_data->path[strlen(req_line_data->path) - 1] == '/')
    {
        strcat(req_line_data->path, "index.html");
    }

    
}

/*****************************************************************************
 * 函  数:    httpd_request_header_analyze
 * 功  能:    解析HTTP报文的请求头
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_header_analyze(int client_sock, const char *method, int *content_length)
{

    int numchars = 1;
    char buf[1024] = {0};

    if (0 == strcasecmp(method, "POST"))
    {
        numchars = httpd_get_line_message(client_sock, buf, sizeof(buf));

        /* 循环读取头信息找到Content-Length字段的值 */
        while ((numchars > 0) && strcmp("\n", buf))
        {
            buf[15] = '\0';/* 目的是为了截取Content-Length: */
   
            if (strcasecmp(buf, "Content-Length:") == 0)
            {
                *content_length = atoi(&(buf[16])); /* 获取Content-Length的值 */
            }
            numchars = httpd_get_line_message(client_sock, buf, sizeof(buf));
        }        
    }
    else
    {
        while ((numchars > 0) && strcmp("\n", buf)) 
        {
            numchars = httpd_get_line_message(client_sock, buf, sizeof(buf));
        }
    }
    
}

/*****************************************************************************
 * 函  数:    httpd_request_method_error
 * 功  能:    返回请求方法错误信息给客户端
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_method_error(int client)
{
	char buf[128] = {0};


	/* 发送501说明相应方法没有实现 */
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);

}

/*****************************************************************************
 * 函  数:    httpd_request_path_error
 * 功  能:    返回请求资源路径错误给客户端
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_path_error(int client)
{
	char buf[128] = {0};

	/* 返回404 */
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


/*****************************************************************************
 * 函  数:    httpd_request_cannot_execute_error
 * 功  能:    返回不能执行CGI程序错误给客户端
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_cannot_execute_error(int client)
{
	char buf[128] = {0};
	
	
	/* 发送500 错误 */
	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	send(client, buf, strlen(buf), 0);

}

/*****************************************************************************
 * 函  数:    httpd_request_bad_error
 * 功  能:    返回HTTP坏请求错误(content_lenght有误)
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_request_bad_error(int client)
{
	char buf[128] = {0};


	/* 发送400错误 */
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

/*****************************************************************************
 * 函  数:    httpd_request_error_deal
 * 功  能:    检查并处理HTTP请求错误
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static int  httpd_request_error_deal(int client, http_request_data_t *h_data)
{
    struct stat st;

    /* 检查请求方法是否正确 */
    if ((0 != strcasecmp(h_data->req_line_data.method, "GET")) && 
        (0 != strcasecmp(h_data->req_line_data.method, "POST")))
    {
        httpd_request_method_error(client);
        return -1;
    }

    /* 检查请求资源路径是否正确 */
    if (stat(h_data->req_line_data.path, &st) == -1) 
    {
        httpd_request_path_error(client);
        return -1;
    }

#ifdef DEBUG
    /* 如果需要执行CGI程序，需要检查请求的CGI脚步是否具有可执行权限 */
    if (1 == h_data->req_line_data.cgi)
    {
        if ((st.st_mode & S_IXUSR) ||
            (st.st_mode & S_IXGRP) ||
            (st.st_mode & S_IXOTH))
        {
            httpd_request_cannot_execute_error(client);
            return -1;
        }
    }
#endif

    /* 如果是POST请求，需检查Content_length长度是否正确 */
    if ((0 == strcasecmp(h_data->req_line_data.method, "POST")) &&
        (h_data->content_length < 0))
    {
        httpd_request_bad_error(client);
        return -1;
    }

   
    return 0;
}

/*****************************************************************************
 * 函  数:    httpd_response_header
 * 功  能:    发送回复报文头
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_response_header(int client)
{
	char buf[1024];

	/* 发送HTTP头 */
	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);    
}

/*****************************************************************************
 * 函  数:    httpd_send_file
 * 功  能:    返回静态请求文件给客户端
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_send_file(int client, const char *filename)
{
	FILE *fp = NULL;
	char buf[1024] = {0};
	

	/* 打开文件 */
	fp = fopen(filename, "r");
	if (fp != NULL)
    {

	    /* 读取并发送文件内容 */
	    fgets(buf, sizeof(buf), fp);
	    while (!feof(fp)) /* 判断文件是否读取到末尾 */
	    {
	        /* 读取并发送文件内容 */
	        send(client, buf, strlen(buf), 0);
	        fgets(buf, sizeof(buf), fp);
	    }
    }
	else
	{
	    /* 如果文件不存在，则返回not_found */
	    httpd_request_path_error(client);        
	}

    /* 关闭文件句柄 */
	fclose(fp);
}


/*****************************************************************************
 * 函  数:    httpd_execute_cgi
 * 功  能:    执行CGI程序处理HTTP请求，并将处理结果发送回客户端
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void httpd_execute_cgi(int client, http_request_data_t *h_data)
{
    pid_t pid;
    int cgi_output[2]; 
    int cgi_input[2];
    int i = 0;
    int status = 0;  
    char c = '\0';


    /* 创建父进程读子进程写的管道 */
    if (pipe(cgi_output) < 0) {
        httpd_request_cannot_execute_error(client);
        return;
     }

    /* 创建子进程写父进程读的管道 */
    if (pipe(cgi_input) < 0) {
        httpd_request_cannot_execute_error(client);
        return;
    }

    /* fork出一个子进程运行cgi脚本 */
    if ( (pid = fork()) < 0 ) {
        httpd_request_cannot_execute_error(client);
        return;
    }   


	if (pid == 0)  /* 子进程: 运行CGI 脚本 */
	{
		char meth_env[40] = {0};
		char query_env[300] = {0};
		char length_env[20] = {0};
		
		/* 1代表着stdout，0代表着stdin，将系统标准输出重定向为cgi_output[1] */
		dup2(cgi_output[1], 1);
		/* 将系统标准输入重定向为cgi_input[0] */
		dup2(cgi_input[0], 0);
		
		/* 关闭了cgi_output中的读通道 */
		close(cgi_output[0]);
		/* 关闭了cgi_input中的写通道 */
		close(cgi_input[1]);
		/* CGI标准需要将请求的方法存储环境变量中，然后和cgi脚本进行交互 */
		/* 存储REQUEST_METHOD */
		sprintf(meth_env, "REQUEST_METHOD=%s", h_data->req_line_data.method);
		putenv(meth_env);
		
		
		if (strcasecmp(h_data->req_line_data.method, "GET") == 0)  /* GET方法 */
		{
			/* 存储QUERY_STRING */
			sprintf(query_env, "QUERY_STRING=%s", h_data->req_line_data.query_string);
			putenv(query_env);
            printf("query_string:%s\n", query_env);
		}
		else  /* POST方法 */
		{   
			/* 存储CONTENT_LENGTH */
			sprintf(length_env, "CONTENT_LENGTH=%d", h_data->content_length);
			putenv(length_env);
		}
		
		/* 执行CGI脚本 */
		execl(h_data->req_line_data.path, h_data->req_line_data.path, NULL);
		
		/* 退出子进程 */
		exit(0);
	
	} 
	else /* 父进程 */
	{    
        /* 关闭了cgi_output中的写通道，注意这是父进程中cgi_output变量和子进程要区分开 */
		close(cgi_output[1]);
        /* 关闭了cgi_input中的读通道 */
		close(cgi_input[0]);
	
		if (strcasecmp(h_data->req_line_data.method, "POST") == 0)
		{
			for (i = 0; i <  h_data->content_length; i++) 
			{
			    /* 开始读取POST中的内容 */
			    recv(client, &c, 1, 0);

			    /* 将数据发送给cgi脚本 */
			    write(cgi_input[1], &c, 1);
			}
        }

		/* 读取cgi脚本返回数据 */
		while (read(cgi_output[0], &c, 1) > 0)
		{
			/* 发送给浏览器 */
			send(client, &c, 1, 0);
		}
	
		/* 运行结束关闭 */
		close(cgi_output[0]);
		close(cgi_input[1]);
	
        /* 等待子进程退出后父进程再退出 */
		waitpid(pid, &status, 0);
	    
    }
}



/*****************************************************************************
 * 函  数:    httpd_accept_client_request
 * 功  能:    处理客户端请求
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
static void *httpd_accept_client_request(void *from_client)
{
    int client = -1;
    http_request_data_t http_data;

    
    client = *(int *)from_client;
    memset(&http_data, 0x00, sizeof(http_data));

    /* 解析HTTP请求行 */
    httpd_request_line_analyze(client, &http_data.req_line_data);

    /* 解析HTTP请求头 */
    httpd_request_header_analyze(client, http_data.req_line_data.method, &http_data.content_length);

#ifndef DEBUG
    printf("method: %s\n", http_data.req_line_data.method);
    printf("path:%s\n", http_data.req_line_data.path);
    printf("cgi = %d\n", http_data.req_line_data.cgi);
    printf("query_string:%s\n", http_data.req_line_data.query_string);
    printf("content_length:%d\n", http_data.content_length);
    printf("\n");
#endif


    /* HTTP请求错误处理 */
    if (-1 == httpd_request_error_deal(client, &http_data))
    {
        printf("httpd request error\n");
        return NULL;
    }
    else
    {
        /* 返回正确响应码200 */
	    send(client, "HTTP/1.0 200 OK\r\n", strlen("HTTP/1.0 200 OK\r\n"), 0);
    }

    /* 处理客户端请求，并将处理结果返回给客户端 */
    if (0 == http_data.req_line_data.cgi) /* 不带参数的GET请求，不需要执行CGI程序，直接返回请求的资源文件 */
    {
        /* 发送回复报文头 */
	    httpd_response_header(client);
        
        /* 发送所请求的资源文件给客户端 */
        httpd_send_file(client, http_data.req_line_data.path);
    }
    else 
    {
        /* 执行CGI程序处理HTTP请求，并将处理结果发送回客户端 */
        httpd_execute_cgi(client, &http_data);
    }
    

    return NULL;
}



/*****************************************************************************
 * 函  数:    main
 * 功  能:    主程序
 * 输  入:    无
 * 输  出:    无
 * 返回值:    无  
 * 创  建:    2020-04-12 changzehai(DTT)
 * 更  新:    无
 ****************************************************************************/
int main(void)
{
    int server_sock = -1;
    int client_sock = -1;
    socklen_t client_addr_len = 0;
    struct sockaddr_in client_addr;
    pthread_t newthread;

    
    client_addr_len = sizeof(client_addr);
    memset(&client_addr, 0x00, sizeof(client_addr));

    /* 启动server socket */
    server_sock = httpd_server_startup();
    printf("httpd running on 8000 !!!\n");

    while (1)
    {
        /* 接受客户端连接 */
        client_sock = accept(server_sock,
                          (struct sockaddr *)&client_addr,
                          &client_addr_len);
        if (-1 == client_sock)
        {
            httpd_error_exit("accept");
        }

        /*启动线程处理新的连接 */
        if (pthread_create(&newthread , NULL, httpd_accept_client_request, (void*)&client_sock) != 0)
        {
            perror("pthread_create failed");
        }
    }

    printf("closed!\n");
    /* 关闭server socket */
    close(server_sock);

    return(0);
}


