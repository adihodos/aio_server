/****************************************************************
 
                   /\ /\ /\
                   || || || /\____________  _______
         __       _||_||_||//-------------\/--------\_________
 |\______||______/ || || ||       | |     |  |  \___/   \
 ||      ||      | ---------------| |     |  |  |___     |
 ||______||______\________________| |_____|  |  |___\___/
 |/      ||        || || || \_____|_|/    |__|__/   
                   || || || 
                   \/ \/ \/

                  And so, it begins... 
****************************************************************/                   
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <tchar.h>
#include <process.h>
#include "base/debug_helpers.h"
#include "base/allocator.h"
#include "base/linked_list.h"
#include "base/atomic.h"
#include "base/lock.h"
#include "base/misc.h"
#include "base/http_codes.h"
#include "base/statistics.h"
#include "server.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")
#pragma comment(lib, "user32.lib")

/*!
 * @def Maximum number of completion notifications to retrieve
 * via GetQueuedCompletionStatusEx().
 */
#define kMaxCompletionCount 16

/*!
 * Since we cannot pass any user defined arguments to SetConsoleCtrlHandler() we
 * need to use this global handles to signal CTRL-C/LOGON/LOGOFF events.
 */
static HANDLE iocp_handle;
static HANDLE iocp_accept_handle;

/*!
 * Options for server.
 */
static struct server_opts sv_options;

/*!
 * Default directory to the resources if none is specified.
 */
static const char* kSVDefaultWWWRootDir = "C:/temp/wwwroot";

/*!
 * Default thread to processor ratio (1:1)
 */
static const int kSVDefaultThreadToProcessorRatio = 1;

/*!
 * Number of maximum connected clients. -1 Means no limit.
 * Not implemented.
 */
static const int kSVDefaultUnlimitedConnections = -1;

/*!
 * Default server port.
 */
static const char* const kSVDefaultPort = "27015";

typedef enum {
  kIOOpcodeConnection   = 0x00,
  kIOOpcodeSockRead     = 0x01,
  kIOOpcodeSockWrite    = 0x02,
  kIOOpcodeSock0BytesRecv = 0x03
} kIOOpcode;

typedef enum {
  kClientCloseAbortative = (1UL << 0),
  kClientCloseRemoveFromList = (1UL << 1)
} kClientCloseOpt;

typedef enum {
  kServerStop = 0x00,
  kServerContinue = 0x01
} kServerAction;

struct aio_data {
  WSAOVERLAPPED   aio_ctx;
  kIOOpcode       aio_opcode;
};

typedef enum {
  kIocpMsgShutDown = 0x00,
  kIocpMsgPostAccept = 0x01
} kIocpMessage;

typedef enum {
  kAcceptExResultSuccess = 0x00,
  kAcceptExResultConnReset = 0x01,
  kAcceptExResultFailed = 0x02
} kAcceptExResult;

/*!
 * @brief Abstract representation of the server.
 */
struct server_data {
  /*!
   * Context for overlapped operations. 
   */
  struct aio_data           io_context;
  /*!
   * Socket to listen.
   */
  SOCKET                    sv_sock;
  /*!
   * Completion port for notifications.
   */
  HANDLE                    sv_iocp;
  /*!
   * Completion port for the accept thread.
   */
  HANDLE                    sv_iocp_accept;
  /*
   * Number of outstanding AcceptEx() calls.
   */
  atomic32_t                sv_outstanding_accepts;
  /*!
   * Array of worker threads.
   */
  HANDLE*                   sv_worker_threads;
  /*!
   * Number of worker threads.
   */
  DWORD                     sv_worker_count;
  /*!
   * Pointer to AcceptEx() Winsock extension.
   */
  LPFN_ACCEPTEX             sv_acceptx;
  /*!
   * Pointer to GetAcceptExSockaddrs() Winsock extension.
   */
  LPFN_GETACCEPTEXSOCKADDRS sv_getacceptexsockaddrs;
  /*!
   * Pointer to TransmitFile() Winsock extension.
   */
  LPFN_TRANSMITFILE         sv_transmit_file;
  /*!
   * Pointer to memory allocator object.
   */
  struct allocator*         sv_allocator;
  /*!
   * List of client data structures in use.
   */
  dlinked_list_handle       sv_clients;
  /*!
   * Lock to serialize access to the client list.
   */
  os_lock_t                 sv_list_lock;
  /*!
   * Enable some statistics if compiled with __ENABLE_STATISTICS__.
   */
  SERVER_STATISTICS_STRUCT_MEMBER_ENTRY(sv_stats)
};

/*!
 * @brief Abstract representation of a client.
 */
struct client {
  /*!
   * Context data for overlapped(asynchronuous) operations.
   */
  struct aio_data       io_context;
  /*!
   * Client socket.
   */
  SOCKET                cl_sock;
  SOCKADDR_STORAGE      cl_sadata;
  /*!
   * Buffer for read/write operations. Make this a multiple of the page size.
   */
  void*                 cl_buffer;
  /*!
   * Handle to the requested resource.
   */
  HANDLE                cl_file;
  /*!
   * Buffers to use when calling TransmitFile().
   */
  TRANSMIT_FILE_BUFFERS cl_ts_buffers;
  size_t                cl_is_tracked;
};

