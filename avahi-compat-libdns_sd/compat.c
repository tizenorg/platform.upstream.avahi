/* $Id$ */

/***
  This file is part of avahi.
 
  avahi is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.
 
  avahi is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General
  Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with avahi; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <netinet/in.h>
#include <fcntl.h>

#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/domain.h>
#include <avahi-common/alternative.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>

#include "warn.h"
#include "dns_sd.h"

enum {
    COMMAND_COME_AGAIN = 0,
    COMMAND_POLL = 'p',
    COMMAND_QUIT = 'q',
    COMMAND_POLL_DONE = 'P',
    COMMAND_POLL_FAILED = 'F'
};

struct _DNSServiceRef_t {
    int n_ref;
    
    AvahiSimplePoll *simple_poll;

    int thread_fd, main_fd;

    pthread_t thread;
    int thread_running;

    pthread_mutex_t mutex;

    void *context;
    DNSServiceBrowseReply service_browser_callback;
    DNSServiceResolveReply service_resolver_callback;
    DNSServiceDomainEnumReply domain_browser_callback;
    DNSServiceRegisterReply service_register_callback;

    AvahiClient *client;
    AvahiServiceBrowser *service_browser;
    AvahiServiceResolver *service_resolver;
    AvahiDomainBrowser *domain_browser;

    char *service_name, *service_name_chosen, *service_regtype, *service_domain, *service_host;
    uint16_t service_port;
    AvahiIfIndex service_interface;
    AvahiStringList *service_txt;

    AvahiEntryGroup *entry_group;
};

#define ASSERT_SUCCESS(r) { int __ret = (r); assert(__ret == 0); }

static DNSServiceErrorType map_error(int error) {
    switch (error) {
        case AVAHI_OK :
            return kDNSServiceErr_NoError;
            
        case AVAHI_ERR_BAD_STATE :
            return kDNSServiceErr_BadState;
            
        case AVAHI_ERR_INVALID_HOST_NAME:
        case AVAHI_ERR_INVALID_DOMAIN_NAME:
        case AVAHI_ERR_INVALID_TTL:
        case AVAHI_ERR_IS_PATTERN:
        case AVAHI_ERR_INVALID_RECORD:
        case AVAHI_ERR_INVALID_SERVICE_NAME:
        case AVAHI_ERR_INVALID_SERVICE_TYPE:
        case AVAHI_ERR_INVALID_PORT:
        case AVAHI_ERR_INVALID_KEY:
        case AVAHI_ERR_INVALID_ADDRESS:
        case AVAHI_ERR_INVALID_SERVICE_SUBTYPE:
            return kDNSServiceErr_BadParam;


        case AVAHI_ERR_LOCAL_COLLISION:
            return kDNSServiceErr_NameConflict;

        case AVAHI_ERR_TOO_MANY_CLIENTS:
        case AVAHI_ERR_TOO_MANY_OBJECTS:
        case AVAHI_ERR_TOO_MANY_ENTRIES:
        case AVAHI_ERR_ACCESS_DENIED:
            return kDNSServiceErr_Refused;

        case AVAHI_ERR_INVALID_OPERATION:
        case AVAHI_ERR_INVALID_OBJECT:
            return kDNSServiceErr_Invalid;

        case AVAHI_ERR_NO_MEMORY:
            return kDNSServiceErr_NoMemory;

        case AVAHI_ERR_INVALID_INTERFACE:
        case AVAHI_ERR_INVALID_PROTOCOL:
            return kDNSServiceErr_BadInterfaceIndex;
        
        case AVAHI_ERR_INVALID_FLAGS:
            return kDNSServiceErr_BadFlags;
            
        case AVAHI_ERR_NOT_FOUND:
            return kDNSServiceErr_NoSuchName;
            
        case AVAHI_ERR_VERSION_MISMATCH:
            return kDNSServiceErr_Incompatible;

        case AVAHI_ERR_NO_NETWORK:
        case AVAHI_ERR_OS:
        case AVAHI_ERR_INVALID_CONFIG:
        case AVAHI_ERR_TIMEOUT:
        case AVAHI_ERR_DBUS_ERROR:
        case AVAHI_ERR_NOT_CONNECTED:
        case AVAHI_ERR_NO_DAEMON:
            break;

    }

    return kDNSServiceErr_Unknown;
}

static const char *add_trailing_dot(const char *s, char *buf, size_t buf_len) {
    if (!s)
        return NULL;

    if (*s == 0)
        return s;

    if (s[strlen(s)-1] == '.')
        return s;

    snprintf(buf, buf_len, "%s.", s);
    return buf;
}

static int read_command(int fd) {
    ssize_t r;
    char command;

    assert(fd >= 0);
    
    if ((r = read(fd, &command, 1)) != 1) {

        if (errno == EAGAIN)
            return COMMAND_COME_AGAIN;
        
        fprintf(stderr, __FILE__": read() failed: %s\n", r < 0 ? strerror(errno) : "EOF");
        return -1;
    }

    return command;
}

static int write_command(int fd, char reply) {
    assert(fd >= 0);

    if (write(fd, &reply, 1) != 1) {
        fprintf(stderr, __FILE__": write() failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int set_nonblock(int fd) {
    int n;

    assert(fd >= 0);

    if ((n = fcntl(fd, F_GETFL)) < 0)
        return -1;

    if (n & O_NONBLOCK)
        return 0;

    return fcntl(fd, F_SETFL, n|O_NONBLOCK);
}


static int poll_func(struct pollfd *ufds, unsigned int nfds, int timeout, void *userdata) {
    DNSServiceRef sdref = userdata;
    int ret;
    
    assert(sdref);
    
    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));

/*     fprintf(stderr, "pre-syscall\n"); */
    ret = poll(ufds, nfds, timeout);
