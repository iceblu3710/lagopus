/*
 * Copyright 2014-2015 Nippon Telegraph and Telephone Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include "lagopus_apis.h"
#include "lagopus_ip_addr.h"
#include "lagopus/datastore.h"
#include "lagopus_session.h"
#include "datastore_apis.h"
#include "datastore_internal.h"
#include "conv_json.h"
#include "cmd_common.h"


#define DATASTORE_DEFAULT_BIND_ADDR	"0.0.0.0"
#define DATASTORE_DEFAULT_BIND_PORT	12345
#define DATASTORE_DEFAULT_PROTOCOL	"tcp"

#define DEFAULT_CONFIG_FILE	CONFDIR "/lagopus.conf"

#define CTOR_IDX        LAGOPUS_MODULE_CONSTRUCTOR_INDEX_BASE + 2


#define SESSION_CONFIGURATOR_NAME	"lagopus.session"





static volatile bool s_do_loop = false;
static volatile bool s_is_started = false;
static volatile bool s_is_initialized = false;
static volatile bool s_is_gracefull = false;
static lagopus_thread_t s_thd = NULL;
static lagopus_mutex_t s_lck = NULL;

static lagopus_ip_address_t *s_bindaddr = NULL;
static uint16_t s_port = DATASTORE_DEFAULT_BIND_PORT;

static session_type_t s_protocol;
static bool s_tls = false;

static char s_current_namespace[DATASTORE_NAMESPACE_MAX + 1];

static pthread_once_t s_once = PTHREAD_ONCE_INIT;

static datastore_interp_t s_interp = NULL;
static char s_conf_file[PATH_MAX];


static lagopus_result_t
s_parse_log(datastore_interp_t *iptr,
            datastore_interp_state_t state,
            size_t argc, const char *const argv[],
            lagopus_hashmap_t *hptr,
            datastore_update_proc_t u_proc,
            datastore_enable_proc_t e_proc,
            datastore_serialize_proc_t s_proc,
            datastore_destroy_proc_t d_proc,
            lagopus_dstring_t *result);

static lagopus_result_t
s_parse_datastore(datastore_interp_t *iptr,
                  datastore_interp_state_t state,
                  size_t argc, const char *const argv[],
                  lagopus_hashmap_t *hptr,
                  datastore_update_proc_t u_proc,
                  datastore_enable_proc_t e_proc,
                  datastore_serialize_proc_t s_proc,
                  datastore_destroy_proc_t d_proc,
                  lagopus_dstring_t *result);

static lagopus_result_t
s_parse_version(datastore_interp_t *iptr,
                datastore_interp_state_t state,
                size_t argc, const char *const argv[],
                lagopus_hashmap_t *hptr,
                datastore_update_proc_t u_proc,
                datastore_enable_proc_t e_proc,
                datastore_serialize_proc_t s_proc,
                datastore_destroy_proc_t d_proc,
                lagopus_dstring_t *result);

static lagopus_result_t
s_parse_tls(datastore_interp_t *iptr,
            datastore_interp_state_t state,
            size_t argc, const char *const argv[],
            lagopus_hashmap_t *hptr,
            datastore_update_proc_t u_proc,
            datastore_enable_proc_t e_proc,
            datastore_serialize_proc_t s_proc,
            datastore_destroy_proc_t d_proc,
            lagopus_dstring_t *result);

static lagopus_result_t
s_parse_snmp(datastore_interp_t *iptr,
             datastore_interp_state_t state,
             size_t argc, const char *const argv[],
             lagopus_hashmap_t *hptr,
             datastore_update_proc_t u_proc,
             datastore_enable_proc_t e_proc,
             datastore_serialize_proc_t s_proc,
             datastore_destroy_proc_t d_proc,
             lagopus_dstring_t *result);


static lagopus_result_t
s_parse_save(datastore_interp_t *iptr,
             datastore_interp_state_t state,
             size_t argc, const char *const argv[],
             lagopus_hashmap_t *hptr,
             datastore_update_proc_t u_proc,
             datastore_enable_proc_t e_proc,
             datastore_serialize_proc_t s_proc,
             datastore_destroy_proc_t d_proc,
             lagopus_dstring_t *result);


static lagopus_result_t
s_parse_load(datastore_interp_t *iptr,
             datastore_interp_state_t state,
             size_t argc, const char *const argv[],
             lagopus_hashmap_t *hptr,
             datastore_update_proc_t u_proc,
             datastore_enable_proc_t e_proc,
             datastore_serialize_proc_t s_proc,
             datastore_destroy_proc_t d_proc,
             lagopus_dstring_t *result);

static lagopus_result_t
s_parse_shutdown(datastore_interp_t *iptr,
                 datastore_interp_state_t state,
                 size_t argc, const char *const argv[],
                 lagopus_hashmap_t *hptr,
                 datastore_update_proc_t u_proc,
                 datastore_enable_proc_t e_proc,
                 datastore_serialize_proc_t s_proc,
                 datastore_destroy_proc_t d_proc,
                 lagopus_dstring_t *result);

static inline lagopus_result_t
s_parse_agent(datastore_interp_t *iptr,
              datastore_interp_state_t state,
              size_t argc, const char *const argv[],
              lagopus_hashmap_t *hptr,
              datastore_update_proc_t u_proc,
              datastore_enable_proc_t e_proc,
              datastore_serialize_proc_t s_proc,
              datastore_destroy_proc_t d_proc,
              lagopus_dstring_t *result);

static void	s_ctors(void) __attr_constructor__(CTOR_IDX);
static void	s_dtors(void) __attr_destructor__(CTOR_IDX);





static inline void
s_set_conf_file(const char *file) {
  const char *s = (IS_VALID_STRING(file) == true) ?
                  file : DEFAULT_CONFIG_FILE;
  ssize_t len = (ssize_t)snprintf(s_conf_file, sizeof(s_conf_file),
                                  "%s", s);
  if (!(len > 0 && strlen(s_conf_file) == (size_t)len)) {
    lagopus_msg_fatal("can't initialize default configuration file as '%s'.\n",
                      s);
  }
}


static inline lagopus_result_t
s_set_addr(const char *addr) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  lagopus_ip_address_t *tmp = NULL;

  const char *s = (IS_VALID_STRING(addr) == true) ?
                  addr : DATASTORE_DEFAULT_BIND_ADDR;

  ret = lagopus_ip_address_create(s, true, &tmp);
  if (ret == LAGOPUS_RESULT_OK) {
    if (s_bindaddr != NULL) {
      lagopus_ip_address_destroy(s_bindaddr);
    }
    s_bindaddr = tmp;
  } else {
    lagopus_msg_fatal("can't initialize datastore default bind addr as "
                      "'%s'.\n", s);
    ret = LAGOPUS_RESULT_INVALID_ARGS;
  }
  return ret;
}


static inline lagopus_result_t
s_set_protocol(const char *protocol) {
  const char *s = (IS_VALID_STRING(protocol) == true) ?
                  protocol: DATASTORE_DEFAULT_PROTOCOL;

  if (strcmp(s, "tcp") == 0) {
    s_protocol = SESSION_TCP;
  } else if (strcmp(s, "tcp6") == 0) {
    s_protocol = SESSION_TCP6;
  } else {
    lagopus_msg_fatal("can't initialize datastore default protocol as "
                      "'%s'.\n", s);
    return LAGOPUS_RESULT_INVALID_ARGS;
  }

  return LAGOPUS_RESULT_OK;
}


static inline void
s_set_tls(const bool tls) {
  s_tls = tls;
}


static inline lagopus_result_t
s_set_current_namespace(const char *namespace) {
  if (IS_VALID_STRING(namespace) == true) {
    snprintf(s_current_namespace, sizeof(s_current_namespace),
             "%s", namespace);
    return LAGOPUS_RESULT_OK;
  } else {
    return LAGOPUS_RESULT_INVALID_ARGS;
  }
}


static inline lagopus_result_t
s_unset_current_namespace(void) {
  snprintf(s_current_namespace, sizeof(s_current_namespace),
           "%s", "");
  return LAGOPUS_RESULT_OK;
}


static inline lagopus_result_t
s_get_current_namespace(char **namespace) {
  if (namespace != NULL) {
    if (s_current_namespace != NULL) {
      if (s_current_namespace[0] == '\0') {
        *namespace = (char *) malloc(sizeof(char));
        if (*namespace != NULL) {
          *namespace[0] = '\0';
          return LAGOPUS_RESULT_OK;
        } else {
          return LAGOPUS_RESULT_NO_MEMORY;
        }
      } else {
        *namespace = strdup(s_current_namespace);
        if (IS_VALID_STRING(*namespace) == true) {
          return LAGOPUS_RESULT_OK;
        } else {
          return LAGOPUS_RESULT_NO_MEMORY;
        }
      }
    } else {
      return LAGOPUS_RESULT_ANY_FAILURES;
    }
  } else {
    return LAGOPUS_RESULT_INVALID_ARGS;
  }
}


static inline lagopus_result_t
s_get_namespace(const char *str, char **namespace) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  char *ns_buf = NULL;
  size_t i = 0;
  size_t idx = 0;
  size_t ns_len = 0;

  if (IS_VALID_STRING(str) == true && namespace != NULL) {
    ret = cmd_string_indexof(str, NAMESPACE_DELIMITER);

    if (ret >= 0) {
      ns_buf = strdup(str);
      if (IS_VALID_STRING(ns_buf) == true) {
        idx = (size_t) ret;
        ns_len = strlen(ns_buf);

        for (i = idx; i < ns_len; i++) {
          ns_buf[i] = '\0';
        }

        *namespace = ns_buf;

        return LAGOPUS_RESULT_OK;
      } else {
        free(ns_buf);
        return LAGOPUS_RESULT_NO_MEMORY;
      }
    } else {
      ret = s_get_current_namespace(&ns_buf);
      if (ret == LAGOPUS_RESULT_OK) {
        *namespace = ns_buf;
        return LAGOPUS_RESULT_OK;
      } else {
        free(ns_buf);
        lagopus_perror(ret);
        return ret;
      }
    }
  } else {
    return LAGOPUS_RESULT_INVALID_ARGS;
  }
}


static inline lagopus_result_t
s_get_fullname(const char *name, char **fullname) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  size_t full_len = 0;
  size_t ns_len = 0;
  size_t delim_len = 0;
  size_t name_len = 0;
  char *current_namespace = NULL;
  char *buf_fullname = NULL;

  if (name != NULL && fullname != NULL) {
    if (strstr(name, NAMESPACE_DELIMITER) == NULL) {

      ret = s_get_current_namespace(&current_namespace);
      if (ret == LAGOPUS_RESULT_OK) {
        ns_len = strlen(current_namespace);
        delim_len = strlen(NAMESPACE_DELIMITER);
        name_len = strlen(name);
        full_len = ns_len + delim_len + name_len;

        buf_fullname = (char *) malloc(sizeof(char) * (full_len + 1));

        if (buf_fullname != NULL) {
          ret = snprintf(buf_fullname, full_len + 1, "%s%s%s",
                         current_namespace,
                         NAMESPACE_DELIMITER,
                         name);
          if (ret >= 0) {
            *fullname = buf_fullname;
            ret = LAGOPUS_RESULT_OK;
          } else {
            lagopus_msg_warning("get fullname failed.\n");
            free((void *) buf_fullname);
            ret = LAGOPUS_RESULT_ANY_FAILURES;
          }
        } else {
          ret = LAGOPUS_RESULT_NO_MEMORY;
        }
      }

      free((void *) current_namespace);

      return ret;
    } else {
      *fullname = strdup(name);
      if (IS_VALID_STRING(*fullname) == true) {
        return LAGOPUS_RESULT_OK;
      } else {
        return LAGOPUS_RESULT_INVALID_ARGS;
      }
    }
  } else {
    return LAGOPUS_RESULT_INVALID_ARGS;
  }
}


static inline lagopus_result_t
s_get_search_target(const char *fullname, char **target) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  char *target_buf = NULL;
  size_t i = 0;
  size_t idx = 0;
  size_t target_len = 0;
  size_t delim_len = 0;

  if (IS_VALID_STRING(fullname) == true && target != NULL) {
    ret = cmd_string_indexof(fullname, NAMESPACE_DELIMITER);
    if (ret >= 0) { /* namespace + name or namespace + delim or delim + name */
      target_buf = strdup(fullname);

      if (IS_VALID_STRING(target_buf) == true) {
        idx = (size_t) ret;
        target_len = strlen(target_buf);
        delim_len = strlen(NAMESPACE_DELIMITER);

        if (target_len == delim_len) { /* only default namespace */
          ret = NS_DEFAULT_NAMESPACE;
        } else if (idx == 0) { /* delim + name */
          ret = NS_FULLNAME;
        } else if (target_len == (idx + delim_len)) { /* namespace + delim */
          for (i = idx; i < target_len; i++) {
            target_buf[i] = '\0';
          }
          ret = NS_NAMESPACE;
        } else { /* namespace + name */
          ret = NS_FULLNAME;
        }
      } else {
        free((void *) target_buf);
        ret = LAGOPUS_RESULT_NO_MEMORY;
      }
    } else if (ret == LAGOPUS_RESULT_NOT_FOUND) { /* name only */
      ret = s_get_fullname(fullname, &target_buf);

      if (ret == LAGOPUS_RESULT_OK) {
        ret = NS_FULLNAME;
      } else {
        free((void *) target_buf);
      }
    }
  }

  if (ret > 0) {
    *target = target_buf;
  }

  return ret;
}





