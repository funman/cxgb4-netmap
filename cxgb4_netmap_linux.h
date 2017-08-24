/*-
 * Copyright (c) 2014 Chelsio Communications, Inc.
 * All rights reserved.
 * Written by: Navdeep Parhar <np@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <bsd_glue.h>
#include <net/netmap.h>
#include <netmap/netmap_kern.h>

#define assert(x) do { \
    if (!(x)) \
        printk(KERN_INFO "%s:%d : %s failed!\n", __func__, __LINE__, #x); \
} while (0)

#ifdef CXGB4_MAIN
static int
cxgb4_netmap_on(struct netmap_adapter *na)
{
	int rc, i;

    struct net_device *dev = na->ifp;
    struct port_info *pi = netdev_priv(dev);
    struct adapter *adap = pi->adapter;

	if ((adap->flags & FULL_INIT_DONE) == 0)
		return EAGAIN;

	/* Must set caps before calling netmap_reset */
	nm_set_native_flags(na);

    /* RX */
    for (i = 0; i < pi->nqsets; i++) {
        int j;
        struct netmap_kring *kring = &na->rx_rings[i];
        struct sge_eth_rxq *nm_rxq = &adap->sge.ethrxq[kring->ring_id + pi->first_qset];
        struct netmap_slot *slot = netmap_reset(na, NR_RX, i, 0);
        continue;
        uint64_t hwidx;
		assert(slot != NULL);	/* XXXNM: error check, not assert */

        if (adap->sge.fl_pg_order == 0) {
            hwidx = 0;
        } else {
            hwidx = 1;//RX_LARGE_PG_BUF;
        }

		/* We deal with 8 bufs at a time */
		assert((na->num_rx_desc & 7) == 0);
		for (j = 0; j < na->num_rx_desc; j++) {
			uint64_t ba;
			//uint64_t hwidx = nm_rxq->fl.desc[j] & 0x1f;
			void *p = PNMB(na, &slot[j], &ba);
			struct page *pg = virt_to_page(p);
            printk(KERN_INFO "[%u] FL %p PHYS 0x%llx VIRT %p HWIDX %llu PG ORDER %u\n",
                j, &nm_rxq->fl, ba, p, hwidx, adap->sge.fl_pg_order);
			assert(ba != 0);
            ba |= hwidx;
			nm_rxq->fl.desc[j] = __cpu_to_be64(ba);
			set_rx_sw_desc(&nm_rxq->fl.sdesc[j], pg, ba);
		}

		j = nm_rxq->fl.pidx = na->num_rx_desc - 8;
		assert((j & 7) == 0);
		j /= 8;	/* driver pidx to hardware pidx */
		wmb();
		t4_write_reg(adap, MYPF_REG(SGE_PF_KDOORBELL_A),
            QID_V(nm_rxq->fl.cntxt_id) | PIDX_V(j));
	}

    /* TX */
#if 0
	for_each_nm_txq(vi, i, nm_txq) {
		struct netmap_kring *kring = &na->tx_rings[nm_txq->nid];
		if (!nm_kring_pending_on(kring) ||
		    nm_txq->cntxt_id != INVALID_NM_TXQ_CNTXT_ID)
			continue;

		//alloc_nm_txq_hwq(vi, nm_txq);
		slot = netmap_reset(na, NR_TX, i, 0);
		assert(slot != NULL);	/* XXXNM: error check, not assert */
	}

	if (vi->nm_rss == NULL) {
		vi->nm_rss = malloc(vi->rss_size * sizeof(uint16_t), M_CXGBE,
		    M_ZERO | M_WAITOK);
	}
	for (i = 0; i < vi->rss_size;) {
		for_each_nm_rxq(vi, j, nm_rxq) {
			vi->nm_rss[i++] = nm_rxq->iq_abs_id;
			if (i == vi->rss_size)
				break;
		}
	}
	rc = -t4_config_rss_range(sc, sc->mbox, vi->viid, 0, vi->rss_size,
	    vi->nm_rss, vi->rss_size);
#else
    rc = 0;
#endif
	if (rc != 0)
		if_printf(dev, "netmap rss_config failed: %d\n", rc);

	return rc;
}

