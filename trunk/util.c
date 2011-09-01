/*****************************************************************************
 * util.c: Utils for the multicat suite
 *****************************************************************************
 * Copyright (C) 2004, 2009, 2011 VideoLAN
 * $Id: util.c 27 2009-10-20 19:15:04Z massiot $
 *
 * Authors: Christophe Massiot <massiot@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <sys/mman.h>
#include <netdb.h>

#include "util.h"

/*****************************************************************************
 * Local declarations
 *****************************************************************************/
#define MAX_MSG 1024
#define PSZ_AUX_EXT "aux"
#define PSZ_TS_EXT "ts"

int i_verbose = VERB_DBG;

/*****************************************************************************
 * msg_Info
 *****************************************************************************/
void msg_Info( void *_unused, const char *psz_format, ... )
{
    if ( i_verbose >= VERB_INFO )
    {
        va_list args;
        char psz_fmt[MAX_MSG];
        va_start( args, psz_format );

        snprintf( psz_fmt, MAX_MSG, "info: %s\n", psz_format );
        vfprintf( stderr, psz_fmt, args );
    }
}

/*****************************************************************************
 * msg_Err
 *****************************************************************************/
void msg_Err( void *_unused, const char *psz_format, ... )
{
    va_list args;
    char psz_fmt[MAX_MSG];
    va_start( args, psz_format );

    snprintf( psz_fmt, MAX_MSG, "error: %s\n", psz_format );
    vfprintf( stderr, psz_fmt, args );
}

/*****************************************************************************
 * msg_Warn
 *****************************************************************************/
void msg_Warn( void *_unused, const char *psz_format, ... )
{
    if ( i_verbose >= VERB_WARN )
    {
        va_list args;
        char psz_fmt[MAX_MSG];
        va_start( args, psz_format );

        snprintf( psz_fmt, MAX_MSG, "warning: %s\n", psz_format );
        vfprintf( stderr, psz_fmt, args );
    }
}

/*****************************************************************************
 * msg_Dbg
 *****************************************************************************/
void msg_Dbg( void *_unused, const char *psz_format, ... )
{
    if ( i_verbose >= VERB_DBG )
    {
        va_list args;
        char psz_fmt[MAX_MSG];
        va_start( args, psz_format );

        snprintf( psz_fmt, MAX_MSG, "debug: %s\n", psz_format );
        vfprintf( stderr, psz_fmt, args );
    }
}

/*****************************************************************************
 * msg_Raw
 *****************************************************************************/
void msg_Raw( void *_unused, const char *psz_format, ... )
{
    va_list args;
    char psz_fmt[MAX_MSG];
    va_start( args, psz_format );

    snprintf( psz_fmt, MAX_MSG, "%s\n", psz_format );
    vfprintf( stderr, psz_fmt, args );
}

/*****************************************************************************
 * wall_Date/real_Date: returns a 27 MHz timestamp
 *****************************************************************************/
static uint64_t _wall_Date( bool b_realtime )
{
#if defined (HAVE_CLOCK_NANOSLEEP)
    struct timespec ts;

    /* Try to use POSIX monotonic clock if available */
    if( b_realtime || clock_gettime( CLOCK_MONOTONIC, &ts ) == EINVAL )
        /* Run-time fallback to real-time clock (always available) */
        (void)clock_gettime( CLOCK_REALTIME, &ts );

    return ((uint64_t)ts.tv_sec * (uint64_t)27000000)
            + (uint64_t)(ts.tv_nsec * 27 / 1000);
#else
    struct timeval tv_date;

    /* gettimeofday() could return an error, and should be tested. However, the
     * only possible error, according to 'man', is EFAULT, which can not happen
     * here, since tv is a local variable. */
    gettimeofday( &tv_date, NULL );
    return( (uint64_t) tv_date.tv_sec * 27000000 + (uint64_t) tv_date.tv_usec * 27 );
#endif
}

