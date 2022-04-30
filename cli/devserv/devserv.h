/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#ifndef _SUSCAN_CLI_DEVSERV_DEVSERV_H
#define _SUSCAN_CLI_DEVSERV_DEVSERV_H

#include <util/compat-unistd.h>
#include <analyzer/impl/remote.h>
#include <util/rbtree.h>
#include <util/hashlist.h>
#include <util/compat-inet.h>

#define SUSCLI_ANSERV_LISTEN_FD 0
#define SUSCLI_ANSERV_CANCEL_FD 1
#define SUSCLI_ANSERV_FD_OFFSET 2

enum suscan_analyzer_inspector_msgkind;

struct suscli_user_entry {
  char    *user;
  char    *password;
  uint64_t permissions;
};

struct suscli_user_entry *suscli_user_entry_new(
  const char *user,
  const char *password,
  uint64_t permissions);

void suscli_user_entry_destroy(struct suscli_user_entry *self);

SUBOOL suscli_devserv_load_users(void);

struct suscli_analyzer_client_inspector_entry {
  SUHANDLE global_handle;
  int32_t  itl_index;
};

struct suscli_analyzer_client_inspector_list {
  pthread_mutex_t inspector_mutex;
  SUBOOL          inspector_mutex_initialized;

  rbtree_t       *inspector_tree;
  unsigned int    inspector_count;
  unsigned int    inspector_pending_count;
};

#define SUSCLI_ANALYZER_CLIENT_TX_MESSAGE 0
#define SUSCLI_ANALYZER_CLIENT_TX_CANCEL  1

#define SUSCLI_ANALYZER_CLIENT_TX_CLEANUP_WATERMARK 50

struct suscli_analyzer_client_tx_thread {
  unsigned int      compress_threshold;
  struct suscan_mq  pool;
  SUBOOL            pool_initialized;
  struct suscan_mq  queue;
  SUBOOL            queue_initialized;
  int               fd;
  int               cancel_pipefd[2];
  pthread_t         thread;
  SUBOOL            thread_cancelled;
  SUBOOL            thread_finished;
  SUBOOL            thread_running;
};

void suscli_analyzer_client_tx_thread_stop(
  struct suscli_analyzer_client_tx_thread *self);

void suscli_analyzer_client_tx_thread_stop_soft(
  struct suscli_analyzer_client_tx_thread *self);

void suscli_analyzer_client_tx_thread_finalize(
    struct suscli_analyzer_client_tx_thread *self);

SUBOOL suscli_analyzer_client_tx_thread_push(
    struct suscli_analyzer_client_tx_thread *self,
    const grow_buf_t *pdu);

SUBOOL suscli_analyzer_client_tx_thread_push_zerocopy(
    struct suscli_analyzer_client_tx_thread *self,
    grow_buf_t *pdu);

SUBOOL suscli_analyzer_client_tx_thread_initialize(
    struct suscli_analyzer_client_tx_thread *self,
    int fd,
    unsigned int compress_threshold);

/* 
 * This strucure relates global request IDs with per-client
 * request IDs and the corresponding client pointer
 *
 * The lifecycle is as follows:
 *   - A client performs a request with a request ID
 *   - A global request ID is allocated
 *   - A request entry is registered, along with the
 *     the global request ID, the client request ID
 *     and a client pointer.
 *      * This requests BELONG to the client
 *      * They are accessed through an rbtree with a monotonic index
 *      * TODO: How do we handle stale requests?
 *   - The request ID is changed to the global ID
 *   - The request is performed
 *   - THe request is answered
 *   - The request ID (global) is translated
 *   - The client is determined and the entry is removed
 * 
 * When a client disconnects, all requests are removed. The
 * access to this list is protected by the client list mutex.
 */

struct suscli_analyzer_request_entry {
  uint32_t client_req_id;
  uint32_t global_req_id;
  int      entry_index;
  struct suscli_analyzer_client *client;
};