static int
cxgb4_netmap_off(struct netmap_adapter *na)
{
#if 0
	struct netmap_kring *kring;
	int rc, i;
	struct sge_nm_txq *nm_txq;
	struct sge_nm_rxq *nm_rxq;

	ASSERT_SYNCHRONIZED_OP(sc);

	if ((vi->flags & VI_INIT_DONE) == 0)
		return (0);

	rc = -t4_config_rss_range(sc, sc->mbox, vi->viid, 0, vi->rss_size,
	    vi->rss, vi->rss_size);
	if (rc != 0)
		if_printf(ifp, "failed to restore RSS config: %d\n", rc);
	nm_clear_native_flags(na);

	for_each_nm_txq(vi, i, nm_txq) {
		struct sge_qstat *spg = (void *)&nm_txq->desc[nm_txq->sidx];

		kring = &na->tx_rings[nm_txq->nid];
		if (!nm_kring_pending_off(kring) ||
		    nm_txq->cntxt_id == INVALID_NM_TXQ_CNTXT_ID)
			continue;

		/* Wait for hw pidx to catch up ... */
		while (be16toh(nm_txq->pidx) != spg->pidx)
			pause("nmpidx", 1);

		/* ... and then for the cidx. */
		while (spg->pidx != spg->cidx)
			pause("nmcidx", 1);

		//free_nm_txq_hwq(vi, nm_txq);
	}
	for_each_nm_rxq(vi, i, nm_rxq) {
		struct irq *irq = &sc->irq[vi->first_intr + i];

		kring = &na->rx_rings[nm_rxq->nid];
		if (!nm_kring_pending_off(kring) ||
		    nm_rxq->iq_cntxt_id == INVALID_NM_RXQ_CNTXT_ID)
			continue;

		while (!atomic_cmpset_int(&irq->nm_state, NM_ON, NM_OFF))
			pause("nmst", 1);

		//free_nm_rxq_hwq(vi, nm_rxq);
	}

	return (rc);
#else
	nm_clear_native_flags(na);
    return 0;
#endif
}

static int
cxgb4_netmap_reg(struct netmap_adapter *na, int on)
{
	int rc;
    struct net_device *dev = na->ifp;
    struct port_info *pi = netdev_priv(dev);
    struct adapter *adapter = pi->adapter;

    if (netif_running(dev)) {
        t4_enable_vi(adapter, adapter->pf, pi->viid, false, false);
    }

	if (on)  {
		rc = cxgb4_netmap_on(na);
    } else {
        rc = cxgb4_netmap_off(na);
    }

    if (netif_running(dev)) {
        t4_enable_vi(adapter, adapter->pf, pi->viid, true, true);
    }

	return rc;
}

/* How many packets can a single type1 WR carry in n descriptors */
static inline int
ndesc_to_npkt(const int n)
{
    //MPASS(n > 0 && n <= SGE_MAX_WR_NDESC);

    return (n * 2 - 1);
}

/* Space (in descriptors) needed for a type1 WR that carries n packets */
static inline int
npkt_to_ndesc(const int n)
{
//    MPASS(n > 0 && n <= MAX_NPKT_IN_TYPE1_WR);

    return ((n + 2) / 2);
}

#define EQ_ESIZE 64 // ?
#define SGE_MAX_WR_NDESC (SGE_MAX_WR_LEN / EQ_ESIZE) /* max WR size in desc */
#define MAX_NPKT_IN_TYPE1_WR        (ndesc_to_npkt(SGE_MAX_WR_NDESC))

#define MPASS(...)