/*     fprintf(stderr, "post-syscall\n"); */
    
    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));

    return ret;
}

static void * thread_func(void *data) {
    DNSServiceRef sdref = data;
    sigset_t mask;

    sigfillset(&mask);
    pthread_sigmask(SIG_BLOCK, &mask, NULL);
    
    sdref->thread = pthread_self();
    sdref->thread_running = 1;

    for (;;) {
        char command;

        if ((command = read_command(sdref->thread_fd)) < 0)
            break;

/*         fprintf(stderr, "Command: %c\n", command); */
        
        switch (command) {

            case COMMAND_POLL: {
                int ret;

                ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));

                for (;;) {
                    errno = 0;
                    
                    if ((ret = avahi_simple_poll_run(sdref->simple_poll)) < 0) {

                        if (errno == EINTR)
                            continue;
                        
                        fprintf(stderr, __FILE__": avahi_simple_poll_run() failed: %s\n", strerror(errno));
                    }

                    break;
                }

                ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));

                if (write_command(sdref->thread_fd, ret < 0 ? COMMAND_POLL_FAILED : COMMAND_POLL_DONE) < 0)
                    break;
                
                break;
            }

            case COMMAND_QUIT:
                return NULL;

            case COMMAND_COME_AGAIN:
                break;
        }
        
    }

    return NULL;
}

