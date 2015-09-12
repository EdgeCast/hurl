//: ----------------------------------------------------------------------------
//: Copyright (C) 2015 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    nconn_pool.cc
//: \details: TODO
//: \author:  Reed P. Morrison
//: \date:    05/27/2015
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

//: ----------------------------------------------------------------------------
//: Includes
//: ----------------------------------------------------------------------------
#include "nconn_pool.h"
#include "nconn_tcp.h"
#include "nconn_ssl.h"

//: ----------------------------------------------------------------------------
//: Macros
//: ----------------------------------------------------------------------------
#define NCONN_POOL_SET_NCONN_OPT(_conn, _opt, _buf, _len) \
        do { \
                int _status = 0; \
                _status = _conn.set_opt((_opt), (_buf), (_len)); \
                if (_status != nconn::NC_STATUS_OK) { \
                        NDBG_PRINT("STATUS_ERROR: Failed to set_opt %d.  Status: %d.\n", _opt, _status); \
                        return nconn::NC_STATUS_ERROR;\
                } \
        } while(0)

#define NCONN_POOL_GET_NCONN_OPT(_conn, _opt, _buf, _len) \
        do { \
                int _status = 0; \
                _status = _conn.get_opt((_opt), (_buf), (_len)); \
                if (_status != nconn::NC_STATUS_OK) { \
                        NDBG_PRINT("STATUS_ERROR: Failed to get_opt %d.  Status: %d.\n", _opt, _status); \
                        return nconn::NC_STATUS_ERROR;\
                } \
        } while(0)

