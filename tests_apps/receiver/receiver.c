#include <getopt.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
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
#define ALLOC 		1	/* allocate and deallocate packets */
#define NO_ALLOC 	2	/* send the same packets always*/

//Sending methods
#define RING 		1	/* send packets to rte_rings */
#define ETHERNET	2	/* send packets to network devices */

#define USE_BURST
#define BURST_SIZE 32u

#define CALC_RX_STATS
//#define CALC_FREE_RETRIES
//#define CALC_CHECKSUM
#define ALLOC_METHOD ALLOC
#define SEND_MODE ETHERNET

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
void init(char * dev_name);
void print_stats(void);
void ALARMhandler(int sig);
void crtl_c_handler(int s);

unsigned int counter = 0;

volatile sig_atomic_t stop;
volatile sig_atomic_t pause_;

void crtl_c_handler(int s);

struct rte_mempool * packets_pool = NULL;
struct port_statistics stats = {0};

unsigned int kk = 0;

#if SEND_MODE == RING
struct rte_ring *rx_ring = NULL;
#elif SEND_MODE == ETHERNET

uint8_t portid;
struct rte_eth_dev_info dev_info;

/* TODO: verify this setup */
static const struct rte_eth_conf port_conf = {
	.rxmode = {
		.split_hdr_size = 0,
		.header_split = 0,
		.hw_ip_checksum = 0,
		.hw_vlan_filter = 0,
		.jumbo_frame = 0,
		.hw_strip_crc = 0,
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static uint16_t nb_rxd = 128;
static uint16_t nb_txd = 512;
#else
#error "bad value for SEND_MODE"
#endif


int main(int argc, char *argv[])
{
	setlocale(LC_NUMERIC, "en_US.utf-8");

	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv +=  retval;

	if(argc < 1)
	{
		RTE_LOG(INFO, APP, "usage: -- portname\n");
		return 0;
	}

	init(argv[1]);

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

//Print information about all the flags!

#ifdef USE_BURST
	RTE_LOG(INFO, APP, "Burst: Enabled.\n");
#else
	RTE_LOG(INFO, APP, "Burst: Disabled.\n");
#endif

#if ALLOC_METHOD == ALLOC
	RTE_LOG(INFO, APP, "Alloc method: ALLOC.\n");
#elif ALLOC_METHOD == NO_ALLOC
	RTE_LOG(INFO, APP, "Alloc method: NO_ALLOC.\n");
#else
	#error "Bad value for ALLOC_METHOD"
#endif

#ifdef CALC_CHECKSUM
	RTE_LOG(INFO, APP, "Calc Checksum: yes.\n");
#else
	RTE_LOG(INFO, APP, "Calc Checksum: no.\n");
#endif

#if SEND_MODE == RING
	RTE_LOG(INFO, APP, "SEND_MODE method RING.\n");
#elif SEND_MODE == ETHERNET
	RTE_LOG(INFO, APP, "SEND_MODE method is ETHERNET.\n");
#endif

	receive_loop();	//Receive packets...

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

#if SEND_MODE == RING
void init(char * dev_name)
{
	char rx_ring_name[RTE_RING_NAMESIZE];
	sprintf(rx_ring_name, "%s_tx", dev_name);

	if ((rx_ring = rte_ring_lookup(rx_ring_name)) == NULL)
		rte_exit(EXIT_FAILURE, "Cannot find RX ring\n");

	packets_pool = rte_mempool_lookup("ovs_mp_1500_0_262144");
	if(packets_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
}
#elif SEND_MODE == ETHERNET
void init(char * dev_name)
{
	int ret;

	/* XXX: is there a better way to get the port id based on the name? */
	portid = atoi(dev_name);

	RTE_LOG(INFO, APP, "Using ethernet port %d\n", portid);

	/* TODO: verify memory pool creation options */
	packets_pool = rte_pktmbuf_pool_create("packets", 256*1024, 32,
		0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if(packets_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");

	rte_eth_dev_info_get(portid, &dev_info);

	ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device\n");

	ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
			rte_eth_dev_socket_id(portid), NULL, packets_pool);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device rx queue\n");

	ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
				rte_eth_dev_socket_id(portid), NULL);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device tx queue\n");

	ret = rte_eth_dev_start(portid);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot start device\n");

	rte_eth_promiscuous_enable(portid);
}
#endif

void receive_loop(void)
{
#ifdef USE_BURST
	struct rte_mbuf * packets_array[BURST_SIZE] = {0};
	//int i;
	int nreceived;
#else
	struct rte_mbuf * mbuf;
	int retval = 0;
#endif

	signal(SIGALRM, ALARMhandler);
	alarm(PRINT_INTERVAL);

	signal (SIGINT,crtl_c_handler);
	while(likely(!stop /*|| (rte_ring_count(rx_ring) != 0)*/))
	{
		while(pause_);
#ifdef USE_BURST
	#if SEND_MODE == RING
	nreceived = rte_ring_sc_dequeue_burst(rx_ring, (void **) packets_array, BURST_SIZE);
	#elif SEND_MODE == ETHERNET
	nreceived = rte_eth_rx_burst(portid, 0, packets_array, BURST_SIZE);
	#endif
	#ifdef CALC_CHECKSUM
		for(i = 0; i < nreceived; i++)
			for(kk = 0; kk < PKT_LEN/8; kk++)
				checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
	#endif

	#if ALLOC_METHOD == ALLOC
		if(likely(nreceived > 0))
			rte_mempool_mp_put_bulk(packets_pool, (void **) packets_array, nreceived);
	#endif

	stats.rx += nreceived;

#else // [NO] USE_BURST
	#error "No burst is not implemented"
#endif //USE_BURST
	}	//while

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}	//function

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
