#include <stdio.h>
#include <stdbool.h>

#include "bdev_nvme.h"

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/copy_engine.h"
#include "spdk/conf.h"
#include "spdk/env.h"
#include "spdk/io_channel.h"
#include "spdk/log.h"
#include "spdk/string.h"
#include "spdk/queue.h"

#define DEVICE_NAME "s4msung"
#define NUM_THREADS 4

/* Used to pass messages between fio threads */
struct pls_msg {
	spdk_thread_fn	cb_fn;
	void		*cb_arg;
};

//each thread contains its own target
typedef struct pls_target_s
{
	struct spdk_bdev	*bd;
	struct spdk_bdev_desc	*desc;
	struct spdk_io_channel	*ch;
} pls_target_t;

typedef struct pls_thread_s
{
        struct spdk_thread *thread; /* spdk thread context */
        struct spdk_ring *ring; /* ring for passing messages to this thread */
	pls_target_t pls_target;
	TAILQ_HEAD(, pls_poller) pollers; /* List of registered pollers on this thread */

} pls_thread_t;

/* A polling function */
struct pls_poller 
{
	spdk_poller_fn		cb_fn;
	void			*cb_arg;
	uint64_t		period_microseconds;
	TAILQ_ENTRY(pls_poller)	link;
};

char *pci_nvme_addr = "0000:02:00.0";
pls_thread_t pls_ctrl_thread;
pls_thread_t pls_thread[NUM_THREADS] = {0};

int init(void);

static void
spdk_fio_bdev_init_done(void *cb_arg, int rc)
{
	printf("bdev init is done\n");
	*(bool *)cb_arg = true;
}

static size_t pls_poll_thread(pls_thread_t *thread)
{
	struct pls_msg *msg;
	//struct spdk_fio_poller *p, *tmp;
	size_t count;

	/* Process new events */
	count = spdk_ring_dequeue(thread->ring, (void **)&msg, 1);
	if (count > 0) {
		//msg->cb_fn(msg->cb_arg);
		free(msg);
	}

	/* Call all pollers */
	/*
	TAILQ_FOREACH_SAFE(p, &thread->pollers, link, tmp) {
		p->cb_fn(p->cb_arg);
	}
	*/

	return count;
}

//This is pass message function for spdk_allocate_thread
//typedef void (*spdk_thread_fn)(void *ctx);
static void pls_send_msg(spdk_thread_fn fn, void *ctx, void *thread_ctx)
{
        pls_thread_t *thread = thread_ctx;
        struct pls_msg *msg;
        size_t count;

        msg = calloc(1, sizeof(*msg));
        assert(msg != NULL);

        msg->cb_fn = fn;
        msg->cb_arg = ctx;

        count = spdk_ring_enqueue(thread->ring, (void **)&msg, 1);
        if (count != 1) {
                SPDK_ERRLOG("Unable to send message to thread %p. rc: %lu\n", thread, count);
        }
}

static struct spdk_poller* pls_start_poller(void *thread_ctx, spdk_poller_fn fn,
                      			    void *arg, uint64_t period_microseconds)
{
        pls_thread_t *thread = thread_ctx;
        struct pls_poller *poller;

        poller = calloc(1, sizeof(*poller));
        if (poller) 
	{
                SPDK_ERRLOG("Unable to allocate poller\n");
                return NULL;
        }

        poller->cb_fn = fn;
        poller->cb_arg = arg;
        poller->period_microseconds = period_microseconds;

        TAILQ_INSERT_TAIL(&thread->pollers, poller, link);

        return (struct spdk_poller *)poller;
}

static void
spdk_fio_stop_poller(struct spdk_poller *poller, void *thread_ctx)
{
	struct pls_poller *lpoller;
	pls_thread_t *thread = thread_ctx;

	lpoller = (struct pls_poller *)poller;

	TAILQ_REMOVE(&thread->pollers, lpoller, link);

	free(lpoller);
}