struct suscli_analyzer_request_entry *
suscli_analyzer_client_allocate_request_unsafe(
  struct suscli_analyzer_client *self,
  uint32_t client_req_id,
  uint32_t global_req_id);

SUBOOL suscli_analyzer_client_dispose_request_unsafe(
  struct suscli_analyzer_client *self,
  struct suscli_analyzer_request_entry *entry);

SUBOOL suscli_analyzer_client_walk_requests_unsafe(
  const struct suscli_analyzer_client *self,
  SUBOOL (*func) (
    struct suscli_analyzer_request_entry *,
    void *userdata),
  void *userdata);

void suscli_analyzer_client_dispose_all_requests(
  struct suscli_analyzer_client *self);


struct suscli_analyzer_client {
  int sfd;
  SUBOOL auth;
  SUBOOL has_source_info;
  SUBOOL accepts_multicast;
  SUBOOL failed;
  SUBOOL closed;
  unsigned int epoch;
  unsigned int compress_threshold;
  struct timeval conntime;
  struct in_addr remote_addr;
  
  const struct suscli_user_entry *user_entry;
  
  struct suscan_analyzer_params analyzer_params;
  struct suscan_remote_partial_pdu_state pdu_state;

  char *name;

  struct suscli_analyzer_client_tx_thread tx;
  struct suscan_analyzer_server_hello server_hello;  /* Read-only */
  struct suscan_analyzer_remote_call  incoming_call; /* RX thread only */

  /* List of opened inspectors. */
  struct suscli_analyzer_client_inspector_list inspectors;

  /* List of created requests */
  rbtree_t *req_table; /* Indexed by entry_index */
  int last_entry_index;

  struct suscli_analyzer_client *next;
  struct suscli_analyzer_client *prev;
};

typedef struct suscli_analyzer_client suscli_analyzer_client_t;

struct suscli_analyzer_client_interceptors {
  void *userdata;

  SUBOOL (*inspector_open) (
      void *userdata,
      suscli_analyzer_client_t *client,
      struct suscan_analyzer_inspector_msg *inspmsg);

  SUBOOL (*inspector_set_id) (
      void *userdata,
      suscli_analyzer_client_t *client,
      struct suscan_analyzer_inspector_msg *inspmsg,
      int32_t itl_index);

  SUBOOL (*inspector_wrong_handle) (
      void *userdata,
      suscli_analyzer_client_t *client,
      enum suscan_analyzer_inspector_msgkind kind,
      SUHANDLE handle,
      uint32_t req_id);
};

SUINLINE SUBOOL
suscli_analyzer_client_test_permission(
  const suscli_analyzer_client_t *self,
  uint64_t perm)
{
  if (self->user_entry == NULL)
    return SU_FALSE;

  return (self->user_entry->permissions & perm) == perm;
}

SUINLINE void
suscli_analyzer_client_inc_inspector_open_request(
    suscli_analyzer_client_t *self)
{
  ++self->inspectors.inspector_pending_count;
}

SUINLINE SUBOOL
suscli_analyzer_client_dec_inspector_open_request(
    suscli_analyzer_client_t *self)
{
  if (self->inspectors.inspector_pending_count == 0)
    return SU_FALSE;

  --self->inspectors.inspector_pending_count;

  return SU_TRUE;
}


SUINLINE SUBOOL
suscli_analyzer_client_has_outstanding_inspectors(
    const suscli_analyzer_client_t *self)
{
  return self->inspectors.inspector_count > 0;
}

SUINLINE SUBOOL
suscli_analyzer_client_is_failed(const suscli_analyzer_client_t *self)
{
  return self->failed;
}

SUINLINE SUBOOL
suscli_analyzer_client_is_closed(const suscli_analyzer_client_t *self)
{
  return self->closed;
}

SUINLINE SUBOOL
suscli_analyzer_client_accepts_multicast(const suscli_analyzer_client_t *self)
{
  return self->accepts_multicast;
}