uint64_t wall_Date( void )
{
    return _wall_Date( false );
}

uint64_t real_Date( void )
{
    return _wall_Date( true );
}

/*****************************************************************************
 * wall_Sleep/real_Sleep
 *****************************************************************************/
static void _wall_Sleep( uint64_t i_delay, bool b_realtime )
{
    struct timespec ts;
    ts.tv_sec = i_delay / 27000000;
    ts.tv_nsec = (i_delay % 27000000) * 1000 / 27;

#if defined( HAVE_CLOCK_NANOSLEEP )
    int val = EINVAL;
    if ( !b_realtime )
        while ( ( val = clock_nanosleep( CLOCK_MONOTONIC, 0, &ts, &ts ) ) == EINTR );
    if( val == EINVAL )
    {
        ts.tv_sec = i_delay / 27000000;
        ts.tv_nsec = (i_delay % 27000000) * 1000 / 27;
        while ( clock_nanosleep( CLOCK_REALTIME, 0, &ts, &ts ) == EINTR );
    }
#else
    while ( nanosleep( &ts, &ts ) && errno == EINTR );
#endif
}

void wall_Sleep( uint64_t i_delay )
{
    _wall_Sleep( i_delay, false );
}

void real_Sleep( uint64_t i_delay )
{
    _wall_Sleep( i_delay, true );
}

/*****************************************************************************
 * GetInterfaceIndex: returns the index of an interface
 *****************************************************************************/
static int GetInterfaceIndex( const char *psz_name )
{
    int i_fd;
    struct ifreq ifr;

    if ( (i_fd = socket( AF_INET, SOCK_DGRAM, 0 )) < 0 )
    {
        msg_Err( NULL, "unable to open socket (%s)", strerror(errno) );
        exit(EXIT_FAILURE);
    }

    strncpy( ifr.ifr_name, psz_name, IFNAMSIZ );
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if ( ioctl( i_fd, SIOCGIFINDEX, &ifr ) < 0 )
    {
        msg_Err( NULL, "unable to get interface index (%s)", strerror(errno) );
        exit(EXIT_FAILURE);
    }

    close( i_fd );

    return ifr.ifr_ifindex;
}

/*****************************************************************************
 * PrintSocket: print socket characteristics for debug purposes
 *****************************************************************************/
static void PrintSocket( const char *psz_text, struct sockaddr_storage *p_bind,
                         struct sockaddr_storage *p_connect )
{
    if ( p_bind->ss_family == AF_INET )
    {
        struct sockaddr_in *p_addr = (struct sockaddr_in *)p_bind;
        msg_Dbg( NULL, "%s bind:%s:%u", psz_text,
                 inet_ntoa( p_addr->sin_addr ), ntohs( p_addr->sin_port ) );
    }
    else if ( p_bind->ss_family == AF_INET6 )
    {
        char buf[INET6_ADDRSTRLEN];
        struct sockaddr_in6 *p_addr = (struct sockaddr_in6 *)p_bind;
        msg_Dbg( NULL, "%s bind:[%s]:%u", psz_text,
                 inet_ntop( AF_INET6, &p_addr->sin6_addr, buf, sizeof(buf) ),
                 ntohs( p_addr->sin6_port ) );
    }

    if ( p_connect->ss_family == AF_INET )
    {
        struct sockaddr_in *p_addr = (struct sockaddr_in *)p_connect;
        msg_Dbg( NULL, "%s connect:%s:%u", psz_text,
                 inet_ntoa( p_addr->sin_addr ), ntohs( p_addr->sin_port ) );
    }
    else if ( p_connect->ss_family == AF_INET6 )
    {
        char buf[INET6_ADDRSTRLEN];
        struct sockaddr_in6 *p_addr = (struct sockaddr_in6 *)p_connect;
        msg_Dbg( NULL, "%s connect:[%s]:%u", psz_text,
                 inet_ntop( AF_INET6, &p_addr->sin6_addr, buf, sizeof(buf) ),
                 ntohs( p_addr->sin6_port ) );
    }
}

