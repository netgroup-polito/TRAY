#include <rte_ethdev.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>

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

#define BURST_SIZE 32

/*
 * when defined the sender loops until all the packets of a burst are sent.
 * Otherwise packets not enqueue are freed
 */
#define SEND_FULL_BURST

#define CALC_TX_STATS
//#define CALC_TX_TRIES
#define CALC_ALLOC_STATS
//#define CALC_CHECKSUM

#define ALLOC_METHOD ALLOC

/* Per-port statistics struct */
struct port_statistics {
	uint32_t tx;
	uint32_t tx_retries;
	uint32_t alloc_fails;
} __rte_cache_aligned;

uint64_t checksum = 0;

/* function prototypes */
void send_loop(void);
void init(char * dev_name);
void print_stats(void);
void ALARMhandler(int sig);
void crtl_c_handler(int s);
inline int send_packets(struct rte_mbuf ** packets);

unsigned int counter = 0;

volatile sig_atomic_t stop;
volatile sig_atomic_t pause_;

struct rte_mempool * packets_pool = NULL;

struct port_statistics stats;

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

	init(argv[1]);

	signal(SIGALRM, ALARMhandler);
	alarm(PRINT_INTERVAL);

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

#if ALLOC_METHOD == ALLOC
	RTE_LOG(INFO, APP, "SEND_MODE method ALLOC.\n");
#elif ALLOC_METHOD == NO_ALLOC
	RTE_LOG(INFO, APP, "SEND_MODE method is NO_ALLOC.\n");
#endif

	send_loop();

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * dev_name)
{
	/* XXX: is there a better way to get the port id based on the name? */
	portid = atoi(dev_name);

	int ret;

	//packets_pool = rte_pktmbuf_pool_create("packets", 256*1024, 32,
	//	0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	packets_pool = rte_mempool_lookup("ovs_mp_2030_0_262144");
	if(packets_pool == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	rte_eth_dev_info_get(portid, &dev_info);

	ret = rte_eth_dev_configure(portid, 1, 1, &port_conf);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device\n");

	ret = rte_eth_tx_queue_setup(portid, 0, nb_txd,
								SOCKET_ID_ANY/*rte_eth_dev_socket_id(portid)*/, NULL);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device tx queue\n");

	ret = rte_eth_rx_queue_setup(portid, 0, nb_rxd,
			rte_eth_dev_socket_id(portid), NULL, packets_pool);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device rx queue\n");

	ret = rte_eth_dev_start(portid);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot start device\n");

	rte_eth_promiscuous_enable(portid);
}

void send_loop(void)
{
	RTE_LOG(INFO, APP, "send_loop()\n");

	char pkt[PKT_SIZE] = {0};

	int retval = 0;
	(void) retval;
#ifdef CALC_CHECKSUM
	unsigned int kk = 0;
#endif
	srand(time(NULL));

	//Initializate packet contents
	int i;
	for(i = 0; i < PKT_SIZE; i++)
		pkt[i] = 0xCC;

	struct rte_mbuf * packets_array[BURST_SIZE] = {0};

/* prealloc packets */
#if ALLOC_METHOD == NO_ALLOC
	int n;
	do
	{
		n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
	} while(n != 0 && !stop);

	for(i = 0; i < BURST_SIZE; i++)
	{
		rte_memcpy(rte_pktmbuf_mtod(packets_array[i], void *), pkt, PKT_SIZE);
		packets_array[i]->next = NULL;
		packets_array[i]->pkt_len = PKT_SIZE;
		packets_array[i]->data_len = PKT_SIZE;
	}
#endif

	RTE_LOG(INFO, APP, "Starting sender loop\n");
	signal (SIGINT, crtl_c_handler);
	stop = 0;
	while(likely(!stop))
	{
		while(pause_);

	#if ALLOC_METHOD == ALLOC
		int n;
		/* get BURST_SIZE free slots */
		do
		{
			//unsigned c = rte_mempool_count(packets_pool);
			//RTE_LOG(INFO, APP, "There are %u free slots avaibale in the pool\n",c );
			(void) pkt;
			n = rte_mempool_get_bulk(packets_pool, (void **) packets_array, BURST_SIZE);
			if(unlikely(n != 0))
				stats.alloc_fails++;
		} while(n != 0 && !stop);


		//Copy data to the buffers
		for(i = 0; i < BURST_SIZE; i++)
		{
			/* XXX: is this a valid aprroach? */
			rte_mbuf_refcnt_set(packets_array[i], 1);

			rte_memcpy(rte_pktmbuf_mtod(packets_array[i], void *), pkt, PKT_SIZE);
			packets_array[i]->next = NULL;
			packets_array[i]->pkt_len = PKT_SIZE;
			packets_array[i]->data_len = PKT_SIZE;

		#ifdef CALC_CHECKSUM
				for(kk = 0; kk < 8; kk++) /** XXX: HARDCODED value**/
					checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
		#endif
		}
	#endif
		stats.tx += send_packets(packets_array);
		//getchar();
		//stats.tx += BURST_SIZE;
	}

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}

/* send packets */
inline int send_packets(struct rte_mbuf ** packets)
{
	int i = 0;
	#ifdef SEND_FULL_BURST
	int ntosend = BURST_SIZE;
	do
	{
		#ifdef CALC_TX_TRIES
		stats.tx_retries++;
		#endif

		i += rte_eth_tx_burst(portid, 0, &packets[i], ntosend - i);
		if(unlikely(stop))
			break;
	} while(unlikely(i < ntosend));
	return BURST_SIZE;
	#else
	int sent = i = rte_eth_tx_burst(portid, 0, &packets[0], BURST_SIZE);
	if (unlikely(i < BURST_SIZE)) {
		do {
			rte_pktmbuf_free(packets[i]);
		} while (++i < BURST_SIZE);
	}
	return sent;
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
