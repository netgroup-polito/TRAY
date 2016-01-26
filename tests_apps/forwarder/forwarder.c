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
void init(char * rx1, char * tx1, char * rx2, char * tx2);
void crtl_c_handler(int s);

volatile sig_atomic_t stop;
void crtl_c_handler(int s);

struct rte_ring *rx_ring1 = NULL;
struct rte_ring *tx_ring1 = NULL;
struct rte_ring *rx_ring2 = NULL;
struct rte_ring *tx_ring2 = NULL;

int main(int argc, char *argv[])
{
	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv +=  retval;

	if(argc < 4)
	{
		RTE_LOG(INFO, APP, "usage: -- rx_ring1 tx_ring1 rx_ring2 tx_ring2\n");
		return 0;
	}

	init(argv[1], argv[2], argv[3], argv[4]);

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

void init(char * rx1, char * tx1, char * rx2, char * tx2)
{

	if ((rx_ring1 = rte_ring_lookup(rx1)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX1 ring: %s\n", rx1);
	}

	if ((tx_ring1 = rte_ring_lookup(tx1)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX1 ring: %s\n", tx1);
	}

	if ((rx_ring2 = rte_ring_lookup(rx2)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX1 ring: %s\n", rx2);
	}

	if ((tx_ring2 = rte_ring_lookup(tx2)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX2 ring: %s\n", tx2);
	}
}

void forward_loop(void)
{
	unsigned rx_pkts;
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
		 /*
		 * Try dequeuing max possible packets first, if that fails, get the
		 * most we can. Loop body should only execute once, maximum.
		 */
		rx_pkts = BURST_SIZE;
		while (unlikely(rte_ring_dequeue_bulk(rx_ring1, pkts, rx_pkts) != 0) &&
			rx_pkts > 0)
		{
			rx_pkts = (uint16_t)RTE_MIN(rte_ring_count(rx_ring1), BURST_SIZE);
		}

		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				rslt = rte_ring_enqueue_bulk(tx_ring1, pkts, rx_pkts);
			} while (rslt == -ENOBUFS);
		}

		/* do the same in the other sense */
		rx_pkts = BURST_SIZE;
		while (unlikely(rte_ring_dequeue_bulk(rx_ring2, pkts, rx_pkts) != 0) &&
			rx_pkts > 0)
		{
			rx_pkts = (uint16_t)RTE_MIN(rte_ring_count(rx_ring2), BURST_SIZE);
		}

		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				rslt = rte_ring_enqueue_bulk(tx_ring2, pkts, rx_pkts);
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
