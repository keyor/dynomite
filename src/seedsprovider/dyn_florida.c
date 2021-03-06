#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>

#include "dyn_seeds_provider.h"
#include "dyn_core.h"
#include "dyn_string.h"


#define USERAGENT "HTMLGET 1.0"
#define REQ_HEADER "GET /%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\n\r\n"

#define IP "127.0.0.1"
#define PAGE "REST/v1/admin/get_seeds"
#define PORT 8080


static uint32_t create_tcp_socket();
static uint8_t *build_get_query(uint8_t *host, uint8_t *page);


static int64_t last = 0; //storing last time for seeds check
static uint32_t last_seeds_hash = 0;


static bool seeds_check()
{
	int64_t now = dn_msec_now();

	int64_t delta = (int64_t)(now - last);
	log_debug(LOG_VERB, "Delta or elapsed time : %d", delta);
	log_debug(LOG_VERB, "Seeds check internal %d", SEEDS_CHECK_INTERVAL);

	if (delta > SEEDS_CHECK_INTERVAL) {
		last = now;
		return true;
	}

	return false;
}


static uint32_t
hash_seeds(uint8_t *seeds, size_t length)
{
    const uint8_t *ptr = seeds;
    uint32_t value = 0;

    while (length--) {
        uint32_t val = (uint32_t) *ptr++;
        value += val;
        value += (value << 10);
        value ^= (value >> 6);
    }
    value += (value << 3);
    value ^= (value >> 11);
    value += (value << 15);

    return value;
}

uint8_t florida_get_seeds(struct context * ctx, struct mbuf *seeds_buf) {
	struct sockaddr_in *remote;
	uint32_t sock;
	uint32_t tmpres;
	uint8_t *get;
	uint8_t buf[BUFSIZ + 1];

	log_debug(LOG_VVERB, "Running florida_get_seeds!");

	if (!seeds_check()) {
		return DN_NOOPS;
	}

	sock = create_tcp_socket();
	if (sock == -1) {
		log_debug(LOG_VVERB, "Unable to create a socket");
		return DN_ERROR;
	}

	remote = (struct sockaddr_in *) dn_alloc(sizeof(struct sockaddr_in *));
	remote->sin_family = AF_INET;
	tmpres = inet_pton(AF_INET, IP, (void *)(&(remote->sin_addr.s_addr)));
	remote->sin_port = htons(PORT);

	if(connect(sock, (struct sockaddr *)remote, sizeof(struct sockaddr)) < 0) {
		log_debug(LOG_VVERB, "Unable to connect the destination");
		return DN_ERROR;
	}
	get = build_get_query((uint8_t*) IP, (uint8_t*) PAGE);

	uint32_t sent = 0;
	while(sent < dn_strlen(get))
	{
		tmpres = send(sock, get+sent, dn_strlen(get)-sent, 0);
		if(tmpres == -1){
			log_debug(LOG_VVERB, "Unable to send query");
			return DN_ERROR;
		}
		sent += tmpres;
	}

	mbuf_rewind(seeds_buf);

	memset(buf, 0, sizeof(buf));
	uint32_t htmlstart = 0;
	uint8_t * htmlcontent;

	//assume that the respsone payload is under BUF_SIZE
	while ((tmpres = recv(sock, buf, BUFSIZ, 0)) > 0) {

		if (htmlstart == 0) {
			/* Under certain conditions this will not work.
			 * If the \r\n\r\n part is splitted into two messages
			 * it will fail to detect the beginning of HTML content
			 */
			htmlcontent = (uint8_t *) strstr((char *)buf, "\r\n\r\n");
			if(htmlcontent != NULL) {
				htmlstart = 1;
				htmlcontent += 4;
			}
		} else {
			htmlcontent = buf;
		}

		if(htmlstart) {
			mbuf_copy(seeds_buf, htmlcontent, tmpres - (htmlcontent - buf));
		}

		memset(buf, 0, tmpres);
	}

	if(tmpres < 0) {
		log_debug(LOG_VVERB, "Error receiving data");
	}

	dn_free(get);
	dn_free(remote);
	close(sock);

	uint32_t seeds_hash = hash_seeds(seeds_buf->pos, mbuf_length(seeds_buf));

	if (last_seeds_hash != seeds_hash) {
		last_seeds_hash = seeds_hash;
	} else {
		return DN_NOOPS;
	}

	return DN_OK;
}


uint32_t create_tcp_socket()
{
	uint32_t sock;
	if((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
		log_debug(LOG_VVERB, "Unable to create TCP socket");
		return DN_ERROR;
	}
	return sock;
}


uint8_t *build_get_query(uint8_t *host, uint8_t *page)
{
	uint8_t *query;
	uint8_t *getpage = page;

	if(getpage[0] == '/'){
		getpage = getpage + 1;
		//fprintf(stderr,"Removing leading \"/\", converting %s to %s\n", page, getpage);
	}

	// -5 is to consider the %s %s %s in REQ_HEADER and the ending \0
	query = (uint8_t *) dn_alloc(dn_strlen(host) + dn_strlen(getpage) + dn_strlen(USERAGENT) + dn_strlen(REQ_HEADER) - 5);

	dn_sprintf(query, REQ_HEADER, getpage, host, USERAGENT);
	return query;
}

