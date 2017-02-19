/*
 * suscan.h: headers, prototypes and declarations for suscan
 * Creation date: Fri Feb  3 19:41:40 2017
 */

#ifndef _MAIN_INCLUDE_H
#define _MAIN_INCLUDE_H

#include <config.h> /* General compile-time configuration parameters */
#include <util.h> /* From util: Common utility library */
#include <sndfile.h>
#include <sigutils/sigutils.h>
#include <sigutils/detect.h>

#include "ctk.h"

#define SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH 15
#define SUSCAN_SOURCE_DIALOG_MAX_BASENAME     SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH
#define SUSCAN_SOURCE_DIALOG_X_PADDING        5
#define SUSCAN_SOURCE_DIALOG_Y_PADDING        9
#define SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET   4

#define ARRAY_SZ(arr) ((sizeof(arr)) / sizeof(arr[0]))

#define SUSCAN_SOURCE_TYPE_BLADE_RF ((void *) 1)
#define SUSCAN_SOURCE_TYPE_HACK_RF  ((void *) 2)
#define SUSCAN_SOURCE_TYPE_IQ_FILE  ((void *) 3)
#define SUSCAN_SOURCE_TYPE_WAV_FILE ((void *) 4)
#define SUSCAN_SOURCE_TYPE_ALSA     ((void *) 5)

#define SUSCAN_WORKER_MESSAGE_TYPE_KEYBOARD      0x0
#define SUSCAN_WORKER_MESSAGE_TYPE_SOURCE_INIT   0x1
#define SUSCAN_WORKER_MESSAGE_TYPE_CHANNEL       0x2
#define SUSCAN_WORKER_MESSAGE_TYPE_EOS           0x3
#define SUSCAN_WORKER_MESSAGE_TYPE_INTERNAL      0x4
#define SUSCAN_WORKER_MESSAGE_TYPE_HALT          0xffffffff

#define SUSCAN_WORKER_INIT_SUCCESS              0
#define SUSCAN_WORKER_INIT_FAILURE              -1

/* Extensible signal source object */
struct xsig_source;

struct xsig_source_params {
  SUBOOL raw_iq;
  unsigned int samp_rate;
  const char *file;
  SUSCOUNT window_size;
  void *private;
  void (*onacquire) (struct xsig_source *source, void *private);
};

struct xsig_source {
  struct xsig_source_params params;
  SF_INFO info;
  uint64_t samp_rate;
  SNDFILE *sf;

  union {
    SUFLOAT *as_real;
    SUCOMPLEX *as_complex;
  };

  SUSCOUNT avail;
};

void xsig_source_destroy(struct xsig_source *source);
struct xsig_source *xsig_source_new(const struct xsig_source_params *params);
SUBOOL xsig_source_acquire(struct xsig_source *source);
su_block_t *xsig_source_create_block(const struct xsig_source_params *params);

struct suscan_mq {
  pthread_mutex_t acquire_lock;
  pthread_cond_t  acquire_cond;

  struct suscan_msg *head;
  struct suscan_msg *tail;
};

enum suscan_field_type {
  SUSCAN_FIELD_TYPE_STRING,
  SUSCAN_FIELD_TYPE_INTEGER,
  SUSCAN_FIELD_TYPE_FLOAT,
  SUSCAN_FIELD_TYPE_FILE
};

union suscan_field_value {
  uint64_t as_int;
  SUFLOAT  as_float;
  char     as_string[0];
};

struct suscan_field {
  enum suscan_field_type type;
  SUBOOL optional;
  const char *name;
  const char *desc;
};

struct suscan_source_config;

struct suscan_source {
  const char *name;
  const char *desc;

  PTR_LIST(struct suscan_field, field);

  su_block_t *(*ctor) (const struct suscan_source_config *);
};

struct suscan_source_config {
  const struct suscan_source *source;
  union suscan_field_value **values;
};

struct suscan_worker {
  struct suscan_mq mq_in;   /* To-thread messages */
  struct suscan_mq *mq_out; /* From-thread messages */
  struct suscan_source_config *config;
  SUBOOL running;