static const size_t kClientObjectSize = sizeof(struct client);
static const size_t kClientIOBuffSize = 2048;

static int client_compare_fn(const void* c1, const void* c2, void* param) {
  const struct client* c_left = (const struct client*) c1;
  const struct client* c_right = (const struct client*) c2;
  UNUSED_PRE(param);
  if (c_left->cl_sock == c_right->cl_sock) {
    return 0;
  } else if(c_left->cl_sock < c_right->cl_sock) {
    return -1;
  } else {
    return 1;
  }
}

static __inline SOCKET create_overlapped_socket(void) {
  return WSASocket(AF_INET,
                   SOCK_STREAM,
                   IPPROTO_TCP,
                   NULL,
                   0,
                   WSA_FLAG_OVERLAPPED);
}

static void server_handle_client_request(struct server_data* sv,
                                         struct client* cl);

static void server_work_loop_ex(struct server_data* sv);

static void client_destructor(void* client_data, void* server_data, void* args);

static void server_handle_client_write_completion(struct server_data* sv,
                                                  struct client* cl);

static void server_cleanup_client_data(struct server_data* sv, 
                                       struct client* cl,
                                       kClientCloseOpt close_opt);

static BOOL server_accept_more_clients(struct server_data* sv,
                                       struct client* cl,
                                       kClientCloseOpt);

static int server_accept_loop(struct server_data* srv_ptr);

/*
 * Entry point function for worker threads. We must use _beginthreadex() 
 * because we need CRT functions like strtok_s() and others.
 */
static unsigned int __stdcall server_worker_thread(void* args);

static SOCKET create_server_socket(void) {
  struct addrinfo   hints;
  struct addrinfo*  svdata        =   NULL ;
  int result                      =   0;
  SOCKET sv_sock                  =   INVALID_SOCKET;

  RtlZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; 

  if (getaddrinfo(NULL, sv_options.so_port, &hints, &svdata)) {
    D_FUNCFAIL_WSAAPI(getaddrinfo());
    goto ERR_CLEANUP;
  }

  sv_sock = WSASocket(svdata->ai_family, 
                      svdata->ai_socktype, 
                      svdata->ai_protocol,
                      NULL,
                      0,
                      WSA_FLAG_OVERLAPPED);

  if (INVALID_SOCKET == sv_sock) {
    D_FUNCFAIL_WSAAPI(WSASocket);
    goto ERR_CLEANUP;
  }

  result = bind(sv_sock, svdata->ai_addr, (int) svdata->ai_addrlen);
  if (SOCKET_ERROR == result) {
    D_FUNCFAIL_WSAAPI(bind);
    goto ERR_CLEANUP;
  }

  result = listen(sv_sock, SOMAXCONN);
  if (SOCKET_ERROR == result) {
    D_FUNCFAIL_WSAAPI(WSASocket);
    goto ERR_CLEANUP;
  }

  freeaddrinfo(svdata);
  return sv_sock;

ERR_CLEANUP :
  if (INVALID_SOCKET != sv_sock) {
    closesocket(sv_sock);
  }
  if (svdata) {
    freeaddrinfo(svdata);
  }
  return INVALID_SOCKET;
}

static BOOL server_update_completion_port(struct server_data* sv, 
                                          HANDLE newhandle ) {
  HANDLE out_handle = CreateIoCompletionPort(newhandle,
                                             sv->sv_iocp,
                                             (ULONG_PTR) newhandle,
                                             0);
  return out_handle != NULL;
}

static BOOL server_post_message(struct server_data* sv, 
                                kIocpMessage msgcode,
                                DWORD msgdata,
                                void* msgdata_ptr) {
  return PostQueuedCompletionStatus(sv->sv_iocp, 
                                    msgdata, 
                                    (ULONG_PTR) msgcode, 
                                    (OVERLAPPED*) msgdata_ptr);
}

/*
 * Handler to respond to CTRL-C, CTRL-BREAK, LOGON/LOGOFF events.
 */
static BOOL WINAPI server_control_handler(DWORD ctrl_type) {
  switch (ctrl_type) {
  case CTRL_LOGOFF_EVENT : case CTRL_SHUTDOWN_EVENT :
    PostQueuedCompletionStatus(iocp_handle, 0, kIocpMsgShutDown, 0);
    PostQueuedCompletionStatus(iocp_accept_handle, 0, kIocpMsgShutDown, 0);
    break;

  default :
    if (IDOK == MessageBox(GetConsoleWindow(),
                           "Do you want to shutdown the server?",
                           "Confirm shutdown",
                           MB_OKCANCEL)) {
      PostQueuedCompletionStatus(iocp_handle, 0, kIocpMsgShutDown, 0);
      PostQueuedCompletionStatus(iocp_accept_handle, 0, kIocpMsgShutDown, 0);
    }
    break;
  }
  return TRUE;
}

