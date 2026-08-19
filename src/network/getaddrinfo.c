#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <endian.h>
#include <errno.h>
#include "lookup.h"
#include "netlink.h"

struct addrconfig_ctx {
	int seen4, seen6;
};

static int addrconfig_cb(void *pctx, struct nlmsghdr *h)
{
	struct addrconfig_ctx *ctx = pctx;
	struct ifaddrmsg *msg = NLMSG_DATA(h);
	struct rtattr *rta;
	if (h->nlmsg_type != RTM_NEWADDR) return 0;
	for (rta = NLMSG_RTA(h, sizeof(*msg)); NLMSG_RTAOK(rta, h); rta = RTA_NEXT(rta)) {
		if (rta->rta_type != IFA_ADDRESS && rta->rta_type != IFA_LOCAL)
			continue;
		if (msg->ifa_family == AF_INET && RTA_DATALEN(rta) == 4) {
			struct in_addr a;
			memcpy(&a, RTA_DATA(rta), 4);
			if (a.s_addr != htonl(INADDR_LOOPBACK))
				ctx->seen4 = 1;
		} else if (msg->ifa_family == AF_INET6 && RTA_DATALEN(rta) == 16) {
			struct in6_addr a;
			memcpy(&a, RTA_DATA(rta), 16);
			if (!IN6_IS_ADDR_LOOPBACK(&a))
				ctx->seen6 = 1;
		}
	}
	return 0;
}

int getaddrinfo(const char *restrict host, const char *restrict serv, const struct addrinfo *restrict hint, struct addrinfo **restrict res)
{
	struct service ports[MAXSERVS];
	struct address addrs[MAXADDRS];
	char canon[256], *outcanon;
	int nservs, naddrs, nais, canon_len, i, j, k;
	int family = AF_UNSPEC, flags = 0, proto = 0, socktype = 0;
	int no_family = 0;
	struct aibuf *out;

	if (!host && !serv) return EAI_NONAME;

	if (hint) {
		family = hint->ai_family;
		flags = hint->ai_flags;
		proto = hint->ai_protocol;
		socktype = hint->ai_socktype;

		const int mask = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST |
			AI_V4MAPPED | AI_ALL | AI_ADDRCONFIG | AI_NUMERICSERV;
		if ((flags & mask) != flags)
			return EAI_BADFLAGS;

		switch (family) {
		case AF_INET:
		case AF_INET6:
		case AF_UNSPEC:
			break;
		default:
			return EAI_FAMILY;
		}
	}

	if (flags & AI_ADDRCONFIG) {
		/* Define the "an address is configured" condition for address
		 * families as the system having at least one non-loopback
		 * address of the family, matching the glibc definition
		 * (__check_pf). The upstream musl definition
		 * (ability to create a socket for the family plus routability
		 * of the loopback address) reports IPv6 as configured on any
		 * host with ::1 assigned to the loopback interface, e.g. in
		 * IPv4-only containers, making names such as "localhost"
		 * resolve to the unusable ::1 first. */
		struct addrconfig_ctx ctx = { 0, 0 };
		int seen[2];
		int tf[2] = { AF_INET, AF_INET6 };
		if (__rtnetlink_enumerate(AF_UNSPEC, AF_UNSPEC,
				addrconfig_cb, &ctx) < 0) {
			/* If interface addresses cannot be enumerated, assume
			 * both families are configured, as glibc does. */
			ctx.seen4 = ctx.seen6 = 1;
		}
		seen[0] = ctx.seen4;
		seen[1] = ctx.seen6;
		for (i=0; i<2; i++) {
			if (family==tf[1-i]) continue;
			if (seen[i]) continue;
			if (family == tf[i]) no_family = 1;
			family = tf[1-i];
		}
	}

	nservs = __lookup_serv(ports, serv, proto, socktype, flags);
	if (nservs < 0) return nservs;

	naddrs = __lookup_name(addrs, canon, host, family, flags);
	if (naddrs < 0) return naddrs;

	if (no_family) return EAI_NODATA;

	nais = nservs * naddrs;
	canon_len = strlen(canon);
	out = calloc(1, nais * sizeof(*out) + canon_len + 1);
	if (!out) return EAI_MEMORY;

	if (canon_len) {
		outcanon = (void *)&out[nais];
		memcpy(outcanon, canon, canon_len+1);
	} else {
		outcanon = 0;
	}

	for (k=i=0; i<naddrs; i++) for (j=0; j<nservs; j++, k++) {
		out[k].slot = k;
		out[k].ai = (struct addrinfo){
			.ai_family = addrs[i].family,
			.ai_socktype = ports[j].socktype,
			.ai_protocol = ports[j].proto,
			.ai_addrlen = addrs[i].family == AF_INET
				? sizeof(struct sockaddr_in)
				: sizeof(struct sockaddr_in6),
			.ai_addr = (void *)&out[k].sa,
			.ai_canonname = outcanon };
		if (k) out[k-1].ai.ai_next = &out[k].ai;
		switch (addrs[i].family) {
		case AF_INET:
			out[k].sa.sin.sin_family = AF_INET;
			out[k].sa.sin.sin_port = htons(ports[j].port);
			memcpy(&out[k].sa.sin.sin_addr, &addrs[i].addr, 4);
			break;
		case AF_INET6:
			out[k].sa.sin6.sin6_family = AF_INET6;
			out[k].sa.sin6.sin6_port = htons(ports[j].port);
			out[k].sa.sin6.sin6_scope_id = addrs[i].scopeid;
			memcpy(&out[k].sa.sin6.sin6_addr, &addrs[i].addr, 16);
			break;			
		}
	}
	out[0].ref = nais;
	*res = &out->ai;
	return 0;
}
