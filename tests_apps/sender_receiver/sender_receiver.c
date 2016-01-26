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
//#define	NUM_PKTS 1000
#define MBUF_SIZE (PKT_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

#define PRINT_INTERVAL 1

//Allocation methods
#define ALLOC_OVS 1		//Use the queues to allocate/free packets
#define ALLOC_APP 2		//Allocate free packets directly in the application

/* Configuration */
#define USE_BURST
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

#if ALLOC_METHOD == ALLOC_OVS
struct rte_ring *alloc_q = NULL;
#endif

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

#ifdef USE_BURST
	RTE_LOG(INFO, APP, "Burst Enabled.\n");
#else
	RTE_LOG(INFO, APP, "Burst Disabled.\n");
#endif

#if ALLOC_METHOD == ALLOC_OVS
	RTE_LOG(INFO, APP, "Alloc method is OVS.\n");
#elif ALLOC_METHOD == ALLOC_APP
	RTE_LOG(INFO, APP, "Alloc method is APP.\n");
#endif

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

#if ALLOC_METHOD == ALLOC_OVS
	if ((alloc_q = rte_ring_lookup("recycling_queue")) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find alloc ring\n");
	}
#endif
}

void send_loop(void)
{
	RTE_LOG(INFO, APP, "send_loop()\n");
	char pkt[PKT_SIZE] = {0};
	int nreceived;

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

#if ALLOC_METHOD == ALLOC_APP
	struct rte_mempool * packets_pool = rte_mempool_lookup("ovs_mp_1500_0_262144");
	//struct rte_mempool * packets_pool = rte_mempool_lookup("packets");

	//Create mempool
	//struct rte_mempool * packets_pool = rte_mempool_create(
	//	"packets",
	//	NUM_PKTS,
	//	MBUF_SIZE,
	//	CACHE_SIZE,					//This is the size of the mempool cache
	//	sizeof(struct rte_pktmbuf_pool_private),
	//	rte_pktmbuf_pool_init,
	//	NULL,
	//	rte_pktmbuf_init,
	//	NULL,
	//	rte_socket_id(),
	//	0 /*NO_FLAGS*/);


	if(packets_pool == NULL)
	{
		RTE_LOG(INFO, APP, "rte_errno: %s\n", rte_strerror(rte_errno));
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	RTE_LOG(INFO, APP, "There are %d free packets in the pool\n",
		rte_mempool_count(packets_pool));

#endif

#ifdef USE_BURST
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

#else
	struct rte_mbuf * mbuf;
	/* prealloc packet */
	do {
		mbuf = rte_pktmbuf_alloc(packets_pool);
	} while(mbuf == NULL);

#endif

	RTE_LOG(INFO, APP, "Starting sender loop\n");
	signal (SIGINT, crtl_c_handler);
	stop = 0;
	while(likely(!stop))
	{
		while(pause_);
#ifdef USE_BURST

	#if ALLOC_METHOD == ALLOC_OVS
		//Try to get BURS_SIZE free slots
		ntosend = rte_ring_dequeue_burst(alloc_q, (void **) packets_array, BURST_SIZE);
	#elif ALLOC_METHOD == ALLOC_APP
		//do
		//{
		//	n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
		//} while(n != 0 && !stop);
		//ntosend = BURST_SIZE;
	#else
		#error "No implemented"
	#endif

		//Copy data to the buffers
		for(i = 0; i < ntosend; i++)
		{
			rte_memcpy(packets_array[i]->buf_addr, pkt, PKT_SIZE);
			//fill_packet(packets_array[i]->pkt.data);
			packets_array[i]->next = NULL;
			packets_array[i]->pkt_len = PKT_SIZE;
			packets_array[i]->data_len = PKT_SIZE;

		#ifdef CALC_CHECKSUM
			for(i = 0; i < ntosend; i++)
				for(kk = 0; kk < 8; kk++)
					checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
		#endif
		}

		//Enqueue data (try until all the allocated packets are enqueue)
		i = 0;
		while(i < ntosend && !stop)
		{
			i += rte_ring_enqueue_burst(tx_ring, (void **) &packets_array[i], ntosend - i);

			/* also dequeue some packets */
			nreceived= rte_ring_dequeue_burst(rx_ring, (void **) packets_array_rx, BURST_SIZE);
			rx += nreceived; /* update statistics */
		}

#else	// [NO] USE_BURST
	#if ALLOC_METHOD  == ALLOC_OVS //Method 1
		//Read a buffer to be used as a buffer for a packet
		retval = rte_ring_dequeue(alloc_q, (void **)&mbuf);
		if(retval != 0)
		{
		#ifdef CALC_ALLOC_STATS
			//stats.alloc_fails++;
		#endif
			continue;
		}
	#elif ALLOC_METHOD  == ALLOC_APP //Method 2
		//mbuf = rte_pktmbuf_alloc(packets_pool);
		//if(mbuf == NULL)
		//{
		//#ifdef CALC_ALLOC_STATS
		//	stats.alloc_fails++;
		//#endif
		//	continue;
		//}
	#else
		#error "ALLOC_METHOD has a non valid value"
	#endif

	#if DELAY_CYCLES > 0
		//This loop increases mumber of packets per second (don't ask me why)
		unsigned long long j = 0;
		for(j = 0; j < DELAY_CYCLES; j++)
			asm("");
	#endif

		//Copy packet to the correct buffer
		rte_memcpy(mbuf->buf_addr, pkt, PKT_SIZE);
		//fill_packet(mbuf->pkt.data);
		//mbuf->pkt.next = NULL;
		//mbuf->pkt.pkt_len = PKT_SIZE;
		//mbuf->pkt.data_len = PKT_SIZE;
		(void) pkt;
		mbuf->next = NULL;
		mbuf->pkt_len = PKT_SIZE;
		mbuf->data_len = PKT_SIZE;

	#ifdef CALC_CHECKSUM
		for(kk = 0; kk < 8; kk++)
			checksum += ((uint64_t *)mbuf->buf_addr)[kk];
	#endif

		//this method avoids dropping packets:
		//Simple tries until the packet is inserted in the queue
		tryagain:
		retval = rte_ring_enqueue(tx_ring, (void *) mbuf);
		if(retval == -ENOBUFS && !stop)
		{
	#ifdef CALC_TX_TRIES
			//stats.tx_retries++;
	#endif
			goto tryagain;
		}

	#ifdef CALC_TX_STATS
		//stats.tx++;
	#endif

#endif //USE_BURST
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
