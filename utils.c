/*
 * Copyright (c) 2023 roleo.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h> 
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/reboot.h>
#include <ctype.h>

#ifdef HAVE_MBEDTLS
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#else
#include <tomcrypt.h>
#endif

#include "utils.h"
#include "log.h"

/**
 * Read a file line by line and send to output after replacing arguments
 * @param out The output type: "sdout", char *ptr or NULL
 * @param filename The input file to process
 * @param num The number of variable arguments
 * @param ... The argument list to replace: src1, dst1, src2, dst2, etc...
 */
long cat(char *out, char *filename, int num, ...)
{
    va_list valist;
    char new_line[MAX_LEN];
    char *l;
    char *ptr = out;
    char *pars, *pare, *par_to_find, *par_to_sub;
    int i;
    long ret = 0;

    FILE* file = fopen(filename, "r");
    if (!file) {
        log_error("Unable to open file %s\n", filename);
        return -1;
    }

    char line[MAX_LEN];

    while (fgets(line, sizeof(line), file)) {
        va_start(valist, num);

        memset(new_line, '\0', sizeof(new_line));
        for (i = 0; i < num/2; i++) {
            par_to_find = va_arg(valist, char *);
            par_to_sub = va_arg(valist, char *);
            pars = strstr(line, par_to_find);
            if (pars != NULL) {
                pare = pars + strlen(par_to_find);
                strncpy(new_line, line, pars - line);
                strcpy(&new_line[pars - line], par_to_sub);
                strncpy(&new_line[pars + strlen(par_to_sub) - line], pare, line + strlen(line) - pare);
            }
            if (new_line[0] != '\0') {
                strcpy(line, new_line);
                memset(new_line, '\0', sizeof(new_line));
            }
        }
        if (new_line[0] == '\0') {
            l = trim(line);
        } else {
            l = trim(new_line);
        }

        if (out != NULL) {
            if (strcmp("stdout", out) == 0) {
                if (*l != '<') {
                    fprintf(stdout, " ");
                }
                fprintf(stdout, "%s", l);
            } else {
                if (*l != '<') {
                    sprintf(ptr, " ");
                    ptr++;
                }
                sprintf(ptr, "%s", l);
                ptr += strlen(l);
            }
        }
        if (*l != '<') {
            ret++;
        }
        ret += strlen(l);

        va_end(valist);
    }
    fclose(file);

    return ret;
}

int get_ip_address(char *address, char *netmask, char *name)
{
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char *host;
    char *mask;
    struct sockaddr_in *sa, *san;

    if (getifaddrs(&ifaddr) == -1) {
        log_error("Error in getifaddrs()");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;

        if ((strcmp(ifa->ifa_name, name) == 0) && (ifa->ifa_addr->sa_family == AF_INET)) {

            sa = (struct sockaddr_in *) ifa->ifa_addr;
            inet_ntop(AF_INET, &(((struct sockaddr_in *)sa)->sin_addr), address, 15);
            san = (struct sockaddr_in *) ifa->ifa_netmask;
            inet_ntop(AF_INET, &(((struct sockaddr_in *)san)->sin_addr), netmask, 15);

            log_debug("Interface: <%s>", ifa->ifa_name);
            log_debug("Address: <%s>", address);
            log_debug("Netmask: <%s>", netmask);
        }
    }

    freeifaddrs(ifaddr);

    return 0;
}

int get_mac_address(char *address, char *name)
{
    struct ifreq ifr;
    struct ifconf ifc;
    char buf[MAX_LEN];
    int success = 0;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock == -1) {
        return -1;
    }

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
        return -2;
    }

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it) {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (strcmp(name, ifr.ifr_name) == 0) {
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
                if (! (ifr.ifr_flags & IFF_LOOPBACK)) {
                    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                        success = 1;
                        break;
                    }
                }
            }
        }
    }

    if (success) {
        sprintf(address, "%02x:%02x:%02x:%02x:%02x:%02x",
                ifr.ifr_hwaddr.sa_data[0], ifr.ifr_hwaddr.sa_data[1],
                ifr.ifr_hwaddr.sa_data[2], ifr.ifr_hwaddr.sa_data[3],
                ifr.ifr_hwaddr.sa_data[4], ifr.ifr_hwaddr.sa_data[5]);
    } else {
        address = NULL;
        return -4;
    }

    return 0;
}

int netmask2prefixlen(char *netmask)
{
    int n;
    int i = 0;

    inet_pton(AF_INET, netmask, &n);

    while (n > 0) {
            n = n >> 1;
            i++;
    }

    return i;
}

