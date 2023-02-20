#include "http_conn.h"
#include <mysql/mysql.h>
#include<sys/stat.h>

const char *ok_200_title = "OK";

const char *error_400_title = "Bad requst";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy. \n ";

const char *error_403_title = "Forbidden";
const char *error_403_form = "You dont have permission to get file from this server \n ";

const char *error_404_title = "Not Found";
const char *error_404_form = "The request file not found\n ";

const char *error_500_title = "Internal Error";
const char *error_500_form = "unusual problem serveing the request\n ";

locker m_lock;
map<string, string> users;

//从数据库中获得username和passwd一一对应存于map中
void http_conn::initmysql_result(Connection_Pool *connPool)
{
    MYSQL *mysql = NULL;

    ConnectionRAII(&mysql, connPool);

    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {

    }

    MYSQL_RES *result = mysql_store_result(mysql);

    int num_fields = mysql_num_fields(result);

    MYSQL_FIELD *fields = mysql_fetch_field(result);

    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }

}

int set_noblocking(int fd)
{
    int old_options = fcntl(fd, F_GETFL);
    int new_options = old_options | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_options);
    return old_options;
}

//在内核事件表注册读事件
void addfd(int epollfd, int fd, bool oneshot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLHUP;

    if (oneshot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_noblocking(fd);
}

//在内核事件表删除事件
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, fd, EPOLL_CTL_DEL, 0);
    close(fd);
}

