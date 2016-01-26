#include <getopt.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_string_fns.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>

#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>
#include <rte_string_fns.h>

#include <time.h>
#include <stdlib.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdio.h>
#include <signal.h>
#include <locale.h>
#include <unistd.h>

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

#define PKT_SIZE 64
#define	NUM_PKTS 1000
#define MBUF_SIZE (PKT_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

#define PRINT_INTERVAL 1

//Allocation methods
#define ALLOC_OVS 1		//Use the queues to allocate/free packets
#define ALLOC_APP 2		//Allocate free packets directly in the application

#define USE_BURST
#define BURST_SIZE 32u

#define CALC_RX_STATS
//#define CALC_FREE_RETRIES
//#define CALC_CHECKSUM
#define ALLOC_METHOD ALLOC_APP

/* Per-port statistics struct */
struct port_statistics {
	uint32_t rx;
	uint32_t free_retries;
} __rte_cache_aligned;

#ifdef CALC_CHECKSUM
	uint64_t checksum = 0;
#endif

/* function prototypes */
void receive_loop(void);
void init(char * rx_ring_name);	//Performs initialization in the case vm to vm communication
void print_stats(void);
void ALARMhandler(int sig);
void crtl_c_handler(int s);

unsigned int counter = 0;

volatile sig_atomic_t stop;
volatile sig_atomic_t pause_;

void crtl_c_handler(int s);

struct port_statistics stats = {0};

unsigned int kk = 0;

struct rte_ring *rx_ring = NULL;

#if ALLOC_METHOD == ALLOC_OVS
struct rte_ring *free_q = NULL;
#endif

int main(int argc, char *argv[])
{
	setlocale(LC_NUMERIC, "en_US.utf-8");

	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv +=  retval;

	RTE_LOG(INFO, APP, "Argc %d!\n", argc);

	if(argc < 1)
	{
		RTE_LOG(INFO, APP, "usage: -- [vm2vm]/[ovs] portname\n");
		return 0;
	}

	init(argv[1]);

	printf("Free count in rx: %d\n", rte_ring_free_count(rx_ring));

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

//Print information about all the flags!

#ifdef USE_BURST
	RTE_LOG(INFO, APP, "Burst: Enabled.\n");
#else
	RTE_LOG(INFO, APP, "Burst: Disabled.\n");
#endif

#if ALLOC_METHOD == ALLOC_OVS
	RTE_LOG(INFO, APP, "Alloc method: OVS.\n");
#elif ALLOC_METHOD == ALLOC_APP
	RTE_LOG(INFO, APP, "Alloc method: APP.\n");
#endif

#ifdef CALC_CHECKSUM
	RTE_LOG(INFO, APP, "Calc Checksum: yes.\n");
#else
	RTE_LOG(INFO, APP, "Calc Checksum: no.\n");
#endif

	receive_loop();	//Receive packets...

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * rx_ring_name)
{
	if ((rx_ring = rte_ring_lookup(rx_ring_name)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX ring\n");
	}

#if ALLOC_METHOD == ALLOC_OVS
	if ((free_q = rte_ring_lookup("recycling_queue")) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find free ring\n");
	}
#endif
}

void receive_loop(void)
{
#ifdef USE_BURST
	struct rte_mbuf * packets_array[BURST_SIZE] = {0};
	int i;
	(void) i;
	int nreceived;
#else
	struct rte_mbuf * mbuf;
	int retval = 0;
#endif

#if ALLOC_METHOD == ALLOC_APP
	//struct rte_mempool * packets_pool = rte_mempool_lookup("packets");
	struct rte_mempool * packets_pool = rte_mempool_lookup("ovs_mp_1500_0_262144");
	if(packets_pool == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}
#endif

	signal(SIGALRM, ALARMhandler);
	alarm(PRINT_INTERVAL);

	signal (SIGINT,crtl_c_handler);
	while(likely(!stop || (rte_ring_count(rx_ring) != 0)))
	{
		while(pause_);
#ifdef USE_BURST
	nreceived = rte_ring_dequeue_burst(rx_ring, (void **) packets_array, BURST_SIZE);

	#ifdef CALC_CHECKSUM
		for(i = 0; i < nreceived; i++)
			for(kk = 0; kk < 8; kk++)
				checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
	#endif

	#if ALLOC_METHOD == ALLOC_OVS
		//Free packets
		i = 0;
		while(i < nreceived)
		{
			i += rte_ring_enqueue_burst(free_q, (void **) &packets_array[i], nreceived - i);
		}
	#elif ALLOC_METHOD == ALLOC_APP
		//if(likely(nreceived > 0))
		//	rte_mempool_mp_put_bulk(packets_pool, (void **) packets_array, nreceived);
	#else
		#error "Not Implemented"
	#endif

	stats.rx += nreceived;

#else // [NO] USE_BURST
		retval = rte_ring_dequeue(rx_ring, (void **)&mbuf);
		if(retval == 0)
		{

	#ifdef CALC_RX_STATS
			stats.rx++;
	#endif

	#ifdef CALC_CHECKSUM
			for(kk = 0; kk < 8; kk++)
				checksum += ((uint64_t *)mbuf->buf_addr)[kk];
	#endif

			//Ok, we read the packet, now it is time to free it
	#if ALLOC_METHOD == ALLOC_OVS			//Method 1:
		tryagain:
		retval = rte_ring_enqueue(free_q, (void *) mbuf);
		if(retval == -ENOBUFS)
		{
		#ifdef CALC_FREE_RETRIES
			stats.free_retries++;
		#endif
			if(!stop)
				goto tryagain;
		}
	#elif 	ALLOC_METHOD == ALLOC_APP  //Method 2
			//rte_pktmbuf_free(mbuf);
	#else
		#error "ALLOC_METHOD has a non valid value"
	#endif
		}
#endif //USE_BURST
	}	//while

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}	//function

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
#ifdef CALC_RX_STATS
	printf("RX Packets:\t%'" PRIu32 "\n", stats.rx);
	stats.rx = 0;
#endif

#ifdef CALC_FREE_RETRIES
	printf("Free Retries:\t%'" PRIu32 "\n", stats.free_retries);
	stats.free_retries = 0;
#endif

	printf("\n");
}

void crtl_c_handler(int s)
{
	(void) s;	//Avoid compile warning
	counter++;
}