  pthread_t thread;
};

typedef struct suscan_worker suscan_worker_t;

/* Worker messages */
struct suscan_worker_status_msg {
  int code;
  char *err_msg;
};

struct suscan_worker_channel_msg {
  const struct suscan_source *source;
  PTR_LIST(struct sigutils_channel, channel);
};

struct suscan_msg {
  uint32_t type;
  void *private;

  struct suscan_msg *next;
};


/*************************** Message queue API *******************************/
SUBOOL suscan_mq_init(struct suscan_mq *mq);
void   suscan_mq_finalize(struct suscan_mq *mq);
void  *suscan_mq_read(struct suscan_mq *mq, uint32_t *type);
SUBOOL suscan_mq_poll(struct suscan_mq *mq, uint32_t *type, void **private);
SUBOOL suscan_mq_write(struct suscan_mq *mq, uint32_t type, void *private);
SUBOOL suscan_mq_write_urgent(struct suscan_mq *mq, uint32_t type, void *private);

/****************************** Worker API ***********************************/
void suscan_worker_status_msg_destroy(struct suscan_worker_status_msg *status);
struct suscan_worker_status_msg *suscan_worker_status_msg_new(
    uint32_t code,
    const char *msg);
SUBOOL suscan_worker_send_status(
    suscan_worker_t *worker,
    uint32_t type,
    int code,
    const char *err_msg_fmt, ...);
void *suscan_worker_read(suscan_worker_t *worker, uint32_t *type);
void suscan_worker_dispose_message(uint32_t type, void *ptr);
void suscan_worker_destroy(suscan_worker_t *worker);
suscan_worker_t *suscan_worker_new(
    struct suscan_source_config *config,
    struct suscan_mq *mq);

/**************************** Source API *************************************/

struct suscan_source *suscan_source_lookup(const char *name);
struct suscan_source *suscan_source_register(
    const char *name,
    const char *desc,
    su_block_t *(*ctor) (const struct suscan_source_config *));
int suscan_source_lookup_field_id(
    const struct suscan_source *source,
    const char *name);
struct suscan_field *suscan_source_field_id_to_field(
    const struct suscan_source *source,
    int id);
struct suscan_field *suscan_source_lookup_field(
    const struct suscan_source *source,
    const char *name);
SUBOOL suscan_source_add_field(
    struct suscan_source *source,
    enum suscan_field_type type,
    SUBOOL optional,
    const char *name,
    const char *desc);
void suscan_source_config_destroy(struct suscan_source_config *config);
struct suscan_source_config *suscan_source_config_new(
    const struct suscan_source *source);
SUBOOL suscan_source_config_set_integer(
    struct suscan_source_config *cfg,
    const char *name,
    uint64_t value);
SUBOOL suscan_source_config_set_float(
    struct suscan_source_config *cfg,
    const char *name,
    SUFLOAT value);
SUBOOL suscan_source_config_set_string(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);
SUBOOL suscan_source_config_set_file(
    struct suscan_source_config *cfg,
    const char *name,
    const char *value);

union suscan_field_value *suscan_source_config_get_value(
    const struct suscan_source_config *cfg,
    const char *name);

struct suscan_source_config *suscan_source_string_to_config(const char *string);


SUBOOL suscan_wav_source_init(void);
SUBOOL suscan_iqfile_source_init(void);

SUBOOL suscan_init_sources(void);

enum ctk_dialog_response suscan_open_source_dialog(
    struct suscan_source_config **config);

/* Message constructors and destructors */
void suscan_worker_status_msg_destroy(struct suscan_worker_status_msg *status);
struct suscan_worker_status_msg *suscan_worker_status_msg_new(
    uint32_t code,
    const char *msg);
void suscan_worker_channel_msg_destroy(struct suscan_worker_channel_msg *msg);
struct suscan_worker_channel_msg *suscan_worker_channel_msg_new(
    const suscan_worker_t *worker,
    struct sigutils_channel **list,
    unsigned int len);
void suscan_worker_dispose_message(uint32_t type, void *ptr);

#endif /* _MAIN_INCLUDE_H */