static BOOL load_function_pointer_from_wsa(SOCKET sock,
                                           GUID guid_fnptr,
                                           void* outbuff,
                                           DWORD buff_size,
                                           WSAOVERLAPPED* aiodata) {
  DWORD bytes_out;
  int result = WSAIoctl(sock,
                        SIO_GET_EXTENSION_FUNCTION_POINTER,
                        &guid_fnptr,
                        sizeof(guid_fnptr),
                        outbuff,
                        buff_size,
                        &bytes_out,
                        aiodata,
                        NULL);
  /*
   * WSAIoctl() returns 0 on success.
   */
  if (!result) {
    return TRUE;
  }

  if (WSA_IO_PENDING != WSAGetLastError()) {
    D_FUNCFAIL_WSAAPI(WSAIoctl);
    return FALSE;
  }

  for(;;) {
    DWORD bytes_out;
    DWORD flags_out;

    Sleep(0);
    result = WSAGetOverlappedResult(sock, 
                                    aiodata, 
                                    &bytes_out, 
                                    FALSE, 
                                    &flags_out);
    if (!result) {
      if (WSA_IO_PENDING != WSAGetLastError()) {
        D_FUNCFAIL_WSAAPI(WSAGetOverlappedResult);
        return FALSE;
      }
      continue;
    }

    break;
  }
  return TRUE;
}

BOOL server_init(struct server_data* sv) {
  SYSTEM_INFO   sysinfo;
  size_t        rollback    = 0;
  int           i           = 0;
  GUID          guid_aex    = WSAID_ACCEPTEX;
  GUID          guid_geaxsd = WSAID_GETACCEPTEXSOCKADDRS;
  GUID          guid_trnsfl = WSAID_TRANSMITFILE;
  WSADATA       wdata;
  int           result;

  result = WSAStartup(MAKEWORD(2, 2), &wdata);
  if (result) {
    D_FMTSTRING("WSAStartup() failed, error %d", result);
    goto ERR_CLEANUP;
  }
  ++rollback; /* level 1 */

  RtlZeroMemory(sv, sizeof(*sv));
  sv->sv_sock = INVALID_SOCKET;

  sv->sv_allocator = allocator_handle;
  lock_init(&sv->sv_list_lock);
  STATS_INITIALIZE((&sv->sv_stats));
  ++rollback; /* level 2 */
  
  sv->sv_clients = dlist_create(client_compare_fn, allocator_handle, sv);
  if (!sv->sv_clients) {
    D_FMTSTRING("Failed to create client list!", DUMMY_VA_MACRO_ARG);
  }

  ++rollback; /* level 3 */

  GetSystemInfo(&sysinfo);
  sv->sv_worker_count = sysinfo.dwNumberOfProcessors * 
      sv_options.so_thread_to_processor_ratio;
  sv->sv_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 
                                       NULL, 
                                       0, 
                                       sv->sv_worker_count);
  if (!sv->sv_iocp) {
    D_FUNCFAIL_WINAPI(CreateIoCompletionPort);
    goto ERR_CLEANUP;
  }
  iocp_handle = sv->sv_iocp;
  ++rollback; /* level 4 */

  sv->sv_iocp_accept = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                              NULL,
                                              0,
                                              0);
  if (!sv->sv_iocp_accept) {
    goto ERR_CLEANUP;
  }
  iocp_accept_handle = sv->sv_iocp_accept;
  ++rollback; /* level 5 */

  sv->sv_sock = create_server_socket();
  if (INVALID_SOCKET == sv->sv_sock) {
    goto ERR_CLEANUP;
  }
  ++rollback; /* level 6 */

  /*
   * Function pointers for acceptex must be loaded before the socket is added
   * to the completion port if we want to avoid notification for those events.
   */
  if (!load_function_pointer_from_wsa(sv->sv_sock,
                                      guid_aex,
                                      &sv->sv_acceptx,
                                      (DWORD) sizeof(sv->sv_acceptx),
                                      (WSAOVERLAPPED*) sv)) {
    D_FMTSTRING("Failed to load function pointer for acceptex()"); 
    goto ERR_CLEANUP;
  }

  if (!load_function_pointer_from_wsa(sv->sv_sock,
                                      guid_geaxsd,
                                      &sv->sv_getacceptexsockaddrs,
                                      (DWORD) sizeof(sv->sv_getacceptexsockaddrs),
                                      (WSAOVERLAPPED*) sv)) {
    D_FMTSTRING("Failed to load function pointer for getacceptexsockaddress()");
    goto ERR_CLEANUP;
  }

  if (!load_function_pointer_from_wsa(sv->sv_sock,
                                      guid_trnsfl,
                                      &sv->sv_transmit_file,
                                      (DWORD) sizeof(sv->sv_transmit_file),
                                      (WSAOVERLAPPED*) sv)) {
    D_FMTSTRING("Failed to load function pointer for TransmitFile()");
    goto ERR_CLEANUP;
  }

  if (!server_update_completion_port(sv, (HANDLE) sv->sv_sock)) {
    D_FMTSTRING("Failed to add server socket to IOCP");
    goto ERR_CLEANUP;
  }
  
  sv->sv_worker_threads = sv->sv_allocator->al_mem_alloc(
      sv->sv_allocator,
      sizeof(HANDLE) * sv->sv_worker_count);
  if (!sv->sv_worker_threads) {
    D_FMTSTRING("Out of memory!");
    goto ERR_CLEANUP;
  }

  RtlZeroMemory(sv->sv_worker_threads, 
                sizeof(HANDLE) * sv->sv_worker_count);

  for (i = 0; i < (int) sv->sv_worker_count; ++i) {
    sv->sv_worker_threads[i] = (HANDLE) _beginthreadex(NULL,
                                                       0,
                                                       server_worker_thread,
                                                       (void*) sv,
                                                       0,
                                                       NULL);
    if (!sv->sv_worker_threads[i]) {
      D_FUNCFAIL_WINAPI(CreateThread());
    }
  }

  SetConsoleCtrlHandler(server_control_handler, TRUE);
  return TRUE;

