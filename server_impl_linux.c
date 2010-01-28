#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "base/basetypes.h"
#include "base/debug_helpers.h"

static const char* const kDefaultServerPort = "27015";

static const size_t kMaxEpollCompletionEntries = 16;

/*!
 * @brief Valid data types that can be associated with a file descriptor and
 * added to an epoll instance.
 */
typedef enum {
  kDataTypeNone,
  kDataTypeFD, /* a file descriptor */
  kDataTypePTR, /* void ptr */
  kDataTypeU32, /* __uint32_t */
  kDataTypeU64 /* __uint64_t */
} kDataTypes;

struct io_context {
  struct epoll_event io_ctx_data;
  int io_ctx_objtype;
  int io_ctx_opcode;
};

struct server {
  struct io_context context;
  int sv_acceptfd;
  int sv_epollfd;
  int sv_termsigfd;
};

struct client {
  struct io_context context;
  int cl_sockfd;
  int cl_filefd;
};

static int server_init(struct server* p_srv);

/*!
 *@brief Creates the socket used to accept connections.
 *@return socket descriptor on success, -1 on failure.
 */
static int create_server_socket(void);

/*!
 *@brief Simple wrapper around epoll_ctl() to ease adding a file descriptor
 * and associated data to an epoll instance.
 *@param epoll_fd Epoll instance descriptor.
 *@param fd Descriptor to add.
 *@param event_flags Events to receive for the descriptor fd.
 *@datatype See kDataTypes enumeration for valid values.
 *@vararg Data to be associated with fd.
 *@return See man page of epoll_ctl().
 */
static int add_fd_to_epoll(
    int epoll_fd, 
    int fd,
    __uint32_t event_flags, 
    int datatype, 
    ...);


int server_start_and_run(int UNUSED_POST(argc), char** UNUSED_POST(argv)) {
  struct server srv;
  if (-1 == server_init(&srv)) {
    D_FMTSTRING("Failed to initialize the server!", "");
  } else {
    D_FMTSTRING("Server initialized ok!", "");
  }
  return 0;
}

static int server_init(struct server* p_srv) {
  assert(p_srv);
  memset(p_srv, 0, sizeof(*p_srv));
  p_srv->sv_acceptfd = p_srv->sv_epollfd = p_srv->sv_termsigfd = -1;
  /*p_srv->context->io_ctx_data.data.ptr = p_srv;*/
  /*p_srv->context->io_ctx_data.data.events = EPOLLIN | EPOLLET | EPOLLRDHUP;*/

  size_t rollback = 0;
  p_srv->sv_acceptfd = create_server_socket();
  if (-1 == p_srv->sv_acceptfd) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 1 - accept socket created.
   */
  ++rollback;

  p_srv->sv_epollfd = epoll_create(kMaxEpollCompletionEntries);
  if (-1 == p_srv->sv_epollfd) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 2 - epoll descriptor allocated.
   */
  ++rollback;

  /*
   * Block SIGINT and create a signal descriptor to receive it via epoll.
   */
  sigset_t sig_mask;
  sigemptyset(&sig_mask);
  if (-1 == sigaddset(&sig_mask, SIGINT)) {
    goto ERR_CLEANUP;
  }
  if (-1 == sigprocmask(SIG_BLOCK, &sig_mask, NULL)) {
    goto ERR_CLEANUP;
  }
  p_srv->sv_termsigfd = signalfd(-1, &sig_mask, SFD_NONBLOCK);
  if (-1 == p_srv->sv_termsigfd) {
    goto ERR_CLEANUP;
  }
  /*
   * Level 3 - signal descriptor for SIGINT allocated.
   */
  ++rollback;
  if (-1 == add_fd_to_epoll(p_srv->sv_epollfd,
                            p_srv->sv_acceptfd,
                            EPOLLIN | EPOLLET | EPOLLRDHUP,
                            kDataTypePTR,
                            p_srv)) {
    goto ERR_CLEANUP;
  }

  return 0;

 ERR_CLEANUP :
  switch (rollback) {
  case 3 :
    close (p_srv->sv_termsigfd);
    p_srv->sv_termsigfd = -1;

  case 2 :
    close (p_srv->sv_epollfd);
    p_srv->sv_epollfd = -1;

  case 1 :
    close (p_srv->sv_acceptfd);
    p_srv->sv_acceptfd = -1;

  default :
    break;
  }

  return -1;
}

static int create_server_socket(void) {
  struct addrinfo hints;
  struct addrinfo* sv_data = NULL;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;

  size_t stage = 0;
  int sock_fd = -1;
  int ret_code = getaddrinfo(NULL,
                             kDefaultServerPort,
                             &hints,
                             &sv_data);
  if (ret_code) {
    goto FINISH;
  }

  sock_fd = socket(sv_data->ai_family, 
                   sv_data->ai_socktype | SOCK_NONBLOCK,
                   sv_data->ai_protocol);
  if (-1 == sock_fd) {
    goto FINISH;
  }
  /*
   * Socket created.
   */
  ++stage;

  ret_code = bind(sock_fd, sv_data->ai_addr, sv_data->ai_addrlen);
  if (-1 == ret_code) {
    goto FINISH;
  }
  /*
   * Socket bound.
   */
  ++stage;

  ret_code = listen(sock_fd, SOMAXCONN);
  if (!ret_code) {
    /*
     * Socket was put in listen mode.
     */
    ++stage;
  }
  
 FINISH :
  if (sv_data) {
    freeaddrinfo(sv_data);
  }
  if (stage != 3) {
    /*
     * Something went wrong. Close the socket descriptor if one was allocated.
     */
    if (-1 != sock_fd) {
      close(sock_fd);
      sock_fd = -1;
    }
  }
  return sock_fd;
}

static int add_fd_to_epoll(
    int epoll_fd, 
    int fd,
		__uint32_t event_flags, 
		int datatype, 
		...) {
  struct epoll_event edata;
  va_list args_ptr;
  va_start(args_ptr, datatype);

  switch (datatype) {
  case kDataTypeFD :
    edata.data.fd = va_arg(args_ptr, int);
    break;

  case kDataTypePTR :
    edata.data.ptr = va_arg(args_ptr, void*);
    break;

  case kDataTypeU32 :
    edata.data.u32 = va_arg(args_ptr, __uint32_t);
    break;

  case kDataTypeU64 :
    edata.data.u64 = va_arg(args_ptr, __uint64_t);
    break;

  default :
    break;
  }

  edata.events = event_flags;
  return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &edata);
}