static int
reclaim_nm_tx_desc(struct sge_txq *nm_txq)
{
    struct sge_qstat *spg = (void *)&nm_txq->desc[nm_txq->size];
    uint16_t hw_cidx = spg->cidx;   /* snapshot */
    int n = 0;

    hw_cidx = be16toh(hw_cidx);

    while (nm_txq->cidx != hw_cidx) {
        struct fw_eth_tx_pkt_wr *wr = (void *)&nm_txq->desc[nm_txq->cidx];
        unsigned npkt;

        MPASS(wr->op_pkd == htobe32(V_FW_WR_OP(FW_ETH_TX_PKTS_WR)));
        MPASS(wr->type == 1);
        MPASS(wr->npkt > 0 && wr->npkt <= MAX_NPKT_IN_TYPE1_WR);

        npkt = /*wr->npkt;*/ (wr->r3 >> 48) & 0xff;
        n += npkt;
        nm_txq->cidx += npkt_to_ndesc(npkt);

        /*
         * We never sent a WR that wrapped around so the credits coming
         * back, WR by WR, should never cause the cidx to wrap around
         * either.
         */
        MPASS(nm_txq->cidx <= nm_txq->size);
        if (unlikely(nm_txq->cidx == nm_txq->size))
            nm_txq->cidx = 0;
    }

    return n;
}



/* How many contiguous free descriptors starting at pidx */
static inline int
contiguous_ndesc_available(struct sge_txq *nm_txq)
{

    if (nm_txq->cidx > nm_txq->pidx)
        return (nm_txq->cidx - nm_txq->pidx - 1);
    else if (nm_txq->cidx > 0)
        return (nm_txq->size - nm_txq->pidx);
    else
        return (nm_txq->size - nm_txq->pidx - 1);
}

static int
cxgb4_netmap_txsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct net_device *dev = na->ifp;
	struct adapter *adapter = netdev2adap(dev);
	unsigned int const head = kring->rhead;
	unsigned int n;
    struct port_info *pi = netdev2pinfo(dev);
	struct sge_eth_txq *nm_txq = &adapter->sge.ethtxq[kring->ring_id + pi->first_qset];

	/*
	 * Tx was at kring->nr_hwcur last time around and now we need to advance
	 * to kring->rhead.  Note that the driver's pidx moves independent of
	 * netmap's kring->nr_hwcur (pidx counts descriptors and the relation
	 * between descriptors and frames isn't 1:1).
	 */

	unsigned int npkt_remaining = head >= kring->nr_hwcur ? head - kring->nr_hwcur :
	    kring->nkr_num_slots - kring->nr_hwcur + head;
    unsigned int reclaimed = 0;
//	unsigned int txcsum = ifp->if_capenable & (IFCAP_TXCSUM | IFCAP_TXCSUM_IPV6);
	while (npkt_remaining) {
        unsigned ndesc_remaining;
        int d;
		reclaimed += reclaim_nm_tx_desc(&nm_txq->q);
		ndesc_remaining = contiguous_ndesc_available(&nm_txq->q);
		/* # of desc needed to tx all remaining packets */
		d = (npkt_remaining / MAX_NPKT_IN_TYPE1_WR) * SGE_MAX_WR_NDESC;
		/* Can't run out of descriptors with packets still remaining */
		assert(ndesc_remaining > 0);

		if (npkt_remaining % MAX_NPKT_IN_TYPE1_WR)
			d += npkt_to_ndesc(npkt_remaining % MAX_NPKT_IN_TYPE1_WR);

		if (d <= ndesc_remaining)
			n = npkt_remaining;
		else {
			/* Can't send all, calculate how many can be sent */
			n = (ndesc_remaining / SGE_MAX_WR_NDESC) *
			    MAX_NPKT_IN_TYPE1_WR;
			if (ndesc_remaining % SGE_MAX_WR_NDESC)
				n += ndesc_to_npkt(ndesc_remaining % SGE_MAX_WR_NDESC);
		}

		/* Send n packets and update nm_txq->pidx and kring->nr_hwcur */
		npkt_remaining -= n;
		//cxgb4_nm_tx(sc, nm_txq, kring, n, npkt_remaining, txcsum);
	}
	assert(npkt_remaining == 0);
	assert(kring->nr_hwcur == head);
	assert(nm_txq->q.db_pidx == nm_txq->q.pidx);

	/*
	 * Second part: reclaim buffers for completed transmissions.
	 */
	if (reclaimed || flags & NAF_FORCE_RECLAIM || nm_kr_txempty(kring)) {
		reclaimed += reclaim_nm_tx_desc(&nm_txq->q);
		kring->nr_hwtail += reclaimed;
		if (kring->nr_hwtail >= kring->nkr_num_slots)
			kring->nr_hwtail -= kring->nkr_num_slots;
	}

	return (0);
}