ERR_CLEANUP :
  /*
   * Fall through is intended here.
   */
  switch (rollback) {
  case 6 :
    BUGSTOP_IF((sv->sv_sock == INVALID_SOCKET), 
               "Should not get here with invalid socket");
    closesocket(sv->sv_sock);

  case 5 :
    BUGSTOP_IF((!sv->sv_iocp_accept), "Accept iocp can't be NULL!");
    CloseHandle(sv->sv_iocp_accept);

  case 4 :
    BUGSTOP_IF((!sv->sv_iocp), "Should not get here with NULL iocp!");
    CloseHandle(sv->sv_iocp);

  case 3 :
    BUGSTOP_IF((!sv->sv_clients), "Should not get here if no client list!");
    dlist_destroy(sv->sv_clients);
    sv->sv_clients = NULL;

  case 2 :
    lock_destroy(&sv->sv_list_lock);
    STATS_UNINITIALIZE((&sv->sv_stats));

  case 1 :
    WSACleanup();

  default :
    break;
  }

  return FALSE;
}

static void server_add_client_to_list(struct server_data* sv,
                                      struct client* cl) {
  lock_acquire(&sv->sv_list_lock);
  BUGSTOP_IF((cl->cl_is_tracked), "Client is already in list!");
  dlist_push_head(sv->sv_clients, cl);
  cl->cl_is_tracked = 1;
  lock_release(&sv->sv_list_lock);
}

static void server_remove_client_from_list(struct server_data* sv,
                                           struct client* cl) {
  lock_acquire(&sv->sv_list_lock);
  BUGSTOP_IF((!cl->cl_is_tracked), "Client is not in list!");
  dlist_remove_item(sv->sv_clients, cl);
  cl->cl_is_tracked = 0;
  lock_release(&sv->sv_list_lock);
}

static struct client* server_init_clientdata(struct server_data* sv,
                                             struct client* cl,
                                             BOOL close_sock) {
  struct client* newclient = NULL;
  size_t options = 0;

  if (cl) {
    /*
     * Reinit an existing client.
     */
    options = kClientCloseRemoveFromList;
    newclient = cl;
    if (close_sock) {
      if (INVALID_SOCKET != newclient->cl_sock) {
        shutdown(newclient->cl_sock, SD_BOTH);
        closesocket(newclient->cl_sock);
        cl->cl_sock = INVALID_SOCKET;
      }
    }
    if (INVALID_HANDLE_VALUE != newclient->cl_file) {
      CloseHandle(newclient->cl_file);
      newclient->cl_file = INVALID_HANDLE_VALUE;
    }
    if (INVALID_SOCKET == newclient->cl_sock) {
      newclient->cl_sock = create_overlapped_socket();
      if (INVALID_SOCKET == newclient->cl_sock) {
        D_FUNCFAIL_WSAAPI(WSASocket);
        goto ERR_CLEANUP;
      }
    }
    RtlZeroMemory(&cl->cl_ts_buffers, sizeof(cl->cl_ts_buffers));
    newclient->io_context.aio_opcode = kIOOpcodeConnection;
  } else {
    /*
     * Create a new client.
     */
    newclient = sv->sv_allocator->al_mem_alloc(sv->sv_allocator, 
                                               sizeof(struct client));
    if (!newclient) {
      return NULL;
    }

    RtlZeroMemory(newclient, sizeof(*newclient));
    newclient->cl_file = INVALID_HANDLE_VALUE;
    newclient->cl_sock = INVALID_SOCKET;
    newclient->cl_buffer = sv->sv_allocator->al_mem_alloc(sv->sv_allocator, 
                                                          kClientIOBuffSize);
    if (!newclient->cl_buffer) {
      goto ERR_CLEANUP;
    }

    newclient->io_context.aio_opcode = kIOOpcodeConnection;
    newclient->cl_sock = create_overlapped_socket();
    if (INVALID_SOCKET == newclient->cl_sock) {
      D_FUNCFAIL_WSAAPI(WSASocket);
      goto ERR_CLEANUP;
    }

    server_add_client_to_list(sv, newclient);
  }

  return newclient;

ERR_CLEANUP :
  server_cleanup_client_data(sv, newclient, options);
  return NULL;
}

