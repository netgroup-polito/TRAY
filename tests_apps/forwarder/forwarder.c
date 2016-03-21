#include <getopt.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <rte_mbuf.h>
#include <rte_memzone.h>
#include <rte_memcpy.h>
#include <rte_ethdev.h>
#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

#define USE_BURST
#define BURST_SIZE 32u

/* function prototypes */
void forward_loop(void);
void init(char * port1, char * port2);
void crtl_c_handler(int s);

volatile sig_atomic_t stop;
void crtl_c_handler(int s);

//Sending methods
#define RING 		1	/* send packets to rte_rings */
#define ETHERNET	2	/* send packets to network devices */
#define SEND_MODE RING


#if SEND_MODE == RING
struct rte_ring *rx_ring1 = NULL;
struct rte_ring *tx_ring1 = NULL;
struct rte_ring *rx_ring2 = NULL;
struct rte_ring *tx_ring2 = NULL;
#elif SEND_MODE == ETHERNET

struct rte_mempool * packets_pool = NULL;

uint8_t portid1;
uint8_t portid2;

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
#endif

int main(int argc, char *argv[])
{
	int retval = 0;

	if ((retval = rte_eal_init(argc, argv)) < 0)
		return -1;

	argc -= retval;
	argv +=  retval;

	if(argc < 2)
	{
		RTE_LOG(INFO, APP, "usage: -- port1 port2\n");
		return 0;
	}

	init(argv[1], argv[2]);

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

#if SEND_MODE == RING
void init(char * port1, char * port2)
{
	char ring1_tx[RTE_RING_NAMESIZE];
	char ring1_rx[RTE_RING_NAMESIZE];

	char ring2_tx[RTE_RING_NAMESIZE];
	char ring2_rx[RTE_RING_NAMESIZE];

	/* be aware that ring name is in ovs point of view */
	sprintf(ring1_rx, "%s_tx", port1);
	sprintf(ring1_tx, "%s_rx", port1);

	sprintf(ring2_rx, "%s_tx", port2);
	sprintf(ring2_tx, "%s_rx", port2);

	if ((rx_ring1 = rte_ring_lookup(ring1_rx)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX1 ring: %s\n", ring1_rx);
	}

	if ((tx_ring1 = rte_ring_lookup(ring1_tx)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX1 ring: %s\n", ring1_tx);
	}

	if ((rx_ring2 = rte_ring_lookup(ring2_rx)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find RX1 ring: %s\n", ring2_rx);
	}

	if ((tx_ring2 = rte_ring_lookup(ring2_tx)) == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find TX2 ring: %s\n", ring2_tx);
	}
}
#elif SEND_MODE == ETHERNET

void init(char * port1, char * port2)
{
	int ret;

	/** first port **/

	/* XXX: is there a better way to get the port id based on the name? */
	portid1 = atoi(port1);

	/* TODO: verify memory pool creation options */
	packets_pool = rte_pktmbuf_pool_create("packets", 256*1024, 32,
		0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	//packets_pool = rte_mempool_create("packets",
	//									256*1024,
	//									MBUF_SIZE,
	//									32,	/*cache size */
	//									sizeof(struct rte_pktmbuf_pool_private),
	//									rte_pktmbuf_pool_init, NULL,
	//									rte_pktmbuf_init, NULL,
	//									rte_socket_id(), 0);

	if(packets_pool == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	rte_eth_dev_info_get(portid1, &dev_info);

	ret = rte_eth_dev_configure(portid1, 1, 1, &port_conf);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device\n");

	ret = rte_eth_tx_queue_setup(portid1, 0, nb_txd,
								SOCKET_ID_ANY/*rte_eth_dev_socket_id(portid1)*/, NULL);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device tx queue\n");

	ret = rte_eth_rx_queue_setup(portid1, 0, nb_rxd,
			rte_eth_dev_socket_id(portid1), NULL, packets_pool);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device rx queue\n");

	ret = rte_eth_dev_start(portid1);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot start device\n");

	rte_eth_promiscuous_enable(portid1);

	/** second port **/

	/* XXX: is there a better way to get the port id based on the name? */
	portid2 = atoi(port2);

	if(packets_pool == NULL)
	{
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	rte_eth_dev_info_get(portid2, &dev_info);

	ret = rte_eth_dev_configure(portid2, 1, 1, &port_conf);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device\n");

	ret = rte_eth_tx_queue_setup(portid2, 0, nb_txd,
								SOCKET_ID_ANY/*rte_eth_dev_socket_id(portid2)*/, NULL);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device tx queue\n");

	ret = rte_eth_rx_queue_setup(portid2, 0, nb_rxd,
			rte_eth_dev_socket_id(portid2), NULL, packets_pool);
	if(ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure device rx queue\n");

	ret = rte_eth_dev_start(portid2);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot start device\n");

	rte_eth_promiscuous_enable(portid2);

}
#endif

void forward_loop(void)
{
	unsigned rx_pkts;
#ifdef USE_BURST
	struct rte_mbuf * pkts[BURST_SIZE] = {0};
#if SEND_MODE == RING
	int rslt = 0;
#endif

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
	#if SEND_MODE == RING
		rx_pkts = BURST_SIZE;
		while (unlikely(rte_ring_dequeue_bulk(rx_ring1, (void **) pkts, rx_pkts) != 0) &&
			rx_pkts > 0)
		{
			rx_pkts = (uint16_t)RTE_MIN(rte_ring_count(rx_ring1), BURST_SIZE);
		}

		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				rslt = rte_ring_enqueue_bulk(tx_ring2, (void **) pkts, rx_pkts);
			} while (rslt == -ENOBUFS);
		}

	#elif SEND_MODE == ETHERNET
		rx_pkts = rte_eth_rx_burst(portid1, 0, pkts, BURST_SIZE);

		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				rx_pkts -= rte_eth_tx_burst(portid2, 0, &pkts[0], rx_pkts);
			} while (rx_pkts > 0);
		}
	#endif

		///* do the same in the other sense */
		//rx_pkts = BURST_SIZE;
		//while (unlikely(rte_ring_dequeue_bulk(rx_ring2, pkts, rx_pkts) != 0) &&
		//	rx_pkts > 0)
		//{
		//	rx_pkts = (uint16_t)RTE_MIN(rte_ring_count(rx_ring2), BURST_SIZE);
		//}
        //
		//if (rx_pkts > 0) {
		//	/* blocking enqueue */
		//	do {
		//		rslt = rte_ring_enqueue_bulk(tx_ring1, pkts, rx_pkts);
		//	} while (rslt == -ENOBUFS);
		//}
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
