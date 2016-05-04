#include <getopt.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>
#include <rte_errno.h>

#include <stdio.h>
#include <signal.h>

#include <locale.h>
#include <time.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <unistd.h>

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

#define PKT_SIZE 64
#define MBUF_SIZE (PKT_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

#define PRINT_INTERVAL 1

//Allocation methods
#define ALLOC_APP 2		//Allocate free packets directly in the application

/* Configuration */
#define BURST_SIZE 32

#define CALC_TX_STATS
//#define CALC_TX_TRIES
#define CALC_ALLOC_STATS
//#define CALC_CHECKSUM

#define ALLOC_METHOD ALLOC_APP
#define DELAY_CYCLES 0

/* Per-port statistics struct */
uint32_t rx;

uint64_t checksum = 0;

/* function prototypes */
void send_loop(void);
void init(char * tx_ring_name, char * rx_ring_name);
void print_stats(void);
void ALARMhandler(int sig);
void crtl_c_handler(int s);

unsigned int counter = 0;

volatile sig_atomic_t stop;
volatile sig_atomic_t pause_;

struct rte_ring *tx_ring = NULL;
struct rte_ring *rx_ring = NULL;

int main(int argc, char *argv[])
{
	setlocale(LC_NUMERIC, "en_US.utf-8");

	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv += retval;

	if(argc < 1)
	{
		RTE_LOG(INFO, APP, "usage: -- port\n");
		return 0;
	}

	char rx_ring_name[RTE_RING_NAMESIZE];
	char tx_ring_name[RTE_RING_NAMESIZE];

	/* be aware that ring name is in ovs point of view */
	sprintf(rx_ring_name, "%s_tx", argv[1]);
	sprintf(tx_ring_name, "%s_rx", argv[1]);

	init(tx_ring_name, rx_ring_name);

	printf("Free count in tx: %d\n", rte_ring_free_count(tx_ring));

	signal(SIGALRM, ALARMhandler);
	alarm(PRINT_INTERVAL);

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

	RTE_LOG(INFO, APP, "Alloc method is APP.\n");

	send_loop();

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * tx_ring_name, char * rx_ring_name)
{
	if ((tx_ring = rte_ring_lookup(tx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX ring\n");
	}

	if ((rx_ring = rte_ring_lookup(rx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX ring\n");
	}
}

void send_loop(void)
{
	RTE_LOG(INFO, APP, "send_loop()\n");
	int nreceived;

	int retval = 0;
	int i;
	(void) retval;
#ifdef CALC_CHECKSUM
	unsigned int kk = 0;
#endif

#if ALLOC_METHOD == ALLOC_APP
	struct rte_mempool * packets_pool = rte_mempool_lookup("ovs_mp_1500_0_262144");
	if(packets_pool == NULL)
	{
		RTE_LOG(INFO, APP, "rte_errno: %s\n", rte_strerror(rte_errno));
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	RTE_LOG(INFO, APP, "There are %d free packets in the pool\n",
		rte_mempool_count(packets_pool));
#endif

	struct rte_mbuf * packets_array[BURST_SIZE] = {0};
	struct rte_mbuf * packets_array_rx[BURST_SIZE] = {0};
	int ntosend;
	int n;
	(void) n;

	/* prealloc packets */
	do
	{
		n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
	} while(n != 0 && !stop);
	ntosend = BURST_SIZE;

	RTE_LOG(INFO, APP, "Starting sender loop\n");
	signal (SIGINT, crtl_c_handler);
	stop = 0;
	while(likely(!stop))
	{
		while(pause_);

#if ALLOC_METHOD == ALLOC_APP
		//do
		//{
		//	n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
		//} while(n != 0 && !stop);
		//ntosend = BURST_SIZE;
#else
		#error "No implemented"
#endif

		//Enqueue data (try until all the allocated packets are enqueue)
		i = 0;
		while(i < ntosend && !stop)
		{
			i += rte_ring_enqueue_burst(tx_ring, (void **) &packets_array[i], ntosend - i);

			/* also dequeue some packets */
			nreceived= rte_ring_dequeue_burst(rx_ring, (void **) packets_array_rx, BURST_SIZE);
			rx += nreceived; /* update statistics */
		}
	}

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}

void  ALARMhandler(int sig)
{
	(void) sig;
	signal(SIGALRM, SIG_IGN);          /* ignore this signal       */
	if(!pause_)
		print_stats();
	signal(SIGALRM, ALARMhandler);     /* reinstall the handler    */
	alarm(PRINT_INTERVAL);

	switch(counter)
	{
		case 0:

		break;

		case 1:
			pause_ = !pause_;
			if(pause_)
				printf("Pausing...\n");
			else
				printf("Resumming...\n");
		break;

		default:
			stop = 1;
			pause_ = 0;
		break;
	}

	counter = 0;
}

void print_stats(void)
{
	printf("RX Packets:\t%'" PRIu32 "\n", rx);
	rx = 0;
//#ifdef CALC_TX_STATS
//	printf("TX Packets:\t%'" PRIu32 "\n", stats.tx);
//	stats.tx = 0;
//#endif
//
//#ifdef CALC_TX_TRIES
//	printf("TX retries:\t%'" PRIu32 "\n", stats.tx_retries);
//	stats.tx_retries = 0;
//#endif
//
//#ifdef CALC_ALLOC_STATS
//	printf("Alloc fails:\t%'" PRIu32 "\n", stats.alloc_fails);
//	stats.alloc_fails = 0;
//#endif

	//printf("\n");
}

void crtl_c_handler(int s)
{
	(void) s;
	counter++;
}