static void server_cleanup_client_data(struct server_data* sv, 
                                       struct client* cl,
                                       kClientCloseOpt close_opt) {
  UNUSED_PRE(close_opt);
  if (INVALID_SOCKET != cl->cl_sock) {
    if (close_opt & kClientCloseAbortative) {
      struct linger abort;
      abort.l_onoff = 1;
      abort.l_linger = 0;
      setsockopt(cl->cl_sock, SOL_SOCKET, SO_LINGER, 
                 (const char*) &abort, (int) sizeof(abort));
    }
    shutdown(cl->cl_sock, SD_BOTH);
    closesocket(cl->cl_sock);
  }

  if (INVALID_HANDLE_VALUE != cl->cl_file) {
    CloseHandle(cl->cl_file);
  }

  if (close_opt & kClientCloseRemoveFromList) {
    server_remove_client_from_list(sv, cl);
  }
  sv->sv_allocator->al_mem_release(sv->sv_allocator, cl->cl_buffer);
  sv->sv_allocator->al_mem_release(sv->sv_allocator, cl);
}

static kAcceptExResult server_post_acceptex_with_client(struct server_data* sv,
                                                        struct client* cl) {
  DWORD bytes_transfered;
  int err_code;
  BOOL result;

  result = sv->sv_acceptx(sv->sv_sock,
                          cl->cl_sock,
                          cl->cl_buffer,
                          0, /* do not wait for data */
                          (DWORD) (sizeof(struct sockaddr_storage) + 16),
                          (DWORD) (sizeof(struct sockaddr_storage) + 16),
                          &bytes_transfered,
                          (OVERLAPPED*) cl);
  if (result) {
    return kAcceptExResultSuccess;
  }

  err_code = WSAGetLastError();
  switch (err_code) {
  case ERROR_IO_PENDING :
    return kAcceptExResultSuccess;
    break;

  case WSAECONNRESET :
    return kAcceptExResultConnReset;
    break;

  default :
    D_FUNCFAIL_WSAAPI(acceptex);
    break;
  }
  return kAcceptExResultFailed;
}

/*!
 * @brief Client disconnected. Re-init the client and post another AcceptEx().
 */
static void server_handle_peer_disconnect(struct server_data* sv,
                                          struct client* cli) {
  if (!server_accept_more_clients(sv, cli, 0)) {
    server_cleanup_client_data(sv, cli, 0);
  }
}

static BOOL server_read_from_client(struct client* cl) {
  WSABUF rd_data;
  DWORD rd_flags = 0;
  int op_result;

  /*
   * With each OVERLAPPED I/O on the socket, the buffers are locked in memory.
   * Since the system imposes a limit on the maximum amount of memory that 
   * can be locked, after a while calls to WSASocket() and socket() will fail
   * with a WSAENOBUFFS error code. Posting an overlapped WSARecv() with 0 bytes
   * avoids this situation.
   */ 
  rd_data.buf = cl->cl_buffer;
  rd_data.len = 0; 
  cl->io_context.aio_opcode = kIOOpcodeSock0BytesRecv;
  op_result = WSARecv(cl->cl_sock,
                      &rd_data,
                      1,
                      NULL, /* get transferred bytes from completion port */
                      &rd_flags,
                      (WSAOVERLAPPED*) cl,
                      NULL /* no completion routine */);
  if (SOCKET_ERROR == op_result && WSA_IO_PENDING != WSAGetLastError()) {
    D_FUNCFAIL_WSAAPI(WSARecv());
    return FALSE;
  }
  return TRUE;
}

static void server_accept_new_connection(struct server_data* sv,
                                         struct client* cl) {
  int result = setsockopt(cl->cl_sock,
                          SOL_SOCKET,
                          SO_UPDATE_ACCEPT_CONTEXT,
                          (const char*) &sv->sv_sock,
                          sizeof(sv->sv_sock));

  if (!result && 
      server_update_completion_port(sv, (HANDLE) cl->cl_sock) &&
      server_read_from_client(cl)) {
    /*
     * Client successfully added to completion port. Decrement the number
     * of outstanding AcceptEx() calls.
     */
    STATS_INCREMENT_CONNECTION_COUNT(&sv->sv_stats);
    atomic32_increment(&sv->sv_outstanding_accepts, -1);
    cl = NULL;
  }
#if defined(_DEBUG)
  else {
    if (result) {
      D_FUNCFAIL_WSAAPI(setsockopt);
    }
  }
#endif
      
  /*
   * Try to post another AcceptEx() call.
   */
  if (!server_accept_more_clients(sv, cl, 0) && cl) {
    server_cleanup_client_data(sv, cl, kClientCloseRemoveFromList);
  }
}

static void server_cleanup_and_shutdown(struct server_data* sv) {
  int i = 0;
  /*server_post_message(sv, kIocpMsgShutDown, 0, 0);*/
  for (i = 0; i < (int) sv->sv_worker_count; ++i) {
    if (sv->sv_worker_threads[i]) {
      WaitForSingleObject(sv->sv_worker_threads[i], INFINITE);
      CloseHandle(sv->sv_worker_threads[i]);
    }
  }

  BUGSTOP_IF((!sv->sv_worker_threads), "Array of worker threads is NULL!");
  sv->sv_allocator->al_mem_release(sv->sv_allocator, sv->sv_worker_threads);

  BUGSTOP_IF((INVALID_SOCKET == sv->sv_sock), "Server socket is invalid!");
  closesocket(sv->sv_sock);

  BUGSTOP_IF((!sv->sv_clients), "List of clients is invalid!");
  dlist_for_each(sv->sv_clients, client_destructor, NULL);
  dlist_destroy(sv->sv_clients);

  BUGSTOP_IF((!sv->sv_iocp), "Completion port is invalid!");
  CloseHandle(sv->sv_iocp);

  BUGSTOP_IF((!sv->sv_iocp_accept), "Completion port is invalid!");
  CloseHandle(sv->sv_iocp_accept);

  lock_destroy(&sv->sv_list_lock);
  STATS_DUMP((&sv->sv_stats));
  STATS_UNINITIALIZE((&sv->sv_stats));
  sv->sv_allocator->al_dump_statistics(sv->sv_allocator, 
                                       GetStdHandle(STD_OUTPUT_HANDLE));
  SetConsoleCtrlHandler(server_control_handler, FALSE);
  WSACleanup();
}

