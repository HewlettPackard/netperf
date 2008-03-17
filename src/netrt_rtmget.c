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

char *
find_egress_interface_by_addr(struct sockaddr *addr) {
  return strdup("EgressByAddr");
}

#if defined(AF_LINK)
char *
find_egress_interface_by_link(struct sockaddr_dl *link) {

  char buffer[IF_NAMESIZE];
  char *cret;

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("link index %d nlen %d alen %d slen %d\n",
	 link->sdl_index,
	 link->sdl_nlen,
	 link->sdl_alen,
	 link->sdl_slen);
#endif

  /* I suspect we could extract the name from the sockaddr_dl
     directly, and perhaps should, but I really don't like mucking
     about with pointers and offsets and characters so will just punt
     to if_indextoname. raj 2008-03-17 */
  if (link->sdl_index != 0) {
    cret = if_indextoname(link->sdl_index,buffer);
    if (NULL != cret)
      return strdup(cret);
    else
      return strdup(strerror(errno));
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
  void *next_hop;
  struct sockaddr_in  *sin;
  struct sockaddr_in6 *sin6;

  sockfd = socket(AF_ROUTE, SOCK_RAW, 0);
  if (sockfd < 0)
    return (strdup("socket"));

  buffer = calloc(1,BUFLEN); 
  if (NULL == buffer)
    return (strdup("calloc"));

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
  else
    return strdup("Unknown AF");

  rtm->rtm_version = RTM_VERSION;
  rtm->rtm_type = RTM_GET;
  rtm->rtm_addrs = RTA_DST;
  rtm->rtm_pid = getpid();
  rtm->rtm_seq = 12865;

  /* point just beyond the rt_msghdr. */
  memcpy((rtm + 1), dest, copy_len);

  /* send the message */
  ret = write(sockfd,rtm,rtm->rtm_msglen);
  if (ret != rtm->rtm_msglen)
    return(strdup("write"));

  /* seek the reply */
  do {
    ret = read(sockfd, rtm, BUFLEN);
    if (ret < sizeof(struct rt_msghdr))
      return strdup("read");
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

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("rtm_msglen %d\n",rtm->rtm_msglen);
  printf("rtm_errno %d\n",rtm->rtm_errno);
  printf("rtm_index %d\n",rtm->rtm_index);
  printf("rtm_flags %x\n",rtm->rtm_flags);
  printf("rtm_addrs %x\n",rtm->rtm_addrs);
#endif

  sin = (struct sockaddr_in *)(rtm +1);
  sin = sin + 1;
  sin6 = (struct sockaddr_in6 *)sin;

#if defined(NETPERF_STANDALONE_DEBUG)
  printf("address two %p family %d\n",sin,sin->sin_family);
#endif

  if (AF_INET == sin->sin_family)
    return find_egress_interface_by_addr((struct sockaddr *)sin);
#if defined(AF_INET6)
  else if (AF_INET6 == sin6->sin6_family)
    return find_egress_interface_by_addr((struct sockaddr *)sin6);
#endif
#if defined(AF_LINK)
  else if (AF_LINK == sin->sin_family)
    return find_egress_interface_by_link((struct sockaddr_dl *)sin);
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
  printf("address is %s\n",inet_ntoa(sin->sin_addr));
  egress_if = find_egress_interface(NULL,(struct sockaddr *)&destination);

  printf("egress interface %p %s\n",egress_if,egress_if);

}
#endif