static DNSServiceRef sdref_new(void) {
    int fd[2] = { -1, -1 };
    DNSServiceRef sdref = NULL;
    pthread_mutexattr_t mutex_attr;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) < 0)
        goto fail;

    if (!(sdref = avahi_new(struct _DNSServiceRef_t, 1)))
        goto fail;

    sdref->n_ref = 1;
    sdref->thread_fd = fd[0];
    sdref->main_fd = fd[1];

    set_nonblock(sdref->main_fd);

    sdref->client = NULL;
    sdref->service_browser = NULL;
    sdref->service_resolver = NULL;
    sdref->domain_browser = NULL;
    sdref->entry_group = NULL;

    sdref->service_name = sdref->service_name_chosen = sdref->service_regtype = sdref->service_domain = sdref->service_host = NULL;
    sdref->service_txt = NULL;

    ASSERT_SUCCESS(pthread_mutexattr_init(&mutex_attr));
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    ASSERT_SUCCESS(pthread_mutex_init(&sdref->mutex, &mutex_attr));

    sdref->thread_running = 0;

    if (!(sdref->simple_poll = avahi_simple_poll_new()))
        goto fail;

    avahi_simple_poll_set_func(sdref->simple_poll, poll_func, sdref);

    /* Start simple poll */
    if (avahi_simple_poll_prepare(sdref->simple_poll, -1) < 0)
        goto fail;

    /* Queue an initial POLL command for the thread */
    if (write_command(sdref->main_fd, COMMAND_POLL) < 0)
        goto fail;
    
    if (pthread_create(&sdref->thread, NULL, thread_func, sdref) != 0)
        goto fail;

    sdref->thread_running = 1;
    
    return sdref;

fail:

    if (sdref)
        DNSServiceRefDeallocate(sdref);

    return NULL;
}

static void sdref_free(DNSServiceRef sdref) {
    assert(sdref);
    
    if (sdref->thread_running) {
        ASSERT_SUCCESS(write_command(sdref->main_fd, COMMAND_QUIT));
        avahi_simple_poll_wakeup(sdref->simple_poll);
        ASSERT_SUCCESS(pthread_join(sdref->thread, NULL));
    }

    if (sdref->client)
        avahi_client_free(sdref->client);

    if (sdref->simple_poll)
        avahi_simple_poll_free(sdref->simple_poll);


    if (sdref->thread_fd >= 0)
        close(sdref->thread_fd);

    if (sdref->main_fd >= 0)
        close(sdref->main_fd);

    ASSERT_SUCCESS(pthread_mutex_destroy(&sdref->mutex));

    avahi_free(sdref->service_name);
    avahi_free(sdref->service_name_chosen);
    avahi_free(sdref->service_regtype);
    avahi_free(sdref->service_domain);
    avahi_free(sdref->service_host);

    avahi_string_list_free(sdref->service_txt);
    
    avahi_free(sdref);
}

static void sdref_ref(DNSServiceRef sdref) {
    assert(sdref);
    assert(sdref->n_ref >= 1);

    sdref->n_ref++;
}

static void sdref_unref(DNSServiceRef sdref) {
    assert(sdref);
    assert(sdref->n_ref >= 1);

    if (--(sdref->n_ref) <= 0)
        sdref_free(sdref);
}

int DNSSD_API DNSServiceRefSockFD(DNSServiceRef sdref) {
    assert(sdref);
    assert(sdref->n_ref >= 1);

    AVAHI_WARN_LINKAGE;
    
    return sdref->main_fd;
}

DNSServiceErrorType DNSSD_API DNSServiceProcessResult(DNSServiceRef sdref) {
    DNSServiceErrorType ret = kDNSServiceErr_Unknown;
    int t;

    assert(sdref);
    assert(sdref->n_ref >= 1);
    
    AVAHI_WARN_LINKAGE;

    sdref_ref(sdref);

    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));
    
    /* Cleanup notification socket */
    if ((t = read_command(sdref->main_fd)) != COMMAND_POLL_DONE) {
        if (t == COMMAND_COME_AGAIN)
            ret = kDNSServiceErr_Unknown;
        goto finish;
    }
    
    if (avahi_simple_poll_dispatch(sdref->simple_poll) < 0)
        goto finish;

    if (sdref->n_ref > 1) /* Perhaps we should die */

        /* Dispatch events */
        if (avahi_simple_poll_prepare(sdref->simple_poll, -1) < 0)
            goto finish;

    if (sdref->n_ref > 1)

        /* Request the poll */
        if (write_command(sdref->main_fd, COMMAND_POLL) < 0)
            goto finish;
    
    ret = kDNSServiceErr_NoError;
    
