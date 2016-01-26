#include <getopt.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <rte_mbuf.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

#define USE_BURST
#define BURST_SIZE 32u

/* function prototypes */
void forward_loop(void);
void init(char * rx_ring_name, char * tx_ring_name);
void crtl_c_handler(int s);

volatile sig_atomic_t stop;
void crtl_c_handler(int s);

struct rte_ring *rx_ring = NULL;
struct rte_ring *tx_ring = NULL;

int main(int argc, char *argv[])
{
	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv +=  retval;

	if(argc < 2)
	{
		RTE_LOG(INFO, APP, "usage: -- rx_ring tx_ring\n");
		return 0;
	}

	init(argv[1], argv[2]);

	printf("Free count in rx: %d\n", rte_ring_free_count(rx_ring));
	printf("Free count in tx: %d\n", rte_ring_free_count(tx_ring));

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

/* Print information about all the flags! */

#ifdef USE_BURST
	RTE_LOG(INFO, APP, "Burst: Enabled.\n");
#else
	RTE_LOG(INFO, APP, "Burst: Disabled.\n");
#endif

	forward_loop();	//Forward packets...

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * rx_ring_name, char * tx_ring_name)
{

	if ((rx_ring = rte_ring_lookup(rx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX ring: %s\n", rx_ring_name);
	}

	if ((tx_ring = rte_ring_lookup(tx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX ring: %s\n", tx_ring_name);
	}
}

void forward_loop(void)
{

#ifdef USE_BURST
	void * pkts[BURST_SIZE] = {0};
	int rslt = 0;
#else
	struct rte_mbuf * mbuf;
#endif

	signal (SIGINT,crtl_c_handler);

/* code from ovs_client.c in ovs repository */
#ifdef USE_BURST
	while(likely(!stop))
	{
		unsigned rx_pkts = BURST_SIZE;

		/*
		 * Try dequeuing max possible packets first, if that fails, get the
		 * most we can. Loop body should only execute once, maximum.
		 */
		while (unlikely(rte_ring_dequeue_bulk(rx_ring, pkts, rx_pkts) != 0) &&
			rx_pkts > 0)
		{
			rx_pkts = (uint16_t)RTE_MIN(rte_ring_count(rx_ring), BURST_SIZE);
		}

		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				rslt = rte_ring_enqueue_bulk(tx_ring, pkts, rx_pkts);
			} while (rslt == -ENOBUFS);
		}
	}
#else // [NO] USE_BURST
	#error "No implemented"
#endif //USE_BURST

}

void crtl_c_handler(int s)
{
	(void) s; /* Avoid compile warning */
	printf("Requesting stop.\n");
	stop = 1;
}