namespace ns_hlx {

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn_pool::get(const std::string &a_host,
                        nconn::scheme_t a_scheme,
                        host_info_t a_host_info,
                        const settings_struct_t &a_settings,
                        nconn::type_t a_type,
                        nconn **ao_nconn)
{

        //NDBG_PRINT("%sGET_CONNECTION%s: a_hash: %lu\n", ANSI_COLOR_BG_BLUE, ANSI_COLOR_OFF, a_hash);
        if(!m_initd)
        {
                init();
        }

        // TODO --fix using label...
        std::string l_label;
        if(a_scheme == nconn::SCHEME_TCP)
        {
                l_label = "http://";
        }
        else if(a_scheme == nconn::SCHEME_SSL)
        {
                l_label = "https://";
        }
        l_label += a_host;

        // ---------------------------------------
        // Try grab from conn cache
        // ---------------------------------------
        //NDBG_PRINT("%sGET_CONNECTION%s: m_idle_conn_ncache.size(): %d\n", ANSI_COLOR_FG_BLUE, ANSI_COLOR_OFF, (int)m_idle_conn_ncache.size());
        // Lookup label
        nconn* const*l_nconn_lkp = m_idle_conn_ncache.get(l_label);
        if(l_nconn_lkp)
        {
                *ao_nconn = const_cast<nconn *>(*l_nconn_lkp);
                return nconn::NC_STATUS_OK;
        }

        //NDBG_PRINT("%sGET_CONNECTION%s: l_nconn: %p m_conn_free_list.size() = %lu\n",
        //                ANSI_COLOR_FG_BLUE, ANSI_COLOR_OFF, l_nconn_lkp, m_conn_idx_free_list.size());

        // Try create new from free-list
        if(m_conn_idx_free_list.empty())
        {
                //NDBG_PRINT("%sGET_CONNECTION%s: evicting.................................\n",
                //                ANSI_COLOR_FG_RED, ANSI_COLOR_OFF);

                // Evict from idle_conn_ncache
                m_idle_conn_ncache.evict();
        }

        if(m_conn_idx_free_list.empty())
        {
                NDBG_PRINT("Error no free connections\n");
                return nconn::NC_STATUS_AGAIN;
        }

        // -----------------------------------------
        // Get connection for reqlet
        // Either return existing connection or
        // null for new connection
        // -----------------------------------------
        nconn *l_nconn = NULL;
        uint32_t l_conn_idx = *(m_conn_idx_free_list.begin());

        // Start client for this reqlet
        //NDBG_PRINT("i_conn: %d\n", *i_conn);
        l_nconn = m_nconn_vector[l_conn_idx];
        // TODO Check for NULL

        //NDBG_PRINT("%sGET_CONNECTION%s: GET[%u] l_nconn: %p m_conn_free_list.size() = %lu\n",
        //                ANSI_COLOR_FG_BLUE, ANSI_COLOR_OFF, l_conn_idx, l_nconn, m_conn_idx_free_list.size());

        if(l_nconn &&
           (l_nconn->m_scheme != a_scheme))
        {
                // Destroy nconn and recreate
                //NDBG_PRINT("Destroy nconn and recreate: %u -- l_nconn->m_scheme: %d -- a_scheme: %d\n",
                //                *i_conn,
                //                l_nconn->m_scheme,
                //                a_scheme);
                delete l_nconn;
                l_nconn = NULL;
        }

        if(!l_nconn)
        {
                //-----------------------------------------------------
                // Create nconn BEGIN
                //-----------------------------------------------------
                // TODO Make function
                //-----------------------------------------------------
                //NDBG_PRINT("CREATING NEW CONNECTION: a_settings.m_num_reqs_per_conn: %d\n", (int)a_settings.m_num_reqs_per_conn);
                if(a_scheme == nconn::SCHEME_TCP)
                {
                        l_nconn = new nconn_tcp(a_settings.m_verbose,
                                                a_settings.m_color,
                                                a_settings.m_num_reqs_per_conn,
                                                a_settings.m_save_response,
                                                a_settings.m_collect_stats,
                                                a_settings.m_connect_only,
                                                a_type);
                }
                else if(a_scheme == nconn::SCHEME_SSL)
                {
                        l_nconn = new nconn_ssl(a_settings.m_verbose,
                                                a_settings.m_color,
                                                a_settings.m_num_reqs_per_conn,
                                                a_settings.m_save_response,
                                                a_settings.m_collect_stats,
                                                a_settings.m_connect_only,
                                                a_type);
                }

                l_nconn->set_idx(l_conn_idx);

                // -------------------------------------------
                // Set options
                // -------------------------------------------
                // Set generic options
                NCONN_POOL_SET_NCONN_OPT((*l_nconn), nconn_tcp::OPT_TCP_RECV_BUF_SIZE, NULL, a_settings.m_sock_opt_recv_buf_size);
                NCONN_POOL_SET_NCONN_OPT((*l_nconn), nconn_tcp::OPT_TCP_SEND_BUF_SIZE, NULL, a_settings.m_sock_opt_send_buf_size);
                NCONN_POOL_SET_NCONN_OPT((*l_nconn), nconn_tcp::OPT_TCP_NO_DELAY, NULL, a_settings.m_sock_opt_no_delay);

                // Set ssl options
                if(a_scheme == nconn::SCHEME_SSL)
                {
                        NCONN_POOL_SET_NCONN_OPT((*l_nconn),
                                               nconn_ssl::OPT_SSL_CIPHER_STR,
                                               a_settings.m_ssl_cipher_list.c_str(),
                                               a_settings.m_ssl_cipher_list.length());

                        NCONN_POOL_SET_NCONN_OPT((*l_nconn),
                                               nconn_ssl::OPT_SSL_CTX,
                                               a_settings.m_ssl_ctx,
                                               sizeof(a_settings.m_ssl_ctx));

                        NCONN_POOL_SET_NCONN_OPT((*l_nconn),
                                               nconn_ssl::OPT_SSL_VERIFY,
                                               &(a_settings.m_ssl_verify),
                                               sizeof(a_settings.m_ssl_verify));

                        //NCONN_POOL_SET_NCONN_OPT((*l_nconn), nconn_ssl::OPT_SSL_OPTIONS,
                        //                              &(m_settings.m_ssl_options),
                        //                              sizeof(m_settings.m_ssl_options));

                }
                //-----------------------------------------------------
                // Create nconn END
                //-----------------------------------------------------
                m_nconn_vector[l_conn_idx] = l_nconn;
        }

        // Set host info
        l_nconn->m_host_info = a_host_info;

        // TODO SET ID!!!
        //l_nconn->m_hash = l_hash;

        m_conn_idx_used_set.insert(l_conn_idx);
        m_conn_idx_free_list.pop_front();

        //NDBG_PRINT("%sGET_CONNECTION%s: ERASED[%u] l_nconn: %p m_conn_free_list.size() = %lu\n",
        //                ANSI_COLOR_FG_RED, ANSI_COLOR_OFF, l_conn_idx, l_nconn, m_conn_idx_free_list.size());

        *ao_nconn = l_nconn;
        return nconn::NC_STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn_pool::add_idle(nconn *a_nconn)
{
        //NDBG_PRINT("%sADD_IDLE%s: a_nconn: %p -- hash: %lu\n", ANSI_COLOR_BG_MAGENTA, ANSI_COLOR_OFF, a_nconn, a_nconn->m_hash);
        if(!m_initd)
        {
                init();
        }

        if(!a_nconn)
        {
                NDBG_PRINT("Error a_nconn == NULL\n");
                return STATUS_ERROR;
        }

        // TODO --fix using label...
        std::string l_label;
        if(a_nconn->m_scheme == nconn::SCHEME_TCP)
        {
                l_label = "http://";
        }
        else if(a_nconn->m_scheme == nconn::SCHEME_SSL)
        {
                l_label = "https://";
        }
        l_label += a_nconn->m_host;

        id_t l_id = m_idle_conn_ncache.insert(l_label, a_nconn);

        // Setting id
        //NDBG_PRINT(" ::NCACHE::%sINSERT%s: SET_ID[%d]\n", ANSI_COLOR_FG_GREEN, ANSI_COLOR_OFF, l_id);

        a_nconn->set_id(l_id);

        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn_pool::cleanup(nconn *a_nconn)
{
        //NDBG_PRINT("%sRELEASE%s:\n", ANSI_COLOR_BG_MAGENTA, ANSI_COLOR_OFF);
        if(!m_initd)
        {
                init();
        }

        if(!a_nconn)
        {
                NDBG_PRINT("Error a_nconn == NULL\n");
                return STATUS_ERROR;
        }

        if(STATUS_OK != a_nconn->nc_cleanup())
        {
                NDBG_PRINT("Error a_nconn == NULL\n");
                return STATUS_ERROR;
        }

        uint32_t l_conn_idx = a_nconn->get_idx();
        a_nconn->reset_stats();
        m_conn_idx_free_list.push_back(l_conn_idx);

        //NDBG_PRINT("%sGET_CONNECTION%s: ADDED[%u] m_conn_free_list.size() = %lu\n",
        //                ANSI_COLOR_BG_GREEN, ANSI_COLOR_OFF, l_conn_idx, m_conn_idx_free_list.size());

        m_conn_idx_used_set.erase(l_conn_idx);
        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int nconn_pool::delete_cb(void* o_1, void *a_2)
{
        //NDBG_PRINT("::NCACHE::%sDELETE%s: %p %p\n", ANSI_COLOR_FG_GREEN, ANSI_COLOR_OFF, o_1, a_2);
        nconn_pool *l_nconn_pool = reinterpret_cast<nconn_pool *>(o_1);
        nconn *l_nconn = *(reinterpret_cast<nconn **>(a_2));
        int32_t l_status = l_nconn_pool->cleanup(l_nconn);
        if(l_status != STATUS_OK)
        {
                NDBG_PRINT("Error performing cleanup\n");
                return STATUS_ERROR;
        }
        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t nconn_pool::release(nconn *a_nconn)
{
        //NDBG_PRINT("%sRELEASE%s:\n", ANSI_COLOR_BG_MAGENTA, ANSI_COLOR_OFF);
        if(!m_initd)
        {
                init();
        }

        if(!a_nconn)
        {
                NDBG_PRINT("Error a_nconn == NULL\n");
                return STATUS_ERROR;
        }

        if(STATUS_OK != cleanup(a_nconn))
        {
                NDBG_PRINT("Error performing cleanup\n");
                return STATUS_ERROR;
        }

        if(m_idle_conn_ncache.size() &&
           a_nconn->m_data1)
        {
                m_idle_conn_ncache.remove(a_nconn->get_id());
        }

        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
void nconn_pool::init(void)
{
        m_idle_conn_ncache.set_delete_cb(delete_cb, this);
};

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
nconn_pool::nconn_pool(uint32_t a_size):
                       m_nconn_vector(a_size),
                       m_conn_idx_free_list(),
                       m_conn_idx_used_set(),
                       m_idle_conn_ncache(a_size, NCACHE_LRU),
                       m_initd(false)
{
        for(uint32_t i_conn = 0; i_conn < a_size; ++i_conn)
        {
                m_nconn_vector[i_conn] = NULL;
                //NDBG_PRINT("ADDING i_conn: %u\n", i_conn);
                m_conn_idx_free_list.push_back(i_conn);
        }

};

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
nconn_pool::~nconn_pool(void)
{
        for(uint32_t i_conn = 0; i_conn < m_nconn_vector.size(); ++i_conn)
        {
                if(m_nconn_vector[i_conn])
                {
                        delete m_nconn_vector[i_conn];
                        m_nconn_vector[i_conn] = NULL;
                }
        }

}

} //namespace ns_hlx {