finish:

    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));

    sdref_unref(sdref);
    
    return ret;
}

void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef sdref) {
    assert(sdref);
    assert(sdref->n_ref >= 1);

    AVAHI_WARN_LINKAGE;

    sdref_unref(sdref);
}

static void service_browser_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AvahiLookupResultFlags flags,
    void *userdata) {

    DNSServiceRef sdref = userdata;
    char type_fixed[AVAHI_DOMAIN_NAME_MAX], domain_fixed[AVAHI_DOMAIN_NAME_MAX];
    assert(b);
    assert(sdref);
    assert(sdref->n_ref >= 1);

    type = add_trailing_dot(type, type_fixed, sizeof(type_fixed));
    domain  = add_trailing_dot(domain, domain_fixed, sizeof(domain_fixed));
    
    switch (event) {
        case AVAHI_BROWSER_NEW:
            sdref->service_browser_callback(sdref, kDNSServiceFlagsAdd, interface, kDNSServiceErr_NoError, name, type, domain, sdref->context);
            break;

        case AVAHI_BROWSER_REMOVE:
            sdref->service_browser_callback(sdref, 0, interface, kDNSServiceErr_NoError, name, type, domain, sdref->context);
            break;

        case AVAHI_BROWSER_FAILURE:
            sdref->service_browser_callback(sdref, 0, interface, map_error(avahi_client_errno(sdref->client)), NULL, NULL, NULL, sdref->context);
            break;
            
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
        case AVAHI_BROWSER_ALL_FOR_NOW:
            break;
    }
}

static void generic_client_callback(AvahiClient *s, AvahiClientState state, void* userdata) {
    DNSServiceRef sdref = userdata;

    assert(s);
    assert(sdref);
    assert(sdref->n_ref >= 1);

    switch (state) {
        case AVAHI_CLIENT_DISCONNECTED: {

            if (sdref->service_browser_callback)
                sdref->service_browser_callback(sdref, 0, 0, kDNSServiceErr_Unknown, NULL, NULL, NULL, sdref->context);
            else if (sdref->service_resolver_callback)
                sdref->service_resolver_callback(sdref, 0, 0, kDNSServiceErr_Unknown, NULL, NULL, 0, 0, NULL, sdref->context);
            else if (sdref->domain_browser_callback)
                sdref->domain_browser_callback(sdref, 0, 0, kDNSServiceErr_Unknown, NULL, sdref->context);

            break;
        }

        case AVAHI_CLIENT_S_RUNNING:
        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_INVALID:
        case AVAHI_CLIENT_S_REGISTERING:
            break;
    }
}

DNSServiceErrorType DNSSD_API DNSServiceBrowse(
    DNSServiceRef *ret_sdref,
    DNSServiceFlags flags,
    uint32_t interface,
    const char *regtype,
    const char *domain,
    DNSServiceBrowseReply callback,
    void *context) {

    DNSServiceErrorType ret = kDNSServiceErr_Unknown;
    int error;
    DNSServiceRef sdref = NULL;
    AvahiIfIndex ifindex;
    
    AVAHI_WARN_LINKAGE;
    
    assert(ret_sdref);
    assert(regtype);
    assert(domain);
    assert(callback);

    if (interface == kDNSServiceInterfaceIndexLocalOnly || flags != 0) {
        AVAHI_WARN_UNSUPPORTED;
        return kDNSServiceErr_Unsupported;
    }

    if (!(sdref = sdref_new()))
        return kDNSServiceErr_Unknown;

    sdref->context = context;
    sdref->service_browser_callback = callback;

    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));
    
    if (!(sdref->client = avahi_client_new(avahi_simple_poll_get(sdref->simple_poll), generic_client_callback, sdref, &error))) {
        ret =  map_error(error);
        goto finish;
    }

    ifindex = interface == kDNSServiceInterfaceIndexAny ? AVAHI_IF_UNSPEC : (AvahiIfIndex) interface;
    
    if (!(sdref->service_browser = avahi_service_browser_new(sdref->client, ifindex, AVAHI_PROTO_UNSPEC, regtype, domain, 0, service_browser_callback, sdref))) {
        ret = map_error(avahi_client_errno(sdref->client));
        goto finish;
    }
    
    ret = kDNSServiceErr_NoError;
    *ret_sdref = sdref;
                                                              
