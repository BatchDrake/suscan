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

#define SU_LOG_DOMAIN "analyzer-server"

#include "devserv.h"
#include <analyzer/msg.h>
#include <sigutils/log.h>
#include <sys/poll.h>
#include <sys/fcntl.h>

SUPRIVATE void suscli_analyzer_server_kick_client(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client);
SUPRIVATE void suscli_analyzer_server_kick_client_unsafe(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client);

/***************************** TX Thread **************************************/

/*
 * This function is called inside the client list mutex
 */
SUPRIVATE SUBOOL
suscli_analyzer_server_intercept_message_unsafe(
    suscli_analyzer_server_t *self,
    uint32_t type,
    void *message,
    suscli_analyzer_client_t **oclient)
{
  struct suscan_analyzer_inspector_msg *inspmsg;
  struct suscan_analyzer_sample_batch_msg *samplemsg;

  int32_t itl_index;
  suscli_analyzer_client_t *client = NULL;
  struct suscli_analyzer_itl_entry *entry = NULL;
  SUHANDLE private_handle;
  SUBOOL ok = SU_FALSE;

  switch (type) {
    case SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR:
      inspmsg = (struct suscan_analyzer_inspector_msg *) message;

      switch (inspmsg->kind) {
        case  SUSCAN_ANALYZER_INSPECTOR_MSGKIND_OPEN:
          client = suscli_analyzer_client_list_lookup_unsafe(
              &self->client_list,
              inspmsg->req_id);

          if (client == NULL) {
            SU_ERROR(
                "Consistency error! No client with given sfd %d\n",
                inspmsg->req_id);
            goto done;
          }

          if (client->sfd != inspmsg->req_id) {
            SU_ERROR(
                "Consistency error! sfd %d does not match req_id\n",
                client->sfd, inspmsg->req_id);
            goto done;
          }

          suscli_analyzer_client_dec_inspector_open_request(client);

          SU_TRYCATCH(
              (itl_index = suscli_analyzer_client_list_alloc_itl_entry_unsafe(
                  &self->client_list,
                  client)) != -1,
                  goto done);

          entry = suscli_analyzer_client_list_get_itl_entry_unsafe(
              &self->client_list,
              itl_index);

          /* Time to create a new handle */
          private_handle = suscli_analyzer_client_register_inspector_handle(
              client,
              inspmsg->handle,
              itl_index);

          entry->private_handle = private_handle;

          SU_INFO(
              "%s: inspector (handle 0x%x) opened\n",
              suscli_analyzer_client_get_name(client),
              private_handle);

          inspmsg->handle = private_handle;
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_CLOSE:
          /* Translate inspector id to the user-defined inspector id */
          itl_index = inspmsg->inspector_id;

          entry = suscli_analyzer_client_list_get_itl_entry_unsafe(
              &self->client_list,
              itl_index);

          if (entry == NULL) {
            SU_ERROR("BUG: Unmatched itl_index\n");
            goto done;
          } else {
            client = entry->client;
            inspmsg->inspector_id = entry->local_inspector_id;

            /* Close local handle */
            SU_TRYCATCH(
                suscli_analyzer_client_dispose_inspector_handle(
                    client,
                    entry->private_handle),
                goto done);

            /* Remove entry from ITL */
            SU_TRYCATCH(
                suscli_analyzer_client_list_dispose_itl_entry_unsafe(
                    &self->client_list,
                    itl_index),
                goto done);
            SU_INFO(
                "%s: inspector (handle 0x%x) closed\n",
                suscli_analyzer_client_get_name(client),
                entry->private_handle);
          }
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_INVALID_CHANNEL:
          client = suscli_analyzer_client_list_lookup_unsafe(
              &self->client_list,
              inspmsg->req_id);

          if (client == NULL) {
            SU_ERROR(
                "Consistency error! No client with given sfd %d\n",
                inspmsg->req_id);
            goto done;
          }

          suscli_analyzer_client_dec_inspector_open_request(client);
          break;

        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_KIND:
        case SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE:
          break;

        default:
          /* Translate inspector id to the user-defined inspector id */
          itl_index = inspmsg->inspector_id;

          entry = suscli_analyzer_client_list_get_itl_entry_unsafe(
              &self->client_list,
              itl_index);

          if (entry == NULL) {
            SU_ERROR(
                "BUG: Unmatched itl_index 0x%x (type %s)\n",
                itl_index,
                suscan_analyzer_inspector_msgkind_to_string(inspmsg->kind));
            goto done;
          } else {
            client = entry->client;
            inspmsg->inspector_id = entry->local_inspector_id;
          }
      }
      break;

    case SUSCAN_ANALYZER_MESSAGE_TYPE_SAMPLES:
      samplemsg = (struct suscan_analyzer_sample_batch_msg *) message;

      /* Translate inspector id to the user-defined inspector id */
      itl_index = samplemsg->inspector_id;

      entry = suscli_analyzer_client_list_get_itl_entry_unsafe(
          &self->client_list,
          itl_index);

      if (entry == NULL) {
        SU_ERROR("BUG: Unmatched itl_index\n");
        goto done;
      } else {
        client = entry->client;
        samplemsg->inspector_id = entry->local_inspector_id;
      }

      break;

  }

  *oclient = client;

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_on_client_inspector(
    const suscli_analyzer_client_t *client,
    void *userdata,
    SUHANDLE private_handle,
    SUHANDLE global_handle)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) userdata;

  if (self->tx_thread_running && self->client_list.epoch == client->epoch) {
    SU_INFO(
          "%s: cleaning up: close handle 0x%x (global 0x%x)\n",
          suscli_analyzer_client_get_name(client),
          private_handle,
          global_handle);

    SU_TRYCATCH(
        suscan_analyzer_close_async(
            self->analyzer,
            global_handle,
            0),
        return SU_FALSE);
  } else {
    /* No analyzer, just remove */
    SU_TRYCATCH(
        suscli_analyzer_client_dispose_inspector_handle_unsafe(
            (suscli_analyzer_client_t *) client,
            private_handle),
        return SU_FALSE);
  }

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_cleanup_client_resources(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client)
{
  SU_TRYCATCH(
      suscli_analyzer_client_for_each_inspector(
          client,
          suscli_analyzer_server_on_client_inspector,
          self),
      return SU_FALSE);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_on_broadcast_error(
    suscli_analyzer_client_t *client,
    void *userdata,
    int error)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) userdata;

  suscli_analyzer_server_kick_client_unsafe(self, client);

  return SU_TRUE;
}

