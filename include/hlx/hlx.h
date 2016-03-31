//: ----------------------------------------------------------------------------
//: Copyright (C) 2014 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    hlx.h
//: \details: TODO
//: \author:  Reed P. Morrison
//: \date:    03/11/2015
//:
//:   Licensed under the Apache License, Version 2.0 (the "License");
//:   you may not use this file except in compliance with the License.
//:   You may obtain a copy of the License at
//:
//:       http://www.apache.org/licenses/LICENSE-2.0
//:
//:   Unless required by applicable law or agreed to in writing, software
//:   distributed under the License is distributed on an "AS IS" BASIS,
//:   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//:   See the License for the specific language governing permissions and
//:   limitations under the License.
//:
//: ----------------------------------------------------------------------------
#ifndef _HLX_H
#define _HLX_H

//: ----------------------------------------------------------------------------
//: Includes
//: ----------------------------------------------------------------------------
// For fixed size types
#include <stdint.h>
#include <string>
#include <list>

//: ----------------------------------------------------------------------------
//: HLX Includes
//: ----------------------------------------------------------------------------
// Types
#include "hlx/atomic.h"

// Enums
#include "hlx/conn_status.h"
#include "hlx/http_status.h"
#include "hlx/scheme.h"
#include "hlx/h_resp.h"

// Support
#include "hlx/stat.h"

//: ----------------------------------------------------------------------------
//: Constants
//: ----------------------------------------------------------------------------
#define HLX_STATUS_OK 0
#define HLX_STATUS_ERROR -1

//: ----------------------------------------------------------------------------
//: Extern Fwd Decl's
//: ----------------------------------------------------------------------------
struct ssl_st;
typedef struct ssl_st SSL;

namespace ns_hlx {

//: ----------------------------------------------------------------------------
//: Fwd Decl's
//: ----------------------------------------------------------------------------
class t_hlx;
class lsnr;
class nresolver;
struct t_conf;
class nconn;
class hconn;
class api_resp;

//: ----------------------------------------------------------------------------
//: hlx
//: ----------------------------------------------------------------------------
class hlx
{
public:
        //: --------------------------------------------------------------------
        //: Types
        //: --------------------------------------------------------------------
        typedef std::list <t_hlx *> t_hlx_list_t;
        typedef std::list <lsnr *> lsnr_list_t;

        // -------------------------------------------------
        // Public methods
        // -------------------------------------------------
        hlx();
        ~hlx();

        // General
        void set_verbose(bool a_val);
        void set_color(bool a_val);
        void set_stats(bool a_val);

        // Settings
        void set_num_threads(uint32_t a_num_threads);
        void set_num_parallel(uint32_t a_num_parallel);
        void set_num_reqs_per_conn(int32_t a_num_reqs_per_conn);
        void set_start_time_ms(uint64_t a_start_time_ms);
        void set_collect_stats(bool a_val);
        void set_update_stats_ms(uint32_t a_update_ms);
        void set_timeout_ms(uint32_t a_val);

        // Server name
        void set_server_name(const std::string &a_name);
        const std::string &get_server_name(void) const;

        // Socket options
        void set_sock_opt_no_delay(bool a_val);
        void set_sock_opt_send_buf_size(uint32_t a_send_buf_size);
        void set_sock_opt_recv_buf_size(uint32_t a_recv_buf_size);

        // DNS settings
        void set_dns_use_sync(bool a_val);
        void set_dns_use_ai_cache(bool a_val);
        void set_dns_ai_cache_file(const std::string &a_file);

        // Listeners
        int32_t register_lsnr(lsnr *a_lsnr);

        // Subrequests
        uint64_t get_next_subr_uuid(void);

        // Helper for apps
        t_hlx_list_t &get_t_hlx_list(void);

        // Running...
        int32_t init_run(void); // init for running but don't start
        int32_t run(void);
        int32_t stop(void);
        int32_t wait_till_stopped(void);
        bool is_running(void);

        // Stats
        void get_stat(t_stat_t &ao_stat);
        void get_stat(const t_stat_list_t &a_stat_list, t_stat_t &ao_stat);
        void get_thread_stat(t_stat_list_t &ao_stat);
        void display_stat(void);

        // TLS config
        // Server ctx
        void set_tls_server_ctx_cipher_list(const std::string &a_cipher_list);
        int set_tls_server_ctx_options(const std::string &a_tls_options_str);
        int set_tls_server_ctx_options(long a_tls_options);
        void set_tls_server_ctx_key(const std::string &a_tls_key);
        void set_tls_server_ctx_crt(const std::string &a_tls_crt);

        // Client ctx
        void set_tls_client_ctx_cipher_list(const std::string &a_cipher_list);
        void set_tls_client_ctx_ca_path(const std::string &a_tls_ca_path);
        void set_tls_client_ctx_ca_file(const std::string &a_tls_ca_file);
        int set_tls_client_ctx_options(const std::string &a_tls_options_str);
        int set_tls_client_ctx_options(long a_tls_options);

        // DNS Resolver
        nresolver *get_nresolver(void);
        bool get_dns_use_sync(void);

private:
        // -------------------------------------------------
        // Private methods
        // -------------------------------------------------
        // Disallow copy/assign
        hlx& operator=(const hlx &);
        hlx(const hlx &);

        int init(void);
        int init_t_hlx_list(void);

        // -------------------------------------------------
        // Private members
        // -------------------------------------------------
        t_conf *m_t_conf;
        bool m_stats;
        uint32_t m_num_threads;
        lsnr_list_t m_lsnr_list;
        nresolver *m_nresolver;
        bool m_dns_use_sync;
        bool m_dns_use_ai_cache;
        std::string m_dns_ai_cache_file;
        uint64_t m_start_time_ms;
        t_hlx_list_t m_t_hlx_list;
        bool m_is_initd;
        uint64_atomic_t m_cur_subr_uid;
        std::string m_server_name;
};

//: ----------------------------------------------------------------------------
//: nconn_utils
//: ----------------------------------------------------------------------------
int nconn_get_fd(nconn &a_nconn);
SSL *nconn_get_SSL(nconn &a_nconn);
long nconn_get_last_SSL_err(nconn &a_nconn);
conn_status_t nconn_get_status(nconn &a_nconn);
const std::string &nconn_get_last_error_str(nconn &a_nconn);

//: ----------------------------------------------------------------------------
//: hnconn_utils
//: ----------------------------------------------------------------------------
// API Responses
api_resp &create_api_resp(hconn &a_hconn);
int32_t queue_api_resp(hconn &a_hconn, api_resp &a_api_resp);
int32_t queue_resp(hconn &a_hconn);
void create_json_resp_str(http_status_t a_status, std::string &ao_resp_str);

// Timer by hconn
typedef int32_t (*timer_cb_t)(void *, void *);
int32_t add_timer(hconn &a_hconn, uint32_t a_ms,
                  timer_cb_t a_cb, void *a_data,
                  void **ao_timer);
int32_t cancel_timer(hconn &a_hconn, void *a_timer);

// Timer by t_hlx
int32_t add_timer(void *a_t_hlx, uint32_t a_ms,
                  timer_cb_t a_cb, void *a_data,
                  void **ao_timer);
int32_t cancel_timer(void *a_t_hlx, void *a_timer);

// Helper
hlx *get_hlx(hconn &a_hconn);

} //namespace ns_hlx {

#endif
