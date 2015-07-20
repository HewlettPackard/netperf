#if defined(HAVE_CONFIG_H)
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bits/sockaddr.h>
#include <asm/types.h>
#include <linux/rtnetlink.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netdb.h>

#include <sys/types.h>
#include <unistd.h>

char *
find_egress_interface(struct sockaddr *source, struct sockaddr *dest) {

  struct sockaddr_nl me,them;
  struct sockaddr_in  *in4;
  struct sockaddr_in6 *in6;

  int interface_index = -1;
  char interface_name[IF_NAMESIZE];

  int s;

  /* so, in our msghdr we will put the address and an iovec pointing
     to our request.  that request will consist of a netlink message
     header, a routing message header and some routing attributes.
     the chalice with the palace holds the pellet with the poison, the
     vessel with the pestle holds the brew which is true.  i guess it
     would have been "too easy" to just have a plain old ioctl that
     one calls to get the route for a given destination and
     source... raj 2008-02-11 */

  struct msghdr msg;

  struct iovec iov;

  struct {
    struct nlmsghdr nl;
    struct rtmsg    rt;
    char   buf[1024];
  } request;

  char reply[1024];

  struct nlmsghdr *nlp;
  struct rtmsg *rtp;
  struct rtattr *rtap;
  int nll,rtl;

  int ret;

  /* create and bind the netlink socket */
  s = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

  memset(&me, 0, sizeof(me));
  me.nl_family = AF_NETLINK;
  me.nl_pid = getpid();

  /* probably should be checking a return value... */
  bind(s, (struct sockaddr *)&me, sizeof(me));

  /* create the message we are going to send */

  memset(&request, 0, sizeof(request));
  request.nl.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
  request.nl.nlmsg_flags = NLM_F_REQUEST;
  request.nl.nlmsg_type = RTM_GETROUTE;

  /* time to add the destination attribute to the request */
  if (dest) {
    in4 = (struct sockaddr_in *)dest;
    in6 = (struct sockaddr_in6 *)dest;
    request.rt.rtm_family = in4->sin_family;
    rtap = (struct rtattr *)request.buf;
    rtap->rta_type = RTA_DST;
    if (AF_INET == in4->sin_family) {
      /* while a sin_addr is a multiple of 4 bytes in length, we
	 should still use RTA_SPACE rather than RTA_LENGTH. kudos to
	 Thomas Graf for pointing that out */
      rtap->rta_len = RTA_SPACE(sizeof(in4->sin_addr));
      memcpy(RTA_DATA(rtap), &(in4->sin_addr), sizeof(in4->sin_addr));
    }
    else if (AF_INET6 == in6->sin6_family) {
      rtap->rta_len = RTA_SPACE(sizeof(in6->sin6_addr));
      memcpy(RTA_DATA(rtap), &(in6->sin6_addr), sizeof(in6->sin6_addr));
    }
    else {
      close(s);
      return strdup("UnknownAddressFamily");
    }
  }
  else {
    /* there must always be a destination */
    printf("No destination specified.\n");
    close(s);
    return strdup("NoDestination");
  }

  /* add the length of our request to our overall message length. it
     should already be suitably padded by the previous RTA_SPACE */
  request.nl.nlmsg_len += rtap->rta_len;

  /* now the src */
  if (source) {
    /* the source goes after the dest, so we can just increment by the
       current value of rta_len */
    in4 = (struct sockaddr_in *)source;
    in6 = (struct sockaddr_in6 *)source;

    /* pointer math is fun.  silly me initially tried to to rtap +=
       rtap->rta_len but since rtap isn't pointing at bytes... again
       kudos to Thomas Graf for finding that mistake */
    rtap = (struct rtattr *)((char *)rtap + (rtap->rta_len));
    rtap->rta_type = RTA_SRC;
    if (AF_INET == in4->sin_family) {
      rtap->rta_len = RTA_SPACE(sizeof(in4->sin_addr));
      memcpy(RTA_DATA(rtap), &(in4->sin_addr), sizeof(in4->sin_addr));
    }
    else if (AF_INET6 == in6->sin6_family) {
      rtap->rta_len = RTA_SPACE(sizeof(in6->sin6_addr));
      memcpy(RTA_DATA(rtap), &(in6->sin6_addr), sizeof(in6->sin6_addr));
    }
    else {
      close(s);
      return strdup("UnknownAddressFamily");
    }


    /* add the length of the just added attribute to the overall
     message length. it should already be suitably padded by the
     previous RTA_SPACE */
    request.nl.nlmsg_len += rtap->rta_len;
  }

  /* address it */
  memset(&them, 0, sizeof(them));
  them.nl_family = AF_NETLINK;

  memset(&msg, 0, sizeof(msg));
  msg.msg_name = (void *)&them;
  msg.msg_namelen = sizeof(them);

  iov.iov_base = (void *) &request.nl;
  iov.iov_len  = request.nl.nlmsg_len;

  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  /* send it */
  ret = sendmsg(s, &msg, 0);

  if (ret < 0) {
    close(s);
    return strdup("SendmsgFailure");
  }

  memset(reply,0,sizeof(reply));
  ret = recv(s, reply, sizeof(reply), 0);

  if (ret < 0) {
    close(s);
    return strdup("RecvmsgFailure");
  }

  nll = ret;

  /* Since we are looking for a single route, one has to wonder if
     this is really necessary, but since all the examples I could find
     seemed to be doing it, I'll simply follow along. raj
     2008-02-11 */

  for (nlp = (struct nlmsghdr *)reply;
       NLMSG_OK(nlp,nll);
       nlp = NLMSG_NEXT(nlp, nll)) {
    /* where oh where might the route header be? */
    rtp = (struct rtmsg *) NLMSG_DATA(nlp);

#if 0
    /* we will ass-u-me we are only interested in results for the main
       routing table */
    if (RT_TABLE_MAIN != rtp->rtm_table) {
      printf("skipping table %d\n",rtp->rtm_table);
      continue;
    }
#endif

    for (rtap = (struct rtattr *) RTM_RTA(rtp), rtl = RTM_PAYLOAD(nlp);
	 RTA_OK(rtap, rtl);
	 rtap = RTA_NEXT(rtap,rtl)) {
      if (RTA_OIF == rtap->rta_type) {
	if (-1 == interface_index){
	  interface_index = *((int *) RTA_DATA(rtap));
	}
	else {
	  close(s);
	  return strdup("MultipleInterfacesFound");
	}
      }
    }
  }

  if (interface_index == -1) {
    /* we didn't find it */
    return strdup("InterfaceNotFound");
  }
  else {
    if (NULL == if_indextoname(interface_index,interface_name)) {
      close(s);
      return strdup("IfIndexToNameFailure");
    }
    else {
      close(s);
      return strdup(interface_name);
    }
  }
}
#if defined(NETPERF_STANDALONE_DEBUG)
int
main(int argc, char *argv[]) {

  struct sockaddr_storage source,destination;
  struct sockaddr_in *sin;
  int ret;
  char *egress_if;

  if ((argc < 2) || (argc > 3)) {
    fprintf(stderr,"%s <destIP> [srcip]\n",argv[0]);
    return -1;
  }

  sin = (struct sockaddr_in *)&destination;
  sin->sin_family = AF_INET;
  sin->sin_addr.s_addr = inet_addr(argv[1]);

  printf("destination address is %s\n",inet_ntoa(sin->sin_addr));

  sin = NULL;

  if (argc == 3) {
    sin = (struct sockaddr_in *)&source;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = inet_addr(argv[2]);
  }

  egress_if = find_egress_interface((struct sockaddr *)sin,(struct sockaddr *)&destination);

  printf("egress interface %p %s\n",egress_if,egress_if);

}
#endif
