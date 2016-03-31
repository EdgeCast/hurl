//: ----------------------------------------------------------------------------
//: Copyright (C) 2014 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    tls_util.cc
//: \details: TODO
//: \author:  Reed P. Morrison
//: \date:    02/07/2014
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
//:                          OpenSSL Support
//: ----------------------------------------------------------------------------

//: ----------------------------------------------------------------------------
//: Includes
//: ----------------------------------------------------------------------------
#include "tls_util.h"

#include "ndebug.h"
#include "hostcheck/hostcheck.h"

#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <openssl/x509v3.h>

#include <map>
#include <algorithm>

//: ----------------------------------------------------------------------------
//:
//: ----------------------------------------------------------------------------
struct CRYPTO_dynlock_value
{
        pthread_mutex_t mutex;
};

namespace ns_hlx {

//: ----------------------------------------------------------------------------
//: Globals
//: ----------------------------------------------------------------------------
static pthread_mutex_t *g_lock_cs;
__thread char gts_last_tls_error[256] = "\0";

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static struct CRYPTO_dynlock_value* dyn_create_function(const char* a_file, int a_line)
{
        struct CRYPTO_dynlock_value* value = new CRYPTO_dynlock_value;
        if (!value) return NULL;

        pthread_mutex_init(&value->mutex, NULL);
        return value;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static void dyn_lock_function(int a_mode,
                              struct CRYPTO_dynlock_value* a_l,
                              const char* a_file,
                              int a_line)
{
        if (a_mode & CRYPTO_LOCK)
        {
                pthread_mutex_lock(&a_l->mutex);
        }
        else
        {
                pthread_mutex_unlock(&a_l->mutex);
        }
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static void dyn_destroy_function(struct CRYPTO_dynlock_value* a_l,
                                 const char* a_file,
                                 int a_line)
{
        if(a_l)
        {
                pthread_mutex_destroy(&a_l->mutex);
                free(a_l);
        }
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static void pthreads_locking_callback(int a_mode, int a_type, const char *a_file, int a_line)
{
#if 0
        fprintf(stdout,"thread=%4d mode=%s lock=%s %s:%d\n",
                        (int)CRYPTO_thread_id(),
                        (mode&CRYPTO_LOCK)?"l":"u",
                                        (type&CRYPTO_READ)?"r":"w",a_file,a_line);
#endif

#if 0
        if (CRYPTO_LOCK_SSL_CERT == type)
                fprintf(stdout,"(t,m,f,l) %ld %d %s %d\n",
                                CRYPTO_thread_id(),
                                a_mode,a_file,a_line);
#endif

        if (a_mode & CRYPTO_LOCK)
        {
                pthread_mutex_lock(&(g_lock_cs[a_type]));
        } else
        {
                pthread_mutex_unlock(&(g_lock_cs[a_type]));

        }

}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static unsigned long pthreads_thread_id(void)
{
        unsigned long ret;

        ret=(unsigned long)pthread_self();
        return(ret);

}

//: ----------------------------------------------------------------------------
//: \details: OpenSSL can safely be used in multi-threaded applications provided
//:           that at least two callback functions are set, locking_function and
//:           threadid_func this function sets those two callbacks.
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
static void tls_init_locking(void)
{
        int l_num_locks = CRYPTO_num_locks();
        g_lock_cs = (pthread_mutex_t *)OPENSSL_malloc(l_num_locks * sizeof(pthread_mutex_t));
        //g_lock_cs =(pthread_mutex_t*)malloc(        l_num_locks * sizeof(pthread_mutex_t));
        for (int i=0; i<l_num_locks; ++i)
        {
                pthread_mutex_init(&(g_lock_cs[i]),NULL);
        }
        CRYPTO_set_id_callback(pthreads_thread_id);
        CRYPTO_set_locking_callback(pthreads_locking_callback);
        CRYPTO_set_dynlock_create_callback(dyn_create_function);
        CRYPTO_set_dynlock_lock_callback(dyn_lock_function);
        CRYPTO_set_dynlock_destroy_callback(dyn_destroy_function);
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
void tls_init(void)
{
        // Initialize the OpenSSL library
        SSL_library_init();

        // Bring in and register error messages
        ERR_load_crypto_strings();
        SSL_load_error_strings();

        // TODO Deprecated???
        //SSLeay_add_tls_algorithms();
        OpenSSL_add_all_algorithms();

        // Set up for thread safety
        tls_init_locking();

        // We MUST have entropy, or else there's no point to crypto.
        if (!RAND_poll())
        {
                return;
        }

        // TODO Old method???
#if 0
        // Random seed
        if (! RAND_status())
        {
                unsigned char bytes[1024];
                for (size_t i = 0; i < sizeof(bytes); ++i)
                        bytes[i] = random() % 0xff;
                RAND_seed(bytes, sizeof(bytes));
        }
#endif
}

//: ----------------------------------------------------------------------------
//: \details: Initialize OpenSSL
//: \return:  ctx on success, NULL on failure
//: \param:   TODO
//: ----------------------------------------------------------------------------
SSL_CTX* tls_init_ctx(const std::string &a_cipher_list,
                      long a_options,
                      const std::string &a_ca_file,
                      const std::string &a_ca_path,
                      bool a_server_flag,
                      const std::string &a_tls_key_file,
                      const std::string &a_tls_crt_file)
{
        SSL_CTX *l_ctx;

        // TODO Make configurable
        if(a_server_flag)
        {
                l_ctx = SSL_CTX_new(SSLv23_server_method());
        }
        else
        {
                l_ctx = SSL_CTX_new(SSLv23_client_method());
        }
        if (l_ctx == NULL)
        {
                ERR_print_errors_fp(stderr);
                NDBG_PRINT("SSL_CTX_new Error: %s\n", ERR_error_string(ERR_get_error(), NULL));
                return NULL;
        }

        if (!a_cipher_list.empty())
        {
                if (! SSL_CTX_set_cipher_list(l_ctx, a_cipher_list.c_str()))
                {
                        NDBG_PRINT("Error cannot set m_cipher list: %s\n", a_cipher_list.c_str());
                        ERR_print_errors_fp(stderr);
                        //close_connection(con, nowP);
                        return NULL;
                }
        }

        const char *l_ca_file = NULL;
        const char *l_ca_path = NULL;
        if(!a_ca_file.empty())
        {
                l_ca_file = a_ca_file.c_str();
        }
        else if(!a_ca_path.empty())
        {
                l_ca_path = a_ca_path.c_str();
        }

        int32_t l_status;
        if(l_ca_file || l_ca_path)
        {
                l_status = SSL_CTX_load_verify_locations(l_ctx, l_ca_file, l_ca_path);
                if(1 != l_status)
                {
                        ERR_print_errors_fp(stdout);
                        NDBG_PRINT("Error performing SSL_CTX_load_verify_locations.  Reason: %s\n",
                                        ERR_error_string(ERR_get_error(), NULL));
                        SSL_CTX_free(l_ctx);
                        return NULL;
                }

                l_status = SSL_CTX_set_default_verify_paths(l_ctx);
                if(1 != l_status)
                {
                        ERR_print_errors_fp(stdout);
                        NDBG_PRINT("Error performing SSL_CTX_set_default_verify_paths.  Reason: %s\n",
                                        ERR_error_string(ERR_get_error(), NULL));
                        SSL_CTX_free(l_ctx);
                        return NULL;
                }
        }

        if(a_options)
        {
                SSL_CTX_set_options(l_ctx, a_options);
                // TODO Check return
                //long l_results = SSL_CTX_set_options(l_ctx, a_options);
                //NDBG_PRINT("Set SSL CTX options: 0x%08lX -set to: 0x%08lX \n", l_results, a_options);

        }

        if(!a_tls_crt_file.empty())
        {
                // set the local certificate from CertFile
                if(SSL_CTX_use_certificate_chain_file(l_ctx, a_tls_crt_file.c_str()) <= 0)
                {
                        NDBG_PRINT("Error performing SSL_CTX_use_certificate_file.\n");
                        ERR_print_errors_fp(stdout);
                        return NULL;
                }
        }

        if(!a_tls_key_file.empty())
        {
                // set the private key from KeyFile (may be the same as CertFile) */
                if(SSL_CTX_use_PrivateKey_file(l_ctx, a_tls_key_file.c_str(), SSL_FILETYPE_PEM) <= 0)
                {
                        NDBG_PRINT("Error performing SSL_CTX_use_PrivateKey_file.\n");
                        ERR_print_errors_fp(stdout);
                        return NULL;
                }
                // verify private key
                if(!SSL_CTX_check_private_key(l_ctx))
                {
                        NDBG_PRINT("Error performing SSL_CTX_check_private_key.\n");
                        fprintf(stdout, "Private key does not match the public certificate\n");
                        return NULL;
                }
        }


        //NDBG_PRINT("SSL_CTX_new success\n");
        return l_ctx;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
void ssl_kill_locks(void)
{
        CRYPTO_set_id_callback(NULL);
        CRYPTO_set_locking_callback(NULL);
        if(g_lock_cs)
        {
                for (int i=0; i<CRYPTO_num_locks(); ++i)
                {
                        pthread_mutex_destroy(&(g_lock_cs[i]));
                }
        }

        OPENSSL_free(g_lock_cs);
        g_lock_cs = NULL;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
typedef std::map <std::string, long>tls_options_map_t;
tls_options_map_t g_tls_options_map;
int32_t get_tls_options_str_val(const std::string a_options_str, long &ao_val)
{

        std::string l_options_str = a_options_str;

        if(g_tls_options_map.empty())
        {
                g_tls_options_map["SSL_OP_NO_SSLv2"] = SSL_OP_NO_SSLv2;
                g_tls_options_map["SSL_OP_NO_SSLv3"] = SSL_OP_NO_SSLv3;
                g_tls_options_map["SSL_OP_NO_TLSv1"] = SSL_OP_NO_TLSv1;
                g_tls_options_map["SSL_OP_NO_TLSv1_2"] = SSL_OP_NO_TLSv1_2;
                g_tls_options_map["SSL_OP_NO_TLSv1_1"] = SSL_OP_NO_TLSv1_1;
        }

        // Remove whitespace
        l_options_str.erase( std::remove_if( l_options_str.begin(), l_options_str.end(), ::isspace ), l_options_str.end() );

        ao_val = 0;

        std::string l_token;
        std::string l_delim = "|";
        size_t l_start = 0U;
        size_t l_end = l_options_str.find(l_delim);

        while(l_end != std::string::npos)
        {
                l_token = l_options_str.substr(l_start, l_end - l_start);
                l_start = l_end + l_delim.length();
                l_end = l_options_str.find(l_delim, l_start);
                //NDBG_PRINT("TOKEN: %s\n", l_token.c_str());
                tls_options_map_t::iterator i_option  = g_tls_options_map.find(l_token);
                if(i_option == g_tls_options_map.end())
                {
                        NDBG_PRINT("Error unrecognized ssl option: %s\n", l_token.c_str());
                        return STATUS_ERROR;
                }
                ao_val |= i_option->second;
        };
        l_token = l_options_str.substr(l_start, l_options_str.length() - l_start);
        //NDBG_PRINT("TOKEN: %s\n", l_token.c_str());
        tls_options_map_t::iterator i_option  = g_tls_options_map.find(l_token);
        if(i_option == g_tls_options_map.end())
        {
                NDBG_PRINT("Error unrecognized ssl option: %s\n", l_token.c_str());
                return STATUS_ERROR;
        }
        ao_val |= i_option->second;

        //NDBG_PRINT("ao_val: 0x%08lX\n", ao_val);

        return STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
const char *get_tls_info_cipher_str(SSL *a_ssl)
{
        if(!a_ssl)
        {
            return NULL;
        }
        return SSL_get_cipher_name(a_ssl);
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
int32_t get_tls_info_protocol_num(SSL *a_ssl)
{
        if(!a_ssl)
        {
            return -1;
        }
        SSL_SESSION *m_tls_session = SSL_get_session(a_ssl);
        if(!m_tls_session)
        {
                return -1;
        }
        return m_tls_session->ssl_version;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
const char *get_tls_info_protocol_str(int32_t a_version)
{
        switch(a_version)
        {
        case SSL2_VERSION:
        {
                return "SSLv2";
        }
        case SSL3_VERSION:
        {
                return "SSLv3";
        }
        case TLS1_2_VERSION:
        {
                return "TLSv1.2";
        }
        case TLS1_1_VERSION:
        {
                return "TLSv1.1";
        }
        case TLS1_VERSION:
        {
                return "TLSv1";
        }
        case DTLS1_VERSION:
        {
                return "DTLSv1";
        }
        case DTLS1_BAD_VER:
        {
                return "DTLSv1-bad";
        }
        default:
        {
                return "unknown";
        }
        }
        return NULL;
}


//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: \notes:   Based on example from "Network Security with OpenSSL" pg. 132
//: ----------------------------------------------------------------------------
int tls_cert_verify_callback_allow_self_signed(int ok, X509_STORE_CTX* store)
{
        if (!ok)
        {
                if(store)
                {
                        // TODO Can add check for depth here.
                        //int depth = X509_STORE_CTX_get_error_depth(store);

                        int err = X509_STORE_CTX_get_error(store);
                        if ((err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
                            (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN))
                        {
                                // Return success despite self-signed
                                return 1;
                        }
                        else
                        {
                                sprintf(gts_last_tls_error, "tls_cert_verify_callback_allow_self_signed Error[%d].  Reason: %s",
                                      err, X509_verify_cert_error_string(err));
                                //NDBG_PRINT("tls_cert_verify_callback_allow_self_signed Error[%d].  Reason: %s\n",
                                //      err, X509_verify_cert_error_string(err));
                        }
                }
        }
        return ok;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: \notes:   Based on example from "Network Security with OpenSSL" pg. 132
//: ----------------------------------------------------------------------------
int tls_cert_verify_callback(int ok, X509_STORE_CTX* store)
{
        if(ok)
        {
                return ok;
        }

        if(store)
        {
                // TODO Can add check for depth here.
                //int depth = X509_STORE_CTX_get_error_depth(store);
                int err = X509_STORE_CTX_get_error(store);
                sprintf(gts_last_tls_error, "Error[%d].  Reason: %s", err, X509_verify_cert_error_string(err));
                //NDBG_PRINT("Error[%d].  Reason: %s\n", err, X509_verify_cert_error_string(err));
        }
        return ok;
}

//: ----------------------------------------------------------------------------
//: \details: Return an array of (RFC 6125 coined) DNS-IDs and CN-IDs in a x509
//:           certificate
//: \return:  TODO
//: \param:   TODO
//: ----------------------------------------------------------------------------
bool tls_x509_get_ids(X509* x509, std::vector<std::string>& ids)
{
        if (!x509)
                return false;

        // First, the DNS-IDs (dNSName entries in the subjectAltName extension)
        GENERAL_NAMES* names =
                (GENERAL_NAMES*)X509_get_ext_d2i(x509, NID_subject_alt_name, NULL, NULL);
        if (names)
        {
                std::string san;
                for (int i = 0; i < sk_GENERAL_NAME_num(names); i++)
                {
                        GENERAL_NAME* name = sk_GENERAL_NAME_value(names, i);

                        if (name->type == GEN_DNS)
                        {
                                san.assign(reinterpret_cast<char*>(ASN1_STRING_data(name->d.uniformResourceIdentifier)),
                                           ASN1_STRING_length(name->d.uniformResourceIdentifier));
                                if (!san.empty())
                                        ids.push_back(san);
                        }
                }
        }

        if (names)
                sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

        // Second, the CN-IDs (commonName attributes in the subject DN)
        X509_NAME* subj = X509_get_subject_name(x509);
        int i = -1;
        while ((i = X509_NAME_get_index_by_NID(subj, NID_commonName, i)) != -1)
        {
                ASN1_STRING* name = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subj, i));

                std::string dn(reinterpret_cast<char*>(ASN1_STRING_data(name)),
                               ASN1_STRING_length(name));
                if (!dn.empty())
                        ids.push_back(dn);
        }

        return ids.empty() ? false : true;
}

//: ----------------------------------------------------------------------------
//: Check host name
//: Based on example from:
//: "Network Security with OpenSSL" pg. 135-136
//: Returns 0 on Success, -1 on Failure
//: ----------------------------------------------------------------------------
static int validate_server_certificate_hostname(X509* a_cert, const char* a_host)
{
        typedef std::vector <std::string> cert_name_list_t;
        cert_name_list_t l_cert_name_list;
        bool l_get_ids_status = false;

        l_get_ids_status = tls_x509_get_ids(a_cert, l_cert_name_list);
        if(!l_get_ids_status)
        {
                // No names found bail out
                //NDBG_PRINT("LABEL[%s]: tls_x509_get_ids returned no names.\n", a_host);
                return -1;
        }

        for(uint32_t i_name = 0; i_name < l_cert_name_list.size(); ++i_name)
        {
                if(Curl_cert_hostcheck(l_cert_name_list[i_name].c_str(), a_host))
                {
                        return 0;
                }
        }

        //NDBG_PRINT("LABEL[%s]: Error hostname match failed.\n", a_host);
        return -1;
}

//: ----------------------------------------------------------------------------
//: \details: TODO
//: \return:  TODO
//: \param:   TODO
//: \notes:   Based on example from "Network Security with OpenSSL" pg. 132
//: ----------------------------------------------------------------------------
int32_t validate_server_certificate(SSL *a_tls, const char* a_host, bool a_disallow_self_signed)
{
        X509* l_cert = NULL;
        //NDBG_PRINT("a_host: %s\n", a_host);
        // Get certificate
        l_cert = SSL_get_peer_certificate(a_tls);
        if(NULL == l_cert)
        {
                //NDBG_PRINT("LABEL[%s]: SSL_get_peer_certificate error.  tls: %p", a_host, (void *)a_tls);
                return -1;
        }
        // Example of displaying cert
        //X509_print_fp(stdout, l_cert);
        // Check host name
        if(a_host)
        {
                int l_status = 0;
                l_status = validate_server_certificate_hostname(l_cert, a_host);
                if(0 != l_status)
                {
                        sprintf(gts_last_tls_error, "Error[%d].  Reason: %s", -1, "hostname check failed");
                        if(NULL != l_cert)
                        {
                                X509_free(l_cert);
                                l_cert = NULL;
                        }
                        return -1;
                }
        }
        if(NULL != l_cert)
        {
                X509_free(l_cert);
                l_cert = NULL;
        }
#if 0
        long l_tls_verify_result;
        l_tls_verify_result = SSL_get_verify_result(a_tls);
        if(l_tls_verify_result != X509_V_OK)
        {

                // Check for self-signed failures
                //a_disallow_self_signed
                if(a_disallow_self_signed == false)
                {
                        if ((l_tls_verify_result == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) ||
                            (l_tls_verify_result == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN))
                        {
                                sprintf(gts_last_tls_error, "Error[%d].  Reason: %s", -1, "self-signed certificate");
                                // No errors return success(0)
                                if(NULL != l_cert)
                                {
                                        X509_free(l_cert);
                                        l_cert = NULL;
                                }
                                //NDBG_PRINT("Error self-signed.\n");
                                return -1;
                        }
                }

                //NDBG_PRINT("LABEL[%s]: SSL_get_verify_result[%ld]: %s",
                //      a_host,
                //      l_tls_verify_result,
                //      X509_verify_cert_error_string(l_tls_verify_result));
                if(NULL != l_cert)
                {
                        X509_free(l_cert);
                        l_cert = NULL;
                }
                //NDBG_PRINT("Error\n");
                return -1;
        }
#endif

        // No errors return success(0)
        return 0;
}

} //namespace ns_hlx {

