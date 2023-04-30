#include<iostream>
#include <arpa/inet.h>
#include <unistd.h>
#include<cstring>
#include <pthread.h> 
#include<sys/stat.h>
#include<fstream>
#include <sstream>
#include <wait.h>

using namespace std;

//***********************函数声明**********************************************

//程序运行失败时输出错误信息
//参数：错误提示
void error_die(const char* str);

//实现网络的初始化
//参数：端口号
//返回值：创建的服务端套接字，如果为0就自动分配一个可用端口
int startup(unsigned short *port);

//读取接收到的请求数据
int GetHeadData(int client_fd, string& str);

//向指定套接字发送一个功能还未实现的错误提示页面
void unImplement(int client_fd);

//向指定套接字发送一个功能还未实现的错误提示页面
void NotFound(int client_fd);

//向指定套接字发送http响应头
//参数：客户端套接字，资源类型，响应码，文件大小
void SendHttpHead(int client_fd, const string &type,
const string &statuscode, size_t file_size);

//发送请求的资源
//参数：客户端套接字，文件流，资源类型，响应码
void SendResource(int client_fd, ifstream& file, 
const string &type, const string &statuscode);

//服务端向指定套接字发送请求的文件
//参数：客户端套接字，文件名称（路径）
void ServerSendFile(int client_fd, const string &fileName);

//线程函数：处理用户请求
void * accept_request(void * arg);

//执行CGI动态解析
void execute_cgi(int client_fd, const string &path, 
const string &method, const string &query_length);

// 处理GET请求
void handle_get(int client_socket, const string &path);

// 处理POST请求
void handle_post(int client_socket, const string &path, string body);

//***********************具体实现***********************************************

void error_die(const char* str){
    perror(str);
    exit(1);
}

int startup(unsigned short *port){

    //1.创建套接字
    int server_fd = socket(
        PF_INET, //指定使用的底层协议族
        SOCK_STREAM, //指定服务类型-流服务
        IPPROTO_IP); //指定具体协议，但前两个参数决定了该参数值，所以直接将其设置为0，表示使用默认协议
    //socket调用成功会返回一个文件描述符，失败返回-1
    if(server_fd == -1){ //若创建失败
        error_die("[socket]");
    }

    //2.设置端口可复用（消灭端口CD时间）,在绑定端口前设置
    int opt = 1;
    int ret;
    ret = setsockopt(server_fd, SOL_SOCKET, 
    SO_REUSEADDR, (const void*)&opt, sizeof(opt));
    if(ret == -1){
        error_die("[setsocketopt]");
    }

    //配置服务端的网络地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; 
    server_addr.sin_port = htons(*port); //要进行字节序转换
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); //接收所有IP地址的请求（每个网卡有不同IP）

    //3. 绑定套接字和网络地址
    //bind将server_addr所指的socket地址分配给server_socket文件描述符，成功返回0，失败返回-1
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        error_die("[bind]");
    }

    //动态分配端口(如果传入端口为0)
    socklen_t nameLen = sizeof(server_addr);
    if(*port == 0){
        ret = getsockname(server_fd, (struct sockaddr*)&server_addr, &nameLen);
        if(ret == -1){
            error_die("[getsockname]");
        }
        *port = server_addr.sin_port;
    }
    
    //4.创建监听队列
    if(listen(server_fd, 5) == -1){
        error_die("[listen]");
    }
    return server_fd;
}

int GetHeadData(int client_fd, string& str){

    int n = 1;
    string line ;
    while(n > 0 && line !="\n"){
        line.clear() ;
        char c = '\0'; //字符串结束符
        while(c != '\n'){
            n = recv(client_fd, &c, 1, 0); //读取到的字节数，成功时总为1
            if(n > 0){
            //htpp协议中，\r\n总是一起出现，加入判断处理\r后不是\n的情况，提高代码健壮性
                if(c == '\r'){ 
                    //查看下一个字符，但不实际读取
                    n = recv(client_fd, &c, 1, MSG_PEEK); 
                    //后面是\n，正常
                    if(n > 0 && c == '\n'){ 
                        //实际读取
                        n = recv(client_fd, &c, 1, 0); 
                    }
                else{ //后面不是\n
                    c = '\n';
                    }
                }
                line += c;
            }
            else{
                c = '\n'; //跳出循环
            }
        }
        str += line;
    }
    cout << '[' << __func__ << __LINE__ << "] str: " << str << endl;
    return str.size();
}

void unImplement(int client_fd){

    string fileName = "documents/unimplement.html"; 
    ifstream file(fileName,  ios::in | ios::binary);
    string type = "text/html";
    string statuscode = "501 Implemented";
    SendResource(client_fd, file, type, statuscode);
    cout << '[' << __func__ << __LINE__ << "] 501发送完毕！" << endl;    
}

void NotFound(int client_fd){
    string fileName = "documents/notfound.html"; 
    ifstream file(fileName,  ios::in | ios::binary);
    string type = "text/html";
    string statuscode = "404 NOT FOUND";
    SendResource(client_fd, file, type, statuscode);
    cout << '[' << __func__ << __LINE__ << "] 404发送完毕！" << endl;    
    file.close();
}