static int
cxgb4_netmap_rxsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_ring *ring = kring->ring;
	struct net_device *dev = na->ifp;
	struct adapter *adapter = netdev2adap(dev);
	unsigned int const head = kring->rhead;
	unsigned int n;
    struct port_info *pi = netdev2pinfo(dev);
	struct sge_eth_rxq *nm_rxq = &adapter->sge.ethrxq[kring->ring_id + pi->first_qset];
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;

	if (netmap_no_pendintr || force_update) {
		kring->nr_hwtail = nm_rxq->rspq.cidx;
		kring->nr_kflags &= ~NKR_PENDINTR;
	}

	/* Userspace done with buffers from kring->nr_hwcur to head */
	n = head >= kring->nr_hwcur ? head - kring->nr_hwcur :
	    kring->nkr_num_slots - kring->nr_hwcur + head;
	n &= ~7U;
	if (n > 0) {
		unsigned int fl_pidx = nm_rxq->fl.pidx;
        int db_val = QID_V(nm_rxq->fl.cntxt_id) | adapter->params.arch.sge_fl_db;
		struct netmap_slot *slot = &ring->slot[fl_pidx];
		uint64_t ba;
		int i, dbinc = 0, sidx = kring->nkr_num_slots;
        printk(KERN_INFO "%s(%u)\n", __func__, n);

		/*
		 * We always deal with 8 buffers at a time.  We must have
		 * stopped at an 8B boundary (fl_pidx) last time around and we
		 * must have a multiple of 8B buffers to give to the freelist.
		 */
		assert((fl_pidx & 7) == 0);
		assert((n & 7) == 0);

#define IDXINCR(idx, incr, wrap) do { \
    idx = wrap - idx > incr ? idx + incr : incr - (wrap - idx); \
} while (0)

		IDXINCR(kring->nr_hwcur, n, sidx);
		IDXINCR(nm_rxq->fl.pidx, n, sidx);

		while (n > 0) {
			for (i = 0; i < 8; i++, fl_pidx++, slot++) {
				PNMB(na, slot, &ba);
				assert(ba != 0);
				nm_rxq->fl.desc[fl_pidx] = __cpu_to_be64(ba);
				slot->flags &= ~NS_BUF_CHANGED;
				assert(fl_pidx <= sidx);
			}
			n -= 8;
			if (fl_pidx == sidx) {
				fl_pidx = 0;
				slot = &ring->slot[0];
			}
			if (++dbinc == 8 && n >= 32) {
				wmb();
				t4_write_reg(adapter, MYPF_REG(SGE_PF_KDOORBELL_A),
				    db_val | PIDX_V(dbinc));
				dbinc = 0;
			}
		}
		assert(nm_rxq->fl.pidx == fl_pidx);

		if (dbinc > 0) {
			wmb();
			t4_write_reg(adapter, MYPF_REG(SGE_PF_KDOORBELL_A),
			    db_val | PIDX_V(dbinc));
		}
	}

	return 0;
}

static void
cxgb4_netmap_intr(struct netmap_adapter *na, int onoff)
{
    printk(KERN_INFO "%s()\n", __func__);
}

static void
cxgb4_nm_attach(struct adapter *adapter, int port)
{
	struct netmap_adapter na;
    struct net_device *dev = adapter->port[port];
    struct port_info *pi = netdev2pinfo(dev);

	bzero(&na, sizeof(na));

	na.ifp = dev;
    na.pdev = &adapter->pdev->dev;

	na.num_tx_desc = na.num_rx_desc = adapter->sge.ethtxq[0/*FIXME */].q.size;
	na.nm_txsync = cxgb4_netmap_txsync;
	na.nm_rxsync = cxgb4_netmap_rxsync;
	na.nm_register = cxgb4_netmap_reg;
    na.num_tx_rings = na.num_rx_rings = pi->nqsets;
    na.nm_intr = cxgb4_netmap_intr;

	netmap_attach(&na);
}

static void
cxgb4_nm_detach(struct adapter *adapter, int port)
{
    struct net_device *dev = adapter->port[port];

	netmap_detach(dev);
}
#endif
