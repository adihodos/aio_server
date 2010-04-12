#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "base/allocator.h"
#include "base/basetypes.h"
#include "base/debug_helpers.h"
#include "base/eintr_wrapper.h"
#include "base/linked_list.h"
#include "server.h"

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

typedef enum {
  kObjectTypeServer,
  kObjectTypeClient,
  kObjectTypeSignal,
  kObjectTypeMessageQueue
} kObjectType;

typedef enum {
  kThreadInitFail = 1ULL,
  kThreadInitOk = 2ULL
} kThreadInitStatus;

/*!
 * @brief Structure to help identify objects that are associated with
 * an epoll instance. Must be first member in a composite object.
 */
struct object_identity {
  int objid_type;
};

/*!
 * @brief Structure of inter-thread messages.
 */
struct message {
  int msg_code;
  union {
    void*     msg_ptr;
    int       msg_fd;
    uint32_t  msg_u32;
    uint64_t  msg_u64;
  } msg_data;
};

typedef enum  {
  /*
   * struct message contains a file descriptor.
   */
  kITTMessageAddClient
} kITTMessageCodes;

/*!
 * @brief Max number of messages we can place into a message queue.
 */
static const long kMessageQueueMaxMsgCount = 10;

static const long kMessageQueueMaxMsgSize = (long) sizeof(struct message);

static const unsigned int kMessageQueueDefaultPrio = 10;

struct signal_object {
  struct object_identity context;
  int    so_sigfds;
};

struct mqueue_object {
  struct object_identity context;
  mqd_t  mq_queuefds;
};

/*!
 * @brief Stores per-thread state data across function calls.
 */
struct worker_thread {
  int                   wk_epoll_fds;
  struct signal_object  wk_termsig;
  struct mqueue_object  wk_messagequeue;
  dlinked_list_handle   wk_clients;
  char*                 wk_mqueue_name;
  struct allocator*     wk_allocator;
  pthread_t             wk_threadid;
  int                   wk_readyevent;
  int                   wk_rollback;
  int                   wk_quitflag;
};

/*!
 * @brief Holds server state data across function calls.
 */
struct server {
  int     sv_acceptfd;
  int     sv_epollfd;
  int     sv_sigfds;
  int     sv_threadrdy_eventfds;
  struct  worker_thread** sv_workers;
  long    sv_worker_count;
  long    sv_next_wk;
  struct  allocator* sv_allocator; 
  int     sv_rollback;
};

static const size_t kClientIOBuffSize = 2048;

struct client {
  struct object_identity context;
  int    cl_sockfd;
  int    cl_filefd;
  void*  cl_buffer;
  int    cl_rollback;
};

static
struct client*
client_create(
    int sock_fds,
    struct allocator* alc
    )
{
  struct client* cl = alc->al_mem_alloc(alc, sizeof(struct client));
  if (!cl) {
    return NULL;
  }

  cl->context.objid_type = kObjectTypeClient;
  cl->cl_sockfd = sock_fds;
  cl->cl_filefd = -1;
  cl->cl_buffer = alc->al_mem_alloc(alc, kClientIOBuffSize);
  if (!cl->cl_buffer) {
    goto ERR_CLEANUP;
  }

  return cl;

ERR_CLEANUP :
  HANDLE_EINTR_ON_SYSCALL(close(sock_fds));
  alc->al_mem_release(alc, cl);
  return NULL;
}

static
void
client_destroy(
    struct client* clnt,
    struct allocator* alc
    )
{
  if (clnt->cl_filefd != -1)
    HANDLE_EINTR_ON_SYSCALL(close(clnt->cl_filefd));
  if (clnt->cl_sockfd != -1)
    HANDLE_EINTR_ON_SYSCALL(close(clnt->cl_sockfd));
  alc->al_mem_release(alc, clnt->cl_buffer);
  alc->al_mem_release(alc, clnt);
}

/*!
 * @brief Main function of a worker thread.
 * @param args Pointer to a worker_thread structure.
 * @return Not used.
 */
static
void*
worker_thread_proc(
    void* args
    );

/*!
 * @brief Allocates and initializes a worker_thread structure.
 * @param mqueue_name Name to use for the thread's message queue.
 * @return Pointer to worker_thread* structure on success, NULL on fail.
 * @remarks When no longer needed call worker_thread_destroy() on the object.
 */
