#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <rte_memcpy.h>
#include <rte_ethdev.h>
#include <rte_config.h>
#include <rte_memzone.h>
#include <rte_mempool.h>

#define RTE_LOGTYPE_APP         RTE_LOGTYPE_USER1

#define BURST_SIZE 32u

/* function prototypes */
void forward_loop(void);
void init(char * port1, char * port2);
void crtl_c_handler(int s);

volatile sig_atomic_t stop;
void crtl_c_handler(int s);

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

	forward_loop();	//Forward packets...

	RTE_LOG(INFO, APP, "Done\n");
	return 0;
}

void init(char * port1, char * port2)
{
	int ret;

	/** first port **/

	/* XXX: is there a better way to get the port id based on the name? */
	portid1 = atoi(port1);

	//packets_pool = rte_pktmbuf_pool_create("packets", 256*1024, 32,
	//	0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	packets_pool = rte_mempool_lookup("ovs_mp_2030_0_262144");
	if (packets_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Cannot find memory pool\n");
	}

	if (packets_pool == NULL) {
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

void forward_loop(void)
{
	unsigned rx_pkts;
	unsigned index;

	struct rte_mbuf * pkts[BURST_SIZE] = {0};

	signal(SIGINT,crtl_c_handler);

	while(likely(!stop))
	{
		rx_pkts = rte_eth_rx_burst(portid1, 0, pkts, BURST_SIZE);
		index = 0;
		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				index += rte_eth_tx_burst(portid2, 0, &pkts[index], rx_pkts - index);
			} while (index < rx_pkts);
		}

		/* do the same in the other sense */
		rx_pkts = rte_eth_rx_burst(portid2, 0, pkts, BURST_SIZE);
		index = 0;
		if (rx_pkts > 0) {
			/* blocking enqueue */
			do {
				index += rte_eth_tx_burst(portid1, 0, &pkts[index], rx_pkts - index);
			} while (index < rx_pkts);
		}
	}
}

void crtl_c_handler(int s)
{
	(void) s; /* Avoid compile warning */
	printf("Requesting stop.\n");
	stop = 1;
}