SUINLINE SUBOOL
suscli_analyzer_client_can_write(const suscli_analyzer_client_t *self)
{
  return !self->closed && !self->failed;
}

SUINLINE SUBOOL
suscli_analyzer_client_is_auth(const suscli_analyzer_client_t *self)
{
  return self->auth;
}

SUINLINE SUBOOL
suscli_analyzer_client_has_source_info(const suscli_analyzer_client_t *self)
{
  return self->has_source_info;
}

SUINLINE const char *
suscli_analyzer_client_get_name(const suscli_analyzer_client_t *self)
{
  return self->name;
}

SUINLINE const char *
suscli_analyzer_client_string_addr(const suscli_analyzer_client_t *self)
{
  return inet_ntoa(self->remote_addr);
}

SUINLINE void
suscli_analyzer_client_set_auth(suscli_analyzer_client_t *self, SUBOOL auth)
{
  self->auth = auth;
}

SUINLINE void
suscli_analyzer_client_set_has_source_info(
    suscli_analyzer_client_t *self,
    SUBOOL has_info)
{
  self->has_source_info = has_info;
}

SUINLINE void
suscli_analyzer_client_mark_failed(suscli_analyzer_client_t *self)
{
  self->failed = SU_TRUE;
}

suscli_analyzer_client_t *suscli_analyzer_client_new(
  int sfd,
  unsigned int compress_threshold);

SUINLINE void
suscli_analyzer_client_set_analyzer_params(
  suscli_analyzer_client_t *self,
  const struct suscan_analyzer_params *params)
{
  self->analyzer_params = *params;
}

SUBOOL suscli_analyzer_client_read(suscli_analyzer_client_t *self);

void suscli_analyzer_client_enable_flags(
  suscli_analyzer_client_t *self,
  uint32_t flags);

struct suscan_analyzer_remote_call *suscli_analyzer_client_take_call(
    suscli_analyzer_client_t *);

SUHANDLE suscli_analyzer_client_register_inspector_handle_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE global_handle,
    int32_t itl_index);


SUHANDLE suscli_analyzer_client_register_inspector_handle(
    suscli_analyzer_client_t *self,
    SUHANDLE global_handle,
    int32_t itl_index);

struct suscli_analyzer_client_inspector_entry *
suscli_analyzer_client_get_inspector_entry_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE private_handle);
    
SUBOOL suscli_analyzer_client_dispose_inspector_handle_unsafe(
    suscli_analyzer_client_t *self,
    SUHANDLE local_handle);

SUBOOL
suscli_analyzer_client_dispose_inspector_handle(
    suscli_analyzer_client_t *self,
    SUHANDLE local_handle);

SUBOOL suscli_analyzer_client_intercept_message(
    suscli_analyzer_client_t *self,
    uint32_t type,
    void *message,
    const struct suscli_analyzer_client_interceptors *interceptors);

SUBOOL suscli_analyzer_client_shutdown(suscli_analyzer_client_t *self);
SUBOOL suscli_analyzer_client_send_hello(suscli_analyzer_client_t *self);
SUBOOL suscli_analyzer_client_deliver_call(
    suscli_analyzer_client_t *self,
    const struct suscan_analyzer_remote_call *call);

SUBOOL suscli_analyzer_client_write_buffer(
    suscli_analyzer_client_t *self,
    const grow_buf_t *buffer);

SUBOOL suscli_analyzer_client_write_buffer_zerocopy(
    suscli_analyzer_client_t *self,
    grow_buf_t *buffer);

SUBOOL suscli_analyzer_client_send_source_info(
    suscli_analyzer_client_t *self,
    const struct suscan_analyzer_source_info *info,
    const struct timeval *timestamp);

SUBOOL suscli_analyzer_client_send_startup_error(
  suscli_analyzer_client_t *self);

SUBOOL suscli_analyzer_client_send_auth_rejected(
    suscli_analyzer_client_t *self);
void suscli_analyzer_client_destroy(suscli_analyzer_client_t *self);