finish:

    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));
    
    if (ret != kDNSServiceErr_NoError)
        DNSServiceRefDeallocate(sdref);

    return ret;
}

static void service_resolver_callback(
    AvahiServiceResolver *r,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *a,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    void *userdata) {

    DNSServiceRef sdref = userdata;

    assert(r);
    assert(sdref);
    assert(sdref->n_ref >= 1);

    switch (event) {
        case AVAHI_RESOLVER_FOUND: {

            char host_name_fixed[AVAHI_DOMAIN_NAME_MAX];
            char full_name[AVAHI_DOMAIN_NAME_MAX];
            int ret;
            char *p = NULL;
            size_t l = 0;

            host_name = add_trailing_dot(host_name, host_name_fixed, sizeof(host_name_fixed));

            if ((p = avahi_new0(char, (l = avahi_string_list_serialize(txt, NULL, 0))+1)))
                avahi_string_list_serialize(txt, p, l);

            ret = avahi_service_name_join(full_name, sizeof(full_name), name, type, domain);
            assert(ret == AVAHI_OK);

            strcat(full_name, ".");
            
            sdref->service_resolver_callback(sdref, 0, interface, kDNSServiceErr_NoError, full_name, host_name, htons(port), l, p, sdref->context);

            avahi_free(p);
            break;
        }

        case AVAHI_RESOLVER_FAILURE:
            sdref->service_resolver_callback(sdref, 0, interface, map_error(avahi_client_errno(sdref->client)), NULL, NULL, 0, 0, NULL, sdref->context);
            break;
    }
}

DNSServiceErrorType DNSSD_API DNSServiceResolve(
    DNSServiceRef *ret_sdref,
    DNSServiceFlags flags,
    uint32_t interface,
    const char *name,
    const char *regtype,
    const char *domain,
    DNSServiceResolveReply callback,
    void *context) {

    DNSServiceErrorType ret = kDNSServiceErr_Unknown;
    int error;
    DNSServiceRef sdref = NULL;
    AvahiIfIndex ifindex;

    AVAHI_WARN_LINKAGE;

    assert(ret_sdref);
    assert(name);
    assert(regtype);
    assert(domain);
    assert(callback);

    if (interface == kDNSServiceInterfaceIndexLocalOnly || flags != 0) {
        AVAHI_WARN_UNSUPPORTED;
        return kDNSServiceErr_Unsupported;
    }

    if (!(sdref = sdref_new()))
        return kDNSServiceErr_Unknown;

    sdref->context = context;
    sdref->service_resolver_callback = callback;

    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));
    
    if (!(sdref->client = avahi_client_new(avahi_simple_poll_get(sdref->simple_poll), generic_client_callback, sdref, &error))) {
        ret =  map_error(error);
        goto finish;
    }

    ifindex = interface == kDNSServiceInterfaceIndexAny ? AVAHI_IF_UNSPEC : (AvahiIfIndex) interface;
    
    if (!(sdref->service_resolver = avahi_service_resolver_new(sdref->client, ifindex, AVAHI_PROTO_UNSPEC, name, regtype, domain, AVAHI_PROTO_UNSPEC, 0, service_resolver_callback, sdref))) {
        ret = map_error(avahi_client_errno(sdref->client));
        goto finish;
    }
    

    ret = kDNSServiceErr_NoError;
    *ret_sdref = sdref;
                                                              
finish:

    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));
    
    if (ret != kDNSServiceErr_NoError)
        DNSServiceRefDeallocate(sdref);

    return ret;
}