//将事件重置为epolloneshot，并且添加写事件或者读事件
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (TRIGMode == 1)
        event.events = EPOLLONESHOT | EPOLLET | EPOLLRDHUP | ev;
    else
        event.events = EPOLLONESHOT | EPOLLHUP | ev;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_conn)
{
    if (real_conn && m_sockfd != -1)
    {
        printf("close:%d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接，外部调用初始化套接子地质
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode, int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, m_sockfd, true, m_TRIGMode);
    m_user_count++;

    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
// check_state 默认为分析请求行状态
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;
    m_download=false;

    memset(m_read_buffer, '\0', READ_BUFFER_SIZE);
    memset(m_wirte_buffer, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行的内容
//返回值为行读取的状态，有BAD，OPEN，OK
//......\r\n 为正常一行
//\r回到开头\n下一行
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buffer[m_checked_idx];

        if (temp == '\r')
        {
            if (m_checked_idx + 1 == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buffer[m_checked_idx + 1] == '\n')
            {
                m_read_buffer[m_checked_idx++] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buffer[m_checked_idx - 1] == '\r')
            {
                m_read_buffer[m_checked_idx - 1] = '\0';
                m_read_buffer[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可以读或者客户关闭连接
//非阻塞ET模式，需要一次性把数据读完
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    int bytes_read = 0;

    // LT读取数据
    if (m_TRIGMode == 0)
    {
        bytes_read = recv(m_sockfd, m_read_buffer, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;
        
        if (bytes_read <= 0)
            return false;

        return true;
    }
    else // ET读取数据
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buffer, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK) //操作会阻塞或者需要重试，表明已经读完。
                    break;
                return false;
            }
            else if (bytes_read == 0)
                return false;

            m_read_idx += bytes_read;
        }

        return true;
    }
}

//解析http请求行，获得请求方法，目标url及版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url = strpbrk(text, " \t"); //strpbrk是在源字符串（s1）中找出最先含有搜索字符串（s2）中任一字符的位置并返回，若找不到则返回空指针。 \t即table建
    
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    
    if (strcasecmp(text, "GET") == 0) //字符串比较函数，GET放在请求的头部
    {
        m_method = GET;
    }
    else if (strcasecmp(text, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");  //strspn()返回的数值为n，则代表字符串s 开头连续有n 个字符都是属于字符串accept内的字符
    m_version = strpbrk(m_url, " \t");
    
    if (!m_version)
    return BAD_REQUEST;

    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    

    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    if (strlen(m_url) == 1)
    {
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_STATE_HEADER;
    if(strncasecmp(m_url,"/file_download/",15)==0)
    {
    m_url+=15;
    m_download=true;
    }
    return NO_REQUEST;
}

//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONNTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if(strncasecmp(text,"Content-Type: multipart/form-data;",34)==0)
    {
        text+=34;
        text += strspn(text, " \t");
        if(strncasecmp(text,"boundary=",9)==0)
        {
        text+=9;
        m_boundary+="--";
        m_boundary+=text;
        m_isupload=1;
        return GET_REQUEST;
        } 
        else
        return BAD_REQUEST;
    }
    else
    {
    if(m_download)
    {
    cout<<"开始下载"<<endl;
    return GET_REQUEST;
    }
    }

    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text)
{

    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // post请求最后输入为用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE  http_conn::upload_file()
{
      m_filestring.append(m_read_buffer+m_checked_idx,m_read_idx-m_checked_idx);
      int end_index=0;
      end_index=m_filestring.find("filename");

      if(end_index!=string::npos)
      {
        m_filestring.erase(0,end_index+string("filename=\"").size());
        m_filename.clear();
        for(int i=0;m_filestring[i]!='\"';++i)
        m_filename+=m_filestring[i];
        m_filestring.erase(0,m_filename.size()+3);  //得到文件名字，并在string中删除该部分；
        m_filename=m_file_addr+"/"+m_filename;      

        
        end_index=m_filestring.find("\r\n");
        m_filestring.erase(0,end_index+2);
        end_index=m_filestring.find("\r\n");
        m_filestring.erase(0,2); //删除type和空行
        cout<<m_filename<<endl;
        if(!ofs.is_open())
        ofs.open(m_filename,ios::app|ios::binary|ios::out);
        // if(ofs.is_open())
        // cout<<"创建文件成功"<<endl;
      }

        end_index=m_filestring.find(m_boundary);
        if(end_index!=string::npos)
        {
        m_filestring.erase(end_index-2,m_boundary.size()+4);
        ofs.write(m_filestring.c_str(),m_filestring.size());
        m_isupload=-1;
        ofs.close();
        // cout<<"传输完成"<<endl;
        return do_request();
        }
        else
        {
        ofs.write(m_filestring.c_str(),m_filestring.size());
        // cout<<"正在传输"<<endl;
        m_isupload++;
        init();
        }
        m_filestring.clear();
        
        return NO_REQUEST;
}
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    //如果是正在上传文件直接跳转
    if(m_isupload>=1)
    return upload_file();

    while ((m_check_state == CHECK_STATE_CONNTENT && line_status == LINE_OK) || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();  //获得http请求中的一行数据
        m_start_line = m_checked_idx;

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }

        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();    //
            }
            break;
        }

        case CHECK_STATE_CONNTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
         
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{

    //处理文件上传完成后返回原来界面
    if(m_isupload>=1)
    {
    return upload_file();
    }
    
    if(m_download)
    {
        string path="./urs/"+m_name+"/"+m_url;

        cout<<"下载路径为:"<<path<<endl;
        if (stat(path.c_str(), &m_file_stat) < 0)
        return NO_RESOURCE;
        int fd = open(path.c_str(), O_RDONLY);
        m_file_address = mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); //mmap将一个文件或者其它对象映射进内存。返回被应社区
        close(fd);
        return FILE_REQUEST;

    }
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);  

    if(m_isupload==-1)
    {
        m_url=(char *)malloc(sizeof(char) * 20);
        strcpy(m_url,"/8");
        cout<<"m_url"<<m_url<<endl;
    }

    const char* p = strchr(m_url, '/');
    cout<<"*(p+1):"<<*(p+1)<<endl;
    //处理cgi
    if (cgi == 1 &&(*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //检测是登陆还是注册
        char flag = m_url[1];
        


        char* m_url_real = (char* )malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);

        free(m_url_real);

        //提出来用户名和密码
        char name[100];
        char password[100];
        int i;
        

        for (i = 5; m_string[i] != '&'; ++i)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
        {
            password[j] = m_string[i];
        }
        password[j] = '\0';
        


        if (*(p + 1) == '3')
        {
            //注册前先检查重复

            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "insert into user(username,passwd) values(");
            strcat(sql_insert, name);
            strcat(sql_insert, ",");
            strcat(sql_insert, password);
            strcat(sql_insert, ")");
            if (users[name]=="")
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users[name]=password;
                m_lock.unlock();

                if (!res)
                {
                    strcpy(m_url, "/log.html");
                }
                else
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
                strcpy(m_url, "/registerError.html");
        } //登陆检查是密码正确
        else if (*(p + 1) == '2')
        {
      
            if (users.find(name) != users.end() && users[name] == password)
            {
                string path;
                m_name=name;
                m_file_addr="./urs/"+m_name;
                check_mydir(m_file_addr);
                path="/../"+m_file_addr+"/welcome.html";
                strcpy(m_url, path.c_str());
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    if (*(p + 1) == '0') //注册页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '1') //登陆页面
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '5') //照片
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '6') //视频
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if (*(p + 1) == '7') // fans
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '8')
    {
        m_isupload=0;
        m_boundary.clear();
        cout<<"here1"<<endl;
        get_myfiles();
        cout<<"here2"<<endl;
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/upload.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
        cout<<"m_real_file:"<<m_real_file<<endl;
    }
    else
    {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0); //mmap将一个文件或者其它对象映射进内存。返回被应社区
    close(fd);
    return FILE_REQUEST;
}