void SendHttpHead(int client_fd, const string &type, 
const string &statuscode, size_t file_size){
    // 构建HTTP响应头
    ostringstream response;
    response << "HTTP/1.1 "<< statuscode << "\r\n";
    response << "Server: WonderHttpd/0.1\r\n";
    // response << "Cotent-Length: " << file_size << "\r\n";
    response << "Content-Type: " << type << "; charset=UTF-8\r\n";
    response << "\r\n";
    // 发送HTTP响应头
    string response_str = response.str();
    if(send(client_fd, response_str.c_str(), response_str.length(), 0) == -1){
        cout << '[' << __func__ << __LINE__ << "] 响应头发送失败" << endl;
    }
}

void SendResource(int client_fd, ifstream& file, 
const string &type, const string &statuscode){

    // 获取文件大小
    file.seekg(0, ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, ios::beg);

    // 分配缓冲区
    char* buffer = new char[file_size];
    if (!buffer) {
        error_die("[Failed to allocate memory]");
    }

    // 读取文件内容到缓冲区
    file.read(buffer, file_size);

    //发送响应头
    SendHttpHead(client_fd, type, statuscode, file_size);

    // 发送文件内容
    if(send(client_fd, buffer, file_size, 0) == -1){
        cout << '[' << __func__ << __LINE__ << "] 资源发送失败" << endl;
    };

    delete[] buffer;
}

void ServerSendFile(int client_fd, const string &fileName){

    //读文件
    ifstream file(fileName,  ios::in | ios::binary);
    if(!file){
        //文件打开失败，返回错误信息
        NotFound(client_fd);
    }
    else{
        //获取路径后缀
        int index = fileName.find_last_of('.');
        string type = fileName.substr(index + 1); 
        //确定Content-type
        if(type == "html"){
            type = "text/html";
        }
        else if(type == "png"){
            type = "image/png";
        }
        else if(type == "jpg"){
            type = "image/jpeg";
        }
        else if( type == "css") {
            type = "text/css";
        }
        else if(type == "js"){
            type = "application/x-javascript";
        }
        string statusCode = "200 OK";
        SendResource(client_fd, file, type, statusCode);
        cout << '[' << __func__ << __LINE__ << "] 资源发送完毕！" << endl;    
    }
    file.close();
}

void cannot_execute(int client_fd){
    ostringstream response;
    response << "HTTP/1.1 500 Internal Server Error\r\n";
    response << "Server: WonderHttpd/0.1\r\n";
    response << "Content-type: text/html\r\n";
    response << "\r\n";
    response << "<P>Error prohibited CGI execution.\r\n";
    // 发送
    string response_str = response.str();
    send(client_fd, response_str.c_str(), response_str.length(), 0);
}