static
struct worker_thread*
worker_thread_create(
    const char* mqueue_name,
    struct allocator* allocator
    );

/*!
 * @brief Starts execution of the thread @thread_data using the attributes
 * @thread_attrs.
 * @return 0 on success, -1 on failure.
 */
static
int
worker_thread_start(
    struct server* srvdata,
    struct worker_thread* thread_data,
    pthread_attr_t*       thread_attrs
    );

/*!
 * @brief Frees all resources prieviously allocated with a call ro
 * worker_thread_create. 
 */
static
void
worker_thread_destroy(
    struct worker_thread* data
    );

static
void
worker_thread_handle_event(
    struct worker_thread* state,
    struct epoll_event* ev_data
    );

static
inline
void
worker_thread_handle_signal_event(
    struct worker_thread* state,
    struct signal_object* UNUSED_POST(sig),
    uint32_t UNUSED_POST(event_flags))
{
  /*
   * Don't bother reading anything from the signal descriptor, just set the
   * quit flag and return.
   */
  state->wk_quitflag = 1;
}

static
inline
void
worker_thread_handle_msgqueue_event(
    struct worker_thread* state,
    struct mqueue_object* mqueue,
    uint32_t event_flags
    )
{
  if (event_flags & EPOLLERR) {
    D_FMTSTRING("Error on message queue!");
    return;
  }

  for (;;) {
    struct message msg;
    ssize_t bytes = HANDLE_EINTR_ON_SYSCALL(
        mq_receive(state->wk_messagequeue.mq_queuefds,
                   (char*) &msg,
                   sizeof(msg),
                   NULL /* don't care about message priority */));
    if (bytes == -1) {
      if (errno == EAGAIN) {
        return;
      }
      D_FUNCFAIL_ERRNO(mq_receive);
      return;
    }
    BUGSTOP_IF((msg.msg_code != kITTMessageAddClient), 
               "Unknown message code");
    /*
     * The client object gets ownership of the socket descriptor.
     * Should anything go wrong in the creation process it will
     * close the socket descriptor.
     */
    struct client* clnt = client_create(msg.msg_data.msg_fd, 
                                        state->wk_allocator);
    if (!clnt) {
      /*
       * Client creation failed. Try to get next message, if any.
       */
      continue;
    }

    if (add_fd_to_epoll(state->wk_epoll_fds, clnt->cl_sockfd,
                        EPOLLIN | EPOLLRDHUP, kDataTypePTR,
                        clnt) == -1) {
      client_destroy(clnt, state->wk_allocator);
      continue;
    }
    dlist_push_tail(state->wk_clients, clnt); 
  }
}

/*!
 * @brief Simple function to compare clients in a linked list.
 * @return 0 if equal, 1 if client @xparam > client @yparam, -1 otherwise.
 */
static
inline
int
client_compare(
    const void* xparam, 
    const void* yparam, 
    void* UNUSED_POST(context)
    ) 
{
  assert(xparam);
  assert(yparam);
  const struct client* c_left = (const struct client*) xparam;
  const struct client* c_right = (const struct client*) yparam;

  if (c_left->cl_sockfd == c_right->cl_sockfd) {
    return 0;
  } else if (c_left->cl_sockfd > c_right->cl_sockfd) {
    return 1;
  } else {
    return -1;
  }
}

static 
int 
server_init(
    struct server* p_srv
    );

/*!
 *@brief Creates the socket used to accept connections.
 *@return socket descriptor on success, -1 on failure.
 */
static 
int 
create_server_socket(
    void
    );

/*!
 *@brief Simple wrapper around epoll_ctl() to ease adding a file descriptor
 * and associated data to an epoll instance.
 *@param epoll_fd Epoll instance descriptor.
 *@param fd Descriptor to add.
 *@param event_flags Events to receive for the descriptor fd.
 *@param datatype See kDataTypes enumeration for valid values.
 *@param varargs Data to be associated with fd.
 *@return See man page of epoll_ctl().
 */
static 
int 
add_fd_to_epoll(
    int epoll_fd, 
    int fd,
    __uint32_t event_flags, 
    int datatype, 
    ...
    );

/*!
 * @brief
 */
static 
void 
server_start_accepting_clients(
    struct server* srv_ptr
    );