int str_subst(char *output, char *request, char *value, char *new_value)
{
    char *s;
    int ret;

    s = strstr(request, value);
    if (s != NULL) {
        strncpy(output, request, s - request);
        strcpy(&output[s - request], new_value);
        strcpy(&output[s - request + strlen(new_value)], s + strlen(value));
        ret = 0;
    } else {
        strcpy(output, request);
        ret = 0;
    }

    return ret;
}

/**
 * Remove spaces from the left of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *ltrim(char *s)
{
    char *p = s;

    while(isspace(*p)) p++;
    return p;
}

/**
 * Remove spaces from the right of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *rtrim(char *s)
{
    int iret = strlen(s);
    char* back = s + iret;
    back--;
    while (isspace(*back)) {
        *back = '\0';
        back--;
    }
    return s;
}

/**
 * Remove spaces from the left and the right of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *trim(char *s)
{
    return ltrim(rtrim(s));
}

/**
 * Remove space, tab, CR and LF from the left of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *ltrim_mf(char *s)
{
    char *p = s;

    while((*p == ' ') || (*p == '\t') || (*p == '\n') || (*p == '\r')) p++;
    return p;
}

/**
 * Remove space, tab, CR and LF from the right of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *rtrim_mf(char *s)
{
    int iret = strlen(s);
    char* back = s + iret;

    if (iret == 0) return s;
    back--;
    while((*back == ' ') || (*back == '\t') || (*back == '\n') || (*back == '\r')) {
        *back = '\0';
        back--;
    }
    return s;
}

/**
 * Remove space, tab, CR and LF from the left and the right of the string
 * @param s The input string
 * @return A pointer to the resulting string
 */
char *trim_mf(char *s)
{
    return ltrim_mf(rtrim_mf(s));
}

int find_action(char *action, int action_size, char *request)
{
    char *sb;
    char *sl;
    char *sg;
    char *ss;
    char *sf;
    char *sp;
    char raw_action[MAX_LEN];
    char *p = (char *) raw_action;

    memset(raw_action, '\0', sizeof(raw_action));
    sb = strstr(request, "Body");
    if (sb != NULL) {
        sl = strstr(sb, "<");
        if (sl != NULL) {
            sg = strstr(sl, ">");
            ss = strstr(sl, " ");
            if ((sg == NULL) && (ss == NULL)) {
                return -2;
            } else if (ss == NULL) {
                sf = sg;
            } else if (sg == NULL) {
                sf = ss;
            } else {
                sf = (sg > ss) ? ss : sg;
            }

            strncpy(p, sl + 1, sf - sl - 1);
            p = trim(p);
            sp = strstr(p, ":");
            if (sp != NULL) p = sp + 1;
            if (strlen(p) < action_size - 1) {
                strcpy(action, p);
                return 0;
            }
        }
    }

    return -1;
}

/**
 * Find element in xml string equal to value
 * @param request The string containing the xml
 * @param name The name of the element
 * @param value The value of the element
 * @return 0 if the element is found, -1 otherwise
 */
int find_element(char *request, char *name, char *value)
{
    char *node_s, *node_e;

    node_s = strstr(request, name);
    if (node_s != NULL) {
        node_e = strstr(node_s + 1, name);
        if (node_e != NULL) {
            node_e[0] = '\0';
            if (strstr(node_s + 1, value) != NULL) {
                node_e[0] = name[0];
                return 0;
            }
            node_e[0] = name[0];
        }
    }

    return -1;
}

/**
 * Find element in xml string and return a pointer
 * @param size The size of the output element
 * @param request The string with the xml
 * @return A pointer to the element inside the xml string (not null terminated).
 *         NULL if not found.
 */
char *get_element(int *size, char *request, char *name)
{
    char *node_s, *node_e;
    char *s, *e;
    char *sl_name;
    char isize;

    isize = strlen(name);
    node_s = strstr(request, name);
    // Check if it's another string that contains name
    if ((*(node_s - 1) != '<') && (*(node_s - 1) != ' ') && (*(node_s - 1) != ':'))
        node_s = strstr(node_s + 1, name);
    if ((*(node_s + isize) != '>') && (*(node_s + isize) != ' ') && (*(node_s + isize) != '\n') && (*(node_s + isize) != '\r'))
        node_s = strstr(node_s + 1, name);

    if (node_s != NULL) {
        while (((*node_s != '<') && (*node_s != ' ')) && (node_s >= request)) {
            node_s--;
            isize++;
        }
        if ((*node_s == '<') || (*node_s == ' ')) {
            node_s++;
            isize--;
        }

        sl_name = (char *) malloc((isize + 2) * sizeof(char));
        sl_name[0] = '/';
        strncpy(&sl_name[1], node_s, isize);
        sl_name[isize + 1] = '\0';

        s = strstr(node_s, ">");
        if (s != NULL) {
            s++;
            node_e = strstr(s, sl_name);
            if (node_e != NULL) {
                e = node_e;
                while ((*e != '<') && (e >= request)) {
                    e--;
                }
                if (*e == '<') {
                    *e = '\0';
                    s = ltrim(s);
                    *size = strlen(s);
                    *e = '<';
                    free(sl_name);

                    return s;
                }
            }
        }
    }
    *size = 0;
    free(sl_name);

    return NULL;
}