struct pollfd;

/*
 * The inspector translation table works as follows:
 *
 * The underlying analyzer keeps its own private SUHANDLE list and their
 * inspector_ids. In the multiple client scenario, we need to translate these
 * global inspector_ids into a client reference and a local inspector id. We
 * do this by keeping a translation table which interprets the inspector_id
 * as an index. In order to prevent this list from growing up uncontrollably,
 * we will keep a freelist of table entries and
 *
 */
struct suscli_analyzer_itl_entry {
  union {
    int32_t  next_free; /* If this entry is free: index of the next entry */
    uint32_t local_inspector_id;
  };

  SUHANDLE private_handle; /* Needed to close private handle */
  suscli_analyzer_client_t *client; /* Must be null if free */
};

struct suscli_multicast_manager;

struct suscli_analyzer_client_list {
  pthread_mutex_t client_mutex;
  SUBOOL          client_mutex_initialized;
  SUBOOL          cleanup_requested;

  struct suscli_multicast_manager *mc_manager;

  /* Actual list */
  suscli_analyzer_client_t *client_head;
  rbtree_t                 *client_tree;
  unsigned int              epoch;

  /* Data descriptors */
  int cancel_fd;
  int listen_fd;

  /* Polling data */
  struct pollfd  *client_pfds;
  unsigned int    client_pfds_alloc;
  unsigned int    client_count;

  /* Inspector translation table */
  rbtree_t       *itl_tree;

  /* Global request table */
  rbtree_t       *req_tree;
};

uint32_t suscli_analyzer_client_list_alloc_global_id_unsafe(
  struct suscli_analyzer_client_list *self);

struct suscli_analyzer_request_entry *
suscli_analyzer_client_list_translate_request_unsafe(
  const struct suscli_analyzer_client_list *self,
  uint32_t global_id);

SUBOOL
suscli_analyzer_client_list_register_request_unsafe(
  struct suscli_analyzer_client_list *self,
  struct suscli_analyzer_request_entry *entry);

SUBOOL
suscli_analyzer_client_list_unregister_request_unsafe(
  struct suscli_analyzer_client_list *self,
  struct suscli_analyzer_request_entry *entry);

SUINLINE SUBOOL
suscli_analyzer_client_list_supports_multicast(
  const struct suscli_analyzer_client_list *self)
{
  return self->mc_manager != NULL;
}

SUINLINE void
suscli_analyzer_client_list_increment_epoch(
    struct suscli_analyzer_client_list *self)
{
  ++self->epoch;
}

SUBOOL suscli_analyzer_client_list_init(
    struct suscli_analyzer_client_list *,
    int listen_fd,
    int cancel_fd,
    const char *ifname);