static int server_main_loop(struct server_data* sv) {
  /*
   * Post a conpletion packet to start accepting.
   */
  server_accept_more_clients(sv, NULL, 0);
  server_accept_loop(sv);
  return 0;
}

static unsigned int __stdcall  server_worker_thread(void* args) {
  server_work_loop_ex((struct server_data*) args);
  return 0;
}

int server_start_and_run(int argc, char** argv) {
  struct server_data my_server;
  int result;
  int i = 0;

  set_unhandled_exceptions_filter();
  sv_options.so_max_clients = kSVDefaultUnlimitedConnections;
  sv_options.so_port = _strdup(kSVDefaultPort);
  sv_options.so_thread_to_processor_ratio = kSVDefaultThreadToProcessorRatio;
  sv_options.so_www_rootdir = _strdup(kSVDefaultWWWRootDir);
  sv_options.so_max_outstanding_accepts = 64;

  while (++i < argc) {
    if (*argv[i] != '-') { 
      fprintf_s(stderr, "\nUnrecognized option: %s", argv[argc]);
      continue;
    }

    ++argv[i];
    if (!strcmp(argv[i], "thread-to-processor-ratio")) {
      int tp_ratio = atoi(argv[++i]);
      if (tp_ratio) {
        sv_options.so_thread_to_processor_ratio = tp_ratio;
      }
    } else if(!strcmp(argv[i], "port")) {
      int port = atoi(argv[++i]);
      if (port > 0 && port < 65535) {
        free(sv_options.so_port);
        sv_options.so_port = _strdup(argv[i]);
      }
    } else if(!strcmp(argv[i], "www-root-dir")) {
      free(sv_options.so_www_rootdir);
      sv_options.so_www_rootdir = _strdup(argv[++i]);
    } else if(!strcmp(argv[i], "connection-limit")) {
      int conn_limit = atoi(argv[++i]);
      if (conn_limit > 0) {
        sv_options.so_max_clients = conn_limit;
      }
    } else if(!strcmp(argv[i], "max-outstanding-acepts")) {
      sv_options.so_max_outstanding_accepts = atoi(argv[++i]); 
    } else {
      fprintf_s(stderr, "\nUnrecognized options %s", argv[i]);
    }
  } 

  setup_http_response_array();
  if (server_init(&my_server)) {
    result = server_main_loop(&my_server);
    server_cleanup_and_shutdown(&my_server);
    free(sv_options.so_port);
    free(sv_options.so_www_rootdir);
    return result;
  }
  return EXIT_FAILURE;
}

/*!
 * @brief Transmits the requested resource to the client.
 * @param res_path Path to the resource requested by the client.
 * @return TRUE on succes, FALSE on fail.
 */