static 
int 
server_process_event(
    struct server* srv_ptr, 
    struct epoll_event* event
    );

static 
void 
server_cleanup(
    struct server* srv_ptr
    );

int 
server_start_and_run(
    int UNUSED_POST(argc), 
    char** UNUSED_POST(argv)
    ) 
{
  struct server state_data;
  if (server_init(&state_data) == 0) {
    server_start_accepting_clients(&state_data);
  }
  server_cleanup(&state_data);
  return 0;
}

/*
 * @@ Not implemented. @@
 */
static
void*
worker_thread_proc(
    void* args
    ) {
  struct worker_thread* state = (struct worker_thread*) args;
  BUGSTOP_IF((!state), "Invalid thread state specified!");
  D_FMTSTRING("Client thread (%u) starting\n", syscall(SYS_gettid));
  
  /*
   * Add the message queue and the termination event to epoll.
   */
  int result = 0;
  if (add_fd_to_epoll(state->wk_epoll_fds, 
                      state->wk_termsig.so_sigfds,
                      EPOLLIN | EPOLLET,
                      kDataTypePTR,
                      (void*) &state->wk_termsig) == 0) {
    ++result;
  } 

  if (add_fd_to_epoll(state->wk_epoll_fds,
                      state->wk_messagequeue.mq_queuefds,
                      EPOLLIN | EPOLLET,
                      kDataTypePTR,
                      (void*) &state->wk_messagequeue) == 0) {
   ++result;
  } 
  
  /*
   * Notify waiter with our initialize status.
   */
  uint64_t init_status = (result == 2 ? kThreadInitOk : kThreadInitFail);
  HANDLE_EINTR_ON_SYSCALL(write(state->wk_readyevent, &init_status,
                                sizeof(init_status)));
  if (result != 2) {
    /*
     * Failed to init so return.
     */
    return NULL;
  }

  /*
   * Loop forever waiting for events.
   */
  for (; !state->wk_quitflag;) {
    struct epoll_event rec_events[kMaxEpollCompletionEntries];
    int ev_count = HANDLE_EINTR_ON_SYSCALL(epoll_wait(state->wk_epoll_fds,
                                                      rec_events,
                                                      kMaxEpollCompletionEntries,
                                                      -1 /* don;t timeout */));
    if (ev_count == -1) {
      D_FUNCFAIL_ERRNO(epoll_wait);
      break;
    }
    for (int i = 0; i < ev_count && !state->wk_quitflag; ++i) {
      /* do_handle_event(state, rec_events + i); */
      worker_thread_handle_event(state, rec_events + i);
    }
  }

  return NULL;
}