SUBOOL suscli_analyzer_client_list_append_client(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

SUBOOL suscli_analyzer_client_list_broadcast_unsafe(
    struct suscli_analyzer_client_list *self,
    const struct suscan_analyzer_remote_call *call,
    SUBOOL (*on_client_error) (
        suscli_analyzer_client_t *client,
        void *userdata,
        int error),
    void *userdata);

suscli_analyzer_client_t *suscli_analyzer_client_list_lookup_unsafe(
    const struct suscli_analyzer_client_list *self,
    int fd);

SUBOOL suscli_analyzer_client_list_force_shutdown(
    struct suscli_analyzer_client_list *self);

SUBOOL suscli_analyzer_client_list_remove_unsafe(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

SUBOOL suscli_analyzer_client_list_attempt_cleanup(
    struct suscli_analyzer_client_list *self);

SUBOOL suscli_analyzer_client_for_each_inspector_unsafe(
    const suscli_analyzer_client_t *self,
    SUBOOL (*func) (
        const suscli_analyzer_client_t *client,
        void *userdata,
        SUHANDLE private_handle,
        SUHANDLE global_handle),
    void *userdata);

SUBOOL suscli_analyzer_client_for_each_inspector(
    suscli_analyzer_client_t *self,
    SUBOOL (*func) (
        const suscli_analyzer_client_t *client,
        void *userdata,
        SUHANDLE private_handle,
        SUHANDLE global_handle),
    void *userdata);

int32_t suscli_analyzer_client_list_alloc_itl_entry_unsafe(
    struct suscli_analyzer_client_list *self,
    suscli_analyzer_client_t *client);

struct suscli_analyzer_itl_entry *
suscli_analyzer_client_list_get_itl_entry_unsafe(
    const struct suscli_analyzer_client_list *self,
    int32_t entry);

SUBOOL suscli_analyzer_client_list_set_inspector_id_unsafe(
  const struct suscli_analyzer_client_list *self,
  int32_t handle,
  uint32_t inspector_id);

SUBOOL suscli_analyzer_client_list_dispose_itl_entry_unsafe(
    struct suscli_analyzer_client_list *self,
    int32_t entry);

SUINLINE unsigned int
suscli_analyzer_client_list_get_count(
    const struct suscli_analyzer_client_list *self)
{
  return self->client_count;
}

void suscli_analyzer_client_list_finalize(struct suscli_analyzer_client_list *);

struct suscli_analyzer_server_params {
  suscan_source_config_t *profile;
  uint16_t    port;
  const char *ifname;
  size_t      compress_threshold;
};

#define SUSCLI_ANALYZER_DEFAULT_COMPRESS_THRESHOLD 1400

#define suscli_analyzer_server_params_INITIALIZER \
{                                                 \
  NULL,        /* profile */                      \
  28001,       /* port */                         \
  NULL,        /* ifname */                       \
  SUSCLI_ANALYZER_DEFAULT_COMPRESS_THRESHOLD      \
}

struct suscli_analyzer_server {
  struct suscli_analyzer_server_params params;
  struct suscli_analyzer_client_list client_list;
  struct suscan_analyzer_params analyzer_params;

  uint16_t listen_port;

  hashlist_t *user_hash;
  PTR_LIST(struct suscli_user_entry, user);

  suscan_analyzer_t *analyzer;
  suscan_source_config_t *config;
  struct suscan_mq mq;
  SUBOOL mq_init;

  pthread_t rx_thread; /* Poll on client_pfds */
  pthread_t tx_thread; /* Wait on suscan_mq_read */
  int cancel_pipefd[2];
  grow_buf_t broadcast_pdu;
  struct suscan_analyzer_remote_call broadcast_call;

  SUBOOL rx_thread_running;
  SUBOOL tx_thread_running;
  SUBOOL tx_halted;
};

typedef struct suscli_analyzer_server suscli_analyzer_server_t;

SUINLINE suscan_source_config_t *
suscli_analyzer_server_get_profile(const suscli_analyzer_server_t *self)
{
  return self->config;
}

SUINLINE uint16_t
suscli_analyzer_server_get_port(const suscli_analyzer_server_t *self)
{
  return self->listen_port;
}

suscli_analyzer_server_t *
suscli_analyzer_server_new(
    suscan_source_config_t *profile,
    uint16_t port);

suscli_analyzer_server_t *
suscli_analyzer_server_new_with_params(
    const struct suscli_analyzer_server_params *server);


SUINLINE SUBOOL
suscli_analyzer_server_is_running(suscli_analyzer_server_t *self)
{
  return self->rx_thread_running;
}

const struct suscli_user_entry *
suscli_analyzer_server_find_user(
  const suscli_analyzer_server_t *self,
  const char *user);

SUBOOL suscli_analyzer_server_add_user(
  suscli_analyzer_server_t *self,
  const char *user,
  const char *password,
  uint64_t permissions);

SUBOOL suscli_analyzer_server_add_all_users(suscli_analyzer_server_t *server);

void suscli_analyzer_server_destroy(suscli_analyzer_server_t *self);

#endif /* _SUSCAN_CLI_DEVSERV_DEVSERV_H */