static BOOL server_transmit_resource(struct server_data* sv,
                                     struct client* cl,
                                     const char* res_path) {
  BOOL result = FALSE;
  BUGSTOP_IF((INVALID_HANDLE_VALUE != cl->cl_file),
             "Client already has a file opened!");

  cl->io_context.aio_opcode = kIOOpcodeSockWrite;
  if (!res_path) {
    /*
     * 400 Bad request.
     */
    cl->cl_ts_buffers.Head = (void*)
      kHTTPResponseArray[kHTTPCode400BadRequest].http_string_spec;
    cl->cl_ts_buffers.HeadLength = (DWORD)
      kHTTPResponseArray[kHTTPCode400BadRequest].string_spec_len;
  } else {
    /*
     * The Windows SDK says that opening the file with 
     * FILE_FLAG_SEQUENTIAL_SCAN improves caching performance.
     */
    char file_path[MAX_PATH + 1];
    string_v_printf(file_path, _countof(file_path), _T("%s/%s"),
                    sv_options.so_www_rootdir, 
                    (*res_path == '/' ? res_path + 1 : res_path));
    cl->cl_file = CreateFile(file_path,
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                             NULL);
    if (INVALID_HANDLE_VALUE == cl->cl_file) {
      DWORD err_code = GetLastError();
      switch (err_code) {
      case ERROR_FILE_NOT_FOUND :
        /*
         * Send 404 Not found.
         */
        cl->cl_ts_buffers.Head = (void*) 
          kHTTPResponseArray[kHTTPCode404NotFound].http_string_spec;
        cl->cl_ts_buffers.HeadLength = (DWORD)
          kHTTPResponseArray[kHTTPCode404NotFound].string_spec_len;
        break;

      default :
        /*
         * Send 500 Internal Error for the rest.
         */
        cl->cl_ts_buffers.Head = (void*)
          kHTTPResponseArray[kHTTPCode500InternalError].http_string_spec;
        cl->cl_ts_buffers.HeadLength = (DWORD)
          kHTTPResponseArray[kHTTPCode500InternalError].string_spec_len;
        break;
      }
    } else {
      /*
       * Send 200 OK.
       */
      LARGE_INTEGER f_size;
      if (GetFileSizeEx(cl->cl_file, &f_size)) {
        DWORD header_length = string_v_printf(
            cl->cl_buffer,
            kClientIOBuffSize,
            kHTTPResponseArray[kHTTPCode200Ok].http_string_spec,
            f_size.QuadPart,
            kHTTPContentTypeTextHtml);
        cl->cl_ts_buffers.Head = cl->cl_buffer;
        cl->cl_ts_buffers.HeadLength = header_length;
      } else {
        /*
         * Failed to query file size. Send 500 Internal error.
         */
        cl->cl_ts_buffers.Head = (void*)
          kHTTPResponseArray[kHTTPCode500InternalError].http_string_spec;
        cl->cl_ts_buffers.HeadLength = (DWORD)
          kHTTPResponseArray[kHTTPCode500InternalError].string_spec_len;
        /*
         * Close the file handle.
         */
        CloseHandle(cl->cl_file);
        cl->cl_file = INVALID_HANDLE_VALUE;
      }
    }
  }

  cl->io_context.aio_ctx.Offset = 0;
  cl->io_context.aio_ctx.OffsetHigh = 0;
  result = sv->sv_transmit_file(
      cl->cl_sock,
      cl->cl_file == INVALID_HANDLE_VALUE ? NULL : cl->cl_file,
      0, /* transmit entire file */
      0, /* use default size for blocks when sending */
      (OVERLAPPED*) cl,
      &cl->cl_ts_buffers,
      TF_DISCONNECT | TF_REUSE_SOCKET);

  if (!result && ERROR_IO_PENDING != WSAGetLastError()) {
    D_FUNCFAIL_WSAAPI("TransmitFile");
    return FALSE;
  }
  return TRUE;
}

static void server_handle_client_request(struct server_data* sv,
                                         struct client* cl) {
  char* buff_request = (char*) cl->cl_buffer;
  char* method = NULL;
  char* document = NULL;
  char* ver = NULL;
  char* next_token = NULL;
  WSABUF rd_buff;
  DWORD bytes_transferred = 0;
  DWORD call_flags = 0;
  int result;

  BUGSTOP_IF((cl->io_context.aio_opcode != kIOOpcodeSock0BytesRecv),
             "Invalid opcode!");
  rd_buff.buf = cl->cl_buffer;
  rd_buff.len = kClientIOBuffSize;

  for (;;) {
    /*
     * Just do a blocking receive.
     */
    result = WSARecv(cl->cl_sock,
                     &rd_buff,
                     1,
                     &bytes_transferred,
                     &call_flags,
                     NULL,
                     NULL);

    if((result && WSAEWOULDBLOCK != WSAGetLastError()) ||
       !bytes_transferred) {
      /*
       * Either an error has occured or the client closed the connection.
       */
      goto FINISH;
    }
    break;
  }

  if ((method = strtok_s(buff_request, " ", &next_token)) == NULL  ||
      (document = strtok_s(NULL, " ", &next_token)) == NULL        ||
      (ver = strtok_s(NULL, " \r\n", &next_token)) == NULL         ||
      _stricmp(method, "get") || _stricmp(ver, "HTTP/1.1")) {
    document = NULL;
  } 

  if (server_transmit_resource(sv, cl, document)) {
    cl = NULL;
  }

FINISH : 
  /*
   * Time to post another AcceptEx().
   */
  if (!server_accept_more_clients(sv, cl, 0) && cl) {
    server_cleanup_client_data(sv, cl, kClientCloseRemoveFromList);
  }
}

