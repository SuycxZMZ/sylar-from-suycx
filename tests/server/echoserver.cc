#include "iomanager.h"
#include "logger.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <strings.h>
#include <iostream>
#include <stack>


static int sock_listen_fd = -1;
void test_accept();

void error(char * msg) 
{
    perror(msg);
    printf("erreur ... \n");
    exit(1);
}

void watch_io_read() 
{
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}

void test_accept()
{
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    int fd = accept(sock_listen_fd, (struct sockaddr*)&addr, &len);
    if (fd < 0) {
        LOG_ERROR("fd = %d, accept error", fd);
    } else {
        fcntl(fd, F_SETFL, O_NONBLOCK);
        sylar::IOManager::GetThis()->addEvent(fd, sylar::IOManager::READ, [fd]() {
            char buffer[1024] = {0};
            bzero(buffer, sizeof(buffer));
            while (true)
            {
                int ret = recv(fd, buffer, sizeof(buffer), 0);
                if (ret > 0) {
                    ret = send(fd, buffer, ret, 0);
                }
                if (ret <= 0) {
                    if (errno == EAGAIN) continue;
                    close(fd);
                    break;
                }
            }
        });
    }

    sylar::IOManager::GetThis()->schedule(watch_io_read);
}

void test_iomanager() 
{
    int portno = 8000;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen_fd < 0) {
        LOG_ERROR("error creating socket ...");
    }

    int yes = 1;
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    bzero((char*)&server_addr, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portno);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOG_ERROR("error binding socket ...");
    }

    if (listen(sock_listen_fd, 1024) < 0) {
        LOG_ERROR("error listening socket ...");
    }

    LOG_INFO("epoll echo server listening for connection on port : %d", portno);
    fcntl(sock_listen_fd, F_SETFL, O_NONBLOCK);

    // sylar::IOManager::GetThis()->addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
    sylar::IOManager iom;
    iom.addEvent(sock_listen_fd, sylar::IOManager::READ, test_accept);
}


int main(int argc, char *argv[])
{
    test_iomanager();
    return 0;
}