int DNSSD_API DNSServiceConstructFullName (
    char *fullName,
    const char *service,   
    const char *regtype,
    const char *domain) {

    AVAHI_WARN_LINKAGE;

    assert(fullName);
    assert(regtype);
    assert(domain);

    if (avahi_service_name_join(fullName, kDNSServiceMaxDomainName, service, regtype, domain) < 0)
        return -1;
    
    return 0;
}

static void domain_browser_callback(
    AvahiDomainBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *domain,
    AvahiLookupResultFlags flags,
    void *userdata) {

    DNSServiceRef sdref = userdata;
    static char domain_fixed[AVAHI_DOMAIN_NAME_MAX];

    assert(b);
    assert(sdref);
    assert(sdref->n_ref >= 1);

    domain  = add_trailing_dot(domain, domain_fixed, sizeof(domain_fixed));

    switch (event) {
        case AVAHI_BROWSER_NEW:
            sdref->domain_browser_callback(sdref, kDNSServiceFlagsAdd, interface, kDNSServiceErr_NoError, domain, sdref->context);
            break;

        case AVAHI_BROWSER_REMOVE:
            sdref->domain_browser_callback(sdref, 0, interface, kDNSServiceErr_NoError, domain, sdref->context);
            break;

        case AVAHI_BROWSER_FAILURE:
            sdref->domain_browser_callback(sdref, 0, interface, map_error(avahi_client_errno(sdref->client)), domain, sdref->context);
            break;
            
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
        case AVAHI_BROWSER_ALL_FOR_NOW:
            break;
    }
}

DNSServiceErrorType DNSSD_API DNSServiceEnumerateDomains(
    DNSServiceRef *ret_sdref,
    DNSServiceFlags flags,
    uint32_t interface,
    DNSServiceDomainEnumReply callback,
    void *context) {

    DNSServiceErrorType ret = kDNSServiceErr_Unknown;
    int error;
    DNSServiceRef sdref = NULL;
    AvahiIfIndex ifindex;

    AVAHI_WARN_LINKAGE;

    assert(ret_sdref);
    assert(callback);

    if (interface == kDNSServiceInterfaceIndexLocalOnly ||
        (flags != kDNSServiceFlagsBrowseDomains &&  flags != kDNSServiceFlagsRegistrationDomains)) {
        AVAHI_WARN_UNSUPPORTED;
        return kDNSServiceErr_Unsupported;
    }

    if (!(sdref = sdref_new()))
        return kDNSServiceErr_Unknown;

    sdref->context = context;
    sdref->domain_browser_callback = callback;

    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));
    
    if (!(sdref->client = avahi_client_new(avahi_simple_poll_get(sdref->simple_poll), generic_client_callback, sdref, &error))) {
        ret =  map_error(error);
        goto finish;
    }

    ifindex = interface == kDNSServiceInterfaceIndexAny ? AVAHI_IF_UNSPEC : (AvahiIfIndex) interface;
    
    if (!(sdref->domain_browser = avahi_domain_browser_new(sdref->client, ifindex, AVAHI_PROTO_UNSPEC, "local",
                                                           flags == kDNSServiceFlagsRegistrationDomains ? AVAHI_DOMAIN_BROWSER_REGISTER : AVAHI_DOMAIN_BROWSER_BROWSE,
                                                           0, domain_browser_callback, sdref))) {
        ret = map_error(avahi_client_errno(sdref->client));
        goto finish;
    }
    
    ret = kDNSServiceErr_NoError;
    *ret_sdref = sdref;
                                                              
finish:

    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));
    
    if (ret != kDNSServiceErr_NoError)
        DNSServiceRefDeallocate(sdref);

    return ret;
}

