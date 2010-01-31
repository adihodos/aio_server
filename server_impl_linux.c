#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#define GLUE_TOKENS(a, b) a ## b

#define STATIC_ASSERTION(condition) \
  unsigned char GLUE_TOKENS(dummyarray, __LINE__)[(condition ? 1 : -1)]

typedef enum {
  kEpollEventConnectionArrived,
  kEpollEventReadCanRead,
  kEpollEventCanWrite
} kEpollEvent;

typedef enum {
  kEpollDataTypeNone,
  kEpollDataTypePTR,
  kEpollDataTypeFD,
  kEpollDataTypeU32,
  kEpollDataTypeU64
} kEpollDataType;

typedef enum {
  kObjectTypeServerData,
  kObjectTypeClientData
} kObjectType;

static const int kMaxEpollEvents = 64;
static const char* const kDefaultServerPort = "27015";

struct io_context {
  /*!
   * Context for non-blocking IO.
   */
  struct epoll_event io_epoll_ctx;
  /*!
   * Operation code (read/write/connect).
   */
  int io_opcode;
  /*!
   * Type of object pointed to.
   */
  int io_objecttype;
};

struct client_data {
  struct io_context io_ctx;
  int c_sockfd;
  int c_filefd;
  unsigned char buffer[1024];
};

struct server_data {
  struct io_context io_ctx;
  int s_sockfd;
  int s_accept_epoll;
  int s_client_epoll;
};

/*!
 *@brief Sets a file descriptor as non-blocking.
 *@return 0 on success, -1 on failure.
 */
static int set_fd_nonblocking(int fd);

/*!
 * @brief Creates a socket in non-blocking mode.
 * @return Socket descriptor on success, -1 on failure.
 */
static inline int create_non_blocking_socket(void) {
  return socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
}

/*!
 * @brief Creates the socket used by the server to accept connections,
 * will put it in listen mode and bind it.
 * @return socket descriptor on success, -1 on failure.
 */
static int create_server_socket(void);

/*!
 * @brief Initializes the data structures needed by the server. 
 * @return 0 on success, -1 on failure.
 */
static int setup_server(struct server_data* srv);

/*!
 * @brief Wrapper around epoll_ctl() to add more easily descriptors 
 * and associated data.
 * @param [in] epoll_fd Epoll descriptor
 * @param [in] src_fd File descriptor to add.
 * @param [in] event_flags See man page for epoll_ctl() for possible values.
 * @param [in] datatype Type of data to associate with src_fd. See members of
 * epoll_data union in man page of epoll_ctl().
 * @param [in] data Generic placeholder for data.
 * @return 0 on success, -1 on failure.
 */
static int add_fd_to_epoll(int epoll_fd,
                           int src_fd,
                           unsigned event_flags,
                           kEpollDataType datatype,
                           void* data);

/*!
 * This is made global to ease debugging.
 */
struct server_data my_server;
/*!
 * @brief Initialized and runs the server.
 */
int server_start_and_run(int argc, char** argv) {
  if (0 == setup_server(&my_server)) {
    fprintf(stdout, "Started");
  }
  return 0;
}

static int setup_server(struct server_data* srv) {
  memset(srv, 0, sizeof(*srv));
  srv->io_ctx.io_objecttype = kObjectTypeServerData;
  size_t rollback = 0;
  /*
   * This epoll descriptor is used by the server to accept connections.
   */
  srv->s_accept_epoll = epoll_create(kMaxEpollEvents);
  if (-1 == srv->s_accept_epoll) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 1.
   */
  ++rollback; 

  /*
   * This epoll descriptor is used by the worker threads to service 
   * client requests.
   */
  srv->s_client_epoll = epoll_create(kMaxEpollEvents);
  if (-1 == srv->s_client_epoll) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 2.
   */
  ++rollback;
  
  srv->s_sockfd = create_server_socket();
  if (-1 == srv->s_sockfd) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 3
   */
  ++rollback;

  /*
   * Add socket to epoll fd.
   */
  if (-1 == add_fd_to_epoll(srv->s_accept_epoll,
                            srv->s_sockfd,
                            EPOLLIN | EPOLLET | EPOLLERR | EPOLLRDHUP,
                            kEpollDataTypePTR,
                            srv)) {
    goto ERR_CLEANUP;
  }

  return 0;

ERR_CLEANUP :
  switch (rollback) {
    case 3 :
      close(srv->s_sockfd);

    case 2 :
      close(srv->s_client_epoll);

    case 1 :
      close(srv->s_accept_epoll);

    default :
      break;
  }

  return -1;
}

static int set_fd_nonblocking(int fd) {
  int old_flags = fcntl(fd, F_GETFL);
  if (-1 == old_flags) {
    return -1;
  }

  return !fcntl(fd, F_SETFL, old_flags | O_NONBLOCK);
}

static int create_server_socket(void) {
  struct addrinfo hints;
  struct addrinfo* s_data = NULL;
  int sock_fd = -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  int ret_code = getaddrinfo(NULL, kDefaultServerPort, &hints, &s_data);
  if (-1 == ret_code) {
    goto ERR_CLEANUP;
  }

  sock_fd = create_non_blocking_socket();
  if (-1 == sock_fd) {
    goto ERR_CLEANUP;
  }

  ret_code = bind(sock_fd, s_data->ai_addr, s_data->ai_addrlen);
  if (-1 == ret_code) {
    goto ERR_CLEANUP;
  }

  ret_code = listen(sock_fd, SOMAXCONN);
  if (-1 == ret_code) {
    goto ERR_CLEANUP;
  }

  freeaddrinfo(s_data);
  return sock_fd;

ERR_CLEANUP :
  if (-1 != sock_fd) {
    close(sock_fd);
  }
  if (s_data) {
    freeaddrinfo(s_data);
  }
  return -1;
}

static int add_fd_to_epoll(int epoll_fd,
                           int src_fd,
                           unsigned event_flags,
                           kEpollDataType datatype,
                           void* data) {
  STATIC_ASSERTION((sizeof(__uint32_t) <= sizeof(void*)));
  STATIC_ASSERTION((sizeof(__uint64_t) <= sizeof(void*)));
  struct epoll_event edata;
  edata.events = event_flags;
  switch (datatype) {
    case kEpollDataTypePTR :
      edata.data.ptr = data;
      break;

    case kEpollDataTypeFD :
      edata.data.fd = (int) data;
      break;

    case kEpollDataTypeU32 :
      edata.data.u32 = (__uint32_t) data;
      break;

    case kEpollDataTypeU64 :
      edata.data.u64 = (__uint64_t) data;
      break;

    default :
      break;
  }

  return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, src_fd, &edata);
}