SUPRIVATE void *
suscli_analyzer_server_tx_thread(void *ptr)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) ptr;
  void *message;
  uint32_t type;
  suscli_analyzer_client_t *client = NULL;
  SUBOOL   mutex_acquired = SU_FALSE;

  grow_buf_t pdu = grow_buf_INITIALIZER;
  struct suscan_analyzer_remote_call call = suscan_analyzer_remote_call_INITIALIZER;

  while ((message = suscan_analyzer_read(self->analyzer, &type)) != NULL) {
    /* TODO: How about making this mutex more granular? */
    SU_TRYCATCH(
        pthread_mutex_lock(&self->client_list.client_mutex) != -1,
        goto done);
    mutex_acquired = SU_TRUE;

    /* vvvvvvvvvvvvvvvvvvvvv Client list mutex acquired vvvvvvvvvvvvvvvvvvvv */
    SU_TRYCATCH(
        suscli_analyzer_server_intercept_message_unsafe(
            self,
            type,
            message,
            &client),
        goto done);

    call.type     = SUSCAN_ANALYZER_REMOTE_MESSAGE;
    call.msg.type = type;
    call.msg.ptr  = message;

    SU_TRYCATCH(suscan_analyzer_remote_call_serialize(&call, &pdu), goto done);

    if (client == NULL) {
      /* No specific client: broadcast */
      suscli_analyzer_client_list_broadcast_unsafe(
          &self->client_list,
          &pdu,
          suscli_analyzer_server_on_broadcast_error,
          self);
    } else {
      if (suscli_analyzer_client_can_write(client)) {
        if (!suscli_analyzer_client_write_buffer_zerocopy(client, &pdu))
          suscli_analyzer_server_kick_client_unsafe(self, client);
      }
    }

    /* ^^^^^^^^^^^^^^^^^^^^^ Client list mutex acquired ^^^^^^^^^^^^^^^^^^^^ */
    SU_TRYCATCH(
        pthread_mutex_unlock(&self->client_list.client_mutex) != -1,
        goto done);
    mutex_acquired = SU_FALSE;

    grow_buf_shrink(&pdu);
    suscan_analyzer_remote_call_finalize(&call);
  }

  /* The king is dead, long live the king! */
  suscli_analyzer_client_list_increment_epoch(&self->client_list);

  if (type == SUSCAN_WORKER_MSG_TYPE_HALT)
    SU_INFO("TX: Analyzer halted. Bye.\n");
  else
    SU_WARNING("TX: Analyzer sent null message (%d)\n", type);