// mmap内存共享映射，在内存中一段地址映射一个文件
void http_conn::umap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int newadd=0;

    if (bytes_to_send == 0) //如果没有发送的信息，重置ONESHOT
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    
    cout<<"bytes_to_send:"<<bytes_to_send<<endl;
    while (1)
    {
    
        temp = writev(m_sockfd, m_iv, m_iv_count); //将两个或者多个不连续缓冲区中的数据一次性写出去。
         
        
        if(temp>=0)
        {
            bytes_have_send+=temp;
            newadd=bytes_have_send-m_write_idx;
        }
        else
        {
            if(errno==EAGAIN)
            {
                if (bytes_have_send >= m_iv[0].iov_len)  //判断第一个文件是否发送完成
                {
                    m_iv[0].iov_len = 0;
                    m_iv[1].iov_base = m_file_address + newadd;
                    m_iv[1].iov_len = bytes_to_send;
                }
                else
                {
                    m_iv[0].iov_base = m_wirte_buffer + bytes_have_send;
                    m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
                }
                modfd(m_epollfd, m_sockfd, EPOLLOUT,m_TRIGMode);
                return true;
            }
            umap();
            return false;
        }
        bytes_to_send -= temp;

        cout<<"bytes_to_send:"<<bytes_to_send<<endl;
        if (bytes_to_send <= 0)
        {

            umap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }

    }

}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    va_list arg_list; //解决变参数问题
    va_start(arg_list, format);

    int len = vsnprintf(m_wirte_buffer + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }

    m_write_idx += len;
    va_end(arg_list);

    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    bool ret;
    ret=add_content_length(content_len);
    ret=add_linger();
    if(m_download)
    add_content("Content-Type: application/octet-stream\r\n");
    ret=add_blank_line();
    return ret;
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

bool http_conn::add_contnet_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {

    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            cout<<"m_file_stat.st_size:"<<m_file_stat.st_size <<endl;
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_wirte_buffer;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
             
            bytes_to_send = m_write_idx + m_file_stat.st_size;
       
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }

    m_iv[0].iov_base = m_wirte_buffer;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode); //向epoll中添加读事件
        return;
    }
    bool wirte_ret = process_write(read_ret); //修改聚集写数据结构iovec,如果需要同时发送文件和网页，需要两个iovec结构
    if (!wirte_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //向epoll中添加写事件
}
//检查和创建用户文件夹
bool http_conn::check_mydir(string path)
{
 
   
   if(access(path.c_str(),0)==-1)
   {
   if(!mkdir(path.c_str(),S_IRWXU))
   {
    string cmd;
    cmd+="cp root/welcome.html ";
    cmd+=path;
    cmd+="/welcome.html";
    system(cmd.c_str());
    return true;
   }
   return false;
   }

   return true;
}

//提取用户目录下的文件并写入welcome.html
void http_conn::get_myfiles()
{
    DIR* dir=NULL;
    dirent* entry;
    m_files.clear();
    m_file_addr="./urs/"+m_name;
    if((dir=opendir(m_file_addr.c_str()))==NULL)
    {
        cout<<m_file_addr<<endl;
        cout<<"打开失败"<<endl;
        return;
    }
    else
    {
        while(entry=readdir(dir))
        {
           m_files.push_back(entry->d_name);

           if(m_files.back()=="."||m_files.back()==".."||m_files.back()=="welcome.html")
           m_files.pop_back();
        }
        closedir(dir);
    }


    ifstream file(m_file_addr+"/welcome.html",ios::in);
     
    if(!file.is_open())
    {
    cout<<"文件打开失败"<<endl;
    return;
    }
    string html;
    string temp;
    while(getline(file,temp))
    {
       html+=temp;
       html+='\n';
    }
    int loc=0;
    loc=html.find("文件列表");
    loc+=13;
    if(loc==string::npos)
    return;

    for(auto s:m_files)
    {
        if(html.find(s)==string::npos)
        {
        temp.clear();
        temp+="<br/>";
        temp+=s;
        temp+="<a href=\"./file_download/";
        temp+=s;
        temp+="\">下载</a>\n";
        html.insert(loc,temp);
        }
    }
    
    file.close();
    ofstream file_w(m_file_addr+"/welcome.html");
    file_w.write(html.c_str(),html.size()); 
    file_w.close();
}