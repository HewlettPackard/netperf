/*
#  Copyright 2021 Hewlett Packard Enterprise Development LP
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>

#if defined(HAVE_SYS_SOCKIO_H)
#include <sys/sockio.h>
#endif

char *
find_egress_interface_by_addr(struct sockaddr *addr, int local_ip_check) {

#ifdef HAVE_GETIFADDRS
#include <ifaddrs.h>

  struct ifaddrs *ifap;
  struct ifaddrs *temp;
  struct sockaddr_in *sin,*tsin;
#ifdef AF_INET6
  struct sockaddr_in6 *sin6,*tsin6;
#endif
  void *addr1,*addr2;
  int ret,cmplen;
  char temp_name[IFNAMSIZ];

  sin = (struct sockaddr_in *)addr;
  sin6 = (struct sockaddr_in6 *)sin;

  ret = getifaddrs(&ifap);

  if (ret < 0) {
    if (local_ip_check)
      return NULL;
    else
      return("ifgetaddrs");
  }

  temp = ifap;
  while (temp) {
    if ((temp->ifa_flags & IFF_UP) &&
	(temp->ifa_addr->sa_family == sin->sin_family)) {
      sin = (struct sockaddr_in *)temp->ifa_addr;
      switch (temp->ifa_addr->sa_family) {
#ifdef AF_INET6
      case AF_INET6:
	addr1 = &(sin6->sin6_addr);
	tsin6 = (struct sockaddr_in6 *)(temp->ifa_addr);
	addr2 = &(tsin6->sin6_addr);
	cmplen = sizeof(tsin6->sin6_addr);
	break;
#endif
      case AF_INET:
	addr1 = &(sin->sin_addr.s_addr);
	tsin = (struct sockaddr_in *)(temp->ifa_addr);
	addr2 = &(tsin->sin_addr.s_addr);
	cmplen = sizeof(struct in_addr);
	break;
      default:
	freeifaddrs(ifap);
	if (local_ip_check)
	  return NULL;
	else
	  return strdup("BadAF");
      }
      if (memcmp(addr1,addr2,cmplen) == 0) {
	strcpy(temp_name,temp->ifa_name);
	freeifaddrs(ifap);
	return strdup(temp_name);
      }
    }
    temp = temp->ifa_next;
  }
  freeifaddrs(ifap);
  if (local_ip_check)
    return NULL;
  else
    return strdup("NotFound");

#else
  char *buf,*ptr;
  int  lastlen,len,cmplen;
  int   sockfd;
  struct ifconf ifc;
  struct ifreq  *ifr;
  struct sockaddr_in *sin,*tsin;
#ifdef AF_INET6
  struct sockaddr_in6 *sin6,*tsin6;
#endif
  void *addr1,*addr2;

  sin = (struct sockaddr_in *)addr;
#ifdef AF_INET6
  sin6 = (struct sockaddr_in6 *)sin;
#endif

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("Looking for %s\n",inet_ntoa(sin->sin_addr));
#endif

  sockfd = socket(AF_INET,SOCK_DGRAM,0);
  if (sockfd < 0) {
    if (local_ip_check)
      return NULL;
    else
      return strdup("socket");
  }
  lastlen = 0;
  len = 100 * sizeof(struct ifreq);
  while (1) {
    buf = malloc(len);
    if (NULL == buf) {
      if (local_ip_check)
	return NULL;
      else
	return strdup("malloc");
    }

    ifc.ifc_len = len;
    ifc.ifc_buf = buf;

    if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
      if (errno != EINVAL || lastlen != 0) {
	free(buf);
	if (local_ip_check)
	  return NULL;
	else
	  return strdup("SIOCIFCONF");
      }
    }
    else {
	if (ifc.ifc_len == lastlen)
	  break;  /* the ioctl was happy */
	lastlen = ifc.ifc_len;
      }
    len += 10 * sizeof(struct ifreq);
    free(buf);
  }

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("ioctl was OK, len is %d\n", ifc.ifc_len);
#endif

  for (ptr = buf; ptr < buf + ifc.ifc_len; ) {
    ifr = (struct ifreq *) ptr;

    switch (ifr->ifr_addr.sa_family) {
#ifdef AF_INET6
    case AF_INET6:
      addr1 = &(sin6->sin6_addr);
      tsin6 = (struct sockaddr_in6 *)&(ifr->ifr_addr);
      addr2 = &(tsin6->sin6_addr);
      cmplen = sizeof(tsin6->sin6_addr);
      len = sizeof(struct sockaddr_in6);
      break;
#endif
    case AF_INET:
    default:
      addr1 = &(sin->sin_addr.s_addr);
      tsin = (struct sockaddr_in *)&(ifr->ifr_addr);
      addr2 = &(tsin->sin_addr.s_addr);
      cmplen = sizeof(struct in_addr);
      len = sizeof(struct sockaddr_in);
      break;
    }

#if defined(NETPERF_STANDALONE_DEBUG)
    printf("hello i am interface %s family %d\n",
	   ifr->ifr_name,
	   ifr->ifr_addr.sa_family);
#endif

#ifdef HAVE_SOCKADDR_SA_LEN
    if (sizeof(struct sockaddr) > ifr->ifr_addr.sa_len)
      len = sizeof(struct sockaddr);
    else
      len = ifr->ifr_addr.sa_len;
#endif

    /* we are basicaly ass-u-me-ing that an ifr is only a name and a
       sockaddr */
    ptr += sizeof(ifr->ifr_name) + len;

    if (ifr->ifr_addr.sa_family != sin->sin_family)
      continue;
    else {

#if defined(NETPERF_STANDALONE_DEBUG)
      printf("addr1 %p addr2 %p len %d\n",addr1,addr2,cmplen);
#endif
      if (0 == memcmp(addr1,addr2,cmplen)) {
	struct ifreq flagsreq;
	flagsreq = *ifr;
	/* we've gotten this far - ass-u-me this will work? */
	ioctl(sockfd,SIOCGIFFLAGS, &flagsreq);
	if (flagsreq.ifr_flags & IFF_UP) {
#if defined(NETPERF_STANDALONE_DEBUG)
	  printf("Interface name %s family %d\n",ifr->ifr_name,ifr->ifr_addr.sa_family);
#endif
	  close(sockfd);
	  /* we should probably close the memory leak one of these days */
	  return strdup(ifr->ifr_name);
	}
      }
    }
  }
  close(sockfd);
  free(buf);
  if (local_ip_check)
    return NULL;
  else
    return strdup("EgressByAddr");
