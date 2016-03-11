#include <rte_eal.h>
#include <rte_debug.h>
#include <rte_ivshmem.h>
#include <rte_ring.h>

#define N_QUEUES (10u)
#define METADATA_NAME "pippo"

int main(int argc, char **argv)
{
	int ret;
	struct rte_ring * tx[N_QUEUES];
	struct rte_ring * rx[N_QUEUES];
	char ring_name[RTE_RING_NAMESIZE];
	unsigned i;

	char cmdline[512];

	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");

	ret = rte_ivshmem_metadata_create(METADATA_NAME);
	if (ret < 0)
		rte_panic("Cannot create ivshmem metadata\n");

	for(i = 0; i < N_QUEUES; i++)
	{
		/* tx */
		snprintf(ring_name, RTE_RING_NAMESIZE, "r%d_tx", i);
		tx[i] = rte_ring_create(ring_name, 256, 0, 0);
		if(tx[i] == NULL)
			rte_exit(EXIT_FAILURE,"Can not create ring %s\n", ring_name);

		/* rx */
		snprintf(ring_name, RTE_RING_NAMESIZE, "r%d_rx", i);
		rx[i] = rte_ring_create(ring_name, 256, 0, 0);
		if(rx[i] == NULL)
			rte_exit(EXIT_FAILURE,"Can not create ring %s\n", ring_name);
	}

	ret = rte_ivshmem_metadata_add_pmd_ring("ringchimbita1", rx, N_QUEUES, tx, N_QUEUES, METADATA_NAME);
	if (ret < 0)
		rte_panic("Cannot add pmd ring\n");

	if (rte_ivshmem_metadata_cmdline_generate(cmdline, sizeof(cmdline), METADATA_NAME) < 0)
		rte_exit(EXIT_FAILURE, "Failed generating command line for qemu\n");

	printf("command: %s\n", cmdline);

	getchar();

	return 0;
}