static
struct 
worker_thread*
worker_thread_create(
    const char* mqueue_name,
    struct allocator* alc
    ) 
{
  /*
   * Each worker thread has an epoll descriptor where it multiplexes
   * socket/message_queue descriptors. Also each worker thread has
   * a message queue where it retrieves notifications about new client
   * connections. Once a client has been accept()'ed, the socket is
   * sent to a worker thread via the message queue.
   * The thread will take ownership of the client, serve it and free
   * all the allocated resources when the client disconnects.
   */
  assert(mqueue_name);
  if (!alc) {
    alc = allocator_handle;
  }

  struct worker_thread* wthread = alc->al_mem_alloc(
      alc, 
      sizeof(struct worker_thread));
  if (!wthread) {
    return NULL;
  }
  memset(wthread, 0, sizeof(*wthread));
  wthread->wk_allocator = alc;

  wthread->wk_termsig.context.objid_type = kObjectTypeSignal;
  wthread->wk_termsig.so_sigfds = eventfd(0, EFD_NONBLOCK);
  if (wthread->wk_termsig.so_sigfds == -1) {
    D_FUNCFAIL_ERRNO(eventfd);
    goto ERR_CLEANUP;
  }
  /*
   * Level 1 - event descriptor allocated.
   */
  ++wthread->wk_rollback;

  /*
   * This list is used to keep track of all the clients that are served
   * by this thread. Once a client pointer arrives via the message queue
   * it is put into this list. The client gets removed when it disconnects
   * or when there's an error on the socket.
   */
  wthread->wk_clients = dlist_create(client_compare, 
                                     wthread->wk_allocator, 
                                     wthread);
  if (!wthread->wk_clients)
    goto ERR_CLEANUP;

  /*
   * Level 2 - client list created.
   */
  ++wthread->wk_rollback;

  wthread->wk_mqueue_name = strdup(mqueue_name);
  if (!wthread->wk_mqueue_name) {
    D_FUNCFAIL_ERRNO(strdup);
    goto ERR_CLEANUP;
  }
  /*
   * Level 3 - memory for queue name allocated.
   */
  ++wthread->wk_rollback;

  wthread->wk_epoll_fds = epoll_create(kMaxEpollCompletionEntries);
  if (wthread->wk_epoll_fds == -1) {
    D_FUNCFAIL_ERRNO(epoll_create);
    goto ERR_CLEANUP;
  }

  /*
   * Level 4 - epoll descriptor allocated.
   */
  ++wthread->wk_rollback;

  /*
   * We only read from the message queue while the accepter thread only
   * writes into it. Note that the call to mq_open
   * will fail with EINVAL if the program is not run as superuser and
   * the values of mq_msgsize or mq_maxmsg are greater than those
   * specified in /proc/sys/fs/mqueue/msgsize_max and
   * /proc/sys/fs/mqueue/msg_max.
   */
  struct mq_attr queue_attributes; 
  queue_attributes.mq_flags = O_NONBLOCK; 
  queue_attributes.mq_maxmsg = kMessageQueueMaxMsgCount;
  queue_attributes.mq_msgsize = kMessageQueueMaxMsgSize;

  wthread->wk_messagequeue.context.objid_type = kObjectTypeMessageQueue;
  wthread->wk_messagequeue.mq_queuefds = mq_open(wthread->wk_mqueue_name,
                                                 O_RDWR | O_EXCL | O_CREAT, 
                                                 S_IRWXU | S_IRGRP | S_IROTH,
                                                 &queue_attributes);
  if (wthread->wk_messagequeue.mq_queuefds == (mqd_t) -1) {
    D_FUNCFAIL_ERRNO(mq_open);
    goto ERR_CLEANUP;
  }
  /*
   * Level 5 - message queue created.
   */
  ++wthread->wk_rollback;

  return wthread;

ERR_CLEANUP :
  /*
   * Fall-through is intended here.
   */
  switch (wthread->wk_rollback) {
    case 4 :
      HANDLE_EINTR_ON_SYSCALL(close(wthread->wk_epoll_fds));

    case 3 :
      free(wthread->wk_mqueue_name);

    case 2 :
      dlist_destroy(wthread->wk_clients);

    case 1 :
      HANDLE_EINTR_ON_SYSCALL(close(wthread->wk_termsig.so_sigfds));

    case 0 :
      wthread->wk_allocator->al_mem_release(wthread->wk_allocator,
                                            wthread);

    default :
      break;
  }  
  return NULL;
}

static
int
worker_thread_start(
    struct server* srvdata,
    struct worker_thread* thread_data,
    pthread_attr_t*       thread_attrs
    )
{
  /*
   * Thread signals init done by writing to this event descriptor
   * a status code (2 - init ok, 1 - init failed).
   */
  thread_data->wk_readyevent = srvdata->sv_threadrdy_eventfds;
  if (pthread_create(&thread_data->wk_threadid, thread_attrs, 
                     worker_thread_proc, thread_data)) {
    D_FUNCFAIL_ERRNO(pthread_create);
    return -1;
  }
  /*
   * Level 6 - pthread_create succeeded.
   */
  ++thread_data->wk_rollback;

  uint64_t buff_event;
  ssize_t result = HANDLE_EINTR_ON_SYSCALL(read(srvdata->sv_threadrdy_eventfds,
                                           &buff_event, sizeof(buff_event)));
  D_FMTSTRING("Bytes read from event %d, value %llu", result, buff_event);
  return result == sizeof(buff_event) && buff_event == kThreadInitOk ? 0 : -1;
}

/*!
 * @brief Frees all resources previously allocated with a call ro
 * worker_thread_create.
 */