/*****************************************************************************
 * ParseNodeService: parse a host:port string
 *****************************************************************************/
static struct addrinfo *ParseNodeService( char *_psz_string, char **ppsz_end,
                                          uint16_t i_default_port,
                                          int *pi_if_index )
{
    int i_family = AF_INET;
    char psz_port_buffer[6];
    char *psz_string = strdup( _psz_string );
    char *psz_node, *psz_port = NULL, *psz_end;
    struct addrinfo *p_res;
    struct addrinfo hint;
    int i_ret;

    if ( psz_string[0] == '[' )
    {
        i_family = AF_INET6;
        psz_node = psz_string + 1;
        psz_end = strchr( psz_node, ']' );
        if ( psz_end == NULL )
        {
            msg_Warn( NULL, "invalid IPv6 address %s", _psz_string );
            free( psz_string );
            return NULL;
        }
        *psz_end++ = '\0';

        char *psz_intf = strrchr( psz_node, '%' );
        if ( psz_intf != NULL )
        {
            *psz_intf++ = '\0';
            if ( pi_if_index != NULL )
                *pi_if_index = GetInterfaceIndex( psz_intf );
        }
    }
    else
    {
        psz_node = psz_string;
        psz_end = strpbrk( psz_string, "@:,/" );
    }

    if ( psz_end != NULL && psz_end[0] == ':' )
    {
        *psz_end++ = '\0';
        psz_port = psz_end;
        psz_end = strpbrk( psz_port, "@:,/" );
    }

    if ( psz_end != NULL )
    {
        *psz_end = '\0';
        if ( ppsz_end != NULL )
            *ppsz_end = _psz_string + (psz_end - psz_string);
    }
    else if ( ppsz_end != NULL )
        *ppsz_end = _psz_string + strlen(_psz_string);

    if ( i_default_port != 0 && (psz_port == NULL || !*psz_port) )
    {
        sprintf( psz_port_buffer, "%u", i_default_port );
        psz_port = psz_port_buffer;
    }

    if ( psz_node[0] == '\0' )
        psz_node = "0.0.0.0";

    memset( &hint, 0, sizeof(hint) );
    hint.ai_family = i_family;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_protocol = 0;
    hint.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG;
    if ( (i_ret = getaddrinfo( psz_node, psz_port, NULL, &p_res )) != 0 )
    {
        //msg_Warn( NULL, "getaddrinfo error: %s", gai_strerror(i_ret) );
        free( psz_string );
        return NULL;
    }

    free( psz_string );
    return p_res;
}

/*****************************************************************************
 * OpenSocket: parse argv and open IPv4 & IPv6 sockets
 *****************************************************************************/