#endif
}

#if defined(AF_LINK)
char *
find_egress_interface_by_link(struct sockaddr_dl *socklink) {

  char buffer[IF_NAMESIZE];
  char *cret;

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("socklink asdf index %d nlen %d alen %d slen %d\n",
	 socklink->sdl_index,
	 socklink->sdl_nlen,
	 socklink->sdl_alen,
	 socklink->sdl_slen);
#endif

  /* I suspect we could extract the name from the sockaddr_dl
     directly, and perhaps should, but I really don't like mucking
     about with pointers and offsets and characters so will just punt
     to if_indextoname. raj 2008-03-17 */
  if (socklink->sdl_index != 0) {
    cret = if_indextoname(socklink->sdl_index,buffer);
    if (NULL != cret)
      return strdup(cret);
    else
      return strdup(strerror(errno));
  }
  else if (socklink->sdl_nlen > 0) {
    /* ok, I might have to care after all */
    strncpy(buffer,socklink->sdl_data,socklink->sdl_nlen);
    return strdup(buffer);
  }
  else
    return strdup("noindex");
}

#endif

/* borrows heavily from W Richard Stevens' getrt.c of unp fame */

#define BUFLEN (sizeof(struct rt_msghdr) + 512)

char *
find_egress_interface(struct sockaddr *source, struct sockaddr *dest) {

  int sockfd;
  int ret;
  struct rt_msghdr  *rtm;
  int copy_len;
  char *buffer;
  struct sockaddr_in  *sin;
  struct sockaddr_in6 *sin6;

  /* first, check if the destination address is a local one. if it is,
     return "lo0" as the interface because we will ass-u-me the
     traffic isn't leaving the host */
  if (NULL != find_egress_interface_by_addr(dest,1)) {
#if defined(NETPERF_STANDALONE_DEBUG)
    printf("Destination is a local IP\n");
#endif
    return strdup("lo0");
  }


  sockfd = socket(AF_ROUTE, SOCK_RAW, 0);
  if (sockfd < 0)
    return strdup("socket");

  buffer = calloc(1,BUFLEN);
  if (NULL == buffer)
    return strdup("calloc");

  rtm = (struct rt_msghdr *)buffer;

  rtm->rtm_msglen = sizeof(struct rt_msghdr);
  sin = (struct sockaddr_in *)dest;

  if (AF_INET == sin->sin_family) {

#if defined(NETPERF_STANDALONE_DEBUG)
    printf("Resolving addr is %s\n",inet_ntoa(sin->sin_addr));
#endif

    rtm->rtm_msglen += sizeof(struct sockaddr_in);
    copy_len = sizeof(struct sockaddr_in);
  }
#if defined(AF_INET6)
  else if (AF_INET6 == sin->sin_family) {
    rtm->rtm_msglen += sizeof(struct sockaddr_in6);
    copy_len = sizeof(struct sockaddr_in6);
  }
#endif
  else {
    free(buffer);
    return strdup("Unknown AF");
  }

  rtm->rtm_version = RTM_VERSION;
  rtm->rtm_type = RTM_GET;
  rtm->rtm_addrs = RTA_DST;
  rtm->rtm_pid = getpid();
  rtm->rtm_seq = 12865;

  /* point just beyond the rt_msghdr. */
  memcpy((rtm + 1), dest, copy_len);

  /* send the message */
  ret = write(sockfd,rtm,rtm->rtm_msglen);
  if (ret != rtm->rtm_msglen) {
    free(buffer);
    return strdup("write");
  }

  /* seek the reply */
  do {
    ret = read(sockfd, rtm, BUFLEN);
    if (ret < sizeof(struct rt_msghdr)) {
      free(buffer);
      return strdup("read");
    }
  } while (rtm->rtm_type != RTM_GET ||
	   rtm->rtm_seq  != 12865 ||
	   rtm->rtm_pid  != getpid());
  if ((rtm->rtm_flags & RTF_GATEWAY) &&
      (rtm->rtm_addrs & RTA_GATEWAY)) {
    /* we have a next hop gateway to resolve. we take advantage of the
       observation that if there is a gateway address there "aways"
       seems to be an RTA_DST in front of it */
    sin = (struct sockaddr_in *)(rtm + 1);
    sin6 = (struct sockaddr_in6 *)sin;
    if (AF_INET == sin->sin_family)
      return find_egress_interface(NULL,(struct sockaddr *)(sin + 1));
    else
      return find_egress_interface(NULL,(struct sockaddr *)(sin6 + 1));
  }

  /* once again, we take "advantage" of the item of interest "always"
     being the second in the list. there seem to be two distinct
     "camps" here - in one camp are AIX and Solaris (at least 5.3 and
     10 respectively) which only resolve as far down as a local
     interface IP address.  in the other camp are HP-UX 11iv3 (11.31)
     and I'm _guessing_ BSD and OSX, who are kind enough to take
     things down to an AF_LINK entry. */

  sin = (struct sockaddr_in *)(rtm +1);
  sin = sin + 1;
  sin6 = (struct sockaddr_in6 *)sin;

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("address two %p family %d\n",sin,sin->sin_family);
#endif

  if (AF_INET == sin->sin_family) {
    return find_egress_interface_by_addr((struct sockaddr *)sin,0);
  }
#if defined(AF_INET6)
  else if (AF_INET6 == sin6->sin6_family) {
    return find_egress_interface_by_addr((struct sockaddr *)sin6,0);
  }
#endif
#if defined(AF_LINK)
  else if (AF_LINK == sin->sin_family) {
    return find_egress_interface_by_link((struct sockaddr_dl *)sin);
  }
#endif
  else
    return strdup("LastHop AF");
}


#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {

  struct sockaddr_storage destination;
  struct sockaddr_in *sin;
  int ret;
  char *egress_if;

  sin = (struct sockaddr_in *)&destination;
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = inet_addr(argv[1]);
  sin->sin_len = sizeof(struct sockaddr_in);
  printf("address is %s\n",inet_ntoa(sin->sin_addr));
  egress_if = find_egress_interface(NULL,(struct sockaddr *)&destination);

  printf("egress interface %p %s\n",egress_if,egress_if);

}
#endif