static
void
worker_thread_destroy(
    struct worker_thread* data
    )
{
  switch (data->wk_rollback) {
    case 6 :
      pthread_join(data->wk_threadid, NULL);

    case 5 :
      mq_close(data->wk_messagequeue.mq_queuefds);
      mq_unlink(data->wk_mqueue_name);

    case 4 :
      HANDLE_EINTR_ON_SYSCALL(close(data->wk_epoll_fds));

    case 3 :
      free(data->wk_mqueue_name);

    case 2 :
      /*
       * TODO : add code to remove clients.
       */
      dlist_destroy(data->wk_clients);

    case 1 :
      HANDLE_EINTR_ON_SYSCALL(close(data->wk_termsig.so_sigfds));

    case 0 :
      data->wk_allocator->al_mem_release(data->wk_allocator,
                                         data);

    default :
      break;
  }
}

static
void
worker_thread_handle_event(
    struct worker_thread* state,
    struct epoll_event* ev_data
    )
{
  struct object_identity* object = (struct object_identity*) ev_data->data.ptr;

  switch (object->objid_type) {
    case kObjectTypeSignal :
      D_FMTSTRING("Thread %d got term signal", syscall(SYS_gettid));
      state->wk_quitflag = 1;
      break;

    case kObjectTypeMessageQueue :
      break;

    case kObjectTypeClient :
      break;

    default :
      D_FMTSTRING("Unknown object type %d", object->objid_type);
      break;
  }
}

static 
int 
server_init(
    struct server* p_srv
    )
{
  assert(p_srv);
  memset(p_srv, 0, sizeof(*p_srv));

  p_srv->sv_allocator = allocator_handle;
  p_srv->sv_acceptfd = create_server_socket();
  if (-1 == p_srv->sv_acceptfd) {
    return -1;
  }
  /*
   * Level 1 - accept socket created.
   */
  ++p_srv->sv_rollback;

  p_srv->sv_epollfd = epoll_create(kMaxEpollCompletionEntries);
  if (-1 == p_srv->sv_epollfd) {
    return -1;
  }
  /*
   * Level 2 - epoll descriptor allocated.
   */
  ++p_srv->sv_rollback;

  /*
   * Block SIGINT and create a signal descriptor to receive it via epoll.
   */
  sigset_t sig_mask;
  sigemptyset(&sig_mask);
  if (-1 == sigaddset(&sig_mask, SIGINT)) {
    return -1;
  }
  if (-1 == sigprocmask(SIG_BLOCK, &sig_mask, NULL)) {
    return -1;
  }
  p_srv->sv_sigfds = signalfd(-1, &sig_mask, SFD_NONBLOCK);
  if (-1 == p_srv->sv_sigfds) {
    return -1;
  }
  /*
   * Level 3 - signal descriptor for SIGINT allocated.
   */
  ++p_srv->sv_rollback;

  /*
   * Add termination signal and accept socket to epoll interface.
   */
  if (-1 == add_fd_to_epoll(p_srv->sv_epollfd,
                            p_srv->sv_sigfds,
                            EPOLLIN | EPOLLET,
                            kDataTypeFD,
                            p_srv->sv_sigfds)) {
    return -1;
  }

  if (-1 == add_fd_to_epoll(p_srv->sv_epollfd,
                            p_srv->sv_acceptfd,
                            EPOLLIN | EPOLLET | EPOLLRDHUP,
                            kDataTypeFD,
                            p_srv->sv_acceptfd)) {
    return -1;
  }

  p_srv->sv_threadrdy_eventfds = eventfd(0, 0);
  if (p_srv->sv_threadrdy_eventfds == -1) {
    D_FUNCFAIL_ERRNO(eventfd);
    return -1;
  }
  /*
   * Level 4 - thread notification event created.
   */
  ++p_srv->sv_rollback;

  /*
   * Get number of available processors. The number of spawned threads is
   * nr_processors * thread_to_proc_ratio.
   */
  long nr_procs = sysconf(_SC_NPROCESSORS_ONLN);
  if (nr_procs == -1) {
    D_FUNCFAIL_ERRNO(sysconf);
    return -1;
  }
  D_FMTSTRING("Online processors %d, will spawn %d threads.",
              nr_procs, nr_procs);

  p_srv->sv_workers = p_srv->sv_allocator->al_mem_alloc(
      p_srv->sv_allocator, (sizeof(struct worker_thread*) * nr_procs));
  if (!p_srv->sv_workers) {
    D_FMTSTRING("Out of memory!");
    return -1;
  }
  /*
   * Level 5 - memory for worker thread data allocated.
   */
  ++p_srv->sv_rollback;
  memset(p_srv->sv_workers, 0, sizeof(struct worker_thread*) * nr_procs);

  /*
   * Initialize data and start worker threads.
   */
  for (long l = 0; l < nr_procs; ++l) {
    char thread_msgqueue[NAME_MAX];
    snprintf(thread_msgqueue, sizeof(thread_msgqueue) - 1, 
             "/__msgqueue_thread_%d__", (int) l);
    struct worker_thread* current = worker_thread_create(thread_msgqueue, 
                                                         p_srv->sv_allocator);
    if (current) {
      if (worker_thread_start(p_srv, current, NULL) == 0) {
        /*
         * Thread successfully initialized, add it to list.
         */
        p_srv->sv_workers[p_srv->sv_worker_count++] = current;
      } else {
        /*
         * Cleanup thread data since pthread_create() failed.
         */
        worker_thread_destroy(current);
      }
    }
  }
  if (!p_srv->sv_worker_count) {
    D_FMTSTRING("Fatal : failed to initialize at least one worker thread!");
    return -1;
  }

  D_FMTSTRING("Started a total of %d worker threads", p_srv->sv_worker_count);
  /*
   * Server is up and running.
   */
  return 0;
}