#include "log_cmd.c"
#include "datastore_cmd.c"
#include "version_cmd.c"
#include "tls_cmd.c"
#include "snmp_cmd.c"
#include "save_cmd.c"
#include "load_cmd.c"
#include "atomic_cmd.c"
#include "namespace_cmd.c"
#include "destroy_all_obj_cmd.c"
#include "shutdown_cmd.c"
#include "agent_cmd.c"





static void
s_once_proc(void) {
  lagopus_result_t r;

  s_set_conf_file(NULL);
  s_set_addr(NULL);
  s_set_protocol(NULL);

  if ((r = datastore_create_interp(&s_interp)) != LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't create the datastore interpretor.\n");
  }

  if ((r = datastore_register_configurator(CONFIGURATOR_NAME)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't register a configurator '%s'.\n",
                      CONFIGURATOR_NAME);
  }

  /*
   * Register non-object commands.
   */
  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "log", s_parse_log)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_exit_fatal("can't initialize the \"log\" command.\n");
  }
  log_initialize();
  log_ofpt_table_add();

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "datastore",
           s_parse_datastore)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the datastore command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "version", s_parse_version)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the version command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "tls", s_parse_tls)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the tls command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "snmp", s_parse_snmp)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the snmp command.\n");
  }
  snmp_initialize();

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "save", s_parse_save)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the save command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "load", s_parse_load)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the load command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "atomic", s_parse_atomic)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the atomic command.\n");
  }
  atomic_cmd_initialize();

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "destroy-all-obj",
           s_parse_destroy_all_obj)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the destroy-all-obj command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "shutdown",
           s_parse_shutdown)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the shutdown command.\n");
  }

  if ((r = datastore_interp_register_command(&s_interp, CONFIGURATOR_NAME,
           "agent",
           s_parse_agent)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("can't regsiter the agent command.\n");
  }

  /*
   * In the initialization process, four functions, which are
   * lagopus_session_tls_server_cert_set, lagopus_session_tls_server_key_set,
   * lagopus_session_tls_ca_dir_set, and lagopus_session_tls_point_conf_set,
   * are called only once before registering the tls command.
   */
  if ((r = lagopus_session_tls_set_server_cert(
             DEFAULT_TLS_CERTIFICATE_FILE)) != LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("lagopus_session_tls_server_cert_set.\n");
  }

  if ((r = lagopus_session_tls_set_server_key(DEFAULT_TLS_PRIVATE_KEY)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("lagopus_session_tls_server_key_set.\n");
  }

  if ((r = lagopus_session_tls_set_ca_dir(DEFAULT_TLS_CERTIFICATE_STORE)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("lagopus_session_tls_ca_dir_set.\n");
  }

  if ((r = lagopus_session_tls_set_trust_point_conf(
             DEFAULT_TLS_TRUST_POINT_CONF)) !=
      LAGOPUS_RESULT_OK) {
    lagopus_perror(r);
    lagopus_msg_fatal("lagopus_session_tls_trust_point_conf_set.\n");
  }
}

static inline void
s_init(void) {
  (void)pthread_once(&s_once, s_once_proc);
}


static void
s_ctors(void) {
  s_init();

  lagopus_msg_debug(5, "called.\n");
}


static inline void
s_final(void) {
  datastore_destroy_interp(&s_interp);
  log_finalize();
  snmp_finalize();
  atomic_cmd_finalize();
}


static void
s_dtors(void) {
  s_final();
}





static void
s_datastore_thd_finalize(const lagopus_thread_t *tptr, bool is_canceled,
                         void *arg) {
  (void)tptr;
  (void)arg;

  if (is_canceled == true) {
    if (s_is_started == false) {
      /*
       * Means this thread is canceled while waiting for the global
       * state change.
       */
      global_state_cancel_janitor();
    }
    datastore_interp_cancel_janitor(&s_interp);
  }

  lagopus_msg_debug(5, "called, %s.\n",
                    (is_canceled == true) ? "canceled" : "exited");
}


static void
s_datastore_thd_destroy(const lagopus_thread_t *tptr, void *arg) {
  (void)tptr;
  (void)arg;

  lagopus_msg_debug(5, "called.\n");
}

#define DATASTORE_SESSION_MAX 1024

static lagopus_result_t
s_datastore_thd_main(const lagopus_thread_t *tptr, void *arg) {
  int num_session_poll = 0;
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  lagopus_session_t session_server = NULL;
  lagopus_session_t poll_array[DATASTORE_SESSION_MAX];
  global_state_t s;
  shutdown_grace_level_t l;
  struct addrunion server_addr;
  char *s_bindaddr_str = NULL;

  (void)tptr;
  (void)arg;

  lagopus_msg_debug(5, "waiting for the gala opening...\n");

  ret = global_state_wait_for(GLOBAL_STATE_STARTED, &s, &l, -1LL);
  lagopus_msg_debug(5, "gala opening.\n");
  if (ret == LAGOPUS_RESULT_OK &&
      s == GLOBAL_STATE_STARTED) {
    int rt;

    lagopus_dstring_t ds = NULL;

    ret = lagopus_dstring_create(&ds);
    if (ret != LAGOPUS_RESULT_OK) {
      lagopus_perror(ret);
      lagopus_msg_error("Can't create a dynamic string. "
                        "The datastore thread exits.\n");
      goto done;
    }

    if (s_tls == true) {
      s_protocol = s_protocol | SESSION_TLS;
    }
    lagopus_msg_debug(5, "s_protocol %x.\n", s_protocol);
    session_server = session_create(s_protocol|SESSION_PASSIVE);
    if (session_server == NULL) {
      ret = LAGOPUS_RESULT_NO_MEMORY;
      goto done;
    }

    ret = lagopus_ip_address_str_get(s_bindaddr, &s_bindaddr_str);
    lagopus_msg_debug(5, "s_bindaddr_str %s.\n", s_bindaddr_str);
    if (ret != LAGOPUS_RESULT_OK) {
      lagopus_perror(ret);
      lagopus_msg_error("Can't create a server session."
                        "The datastore thread exits.\n");
      goto done;
    }

    if (SESSION_TCP6 == (SESSION_TCP6 & s_protocol)) {
      rt = addrunion_ipv6_set(&server_addr, s_bindaddr_str);
      lagopus_msg_debug(5, "s_bindaddr_str(IPv6) %s.\n", s_bindaddr_str);
    } else {
      rt = addrunion_ipv4_set(&server_addr, s_bindaddr_str);
      lagopus_msg_debug(5, "s_bindaddr_str(IPv4) %s.\n", s_bindaddr_str);
    }
    if (rt == 0) {
      lagopus_msg_error("The server address must correspond "
                        "to the address falily.\n");
      ret = LAGOPUS_RESULT_INVALID_ARGS;
      goto done;
    }
    ret = session_bind(session_server, &server_addr, s_port);
    lagopus_msg_debug(5, "s_port %d.\n", s_port);
    if (ret != LAGOPUS_RESULT_OK) {
      lagopus_perror(ret);
      lagopus_msg_error("Can't create a server session."
                        "The datastore thread exits.\n");
      goto done;
    }

    s_is_started = true;

    lagopus_msg_debug(5, "gala opening.\n");

    num_session_poll = 0;
    session_read_event_set(session_server);
    poll_array[num_session_poll++] = session_server;
    session_server = NULL;

    /*
     * The main task loop.
     */
    while (s_do_loop == true) {
      int i, nevents = 0;

      ret = session_poll(poll_array, num_session_poll, 500 /* msec */);
      lagopus_msg_debug(5, "num_session_poll %d.\n", num_session_poll);
      if (ret == LAGOPUS_RESULT_POSIX_API_ERROR) {
        if (errno != EINTR && errno != EAGAIN) {
          lagopus_msg_warning("poll: %s", strerror(errno));
        }
        continue;
      }

      nevents = (int) ret;
      for (i = 0; i < num_session_poll; i++) {
        bool is_ready = false, do_close = false;

        ret = session_is_readable(poll_array[i], &is_ready);
        if (is_ready) {
          lagopus_msg_debug(5, "session_is_readable[%d] = true.\n", i);
          nevents--;
          if (session_is_passive(poll_array[i])) { /* listening socket */
            lagopus_session_t accepted_session;
            lagopus_msg_debug(5, "session_is_passive[%d] = true.\n", i);

            ret = session_accept(poll_array[i], &accepted_session);
            if (ret != LAGOPUS_RESULT_OK) {
              lagopus_perror(ret);
              continue;
            }
            if ((num_session_poll + 1) == DATASTORE_SESSION_MAX) {
              lagopus_msg_warning("session overflow %d", DATASTORE_SESSION_MAX);
              session_destroy(accepted_session);
              continue;
            }
            session_read_event_set(accepted_session);
            poll_array[num_session_poll++] = accepted_session;
          } else { /* accepted socket */
            lagopus_msg_debug(5, "session_is_passive[%d] = false.\n", i);
            (void)lagopus_dstring_clear(&ds);
            ret = datastore_interp_eval_session(&s_interp, SESSION_CONFIGURATOR_NAME,
                                                poll_array[i], false, &ds);
            if (ret != LAGOPUS_RESULT_OK) {
              if (ret != LAGOPUS_RESULT_EOF) {
                char *errstr = NULL;
                lagopus_perror(ret);
                (void)lagopus_dstring_str_get(&ds, &errstr);
                if (IS_VALID_STRING(errstr) == true) {
                  lagopus_msg_error("Got an interpretor error: %s\n",
                                    errstr);
                } else {
                  lagopus_msg_error("Got an interpretor error.\n");
                }
                free((void *)errstr);
              } else {
                do_close = true;
              }
            }
          }
        } else if (ret != LAGOPUS_RESULT_OK) {
          lagopus_msg_debug(5, "session_is_readable[%d] = false.\n", i);
          nevents--;
          do_close = true;
        }
        if (do_close) {
          session_destroy(poll_array[i]);
          if (i < (num_session_poll - 1)) {
            poll_array[i] = poll_array[num_session_poll - 1];
          }
          num_session_poll--;
        }
        if (nevents == 0) {
          break;
        }
        lagopus_msg_debug(5, "for loop, %d / %d.\n", i + 1, num_session_poll);
      }
    }

    if (s_is_gracefull == true) {
      lagopus_msg_debug(5, "mimicking gracefull shutdown done.\n");
      ret = LAGOPUS_RESULT_OK;
    } else {
      ret = 1LL;
    }

 done:
    (void)lagopus_dstring_clear(&ds);
    (void)lagopus_dstring_destroy(&ds);
    session_destroy(session_server);
  }

  return ret;
}





static inline void
s_lock(void) {
  if (s_lck != NULL) {
    (void)lagopus_mutex_lock(&s_lck);
  }
}


static inline void
s_unlock(void) {
  if (s_lck != NULL) {
    (void)lagopus_mutex_unlock(&s_lck);
  }
}





lagopus_result_t
datastore_initialize(int argc,
                     const char *const argv[],
                     void *extarg,
                     lagopus_thread_t **thdptr) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;

  (void)extarg;

  lagopus_msg_debug(5, "called.\n");

  if (thdptr != NULL) {
    *thdptr = NULL;
  }

  s_lock();
  {
    if (s_is_initialized == false) {
      int i;

      lagopus_msg_debug(5, "argc: %d\n", argc);
      for (i = 0; i < argc; i++) {
        lagopus_msg_debug(5, "%5d: '%s'\n", i, argv[i]);
      }

      if ((ret = datastore_all_commands_initialize()) != LAGOPUS_RESULT_OK) {
        goto unlock;
      }

      if ((ret = datastore_register_configurator(SESSION_CONFIGURATOR_NAME)) !=
          LAGOPUS_RESULT_OK) {
        lagopus_perror(ret);
        lagopus_msg_fatal("can't register a configurator '%s'.\n",
                          SESSION_CONFIGURATOR_NAME);
      }

      ret = lagopus_thread_create(&s_thd,
                                  s_datastore_thd_main,
                                  s_datastore_thd_finalize,
                                  s_datastore_thd_destroy,
                                  "datastore", NULL);
      if (ret == LAGOPUS_RESULT_OK) {
        s_is_initialized = true;
        if (thdptr != NULL) {
          *thdptr = &s_thd;
        }
      }
    } else {
      ret = LAGOPUS_RESULT_OK;
    }
  }
unlock:
  s_unlock();

  return ret;
}


lagopus_result_t
datastore_start(void) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;

  lagopus_msg_debug(5, "called.\n");

  if (s_thd != NULL) {

    s_lock();
    {
      if (s_is_initialized == true) {
        ret = lagopus_thread_start(&s_thd, false);
        if (ret == LAGOPUS_RESULT_OK) {
          s_do_loop = true;
        }
      } else {
        ret = LAGOPUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
    s_unlock();

  } else {
    ret = LAGOPUS_RESULT_INVALID_OBJECT;
  }

  return ret;
}


lagopus_result_t
datastore_shutdown(shutdown_grace_level_t l) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;

  lagopus_msg_debug(5, "called.\n");

  if (s_thd != NULL) {
    s_lock();
    {
      if (s_is_initialized == true) {
        if (l == SHUTDOWN_GRACEFULLY) {
          s_is_gracefull = true;
        }
        s_do_loop = false;
        ret = LAGOPUS_RESULT_OK;
      } else {
        ret = LAGOPUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
    s_unlock();

  } else {
    ret = LAGOPUS_RESULT_INVALID_OBJECT;
  }

  return ret;
}


lagopus_result_t
datastore_stop(void) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;

  lagopus_msg_debug(5, "called.\n");

  if (s_thd != NULL) {

    s_lock();
    {
      if (s_is_initialized == true) {
        ret = lagopus_thread_cancel(&s_thd);
      } else {
        ret = LAGOPUS_RESULT_INVALID_STATE_TRANSITION;
      }
    }
    s_unlock();

  } else {
    ret = LAGOPUS_RESULT_INVALID_OBJECT;
  }

  return ret;
}


void
datastore_finalize(void) {
  lagopus_msg_debug(5, "called.\n");

  if (s_thd != NULL) {

    s_lock();
    {
      datastore_unregister_configurator(SESSION_CONFIGURATOR_NAME);
      lagopus_thread_destroy(&s_thd);
    }
    s_unlock();

  }
}





static inline bool
s_is_readable_file(const char *file) {
  bool ret = false;
  struct stat s;

  if (stat(file, &s) == 0) {
    if (!S_ISDIR(s.st_mode)) {
      ret = true;
    }
  }

  return ret;
}





static inline lagopus_result_t
s_load_conf(bool do_preload) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;
  const char *file = IS_VALID_STRING(s_conf_file) ?
                     s_conf_file : DEFAULT_CONFIG_FILE;
  lagopus_dstring_t ds;

  lagopus_dstring_create(&ds);

  if (do_preload == true) {
    ret = datastore_interp_eval_file_partial(&s_interp, CONFIGURATOR_NAME,
          file, &ds);
  } else {
    ret = datastore_interp_eval_file(&s_interp, CONFIGURATOR_NAME,
                                     file, &ds);
  }
  if (ret == LAGOPUS_RESULT_POSIX_API_ERROR) {
    /* ignore POSIX error. (Mainly for file not found error.) */
    ret = LAGOPUS_RESULT_OK;
  } else if (ret != LAGOPUS_RESULT_OK) {
    char *err = NULL;
    lagopus_result_t st;

    if ((st = lagopus_dstring_str_get(&ds, &err)) == LAGOPUS_RESULT_OK) {
      if (IS_VALID_STRING(err) == true) {
        lagopus_msg_error("file: %s, %s\n", file, err);
      } else {
        lagopus_msg_error("got error(s) while parsing '%s'.\n",
                          file);
      }
    }

    free((void *)err);
  }

  lagopus_dstring_destroy(&ds);

  return ret;
}





lagopus_result_t
datastore_set_config_file(const char *file) {
  lagopus_result_t ret = LAGOPUS_RESULT_ANY_FAILURES;

  if (IS_VALID_STRING(file) == true) {
    errno = 0;
    if (s_is_readable_file(file) == true) {
      ssize_t len = (ssize_t)snprintf(s_conf_file, sizeof(s_conf_file),
                                      "%s", file);
      if (len > 0 && strlen(file) == (size_t)len) {
        ret = LAGOPUS_RESULT_OK;
      } else if (len < 0) {
        ret = LAGOPUS_RESULT_POSIX_API_ERROR;
      } else {
        ret = LAGOPUS_RESULT_INVALID_ARGS;
      }
    } else {
      if (errno != 0) {
        ret = LAGOPUS_RESULT_POSIX_API_ERROR;
      } else {
        ret = LAGOPUS_RESULT_INVALID_ARGS;
      }
    }
  } else {
    ret = LAGOPUS_RESULT_INVALID_ARGS;
  }

  if (ret != LAGOPUS_RESULT_OK) {
    s_set_conf_file(NULL);
  }

  return ret;
}


lagopus_result_t
datastore_get_config_file(const char **file) {
  if (file != NULL) {
    *file = s_conf_file;
    return LAGOPUS_RESULT_OK;
  } else {
    return LAGOPUS_RESULT_INVALID_ARGS;
  }
}


lagopus_result_t
datastore_preload_config(void) {
  return s_load_conf(true);
}


lagopus_result_t
datastore_load_config(void) {
  return s_load_conf(false);
}


lagopus_result_t
namespace_get_current_namespace(char **namespace) {
  return s_get_current_namespace(namespace);
}


lagopus_result_t
namespace_get_namespace(const char *fullname, char **namespace) {
  return s_get_namespace(fullname, namespace);
}


lagopus_result_t
namespace_get_fullname(const char *name, char **fullname) {
  return s_get_fullname(name, fullname);
}


lagopus_result_t
namespace_get_search_target(const char *fullname, char **target) {
  return s_get_search_target(fullname, target);
}


datastore_interp_t datastore_get_master_interp(void);

datastore_interp_t
datastore_get_master_interp(void) {
  if (s_interp != NULL) {
    return s_interp;
  } else {
    lagopus_exit_fatal("The datastore master interpretor "
                       "is not initialized.\n");
    /* not reached. */
    return NULL;
  }
}
