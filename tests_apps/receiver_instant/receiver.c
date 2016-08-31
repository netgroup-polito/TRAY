#include <rte_ethdev.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <rte_cycles.h>

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

//Allocation methods
#define ALLOC 		1	/* allocate and deallocate packets */
#define NO_ALLOC 	2	/* send the same packets always*/

#define BURST_SIZE 32u

#define CALC_RX_STATS
#define ALLOC_METHOD NO_ALLOC

#define SAMPLE_MS 5
#define SAMPLE_TSC (SAMPLE_MS*rte_get_tsc_hz()/1000)

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
void crtl_c_handler(int s);
int record_stats(void *arg);

unsigned int counter = 0;

volatile sig_atomic_t stop;

void crtl_c_handler(int s);

struct rte_mempool * packets_pool = NULL;
struct port_statistics stats = {0};

unsigned int kk = 0;

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
	argv +=  retval;

	if (argc < 1) {
		RTE_LOG(INFO, APP, "usage: -- portname\n");
		return 0;
	}

	init(argv[1]);

	RTE_LOG(INFO, APP, "Finished Process Init.\n");

//Print information about all the flags!

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

	retval = rte_eal_remote_launch(record_stats, NULL, 1);
	if (retval) {
		RTE_LOG(ERR, APP, "error launching remote function...\n");
		return -1;
	}

	receive_loop();	//Receive packets...

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * dev_name)
{
	int ret;

	/* XXX: is there a better way to get the port id based on the name? */
	portid = atoi(dev_name);

	RTE_LOG(INFO, APP, "Using ethernet port %d\n", portid);

	//packets_pool = rte_pktmbuf_pool_create("packets", 256*1024, 32,
	//	0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	packets_pool = rte_mempool_lookup("ovs_mp_2030_0_262144");
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

void receive_loop(void)
{
	struct rte_mbuf * packets_array[BURST_SIZE] = {0};
	int nreceived;

	signal(SIGINT,crtl_c_handler);
	while (likely(!stop)) {
		nreceived = rte_eth_rx_burst(portid, 0, packets_array, BURST_SIZE);
	#ifdef CALC_CHECKSUM
		for (i = 0; i < nreceived; i++)
			for (kk = 0; kk < PKT_LEN/8; kk++)
				checksum += ((uint64_t *)packets_array[i]->buf_addr)[kk];
	#endif

	#if ALLOC_METHOD == ALLOC
		if (likely(nreceived > 0))
			rte_mempool_mp_put_bulk(packets_pool, (void **) packets_array, nreceived);
	#endif

	stats.rx += nreceived;
	}	//while

#ifdef CALC_CHECKSUM
	printf("Checksum was %" PRIu64 "\n", checksum);
#endif

}	//function

void crtl_c_handler(int s)
{
	(void) s;	//Avoid compile warning
	stop = 1;
}

int record_stats(void *arg)
{
	(void) arg;
	uint64_t stats_vector[120*1000/SAMPLE_MS];
	uint64_t current, old;
	unsigned int index = 0;

	old = rte_get_timer_cycles();

	while (!stop) {
		current = rte_get_timer_cycles();
		if ((current - old) > SAMPLE_TSC) {
			old = current;
			stats_vector[index++] = stats.rx;
		}
	}

	unsigned int i;

	FILE * f = fopen("stats.txt", "w");
	if (f == NULL) {
		RTE_LOG(ERR, APP, "error crearting file\n");
		return -1;
	}

	for (i = 0; i < index; i++) {
		fprintf(f, "%" PRIu64 "\n", stats_vector[index]);
	}

	fclose(f);

	return 0;
}