static 
int 
create_server_socket(
    void
    ) 
{
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

static 
int 
add_fd_to_epoll(
    int epoll_fd, 
    int fd,
		__uint32_t event_flags, 
		int datatype, 
		...
    ) 
{
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

  va_end(args_ptr);
  edata.events = event_flags;
  return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &edata);
}

static 
void 
server_start_accepting_clients(
    struct server* srv_ptr
    )
{
  struct epoll_event rec_events[kMaxEpollCompletionEntries];
  int should_quit = 0;
  for (; !should_quit;) {
    int event_cnt = HANDLE_EINTR_ON_SYSCALL(epoll_wait(srv_ptr->sv_epollfd,
                                                       rec_events,
                                                       kMaxEpollCompletionEntries,
                                                       -1));
    if (event_cnt == -1) {
      D_FUNCFAIL_ERRNO(epoll_wait);
      return;
    }

    for (int i = 0; i < event_cnt && !should_quit; ++i) {
      should_quit = server_process_event(srv_ptr, &rec_events[i]);
    }
  }
}

static 
int 
server_process_event(
    struct server* srv_ptr, 
    struct epoll_event* event
    ) 
{
  if (event->data.fd == srv_ptr->sv_acceptfd) {
    D_FMTSTRING("accept event!");
  } else if(event->data.fd == srv_ptr->sv_sigfds) {
    D_FMTSTRING("SIGQUIT/SIGINT - stopping threads and exiting");
    /*
     * Send termination signal to all running threads.
     */
    for (long i = 0; i < srv_ptr->sv_worker_count; ++i) {
      uint64_t buff = (uint64_t) 1;
      HANDLE_EINTR_ON_SYSCALL(
          write(srv_ptr->sv_workers[i]->wk_termsig.so_sigfds, 
                &buff, sizeof(buff)));
    }
    return 1;
  } else {
    D_FMTSTRING("No handler for descriptor %d", event->data.fd);
  }
  return 0;
}

static 
void 
server_cleanup(
    struct server* srv_ptr
    ) 
{
  assert(srv_ptr);

  /*
   * Fallthrough is intended.
   */
  switch (srv_ptr->sv_rollback) {
    case 5 :
      for (long i = 0; i < srv_ptr->sv_worker_count; ++i) {
        worker_thread_destroy(srv_ptr->sv_workers[i]);
      }
      srv_ptr->sv_allocator->al_mem_release(srv_ptr->sv_allocator,
                                            srv_ptr->sv_workers);

    case 4 :
      HANDLE_EINTR_ON_SYSCALL(close(srv_ptr->sv_threadrdy_eventfds));

    case 3 :
      HANDLE_EINTR_ON_SYSCALL(close(srv_ptr->sv_sigfds));

    case 2 :
      HANDLE_EINTR_ON_SYSCALL(close(srv_ptr->sv_epollfd));

    case 1 :
      HANDLE_EINTR_ON_SYSCALL(close(srv_ptr->sv_acceptfd));

    default :
      break;
  }
}