int html_escape(char *url, int max_len)
{
    int i, count = 0;
    int s_tmp[max_len];
    char *f, *t;

    // Count chars to escape
    for (i = 0; i < strlen(url); i++)
    {
        switch (url[i]) {
        case '\"':
            count += 5;
            break;
        case '&':
            count += 4;
            break;
        case '\'':
            count += 4;
            break;
        case '<':
            count += 3;
            break;
        case '>':
            count += 3;
            break;
        }
    }

    if (strlen(url) + count + 1 > max_len) {
        return -1;
    }

    f = url;
    t = (char *) &s_tmp[0];
    while (*f != '\0')
    {
        switch (*f) {
        case '\"':
            *t = '&';
            t++;
            *t = 'q';
            t++;
            *t = 'u';
            t++;
            *t = 'o';
            t++;
            *t = 't';
            t++;
            *t = ';';
            break;
        case '&':
            *t = '&';
            t++;
            *t = 'a';
            t++;
            *t = 'm';
            t++;
            *t = 'p';
            t++;
            *t = ';';
            break;
        case '\'':
            *t = '&';
            t++;
            *t = '#';
            t++;
            *t = '3';
            t++;
            *t = '9';
            t++;
            *t = ';';
            break;
        case '<':
            *t = '&';
            t++;
            *t = 'l';
            t++;
            *t = 't';
            t++;
            *t = ';';
            break;
        case '>':
            *t = '&';
            t++;
            *t = 'g';
            t++;
            *t = 't';
            t++;
            *t = ';';
            break;
        default:
            *t = *f;
        }
        t++;
        f++;
    }

    strcpy(url, (char *) s_tmp);
}

/**
 * Hashes a given sequence of bytes using the SHA1 algorithm
 * @param output The hash
 * @param output_size The size of the buffer containing the hash (>= 20)
 * @param input The input sequence pointer
 * @param input_size The size of the input sequence
 * @return A malloc-allocated pointer to the resulting data. 20 bytes long.
 */
int hashSHA1(char* input, unsigned long input_size, char *output, int output_size)
{
    if (output_size < 20)
        return -1;

#ifdef HAVE_MBEDTLS
    //Initial
    mbedtls_sha1_context ctx;

    //Initialize a state variable for the hash
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    //Process the text - remember you can call process() multiple times
    mbedtls_sha1_update(&ctx, (const unsigned char*) input, input_size);
    //Finish the hash calculation
    mbedtls_sha1_finish(&ctx, output);
    mbedtls_sha1_free(&ctx);
#else
    //Initial
    hash_state md;

    //Initialize a state variable for the hash
    sha1_init(&md);
    //Process the text - remember you can call process() multiple times
    sha1_process(&md, (const unsigned char*) input, input_size);
    //Finish the hash calculation
    sha1_done(&md, output);
#endif

    return 0;
}

/**
 * Decode a base64 string
 * @param output The decoded sequence pointer
 * @param output_size The size of the buffer/decoded sequence
 * @param input The input sequence pointer
 * @param input_size The size of the input sequence
 */
void b64_decode(unsigned char *input, unsigned int input_size, unsigned char *output, unsigned long *output_size)
{
#ifdef HAVE_MBEDTLS
    size_t olen;
    mbedtls_base64_decode(output, *output_size, &olen, input, input_size);
    *output_size = olen;
#else
    base64_decode(input, input_size, output, output_size);
#endif
}

/**
 * Encode a base64 string
 * @param output The encoded sequence pointer
 * @param output_size The size of the buffer/encoded sequence
 * @param input The input sequence pointer
 * @param input_size The size of the input sequence
 */
void b64_encode(unsigned char *input, unsigned int input_size, unsigned char *output, unsigned long *output_size)
{
#ifdef HAVE_MBEDTLS
    size_t olen;
    mbedtls_base64_encode(output, *output_size, &olen, input, input_size);
    *output_size = olen;
#else
    base64_encode(input, input_size, output, output_size);
#endif
}

void *reboot_thread(void *arg)
{
    sync();
    setuid(0);
    sync();
    sleep(3);
    sync();
    reboot(RB_AUTOBOOT);

    return NULL;
}