int OpenSocket( const char *_psz_arg, int i_ttl, uint16_t i_bind_port,
                uint16_t i_connect_port, unsigned int *pi_weight, bool *pb_tcp )
{
    struct sockaddr_storage bind_addr, connect_addr;
    int i_fd, i;
    char *psz_arg = strdup(_psz_arg);
    char *psz_token = psz_arg;
    char *psz_token2 = NULL;
    int i_bind_if_index = 0, i_connect_if_index = 0;
    in_addr_t i_if_addr = INADDR_ANY;
    int i_tos = 0;
    bool b_tcp;
    struct addrinfo *p_ai;
    int i_family;
    socklen_t i_sockaddr_len;

    bind_addr.ss_family = AF_UNSPEC;
    connect_addr.ss_family = AF_UNSPEC;

    if ( pb_tcp == NULL )
        pb_tcp = &b_tcp;
    *pb_tcp = false;

    psz_token2 = strrchr( psz_arg, ',' );
    if ( psz_token2 )
    {
        *psz_token2++ = '\0';
        if ( pi_weight )
            *pi_weight = strtoul( psz_token2, NULL, 0 );
    }
    else if ( pi_weight )
        *pi_weight = 1;

    psz_token2 = strchr( psz_arg, '/' );
    if ( psz_token2 )
        *psz_token2 = '\0';

    if ( *psz_token == '\0' )
        return -1;

    /* Hosts */
    if ( psz_token[0] != '@' )
    {
        p_ai = ParseNodeService( psz_token, &psz_token, i_connect_port,
                                 &i_connect_if_index );
        if ( p_ai == NULL )
            return -1;
        memcpy( &connect_addr, p_ai->ai_addr, p_ai->ai_addrlen );
        freeaddrinfo( p_ai );
    }

    if ( psz_token[0] == '@' )
    {
        psz_token++;
        p_ai = ParseNodeService( psz_token, &psz_token, i_bind_port,
                                 &i_bind_if_index );
        if ( p_ai == NULL )
            return -1;
        memcpy( &bind_addr, p_ai->ai_addr, p_ai->ai_addrlen );
        freeaddrinfo( p_ai );
    }

    if ( bind_addr.ss_family == AF_UNSPEC &&
         connect_addr.ss_family == AF_UNSPEC )
        return -1;

    /* Weights and options */
    if ( psz_token2 )
    {
        do
        {
            *psz_token2++ = '\0';

#define IS_OPTION( option ) (!strncasecmp( psz_token2, option, strlen(option) ))
#define ARG_OPTION( option ) (psz_token2 + strlen(option))

            if ( IS_OPTION("ifindex=") )
                i_bind_if_index = i_connect_if_index =
                    strtol( ARG_OPTION("ifindex="), NULL, 0 );
            else if ( IS_OPTION("ifaddr=") )
                i_if_addr = inet_addr( ARG_OPTION("ifaddr=") );
            else if ( IS_OPTION("ttl=") )
                i_ttl = strtol( ARG_OPTION("ttl="), NULL, 0 );
            else if ( IS_OPTION("tos=") )
                i_tos = strtol( ARG_OPTION("tos="), NULL, 0 );
            else if ( IS_OPTION("tcp") )
                *pb_tcp = true;
            else
                msg_Warn( NULL, "unrecognized option %s", psz_token2 );

#undef IS_OPTION
#undef ARG_OPTION
        }
        while ( (psz_token2 = strchr( psz_token2, '/' )) != NULL );
    }

    free( psz_arg );

    /* Sanity checks */
    if ( bind_addr.ss_family != AF_UNSPEC && connect_addr.ss_family != AF_UNSPEC
          && bind_addr.ss_family != connect_addr.ss_family )
    {
        msg_Err( NULL, "incompatible address types" );
        exit(EXIT_FAILURE);
    }
    if ( bind_addr.ss_family != AF_UNSPEC ) i_family = bind_addr.ss_family;
    else i_family = connect_addr.ss_family;
    i_sockaddr_len = (i_family == AF_INET) ? sizeof(struct sockaddr_in) :
                     sizeof(struct sockaddr_in6);

    if ( i_bind_if_index && i_connect_if_index
          && i_bind_if_index != i_connect_if_index )
    {
        msg_Err( NULL, "incompatible bind and connect interfaces" );
        exit(EXIT_FAILURE);
    }
    if ( i_connect_if_index ) i_bind_if_index = i_connect_if_index;
    else i_connect_if_index = i_bind_if_index;

    if ( bind_addr.ss_family == AF_UNSPEC
          && connect_addr.ss_family == AF_UNSPEC )
    {
        msg_Err( NULL, "ambiguous address declaration" );
        exit(EXIT_FAILURE);
    }

    /* Socket configuration */
    if ( (i_fd = socket( i_family, *pb_tcp ? SOCK_STREAM : SOCK_DGRAM,
                         0 )) < 0 )
    {
        msg_Err( NULL, "unable to open socket (%s)", strerror(errno) );
        exit(EXIT_FAILURE);
    }

    i = 1;
    if ( setsockopt( i_fd, SOL_SOCKET, SO_REUSEADDR, (void *)&i,
                     sizeof(i) ) == -1 )
    {
        msg_Err( NULL, "unable to set socket (%s)", strerror(errno) );
        exit(EXIT_FAILURE);
    }

    if ( i_family == AF_INET6 )
    {
        struct sockaddr_in6 *p_addr =
            (struct sockaddr_in6 *)&bind_addr;

        if ( i_bind_if_index
              && setsockopt( i_fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                     (void *)&i_bind_if_index, sizeof(i_bind_if_index) ) < 0 )
        {
            msg_Err( NULL, "couldn't set interface index" );
            PrintSocket( "socket definition:", &bind_addr, &connect_addr );
            exit(EXIT_FAILURE);
        }

        if ( bind_addr.ss_family != AF_UNSPEC )
        {
            if ( IN6_IS_ADDR_MULTICAST( &p_addr->sin6_addr ) )
            {
                struct ipv6_mreq imr;
                struct sockaddr_in6 bind_addr_any = *p_addr;
                bind_addr_any.sin6_addr = in6addr_any;

                if ( bind( i_fd, (struct sockaddr *)&bind_addr_any,
                           sizeof(bind_addr_any) ) < 0 )
                {
                    msg_Err( NULL, "couldn't bind" );
                    PrintSocket( "socket definition:", &bind_addr,
                                 &connect_addr );
                    exit(EXIT_FAILURE);
                }

                imr.ipv6mr_multiaddr = p_addr->sin6_addr;
                imr.ipv6mr_interface = i_bind_if_index;

                /* Join Multicast group without source filter */
                if ( setsockopt( i_fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ipv6_mreq) ) < 0 )
                {
                    msg_Err( NULL, "couldn't join multicast group" );
                    PrintSocket( "socket definition:", &bind_addr,
                                  &connect_addr );
                    exit(EXIT_FAILURE);
                }
            }
            else
                goto normal_bind;
        }
    }
    else if ( bind_addr.ss_family != AF_UNSPEC )
    {
normal_bind:
        if ( bind( i_fd, (struct sockaddr *)&bind_addr, i_sockaddr_len ) < 0 )
        {
            msg_Err( NULL, "couldn't bind" );
            PrintSocket( "socket definition:", &bind_addr, &connect_addr );
            exit(EXIT_FAILURE);
        }
    }

    if ( !*pb_tcp )
    {
        /* Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to
         * avoid packet loss caused by scheduling problems */
        i = 0x80000;
        setsockopt( i_fd, SOL_SOCKET, SO_RCVBUF, (void *) &i, sizeof( i ) );

        /* Join the multicast group if the socket is a multicast address */
        struct sockaddr_in *p_bind_addr = (struct sockaddr_in *)&bind_addr;
        struct sockaddr_in *p_connect_addr = (struct sockaddr_in *)&connect_addr;
        if ( bind_addr.ss_family == AF_INET
              && IN_MULTICAST( ntohl(p_bind_addr->sin_addr.s_addr)) )
        {
            if ( connect_addr.ss_family != AF_UNSPEC )
            {
                /* Source-specific multicast */
                struct ip_mreq_source imr;
                imr.imr_multiaddr = p_bind_addr->sin_addr;
                imr.imr_interface.s_addr = i_if_addr;
                imr.imr_sourceaddr = p_connect_addr->sin_addr;
                if ( i_bind_if_index )
                    msg_Warn( NULL, "ignoring ifindex option in SSM" );

                if ( setsockopt( i_fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                            (char *)&imr, sizeof(struct ip_mreq_source) ) < 0 )
                    msg_Err( NULL, "couldn't join multicast group (%s)",
                             strerror(errno) );
                    PrintSocket( "socket definition:", &bind_addr,
                                 &connect_addr );
                    exit(EXIT_FAILURE);
            }
            else if ( i_bind_if_index )
            {
                /* Linux-specific interface-bound multicast */
                struct ip_mreqn imr;
                imr.imr_multiaddr = p_bind_addr->sin_addr;
                imr.imr_address.s_addr = i_if_addr;
                imr.imr_ifindex = i_bind_if_index;

                if ( setsockopt( i_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ip_mreqn) ) < 0 )
                    msg_Err( NULL, "couldn't join multicast group (%s)",
                             strerror(errno) );
                    PrintSocket( "socket definition:", &bind_addr,
                                 &connect_addr );
                    exit(EXIT_FAILURE);
            }
            else
            {
                /* Regular multicast */
                struct ip_mreq imr;
                imr.imr_multiaddr = p_bind_addr->sin_addr;
                imr.imr_interface.s_addr = i_if_addr;

                if ( setsockopt( i_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ip_mreq) ) < 0 )
                {
                    msg_Err( NULL, "couldn't join multicast group (%s)",
                             strerror(errno) );
                    PrintSocket( "socket definition:", &bind_addr,
                                 &connect_addr );
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    if ( connect_addr.ss_family != AF_UNSPEC )
    {
        if ( connect( i_fd, (struct sockaddr *)&connect_addr,
                      i_sockaddr_len ) < 0 )
        {
            msg_Err( NULL, "cannot connect socket (%s)",
                     strerror(errno) );
            PrintSocket( "socket definition:", &bind_addr, &connect_addr );
            exit(EXIT_FAILURE);
        }

        if ( !*pb_tcp )
        {
            if ( i_ttl )
            {
                struct sockaddr_in *p_v4_addr =
                    (struct sockaddr_in *)&connect_addr;
                struct sockaddr_in6 *p_v6_addr =
                    (struct sockaddr_in6 *)&connect_addr;

                if ( i_family == AF_INET
                      && IN_MULTICAST( ntohl(p_v4_addr->sin_addr.s_addr) ) )
                {
                    if ( setsockopt( i_fd, IPPROTO_IP, IP_MULTICAST_TTL,
                                     (void *)&i_ttl, sizeof(i_ttl) ) == -1 )
                    {
                        msg_Err( NULL, "couldn't set TTL (%s)",
                                 strerror(errno) );
                        PrintSocket( "socket definition:", &bind_addr,
                                     &connect_addr );
                        exit(EXIT_FAILURE);
                    }
                }

                if ( i_family == AF_INET6
                      && IN6_IS_ADDR_MULTICAST( &p_v6_addr->sin6_addr ) )
                {
                    if ( setsockopt( i_fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                                     (void *)&i_ttl, sizeof(i_ttl) ) == -1 )
                    {
                        msg_Err( NULL, "couldn't set TTL (%s)",
                                 strerror(errno) );
                        PrintSocket( "socket definition:", &bind_addr,
                                     &connect_addr );
                        exit(EXIT_FAILURE);
                    }
                }
            }

            if ( i_tos )
            {
                if ( setsockopt( i_fd, IPPROTO_IP, IP_TOS,
                                 (void *)&i_tos, sizeof(i_tos) ) == -1 )
                {
                    msg_Err( NULL, "couldn't set TOS (%s)", strerror(errno) );
                    PrintSocket( "socket definition:", &bind_addr,
                                 &connect_addr );
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    else if ( *pb_tcp )
    {
        /* Open in listen mode - wait for an incoming connection */
        int i_new_fd;
        if ( listen( i_fd, 1 ) < 0 )
        {
            msg_Err( NULL, "couldn't listen (%s)", strerror(errno) );
            PrintSocket( "socket definition:", &bind_addr, &connect_addr );
            exit(EXIT_FAILURE);
        }

        while ( (i_new_fd = accept( i_fd, NULL, NULL )) < 0 )
        {
            if ( errno != EINTR )
            {
                msg_Err( NULL, "couldn't accept (%s)", strerror(errno) );
                PrintSocket( "socket definition:", &bind_addr, &connect_addr );
                exit(EXIT_FAILURE);
            }
        }
        close( i_fd );
        return i_new_fd;
    }

    return i_fd;
}

/*****************************************************************************
 * StatFile: parse argv and return the mode of the named file
 *****************************************************************************/
mode_t StatFile( const char *psz_arg )
{
    struct stat sb;
    if ( stat( psz_arg, &sb ) < 0 )
        return 0;
    return sb.st_mode;
}

/*****************************************************************************
 * OpenFile: parse argv and open file descriptors
 *****************************************************************************/
int OpenFile( const char *psz_arg, bool b_read, bool b_append )
{
    struct stat sb;
    int i_fd;
    int i_mode = b_read ? O_RDONLY : O_WRONLY;

    if ( stat( psz_arg, &sb ) < 0 )
    {
        if ( b_read )
        {
            msg_Err( NULL, "file %s doesn't exist (%s)", psz_arg,
                     strerror(errno) );
            exit(EXIT_FAILURE);
        }
        i_mode |= O_CREAT;
    }
    else if ( !(S_ISCHR(sb.st_mode) || S_ISFIFO(sb.st_mode)) && !b_read )
    {
        if ( b_append )
            i_mode |= O_APPEND;
        else
            i_mode |= O_TRUNC;
    }

    if ( (i_fd = open( psz_arg, i_mode, 0644 )) < 0 )
    {
        msg_Err( NULL, "couldn't open file %s (%s)", psz_arg, strerror(errno) );
        exit(EXIT_FAILURE);
    }

    return i_fd;
}

/*****************************************************************************
 * GetAuxFile: generate a file name for the TS file
 * Remember to free the returned string
 *****************************************************************************/
char *GetAuxFile( const char *psz_arg, size_t i_payload_size )
{
    char *psz_aux = malloc( strlen(psz_arg) + 256 );
    char *psz_token;

    strcpy( psz_aux, psz_arg );

    psz_token = strrchr( psz_aux, '/' );
    if ( psz_token != NULL )
        psz_token++;
    else
        psz_token = psz_aux + 1;
    if ( *psz_token ) psz_token++; /* Skip first character of base name */

    /* Strip extension */
    psz_token = strrchr( psz_token, '.' );
    if ( psz_token ) *psz_token = '\0';

    /* Append extension */
    strcat( psz_aux, "." PSZ_AUX_EXT );
    if ( i_payload_size != DEFAULT_PAYLOAD_SIZE )
        sprintf( psz_aux + strlen(psz_aux), "%zu", i_payload_size );

    return psz_aux;
}

/*****************************************************************************
 * OpenAuxFile
 *****************************************************************************/
FILE *OpenAuxFile( const char *psz_arg, bool b_read, bool b_append )
{
    FILE *p_aux;

    if ( (p_aux = fopen( psz_arg,
                         b_read ? "rb" : (b_append ? "ab" : "wb") )) == NULL )
    {
        msg_Err( NULL, "couldn't open file %s (%s)", psz_arg,
                 strerror(errno) );
        exit(EXIT_FAILURE);
    }

    return p_aux;
}


/*****************************************************************************
 * LookupAuxFile: find an STC in an auxiliary file
 *****************************************************************************/
off_t LookupAuxFile( const char *psz_arg, int64_t i_wanted, bool b_absolute )
{
    uint8_t *p_aux;
    off_t i_offset1 = 0, i_offset2;
    int i_stc_fd;
    struct stat stc_stat;

    if ( (i_stc_fd = open( psz_arg, O_RDONLY )) == -1 )
    {
        msg_Err( NULL, "unable to open %s (%s)", psz_arg, strerror(errno) );
        return -1;
    }

    if ( fstat( i_stc_fd, &stc_stat ) == -1
          || stc_stat.st_size < sizeof(uint64_t) )
    {
        msg_Err( NULL, "unable to stat %s (%s)", psz_arg, strerror(errno) );
        return -1;
    }

    p_aux = mmap( NULL, stc_stat.st_size, PROT_READ, MAP_SHARED,
                  i_stc_fd, 0 );
    if ( p_aux == MAP_FAILED )
    {
        msg_Err( NULL, "unable to mmap %s (%s)", psz_arg, strerror(errno) );
        return -1;
    }

    i_offset2 = stc_stat.st_size / sizeof(uint64_t);

    if ( i_wanted < 0 )
        i_wanted += FromSTC( p_aux + (i_offset2 - 1) * sizeof(uint64_t) );
    else if ( !b_absolute )
        i_wanted += FromSTC( p_aux );

    if ( i_wanted < 0 )
    {
        msg_Err( NULL, "invalid offset" );
        return -1;
    }

    for ( ; ; )
    {
        off_t i_mid_offset = (i_offset1 + i_offset2) / 2;
        uint8_t *p_mid_aux = p_aux + i_mid_offset * sizeof(uint64_t);
        uint64_t i_mid_stc = FromSTC( p_mid_aux );

        if ( i_offset1 == i_mid_offset )
            break;

        if ( i_mid_stc >= i_wanted )
            i_offset2 = i_mid_offset;
        else
            i_offset1 = i_mid_offset;
    }

    munmap( p_aux, stc_stat.st_size );
    close( i_stc_fd );

    return i_offset2;
}

/*****************************************************************************
 * GetDirFile: return the prefix of the file according to the STC
 *****************************************************************************/
uint64_t GetDirFile( uint64_t i_rotate_size, int64_t i_wanted )
{
    if ( i_wanted <= 0 )
        i_wanted += real_Date();
    if ( i_wanted <= 0 )
        return 0;

    return i_wanted / i_rotate_size;
}

/*****************************************************************************
 * OpenDirFile: return fd + aux file pointer
 *****************************************************************************/
int OpenDirFile( const char *psz_dir_path, uint64_t i_file, bool b_read,
                 size_t i_payload_size, FILE **pp_aux_file )
{
    int i_fd;
    char psz_file[strlen(psz_dir_path) + sizeof(PSZ_TS_EXT) +
                  sizeof(".18446744073709551615")];
    sprintf( psz_file, "%s/%"PRIu64"."PSZ_TS_EXT, psz_dir_path, i_file );

    i_fd = OpenFile( psz_file, b_read, !b_read );
    if ( i_fd < 0 ) return -1;

    char *psz_aux_file = GetAuxFile( psz_file, i_payload_size );
    *pp_aux_file = OpenAuxFile( psz_aux_file, b_read, !b_read );
    free( psz_aux_file );
    if ( *pp_aux_file == NULL )
    {
        close( i_fd );
        return -1;
    }
    return i_fd;
}

/*****************************************************************************
 * LookupDirAuxFile: find an STC in an auxiliary file of a directory
 *****************************************************************************/
off_t LookupDirAuxFile( const char *psz_dir_path, uint64_t i_file,
                        int64_t i_wanted, size_t i_payload_size )
{
    off_t i_ret;
    char psz_file[strlen(psz_dir_path) + sizeof(PSZ_TS_EXT) +
                  sizeof(".18446744073709551615")];
    sprintf( psz_file, "%s/%"PRIu64"."PSZ_TS_EXT, psz_dir_path, i_file );

    char *psz_aux_file = GetAuxFile( psz_file, i_payload_size );
    i_ret = LookupAuxFile( psz_aux_file, i_wanted, true );
    free( psz_aux_file );
    return i_ret;
}
