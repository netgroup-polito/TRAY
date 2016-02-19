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
#define ALLOC 		1	/* allocate and deallocate packets */
#define NO_ALLOC 	2	/* send the same packets always*/

//Sending methods
#define RING 		1	/* send packets to rte_rings */
#define ETHERNET	2	/* send packets to network devices */

/* Configuration */
#define USE_BURST
#define BURST_SIZE 32

#define CALC_TX_STATS
//#define CALC_TX_TRIES
#define CALC_ALLOC_STATS
//#define CALC_CHECKSUM

#define ALLOC_METHOD ALLOC
#define SEND_MODE RING

/* Per-port statistics struct */
struct port_statistics {
	uint32_t tx;
	uint32_t tx_retries;
	uint32_t alloc_fails;
} __rte_cache_aligned;

uint64_t checksum = 0;

/* function prototypes */
void send_loop(void);
void init(char * tx_ring_name);
void print_stats(void);
void ALARMhandler(int sig);
void crtl_c_handler(int s);


inline void send_packets(void ** packets);

unsigned int counter = 0;

volatile sig_atomic_t stop;
volatile sig_atomic_t pause_;

struct port_statistics stats;

struct rte_ring *tx_ring = NULL;

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
		RTE_LOG(INFO, APP, "usage: -- portname\n");
		return 0;
	}

	char tx_ring_name[RTE_RING_NAMESIZE];
	sprintf(tx_ring_name, "%s_rx", argv[1]);
	init(tx_ring_name);

	printf("Free count in tx: %d\n", rte_ring_free_count(tx_ring));

	signal(SIGALRM, ALARMhandler);
	alarm(PRINT_INTERVAL);

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

#ifdef USE_BURST
	RTE_LOG(INFO, APP, "Burst Enabled.\n");
#else
	RTE_LOG(INFO, APP, "Burst Disabled.\n");
#endif

#if ALLOC_METHOD == ALLOC
	RTE_LOG(INFO, APP, "Alloc method ALLOC.\n");
#elif ALLOC_METHOD == NO_ALLOC
	RTE_LOG(INFO, APP, "Alloc method is NO_ALLOC.\n");
#endif

	send_loop();

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * tx_ring_name)
{
	if ((tx_ring = rte_ring_lookup(tx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX ring\n");
	}
}

void send_loop(void)
{
	RTE_LOG(INFO, APP, "send_loop()\n");

	char pkt[PKT_SIZE] = {0};
	struct rte_mempool * packets_pool;

	int retval = 0;
	(void) retval;
#ifdef CALC_CHECKSUM
	unsigned int kk = 0;
#endif
	srand(time(NULL));

	//Initializate packet contents
	int i;
	for(i = 0; i < PKT_SIZE; i++)
		pkt[i] = rand()%256;


	packets_pool = rte_mempool_lookup("ovs_mp_1500_0_262144");

	if(packets_pool == NULL)
	{
		RTE_LOG(INFO, APP, "rte_errno: %s\n", rte_strerror(rte_errno));
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	RTE_LOG(INFO, APP, "There are %d free packets in the pool\n",
		rte_mempool_count(packets_pool));

	struct rte_mbuf * packets_array[BURST_SIZE] = {0};

/* prealloc packets */
#if ALLOC_METHOD == NO_ALLOC
	#ifdef USE_BURST
	int n;
	do
	{
		n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
	} while(n != 0 && !stop);
	#else
	struct rte_mbuf * mbuf;

	do {
		mbuf = rte_pktmbuf_alloc(packets_pool);
	} while(mbuf == NULL);

	#endif
#endif

	RTE_LOG(INFO, APP, "Starting sender loop\n");
	signal (SIGINT, crtl_c_handler);
	stop = 0;
	while(likely(!stop))
	{
		while(pause_);
#ifdef USE_BURST

	#if ALLOC_METHOD == NO_ALLOC
	#elif ALLOC_METHOD == ALLOC
	#else
	#error "Bad value for ALLOC_METHOD"
	#endif

	#if ALLOC_METHOD == ALLOC
		int n;
		/* get BURST_SIZE free slots */
		do
		{
			n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
		} while(n != 0 && !stop);
	#endif

		//Copy data to the buffers
		for(i = 0; i < BURST_SIZE; i++)
		{
			rte_memcpy(packets_array[i]->buf_addr, pkt, PKT_SIZE);
			packets_array[i]->next = NULL;
			packets_array[i]->pkt_len = PKT_SIZE;
			packets_array[i]->data_len = PKT_SIZE;

		#ifdef CALC_CHECKSUM
				for(kk = 0; kk < 8; kk++) /** XXX: HARDCODED value**/
					checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
		#endif
		}

		send_packets((void **)packets_array);

		stats.tx += BURST_SIZE;

#else	// [NO] USE_BURST
	#error "NO burst is not implemented"
#endif //USE_BURST
	}

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}

inline void send_packets(void ** packets)
{
#if SEND_MODE == RING
	/* enqueue data (try until all the allocated packets are enqueued) */
	int i = 0;
	int ntosend = BURST_SIZE;
	while(i < ntosend && !stop)
	{
		i += rte_ring_enqueue_burst(tx_ring, (void **) &packets[i], ntosend - i);
	}
#elif SEND_MODE == ETHERNET

#endif
}

void ALARMhandler(int sig)
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

#ifdef CALC_TX_STATS
	printf("TX Packets:\t%'" PRIu32 "\n", stats.tx);
	stats.tx = 0;
#endif

#ifdef CALC_TX_TRIES
	printf("TX retries:\t%'" PRIu32 "\n", stats.tx_retries);
	stats.tx_retries = 0;
#endif

#ifdef CALC_ALLOC_STATS
	printf("Alloc fails:\t%'" PRIu32 "\n", stats.alloc_fails);
	stats.alloc_fails = 0;
#endif

	printf("\n");
}

void crtl_c_handler(int s)
{
	(void) s;
	counter++;
}