int init(void)
{
	int rv = 0;
	struct spdk_conf *config;
	struct spdk_env_opts opts;
	bool done = false;
	size_t cnt;

	//this identifies an unique endpoint on an NVMe fabric
	struct spdk_nvme_transport_id trid = {};
	const char *names[NVME_MAX_BDEVS_PER_RPC];
	size_t count = NVME_MAX_BDEVS_PER_RPC;
	int i;


	/* Parse the SPDK configuration file */

	//just allocate mem via calloc
	config = spdk_conf_allocate();
	if (!config) {
		SPDK_ERRLOG("Unable to allocate configuration file\n");
		return -1;
	}

	//read user file to init spdk_conf struct
	rv = spdk_conf_read(config, "bdev_pls.conf");
	if (rv != 0) {
		SPDK_ERRLOG("Invalid configuration file format\n");
		spdk_conf_free(config);
		return -1;
	}
	if (spdk_conf_first_section(config) == NULL) {
		SPDK_ERRLOG("Invalid configuration file format\n");
		spdk_conf_free(config);
		return -1;
	}
	spdk_conf_set_as_default(config);

	/* Initialize the environment library */
	spdk_env_opts_init(&opts);
	opts.name = "bdev_pls";

	if (spdk_env_init(&opts) < 0) {
		SPDK_ERRLOG("Unable to initialize SPDK env\n");
		spdk_conf_free(config);
		return -1;
	}
	spdk_unaffinitize_thread();

	//ring init (calls rte_ring_create() from DPDK inside)
	pls_ctrl_thread.ring = spdk_ring_create(SPDK_RING_TYPE_MP_SC, 4096, SPDK_ENV_SOCKET_ID_ANY);
	if (!pls_ctrl_thread.ring) 
	{
		SPDK_ERRLOG("failed to allocate ring\n");
		return -1;
	}

	// Initializes the calling(current) thread for I/O channel allocation
	/* typedef void (*spdk_thread_pass_msg)(spdk_thread_fn fn, void *ctx,
				     void *thread_ctx); */
	
	pls_ctrl_thread.thread = spdk_allocate_thread(pls_send_msg, pls_start_poller,
                                 spdk_fio_stop_poller, &pls_ctrl_thread, "pls_ctrl_thread");

        if (!pls_ctrl_thread.thread) 
	{
                spdk_ring_free(pls_ctrl_thread.ring);
                SPDK_ERRLOG("failed to allocate thread\n");
                return -1;
        }

	/* Initialize the copy engine */
	spdk_copy_engine_initialize();

	/* Initialize the bdev layer */
	spdk_bdev_initialize(spdk_fio_bdev_init_done, &done);

	/* First, poll until initialization is done. */
	do {
		pls_poll_thread(&pls_ctrl_thread);
	} while (!done);

	/*
	 * Continue polling until there are no more events.
	 * This handles any final events posted by pollers.
	 */
	do {
		cnt = pls_poll_thread(&pls_ctrl_thread);
	} while (cnt > 0);


	//create device
	/*
	spdk_bdev_nvme_create(struct spdk_nvme_transport_id *trid,
		      const char *base_name,
		      const char **names, size_t *count)
	*/
	//fill up trid.
	trid.trtype = SPDK_NVME_TRANSPORT_PCIE;
	trid.adrfam = 0;
	memcpy(trid.traddr, pci_nvme_addr, strlen(pci_nvme_addr));
	
	//in names returns names of created devices, in count returns number of devices
	rv = spdk_bdev_nvme_create(&trid, DEVICE_NAME, names, &count);

	if (rv)
	{
		printf("error: can't create bdev device!\n");
		return -1;
	}
	for (i = 0; i < count; i++) 
	{
		printf("#%d: device %s created \n", i, names[i]);
	}



	return rv;
}

int init_thread(pls_thread_t *t)
{
	t->ring = spdk_ring_create(SPDK_RING_TYPE_MP_SC, 4096, SPDK_ENV_SOCKET_ID_ANY);
	if (!t->ring) 
	{
		printf("failed to allocate ring\n");
		rv = -1; return rv;
	}

	// Initializes the calling(current) thread for I/O channel allocation
	/* typedef void (*spdk_thread_pass_msg)(spdk_thread_fn fn, void *ctx,
				     void *thread_ctx); */
	
	t->thread = spdk_allocate_thread(pls_send_msg, pls_start_poller,
                                 spdk_fio_stop_poller, t/*XXX*/, "pls_ctrl_thread");

        if (!t->thread) 
	{
                spdk_ring_free(t->ring);
                SPDK_ERRLOG("failed to allocate thread\n");
                return -1;
        }

	int rv = 0;
	t->pls_target.bd = spdk_bdev_get_by_name(DEVICE_NAME);
	if (!bd)
	{
		printf("failed to get device\n");
		rv = 1; return rv;
	}
	else
		printf("got device with name %s\n", DEVICE_NAME);

	//returns a descriptor
	rv = spdk_bdev_open(t->pls_target.bd, 1, NULL, NULL, &t->pls_target.desc);
	if (rv)
	{
		printf("failed to open device\n");
		return rv;
	}

	t->pls_target.ch = spdk_bdev_get_io_channel(t->pls_target.desc);
	if (!t->pls_target.ch) 
	{
		printf("Unable to get I/O channel for bdev.\n");
		spdk_bdev_close(t->pls_target.desc);
		rv = -1; return rv;
	}

	return rv;
}

int main(int argc, char *argv[])
{
	int rv = 0;
	int i;

	rv = init();
	if (rv)
	{
		printf("init failed. exiting\n");
		exit(1);
	}

	for (i = 0; i < THREADS_NUM; i++)
	{
		rv = init_thread(&pls_thread[i]);
		if (rv)
		{
			printf("open_dev failed. exiting\n");
			exit(1);
		}
	}




	return rv;
}