void execute_cgi(int client_fd, const string &path, 
const string &method, const string &query_length){

    string h = "HTTP/1.0 200 OK\r\n";
    send(client_fd, h.c_str(), h.length(), 0);
    
    //下面这里创建两个管道，用于两个进程间通信
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    if (pipe(cgi_output) < 0) {
        cannot_execute(client_fd);
        return;
    }
    if (pipe(cgi_input) < 0) {
        cannot_execute(client_fd);
        return;
    }

    //创建一个子进程
    if ( (pid = fork()) < 0 ) {
        cannot_execute(client_fd);
        return;
    }

    //子进程用来执行 cgi 脚本
    else if (pid == 0)  /* child: CGI script */
    {
        char meth_env[255];
        char query_env[255];
        char length_env[255];
        // char contenttype_env[255];

        //将子进程的输出由标准输出重定向到 cgi_ouput 的管道写端上
        dup2(cgi_output[1], 1);
        //将子进程的输入由标准输入重定向到 cgi_ouput 的管道读端上
        dup2(cgi_input[0], 0);
        //关闭 cgi_ouput 管道的读端与cgi_input 管道的写端
        close(cgi_output[0]);
        close(cgi_input[1]);

        //构造一个环境变量
        sprintf(meth_env, "REQUEST_METHOD=%s", method.c_str());
        //将这个环境变量加进子进程的运行环境中
        putenv(meth_env);

        //根据http 请求的不同方法，构造并存储不同的环境变量
        if (strcasecmp(method.c_str(), "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_length.c_str());
            putenv(query_env);
        }
        else {   /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", stoi(query_length));
            putenv(length_env);
        }
        //最后将子进程替换成另一个进程并执行 cgi 脚本
        execl(path.c_str(), path.c_str(), NULL);
        exit(0);
    }
    else {    
        //父进程则关闭了 cgi_output管道的写端和 cgi_input 管道的读端
        close(cgi_output[1]);
        close(cgi_input[0]);
        //如果是 POST 方法的话就继续读 body 的内容，并写到 cgi_input 管道里让子进程去读
        if (strcasecmp(method.c_str(), "POST") == 0)
        {
            int contentlength = stoi(query_length);
            char* buffer = new char[contentlength + 1];
            read(client_fd, buffer, contentlength);
            buffer[contentlength] = '\0';
            cout << '[' << __func__ << __LINE__ << "] buffer: " << buffer << endl; 
            write(cgi_input[1], buffer, strlen(buffer));
        }
        //然后从 cgi_output 管道中读子进程的输出，并发送到客户端去
        char c = '\0';
        while (read(cgi_output[0], &c, 1) > 0){
            cout << '[' << __func__ << __LINE__ << "] char c : " << c <<endl ;
            send(client_fd, &c, 1, 0);
        } 

        //关闭管道
        close(cgi_output[0]);
        close(cgi_input[1]);
        //等待子进程的退出
        int status;
        waitpid(pid, &status, 0);
    }
}

void handle_get(int client_fd, const string &url){
    
    string query_string = url;
    //判断是否有？
    int index = query_string.find('?');
    query_string = query_string.substr(index + 1);
    cout << '[' << __func__ << __LINE__ << "] query_string: " << query_string << endl;

    //修改路径指向存放文件的目录
    string path = url;
    if(index != -1){
        path = url.substr(0, index);
    }
    path = "documents" + path; 
    cout << '[' << __func__ << __LINE__ << "] path_get: " << path << endl;
    //如果路径最后为/，表示未指定请求的具体文件
    if(path[path.size()-1] == '/'){  
        //默认返回该目录下的index.html文件
        path += "index.html";   
    }

    struct stat status; //路径是文件还是文件夹的标志
    if(stat(path.c_str(), &status) == -1){ //访问失败，即该路径不存在
        //向浏览器返回访问的资源不存在的错误提示
        cout << '[' << __func__ << __LINE__ << "] not found" <<endl;
        NotFound(client_fd); 
    }
    else{
        //如果路径是一个文件夹
        if(status.st_mode & S_IFMT == S_IFDIR){
            //返回该文件夹下的index.html文件
            path += "/index.html";
        }
        if(index == -1 ){
            //服务器发送文件
            ServerSendFile(client_fd, path);
        }
        else{
            cout << '[' << __func__ << __LINE__ << "] 开启CGI"<<endl;
            string method = "GET";
            execute_cgi(client_fd, path, method, query_string);
        }
    }  
    return;     
}

void handle_post(int client_fd, const string &url, string body){
    int pos = body.find("Content-Length");
    string Content_Length;
    for(int i = pos; i < body.size(); i++){
        if(body[i] != '\n'){
            Content_Length += body[i];
        }
        else{
            break;
        }
    }
    pos = Content_Length.find(":");
    Content_Length = Content_Length.substr(pos + 2);
    cout << '[' << __func__ << __LINE__ << "] Content_Length =" << Content_Length << endl;
    string method = "POST";
    string path = url;
    path = "documents" + path; 
    cout << '[' << __func__ << __LINE__ << "] path_post = " << path << endl;
    execute_cgi(client_fd, path, method, Content_Length);
    return;
}

void * accept_request(void * arg) {

    string recvStr; //保存读取的数据
    int client_fd = *(int *)arg; //客户端套接字
    
    //读取接受的数据包
    int numchars = GetHeadData(client_fd, recvStr);
    // cout << '[' << __func__ << __LINE__ << "] recvStr1 = " << recvStr << endl;

    // 解析请求方法和路径
    size_t pos;
    string method, path;
    pos = recvStr.find(" ");
    method = recvStr.substr(0, pos);
    cout << '[' << __func__ << __LINE__ << "] method = " << method << endl;
    recvStr.erase(0, pos + 1);
    pos = recvStr.find(" ");
    path = recvStr.substr(0, pos);
    cout << '[' << __func__ << __LINE__ << "] path0 = " << path << endl;
    
    // 处理GET请求
    if (method == "GET") {
        handle_get(client_fd, path);
    }
    // 处理POST请求
    else if (method == "POST") {
        handle_post(client_fd, path, recvStr);
    }
    // 不支持的请求方法
    else {
        unImplement(client_fd);
    }
    //关闭客户端套接字
    close(client_fd);
    cout << '[' << __func__ << __LINE__ << "]*****************已关闭连接*****************" << endl;
    return NULL;
}

int main(void){
    unsigned short port = 10000;
    int server_fd = startup(&port);
    cout << "httpd服务已启动，正在监听" << port << "号端口..."<< endl;

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    while(1){
        //阻塞式等待用户通过浏览器发起访问
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if(client_fd == -1){
            error_die("[accept]");
        }
        cout << "Client connected: " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;
        // 创建一个子线程
        pthread_t tid; //接收创建的线程的ID
        int ret = pthread_create(&tid, NULL, accept_request, (void *)&client_fd);
        if(ret != 0) {
            char * errstr = strerror(ret);
            error_die(errstr);
        }
    }
    close(server_fd); //关闭套接字
    return 0;
}