static kServerAction server_process_completions(struct server_data* sv,
                                                OVERLAPPED_ENTRY* notifications,
                                                ULONG notification_count) {
  ULONG index = 0;
  for (index = 0; index < notification_count; ++index) {
    struct client* ptr_client;
    if (notifications[index].lpCompletionKey == kIocpMsgShutDown) {
      return kServerStop;
    }

    ptr_client = (struct client*) notifications[index].lpOverlapped;
    BUGSTOP_IF((!ptr_client), "Invalid client handle!");

    if (!notifications[index].dwNumberOfBytesTransferred && 
        (ptr_client->io_context.aio_opcode != kIOOpcodeConnection) &&
        (ptr_client->io_context.aio_opcode != kIOOpcodeSock0BytesRecv)) {
      /*
       * 0 bytes received is allowable on connection and the initial WSARecv()
       * call. In any other cases it means that the client closed the
       * connection.
       */
      if (!server_accept_more_clients(sv, ptr_client, kClientCloseAbortative) &&
          ptr_client) {
        server_cleanup_client_data(sv, ptr_client, kClientCloseRemoveFromList);
      }
      continue;
    }

    switch (ptr_client->io_context.aio_opcode) {
    case kIOOpcodeConnection :
      /*
       * Handle incoming connection.
       */
      server_accept_new_connection(sv, ptr_client);
      break;

    case kIOOpcodeSock0BytesRecv :
      /*
       * Read of client request is complete. Process the request.
       */
      D_FMTSTRING("Read completed, bytes %u", 
                  notifications[index].dwNumberOfBytesTransferred);
      server_handle_client_request(sv, ptr_client);
      break;

    case kIOOpcodeSockWrite :
      /*
       * Write completed.
       * We only need to close the handle of the file that the client 
       * requested (in case we sent 200 OK to it) because TransmitFile()
       * takes care of disconnecting after sending all the data.
       */
      D_FMTSTRING("Write completed, bytes %u", 
                  notifications[index].dwNumberOfBytesTransferred);
      STATS_UPDATE_TRANSFER_COUNT(
          &sv->sv_stats,
          notifications[index].dwNumberOfBytesTransferred);
      server_handle_client_write_completion(sv, ptr_client);
      break;

    default :
      /*
       * We should not get here. It means that you probably defined a new opcode
       * but haven't added a handler for it in the switch above.
       */
      D_FMTSTRING("Unknown opcode %d", ptr_client->io_context.aio_opcode);
      D_STACKTRACE();
      D_DBGBREAK();
      break;
    }
  }

  return kServerContinue;
}

static void server_work_loop_ex(struct server_data* sv) {
  for (;;) {
    OVERLAPPED_ENTRY  completed_operations[kMaxCompletionCount];
    ULONG             completed_operation_count = 0;
    BOOL              result;

    result = GetQueuedCompletionStatusEx(sv->sv_iocp,
                                         completed_operations,
                                         _countof(completed_operations),
                                         &completed_operation_count,
                                         INFINITE, /* wait forever */
                                         FALSE /* no alertable wait */);
    if (!result) {
      D_FUNCFAIL_WINAPI(GetQueuedCompletionStatusEx);
      break;
    }

    if (kServerStop == server_process_completions(sv,
                                                  completed_operations,
                                                  completed_operation_count)) {
      server_post_message(sv, kIocpMsgShutDown, 0, 0);
      break;
    }
  }
}

static void client_destructor(void* client_data, void* server_data, void* args) {
  UNUSED_PRE(args);
  server_cleanup_client_data(server_data, 
                             client_data,
                             kClientCloseAbortative); 
}

static void server_handle_client_write_completion(struct server_data* sv,
                                                  struct client* cl) {
  if (!server_accept_more_clients(sv, cl, 0)) {
    D_FUNCFAIL_WINAPI(PostQueuedCompletionStatus);
    server_cleanup_client_data(sv, cl, kClientCloseRemoveFromList);
  }
}

static int server_accept_loop(struct server_data* srv_ptr) {
  for (;;) {
    DWORD client_opts = 0;
    OVERLAPPED* io_ctx = NULL;
    ULONG_PTR   io_opcode;
    struct client* cli = NULL;
    BOOL result = GetQueuedCompletionStatus(srv_ptr->sv_iocp_accept,
                                            &client_opts,
                                            &io_opcode,
                                            (OVERLAPPED**) &io_ctx,
                                            INFINITE);
    if (!result) {
      D_FUNCFAIL_WINAPI(GetQueuedCompletionStatus);
      continue;
    }

    if (io_opcode == kIocpMsgShutDown) {
      break;
    }

    for (cli = (struct client*) io_ctx; 
         atomic32_load(&srv_ptr->sv_outstanding_accepts) < 
         sv_options.so_max_outstanding_accepts;
         cli = NULL) {

      if (!cli) {
        /*
         * Allocate new client.
         */
        cli = server_init_clientdata(srv_ptr, NULL, FALSE);
      } else {
        /*
         * Reuse existing client structure.
         */
        cli = server_init_clientdata(srv_ptr, cli, client_opts);
      }

      if (!cli) {
        /*
         * We're running low on resources. Bail out.
         */
        D_FMTSTRING("Client allocation/reinit failed!");
        break;
      }

      for(;;) {
        kAcceptExResult ret_code = server_post_acceptex_with_client(srv_ptr, 
                                                                    cli);
        switch (ret_code) {
          case kAcceptExResultSuccess :
            atomic32_increment(&srv_ptr->sv_outstanding_accepts, 1);
            break;

          case kAcceptExResultConnReset :
            /*
             * Client closed connection. Try to reuse the data and post another
             * AcceptEx().
             */
            cli = server_init_clientdata(srv_ptr, cli, TRUE);
            if (cli) {
              continue;
            } else {
              break;
            }
            break;

          case kAcceptExResultFailed :
            server_cleanup_client_data(srv_ptr, cli, kClientCloseRemoveFromList);
            break;

          default :
            break;
        }
        break;
      }
    }
  }

  return 0;
}

static BOOL server_accept_more_clients(struct server_data* sv_ptr,
                                       struct client* cl,
                                       kClientCloseOpt options) {
  return PostQueuedCompletionStatus(sv_ptr->sv_iocp_accept,
                                    options,
                                    kIocpMsgPostAccept,
                                    (OVERLAPPED*) cl);
}