static void reg_report_error(DNSServiceRef sdref, DNSServiceErrorType error) {
    char regtype_fixed[AVAHI_DOMAIN_NAME_MAX], domain_fixed[AVAHI_DOMAIN_NAME_MAX];
    const char *regtype, *domain;
    assert(sdref);
    assert(sdref->n_ref >= 1);

    assert(sdref->service_register_callback);

    regtype = add_trailing_dot(sdref->service_regtype, regtype_fixed, sizeof(regtype_fixed));
    domain = add_trailing_dot(sdref->service_domain, domain_fixed, sizeof(domain_fixed));
    
    sdref->service_register_callback(
        sdref, 0, error,
        sdref->service_name_chosen ? sdref->service_name_chosen : sdref->service_name,
        regtype,
        domain,
        sdref->context);
}

static int reg_create_service(DNSServiceRef sdref) {
    int ret;
    const char *real_type;
    
    assert(sdref);
    assert(sdref->n_ref >= 1);

    real_type = avahi_get_type_from_subtype(sdref->service_regtype);
    
    if ((ret = avahi_entry_group_add_service_strlst(
        sdref->entry_group,
        sdref->service_interface,
        AVAHI_PROTO_UNSPEC,
        0,
        sdref->service_name_chosen,
        real_type ? real_type : sdref->service_regtype,
        sdref->service_domain,
        sdref->service_host,
        sdref->service_port,
        sdref->service_txt)) < 0)
        return ret;

    
    if (real_type) {
        /* Create a subtype entry */

        if (avahi_entry_group_add_service_subtype(
                sdref->entry_group,
                sdref->service_interface,
                AVAHI_PROTO_UNSPEC,
                0,
                sdref->service_name_chosen,
                real_type,
                sdref->service_domain,
                sdref->service_regtype) < 0)
            return ret;

    }

    if ((ret = avahi_entry_group_commit(sdref->entry_group)) < 0)
        return ret;

    return 0;
}

static void reg_client_callback(AvahiClient *s, AvahiClientState state, void* userdata) {
    DNSServiceRef sdref = userdata;

    assert(s);
    assert(sdref);
    assert(sdref->n_ref >= 1);

    /* We've not been setup completely */
    if (!sdref->entry_group)
        return;
    
    switch (state) {
        case AVAHI_CLIENT_DISCONNECTED: 

            reg_report_error(sdref, kDNSServiceErr_NoError);
            break;
        
        case AVAHI_CLIENT_S_RUNNING: {
            int ret;

            if (!sdref->service_name) {
                const char *n;
                /* If the service name is taken from the host name, copy that */

                avahi_free(sdref->service_name_chosen);
                sdref->service_name_chosen = NULL;

                if (!(n = avahi_client_get_host_name(sdref->client))) {
                    reg_report_error(sdref, map_error(avahi_client_errno(sdref->client)));
                    return;
                }

                if (!(sdref->service_name_chosen = avahi_strdup(n))) {
                    reg_report_error(sdref, kDNSServiceErr_NoMemory);
                    return;
                }
            }
            
            /* Register the service */

            if ((ret = reg_create_service(sdref)) < 0) {
                reg_report_error(sdref, map_error(ret));
                return;
            }
            
            break;
        }
            
        case AVAHI_CLIENT_S_COLLISION:

            /* Remove our entry */
            avahi_entry_group_reset(sdref->entry_group);
            
            break;

        case AVAHI_CLIENT_S_INVALID:
        case AVAHI_CLIENT_S_REGISTERING:
            /* Ignore */
            break;
    }

}

static void reg_entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
    DNSServiceRef sdref = userdata;

    assert(g);

    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED:
            /* Inform the user */
            reg_report_error(sdref, kDNSServiceErr_NoError);

            break;

        case AVAHI_ENTRY_GROUP_COLLISION: {
            char *n;
            int ret;
            
            /* Remove our entry */
            avahi_entry_group_reset(sdref->entry_group);

            assert(sdref->service_name_chosen);

            /* Pick a new name */
            if (!(n = avahi_alternative_service_name(sdref->service_name_chosen))) {
                reg_report_error(sdref, kDNSServiceErr_NoMemory);
                return;
            }
            avahi_free(sdref->service_name_chosen);
            sdref->service_name_chosen = n;

            /* Register the service with that new name */
            if ((ret = reg_create_service(sdref)) < 0) {
                reg_report_error(sdref, map_error(ret));
                return;
            }
            
            break;
        }

        case AVAHI_ENTRY_GROUP_REGISTERING:
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
            /* Ignore */
            break;
    }
}