done:
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_list.client_mutex);

  grow_buf_clear(&pdu);
  suscan_analyzer_remote_call_finalize(&call);

  suscli_analyzer_client_list_force_shutdown(&self->client_list);

  self->tx_halted = SU_TRUE;

  return NULL;
}

/***************************** RX Thread **************************************/
SUPRIVATE SUBOOL
suscli_analyzer_server_process_auth_message(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client,
    const struct suscan_analyzer_remote_call *call)
{
  uint8_t auth_token[SHA256_BLOCK_SIZE];
  char *new_name;

  SUBOOL ok = SU_FALSE;

  if (call->type != SUSCAN_ANALYZER_REMOTE_AUTH_INFO) {
    SU_ERROR(
        "%s: expected auth info, received type = %d\n",
        suscli_analyzer_client_get_name(client),
        call->type);
    goto done;
  }

  SU_INFO(
      "%s (%s): received authentication tokens from user `%s'\n",
      suscli_analyzer_client_get_name(client),
      call->client_auth.client_name,
      call->client_auth.user);

  suscan_analyzer_server_compute_auth_token(
        auth_token,
        self->user,
        self->password,
        client->server_hello.sha256salt);

  /* Compare tokens */
  if (memcmp(
      call->client_auth.sha256token,
      auth_token,
      SHA256_BLOCK_SIZE) != 0) {
    SU_INFO(
          "%s (%s): authentication rejected\n",
          suscli_analyzer_client_get_name(client),
          call->client_auth.client_name);
  } else {
    SU_INFO(
          "%s (%s): login successful\n",
          suscli_analyzer_client_get_name(client),
          call->client_auth.client_name,
          call->client_auth.user);
    SU_TRYCATCH(
        new_name = strbuild(
            "%s (%s)",
            suscli_analyzer_client_get_name(client),
            call->client_auth.client_name),
        goto done);

    free(client->name);
    client->name = new_name;
    client->auth = SU_TRUE;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_start_analyzer(suscli_analyzer_server_t *self)
{
  struct suscan_analyzer_params params =
      suscan_analyzer_params_INITIALIZER;
  suscan_analyzer_t *analyzer = NULL;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(self->analyzer == NULL,   goto done);
  SU_TRYCATCH(!self->tx_thread_running, goto done);

  SU_TRYCATCH(
      analyzer = suscan_analyzer_new(
          &params,
          self->config,
          &self->mq),
      goto done);

  self->analyzer = analyzer;
  self->tx_halted = SU_FALSE;

  SU_TRYCATCH(
      pthread_create(
          &self->tx_thread,
          NULL,
          suscli_analyzer_server_tx_thread,
          self) != -1,
      goto done);
  self->tx_thread_running = SU_TRUE;

  analyzer = NULL;

  ok = SU_TRUE;

done:
  if (analyzer != NULL) {
    suscan_analyzer_destroy(analyzer);
    suscan_analyzer_consume_mq(&self->mq);
  }

  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_on_open(
    void *userdata,
    suscli_analyzer_client_t *client,
    struct suscan_analyzer_inspector_msg *inspmsg)
{
  /*
   * Client requested opening a inspector. This implies matching the request
   * with the corresponding response. We do this by adjusting the request ID
   * to the socket descriptor (sfd) of the client.
   */

  suscli_analyzer_client_inc_inspector_open_request(client);

  inspmsg->req_id = client->sfd;

  SU_INFO(
        "%s: open request of `%s' inspector on freq %+lg MHz (bw = %g kHz)\n",
        suscli_analyzer_client_get_name(client),
        inspmsg->class_name,
        (inspmsg->channel.fc + inspmsg->channel.ft) * 1e-6,
        inspmsg->channel.bw * 1e-3);

  return SU_TRUE;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_on_set_id(
    void *userdata,
    suscli_analyzer_client_t *client,
    struct suscan_analyzer_inspector_msg *inspmsg,
    int32_t itl_index)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) userdata;
  SUBOOL mutex_acquired = SU_FALSE;
  SUBOOL ok = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->client_list.client_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  /* vvvvvvvvvvvvvvvvvvvvvvvvvvvv Client mutex vvvvvvvvvvvvvvvvvvvvvvvvvvvvv */
  SU_TRYCATCH(itl_index >= 0, goto done);
  SU_TRYCATCH(itl_index < self->client_list.itl_count, goto done);

  self->client_list.itl_table[itl_index].local_inspector_id =
      inspmsg->inspector_id;

  inspmsg->inspector_id = itl_index;

  ok = SU_TRUE;

done:
  /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Client mutex ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_list.client_mutex);

  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_on_wrong_handle(
    void *userdata,
    suscli_analyzer_client_t *client,
    enum suscan_analyzer_inspector_msgkind kind,
    SUHANDLE handle,
    uint32_t req_id)
{
  suscli_analyzer_server_t *self = (suscli_analyzer_server_t *) userdata;
  struct suscan_analyzer_inspector_msg *newmsg = NULL;
  SUBOOL ok = SU_FALSE;

  SU_INFO(
      "%s: %s: wrong inspector handle 0x%x\n",
      suscli_analyzer_client_get_name(client),
      suscan_analyzer_inspector_msgkind_to_string(kind),
      handle);

  SU_TRYCATCH(
      newmsg = suscan_analyzer_inspector_msg_new(
          SUSCAN_ANALYZER_INSPECTOR_MSGKIND_WRONG_HANDLE,
          req_id),
      goto done);

  newmsg->handle = handle;

  SU_TRYCATCH(
      suscan_mq_write(
          &self->mq,
          SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
          newmsg),
      goto done);

  newmsg = NULL;

  ok = SU_TRUE;

done:
  if (newmsg != NULL)
    suscan_analyzer_dispose_message(
        SUSCAN_ANALYZER_MESSAGE_TYPE_INSPECTOR,
        newmsg);

  return ok;
}

SUPRIVATE void
suscli_analyzer_server_kick_client_unsafe(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client)
{
  if (!suscli_analyzer_client_is_closed(client))
    suscli_analyzer_client_shutdown(client);

  if (!suscli_analyzer_client_is_failed(client)) {
    suscli_analyzer_server_cleanup_client_resources(self, client);
    suscli_analyzer_client_mark_failed(client);
  }
}

SUPRIVATE void
suscli_analyzer_server_kick_client(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client)
{
  SUBOOL mutex_acquired = SU_FALSE;

  SU_TRYCATCH(
      pthread_mutex_lock(&self->client_list.client_mutex) != -1,
      goto done);
  mutex_acquired = SU_TRUE;

  /* vvvvvvvvvvvvvvvvvvvvvvvvvvvv Client mutex vvvvvvvvvvvvvvvvvvvvvvvvvvvvv */
  suscli_analyzer_server_kick_client_unsafe(self, client);

done:
  /* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^ Client mutex ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
  if (mutex_acquired)
    (void) pthread_mutex_unlock(&self->client_list.client_mutex);
}

SUPRIVATE SUBOOL
suscli_analyzer_server_deliver_call(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *caller,
    struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  struct suscli_analyzer_client_interceptors interceptors = {
      .userdata               = self,
      .inspector_set_id       = suscli_analyzer_server_on_set_id,
      .inspector_open         = suscli_analyzer_server_on_open,
      .inspector_wrong_handle = suscli_analyzer_server_on_wrong_handle
  };

  switch (call->type) {
    case SUSCAN_ANALYZER_REMOTE_SET_FREQUENCY:
      SU_TRYCATCH(
          suscan_analyzer_set_freq(self->analyzer, call->freq, call->lnb),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_GAIN:
      SU_TRYCATCH(
          suscan_analyzer_set_gain(
              self->analyzer,
              call->gain.name,
              call->gain.value),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_ANTENNA:
      SU_TRYCATCH(
          suscan_analyzer_set_antenna(self->analyzer, call->antenna),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BANDWIDTH:
      SU_TRYCATCH(
          suscan_analyzer_set_bw(self->analyzer, call->bandwidth),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_PPM:
      SU_TRYCATCH(
          suscan_analyzer_set_ppm(self->analyzer, call->ppm),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_DC_REMOVE:
      SU_TRYCATCH(
          suscan_analyzer_set_dc_remove(self->analyzer, call->dc_remove),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_IQ_REVERSE:
      SU_TRYCATCH(
          suscan_analyzer_set_iq_reverse(self->analyzer, call->iq_reverse),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_AGC:
      SU_TRYCATCH(
          suscan_analyzer_set_agc(self->analyzer, call->agc),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_FORCE_EOS:
      if (self->client_list.client_count == 1) {
        SU_TRYCATCH(
            suscan_analyzer_force_eos(self->analyzer),
            goto done);
      } else {
        SU_WARNING("Force EOS message ignored (other consumers online)\n");
        suscli_analyzer_server_kick_client(self, caller);
      }
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SWEEP_STRATEGY:
      SU_TRYCATCH(
          suscan_analyzer_set_sweep_stratrgy(
              self->analyzer,
              call->sweep_strategy),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_SPECTRUM_PARTITIONING:
      SU_TRYCATCH(
          suscan_analyzer_set_spectrum_partitioning(
              self->analyzer,
              call->spectrum_partitioning),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_HOP_RANGE:
      SU_TRYCATCH(
          suscan_analyzer_set_hop_range(
              self->analyzer,
              call->hop_range.min,
              call->hop_range.max),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_SET_BUFFERING_SIZE:
      SU_TRYCATCH(
          suscan_analyzer_set_buffering_size(
              self->analyzer,
              call->buffering_size),
          goto done);
      break;

    case SUSCAN_ANALYZER_REMOTE_MESSAGE:
      if (suscli_analyzer_client_intercept_message(
          caller,
          call->msg.type,
          call->msg.ptr,
          &interceptors)) {
        SU_TRYCATCH(
          suscan_analyzer_write(
              self->analyzer,
              call->msg.type,
              call->msg.ptr),
          goto done);

        /* Message delivered, give ownership */
        call->msg.ptr = NULL;
      }
      break;

    case SUSCAN_ANALYZER_REMOTE_REQ_HALT:
      /* TODO: Acknowledge something. */
      if (self->client_list.client_count == 1) {
        suscan_analyzer_req_halt(self->analyzer);
      } else {
        SU_WARNING("Halt message ignored (other consumers online)\n");
        suscli_analyzer_server_kick_client(self, caller);
      }
      break;

    default:
      SU_ERROR("Invalid call code %d\n", call->type);
      goto done;
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_process_call(
    suscli_analyzer_server_t *self,
    suscli_analyzer_client_t *client,
    struct suscan_analyzer_remote_call *call)
{
  SUBOOL ok = SU_FALSE;

  if (suscli_analyzer_client_is_auth(client)) {
    SU_TRYCATCH(
        suscli_analyzer_server_deliver_call(self, client, call),
        goto done);
  } else {
    SU_TRYCATCH(
        suscli_analyzer_server_process_auth_message(self, client, call),
        goto done);

    if (suscli_analyzer_client_is_auth(client)) {
      /*
       * Authentication successful! Now the client is entitled to make
       * changes in the server. First, ensure the analyzer object is
       * running.
       */
      if (self->analyzer == NULL) {
        if (!suscli_analyzer_server_start_analyzer(self)) {
          SU_ERROR("Failed to initialize analyzer. Rejecting client\n");
          suscli_analyzer_server_kick_client(self, client);

          /* Yep, no errors. Assume graceful disconnection. */
          ok = SU_TRUE;
          goto done;
        }
      }

      /*
       * Now that we are sure that the analyzer object exists, we update
       * the client with the source information. This is important from the
       * endpoint perspective in order to be aware of frequency limits, sample
       * rate, etc
       */

      SU_TRYCATCH(
          suscli_analyzer_client_send_source_info(
              client,
              suscan_analyzer_get_source_info(self->analyzer)),
          goto done);

      /* We locally request a global update of params */
      suscan_analyzer_write(
          self->analyzer,
          SUSCAN_ANALYZER_MESSAGE_TYPE_GET_PARAMS,
          "LOCAL");
    } else {
      /* Authentication failed. Mark as failed. */
      SU_WARNING("Client did not pass the challenge, kicking him\n");
      suscli_analyzer_client_send_auth_rejected(client);
      suscli_analyzer_server_kick_client(self, client);
    }
  }

  ok = SU_TRUE;

done:
  return ok;
}

SUPRIVATE SUBOOL
suscli_analyzer_server_register_clients(suscli_analyzer_server_t *self)
{
  int fd;
  suscli_analyzer_client_t *client = NULL;
  struct sockaddr_in inaddr;
  socklen_t len = sizeof(struct sockaddr_in);
  SUBOOL ok = SU_FALSE;

  while ((fd = accept(
      self->client_list.listen_fd,
      (struct sockaddr *) &inaddr,
      &len)) != -1) {
    SU_TRYCATCH(
        client = suscli_analyzer_client_new(fd),
        goto done);

    SU_TRYCATCH(
        suscli_analyzer_client_list_append_client(&self->client_list, client),
        goto done);

    /* Send authentication challenge in client hello */
    SU_TRYCATCH(suscli_analyzer_client_send_hello(client), goto done);

    client = NULL;
  }

  SU_TRYCATCH(errno == EAGAIN, goto done);

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR("errno: %s\n", strerror(errno));

  if (client != NULL)
    suscli_analyzer_client_destroy(client);

  return ok;
}

SUPRIVATE void
suscli_analyzer_server_clean_dead_threads(suscli_analyzer_server_t *self)
{
  if (self->tx_thread_running && self->tx_halted) {
    pthread_join(self->tx_thread, NULL);

    if (self->analyzer != NULL) {
      suscan_analyzer_destroy(self->analyzer);
      suscan_analyzer_consume_mq(&self->mq);
      self->analyzer = NULL;
    }

    self->tx_thread_running = SU_FALSE;
  }
}

SUPRIVATE void *
suscli_analyzer_server_rx_thread(void *userdata)
{
  suscli_analyzer_server_t *self =
      (suscli_analyzer_server_t *) userdata;
  suscli_analyzer_client_t *client = NULL;
  struct suscan_analyzer_remote_call *call;
  struct rbtree_node *node;
  int count;
  unsigned int i;
  struct pollfd *pfds;
  SUBOOL ok = SU_FALSE;

  for (;;) {
    /*
     * The PFD list is updated from the RX thread only. This access is safe
     * We will protect here only write access to the client list.
     */
    pfds = self->client_list.client_pfds;

    SU_TRYCATCH(
        (count = poll(
            pfds,
            self->client_list.client_count + SUSCLI_ANSERV_FD_OFFSET,
            -1)) > 0,
        goto done);

    suscli_analyzer_server_clean_dead_threads(self);

    if (pfds[SUSCLI_ANSERV_CANCEL_FD].revents & POLLIN) {
      /* Cancel requested */
      ok = SU_TRUE;
      goto done;
    } else if (pfds[SUSCLI_ANSERV_LISTEN_FD].revents & POLLIN) {
      /*
       * New client. We cannot continue inspecting the pfds because they
       * have been overwritten by update_pollfds.
       */
      SU_TRYCATCH(suscli_analyzer_server_register_clients(self), goto done);
    } else {
      /* Traverse client list and look for pending messages */
      for (i = 0; count > 0 && i <= self->client_list.client_count; ++i) {
        if (pfds[i + SUSCLI_ANSERV_FD_OFFSET].revents && POLLIN) {
          SU_TRYCATCH(
              node = rbtree_search(
                  self->client_list.client_tree,
                  pfds[i + SUSCLI_ANSERV_FD_OFFSET].fd,
                  RB_EXACT),
              goto done);
          client = node->data;

          SU_TRYCATCH(client != NULL, goto done);

          if (!suscli_analyzer_client_is_failed(client)) {
            if (!suscli_analyzer_client_read(client)) {
              suscli_analyzer_server_kick_client(self, client);
            } else if ((call = suscli_analyzer_client_take_call(client))
                != NULL) {
              /* Call completed from client, process it and do stuff */
              SU_TRYCATCH(
                  suscli_analyzer_server_process_call(self, client, call),
                  goto done);
            }
          }
          --count;
        }
      }

      /* This is actually a consistency condition */
      SU_TRYCATCH(count == 0, goto done);
    }

    /* Some sockets may have been marked as dead. Clean them up */
    SU_TRYCATCH(
        suscli_analyzer_client_list_attempt_cleanup(&self->client_list),
        goto done);

    if (self->tx_thread_running && self->client_list.client_count == 0)
      suscan_analyzer_req_halt(self->analyzer);
  }

  ok = SU_TRUE;

done:
  if (!ok)
    SU_ERROR("errno: %s\n", strerror(errno));

  return NULL;
}

SUPRIVATE int
suscli_analyzer_server_create_socket(uint16_t port)
{
  struct sockaddr_in addr;
  int enable = 1;
  int sfd = -1;
  int fd = -1;
  int flags;

  SU_TRYCATCH(
      (fd = socket(AF_INET, SOCK_STREAM, 0)) != -1,
      goto done);

  if ((flags = fcntl(fd, F_GETFL)) == -1) {
    SU_ERROR("Failed to perform fcntl on socket: %s\n", strerror(errno));
    goto done;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    SU_ERROR("Failed to make socket non blocking: %s\n", strerror(errno));
    goto done;
  }

  SU_TRYCATCH(
      (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) != -1,
      goto done);

  addr.sin_family      = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port        = htons(port);

  if (bind(fd, (struct sockaddr *) &addr, sizeof (struct sockaddr_in)) == -1) {
    SU_ERROR(
        "failed to bind socket to port %d for listen: %s\n",
        port,
        strerror(errno));
    goto done;
  }

  if (listen(fd, 5) == -1) {
    SU_ERROR("failed to listen on socket: %s\n", strerror(errno));
    goto done;
  }

  sfd = fd;
  fd = -1;

done:
  if (fd != -1)
    close(fd);

  return sfd;
}

suscli_analyzer_server_t *
suscli_analyzer_server_new(
    suscan_source_config_t *profile,
    uint16_t port,
    const char *user,
    const char *password)
{
  suscli_analyzer_server_t *new = NULL;
  int sfd = -1;

  SU_TRYCATCH(new = calloc(1, sizeof(suscli_analyzer_server_t)), goto done);

  new->client_list.listen_fd = -1;
  new->client_list.cancel_fd = -1;

  new->cancel_pipefd[0] = -1;
  new->cancel_pipefd[1] = -1;

  SU_TRYCATCH(suscan_mq_init(&new->mq), goto done);
  new->mq_init = SU_TRUE;

  SU_TRYCATCH(new->user = strdup(user), goto done);
  SU_TRYCATCH(new->password = strdup(password), goto done);

  new->listen_port = port;
  SU_TRYCATCH(new->config = suscan_source_config_clone(profile), goto done);

  SU_TRYCATCH(pipe(new->cancel_pipefd) != -1, goto done);

  SU_TRYCATCH(
      (sfd = suscli_analyzer_server_create_socket(port)) != -1,
      goto done);

  SU_TRYCATCH(
      suscli_analyzer_client_list_init(
          &new->client_list,
          sfd,
          new->cancel_pipefd[0]),
      goto done);

  SU_TRYCATCH(
      pthread_create(
          &new->rx_thread,
          NULL,
          suscli_analyzer_server_rx_thread,
          new) != -1,
      goto done);
  new->rx_thread_running = SU_TRUE;

  return new;

done:
  if (new != NULL)
    suscli_analyzer_server_destroy(new);

  return NULL;
}

SUPRIVATE void
suscli_analyzer_server_cancel_rx_thread(suscli_analyzer_server_t *self)
{
  char b = 1;

  (void) write(self->cancel_pipefd[1], &b, 1);
}

void
suscli_analyzer_server_destroy(suscli_analyzer_server_t *self)
{
  if (self->rx_thread_running) {
    if (self->analyzer != NULL) {
      suscan_analyzer_req_halt(self->analyzer);
      if (self->tx_thread_running)
        pthread_join(self->tx_thread, NULL);

      if (self->analyzer != NULL) {
        suscan_analyzer_destroy(self->analyzer);
        suscan_analyzer_consume_mq(&self->mq);
      }
    }

    suscli_analyzer_server_cancel_rx_thread(self);
    pthread_join(self->rx_thread, NULL);
  }

  if (self->client_list.listen_fd != -1)
    close(self->client_list.listen_fd);

  if (self->cancel_pipefd[0] != -1)
    close(self->cancel_pipefd[0]);

  if (self->cancel_pipefd[1] != -1)
    close(self->cancel_pipefd[1]);

  suscli_analyzer_client_list_finalize(&self->client_list);

  if (self->config != NULL)
    suscan_source_config_destroy(self->config);

  if (self->user != NULL)
    free(self->user);

  if (self->password != NULL)
    free(self->password);

  if (self->mq_init)
    suscan_mq_finalize(&self->mq);

  free(self);
}