DNSServiceErrorType DNSSD_API DNSServiceRegister (
    DNSServiceRef *ret_sdref,
    DNSServiceFlags flags,
    uint32_t interface,
    const char *name,        
    const char *regtype,
    const char *domain,      
    const char *host,        
    uint16_t port,
    uint16_t txtLen,
    const void *txtRecord,   
    DNSServiceRegisterReply callback,    
    void *context) {

    DNSServiceErrorType ret = kDNSServiceErr_Unknown;
    int error;
    DNSServiceRef sdref = NULL;

    AVAHI_WARN_LINKAGE;

    assert(ret_sdref);
    assert(callback);
    assert(regtype);

    if (interface == kDNSServiceInterfaceIndexLocalOnly || flags) {
        AVAHI_WARN_UNSUPPORTED;
        return kDNSServiceErr_Unsupported;
    }

    if (!(sdref = sdref_new()))
        return kDNSServiceErr_Unknown;

    sdref->context = context;
    sdref->service_register_callback = callback;

    sdref->service_name = avahi_strdup(name);
    sdref->service_regtype = regtype ? avahi_normalize_name_strdup(regtype) : NULL;
    sdref->service_domain = domain ? avahi_normalize_name_strdup(domain) : NULL;
    sdref->service_host = host ? avahi_normalize_name_strdup(host) : NULL;
    sdref->service_interface = interface == kDNSServiceInterfaceIndexAny ? AVAHI_IF_UNSPEC : (AvahiIfIndex) interface;
    sdref->service_port = ntohs(port);
    sdref->service_txt = txtRecord && txtLen > 0 ? avahi_string_list_parse(txtRecord, txtLen) : NULL;

    /* Some OOM checking would be cool here */
    
    ASSERT_SUCCESS(pthread_mutex_lock(&sdref->mutex));
    
    if (!(sdref->client = avahi_client_new(avahi_simple_poll_get(sdref->simple_poll), reg_client_callback, sdref, &error))) {
        ret =  map_error(error);
        goto finish;
    }

    if (!sdref->service_domain) {
        const char *d;

        if (!(d = avahi_client_get_domain_name(sdref->client))) {
            ret = map_error(avahi_client_errno(sdref->client));
            goto finish;
        }

        if (!(sdref->service_domain = avahi_strdup(d))) {
            ret = kDNSServiceErr_NoMemory;
            goto finish;
        }
    }

    if (!(sdref->entry_group = avahi_entry_group_new(sdref->client, reg_entry_group_callback, sdref))) {
        ret = map_error(avahi_client_errno(sdref->client));
        goto finish;
    }

    if (avahi_client_get_state(sdref->client) == AVAHI_CLIENT_S_RUNNING) {
        const char *n;

        if (sdref->service_name)
            n = sdref->service_name;
        else {
            if (!(n = avahi_client_get_host_name(sdref->client))) {
                ret = map_error(avahi_client_errno(sdref->client));
                goto finish;
            }
        }

        if (!(sdref->service_name_chosen = avahi_strdup(n))) {
            ret = kDNSServiceErr_NoMemory;
            goto finish;
        }

            
        if ((error = reg_create_service(sdref)) < 0) {
            ret = map_error(error);
            goto finish;
        }
    }
    
    ret = kDNSServiceErr_NoError;
    *ret_sdref = sdref;
                                                              
finish:

    ASSERT_SUCCESS(pthread_mutex_unlock(&sdref->mutex));
    
    if (ret != kDNSServiceErr_NoError)
        DNSServiceRefDeallocate(sdref);

    return ret;
}

