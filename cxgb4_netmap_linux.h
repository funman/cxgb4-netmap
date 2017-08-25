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
        uint64_t hwidx;
		assert(slot != NULL);	/* XXXNM: error check, not assert */
        continue;

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
    assert(nm_native_on(na));

    for (i = 0; i < pi->nqsets; i++) {
        assert(i < na->num_tx_rings);
//        struct sge_eth_txq *nm_txq = &adap->sge.ethtxq[kring->ring_id + pi->first_qset];
        struct netmap_slot *slot = netmap_reset(na, NR_TX, i, 0);
		assert(slot != NULL);	/* XXXNM: error check, not assert */
		/*if (!nm_kring_pending_on(kring) ||
		    nm_txq->cntxt_id != INVALID_NM_TXQ_CNTXT_ID)
			continue;*/
		//alloc_nm_txq_hwq(vi, nm_txq);
	}
#if  0
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

        MPASS(wr->op_pkd == htobe32(FW_WR_OP_V(FW_ETH_TX_PKTS_WR)));
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

/* This function copies 64 byte coalesced work request to
 * memory mapped BAR2 space. For coalesced WR SGE fetches
 * data from the FIFO instead of from Host.
 */
static void cxgb_pio_copy(u64 __iomem *dst, u64 *src)
{
    int count = 8;

    while (count) {
        writeq(*src, dst);
        src++;
        dst++;
        count--;
    }
}


/**
 *  ring_tx_db - check and potentially ring a Tx queue's doorbell
 *  @adap: the adapter
 *  @q: the Tx queue
 *  @n: number of new descriptors to give to HW
 *
 *  Ring the doorbel for a Tx queue.
 */
static inline void ring_tx_db(struct adapter *adap, struct sge_txq *q, int n)
{
    /* Make sure that all writes to the TX Descriptors are committed
     * before we tell the hardware about them.
     */
    wmb();

    /* If we don't have access to the new User Doorbell (T5+), use the old
     * doorbell mechanism; otherwise use the new BAR2 mechanism.
     */
    if (unlikely(q->bar2_addr == NULL)) {
        u32 val = PIDX_V(n);
        unsigned long flags;

        /* For T4 we need to participate in the Doorbell Recovery
         * mechanism.
         */
        spin_lock_irqsave(&q->db_lock, flags);
        if (!q->db_disabled)
            t4_write_reg(adap, MYPF_REG(SGE_PF_KDOORBELL_A),
                     QID_V(q->cntxt_id) | val);
        else
            q->db_pidx_inc += n;
        q->db_pidx = q->pidx;
        spin_unlock_irqrestore(&q->db_lock, flags);
    } else {
        u32 val = PIDX_T5_V(n);

        /* T4 and later chips share the same PIDX field offset within
         * the doorbell, but T5 and later shrank the field in order to
         * gain a bit for Doorbell Priority.  The field was absurdly
         * large in the first place (14 bits) so we just use the T5
         * and later limits and warn if a Queue ID is too large.
         */
        WARN_ON(val & DBPRIO_F);

        /* If we're only writing a single TX Descriptor and we can use
         * Inferred QID registers, we can use the Write Combining
         * Gather Buffer; otherwise we use the simple doorbell.
         */
        if (n == 1 && q->bar2_qid == 0) {
            int index = (q->pidx
                     ? (q->pidx - 1)
                     : (q->size - 1));
            u64 *wr = (u64 *)&q->desc[index];

            cxgb_pio_copy((u64 __iomem *)
                      (q->bar2_addr + SGE_UDB_WCDOORBELL),
                      wr);
        } else {
            writel(val | QID_V(q->bar2_qid),
                   q->bar2_addr + SGE_UDB_KDOORBELL);
        }

        /* This Write Memory Barrier will force the write to the User
         * Doorbell area to be flushed.  This is needed to prevent
         * writes on different CPUs for the same queue from hitting
         * the adapter out of order.  This is required when some Work
         * Requests take the Write Combine Gather Buffer path (user
         * doorbell area offset [SGE_UDB_WCDOORBELL..+63]) and some
         * take the traditional path where we simply increment the
         * PIDX (User Doorbell area SGE_UDB_KDOORBELL) and have the
         * hardware DMA read the actual Work Request.
         */
        wmb();
    }
}

#if 0 // freebsd
/*
 * Write work requests to send 'npkt' frames and ring the doorbell to send them
 * on their way.  No need to check for wraparound.
 */
static void
cxgb4_nm_tx(struct adapter *sc, struct sge_txq *nm_txq,
    struct netmap_kring *kring, int npkt, int npkt_remaining, int txcsum)
{
    struct netmap_ring *ring = kring->ring;
    struct netmap_slot *slot;
    const u_int lim = kring->nkr_num_slots - 1;
    struct fw_eth_tx_pkt_wr *wr = (void *)&nm_txq->desc[nm_txq->pidx];
    uint16_t len;
    uint64_t ba;
    struct cpl_tx_pkt_core *cpl;
    struct ulptx_sgl *usgl;
    int i, n;

    while (npkt) {
        n = min(npkt, MAX_NPKT_IN_TYPE1_WR);
        len = 0;

        wr = (void *)&nm_txq->desc[nm_txq->pidx];
        wr->op_immdlen = htobe32(FW_WR_OP_V(FW_ETH_TX_PKT_WR));
        wr->equiq_to_len16 = htobe32(FW_WR_LEN16_V(npkt_to_len16(n)));
//        wr->npkt = n;
        wr->r3 = cpu_to_be64(0 | (n << 8) | 1);
//        wr->type = 1;
        cpl = (void *)(wr + 1);

        for (i = 0; i < n; i++) {
            slot = &ring->slot[kring->nr_hwcur];
            PNMB(kring->na, slot, &ba);
            MPASS(ba != 0);

            cpl->ctrl0 = nm_txq->cpl_ctrl0;
            cpl->pack = 0;
            cpl->len = htobe16(slot->len);
            /*
             * netmap(4) says "netmap does not use features such as
             * checksum offloading, TCP segmentation offloading,
             * encryption, VLAN encapsulation/decapsulation, etc."
             *
             * So the ncxl interfaces have tx hardware checksumming
             * disabled by default.  But you can override netmap by
             * enabling IFCAP_TXCSUM on the interface manully.
             */
            cpl->ctrl1 = txcsum ? 0 :
                htobe64(F_TXPKT_IPCSUM_DIS | F_TXPKT_L4CSUM_DIS);

            usgl = (void *)(cpl + 1);
            usgl->cmd_nsge = htobe32(ULPTX_CMD_V(ULP_TX_SC_DSGL) |
                ULPTX_NSGE_V(1));
            usgl->len0 = htobe32(slot->len);
            usgl->addr0 = htobe64(ba);

            slot->flags &= ~(NS_REPORT | NS_BUF_CHANGED);
            cpl = (void *)(usgl + 1);
            MPASS(slot->len + len <= UINT16_MAX);
            len += slot->len;
            kring->nr_hwcur = nm_next(kring->nr_hwcur, lim);
        }
        wr->plen = htobe16(len);

        npkt -= n;
        nm_txq->pidx += npkt_to_ndesc(n);
        MPASS(nm_txq->pidx <= nm_txq->sidx);
        if (__predict_false(nm_txq->pidx == nm_txq->sidx)) {
            /*
             * This routine doesn't know how to write WRs that wrap
             * around.  Make sure it wasn't asked to.
             */
            MPASS(npkt == 0);
            nm_txq->pidx = 0;
        }

        if (npkt == 0 && npkt_remaining == 0) {
            /* All done. */
            if (lazy_tx_credit_flush == 0) {
                wr->equiq_to_len16 |= htobe32(F_FW_WR_EQUEQ |
                    F_FW_WR_EQUIQ);
                nm_txq->equeqidx = nm_txq->pidx;
                nm_txq->equiqidx = nm_txq->pidx;
            }
            ring_nm_txq_db(sc, nm_txq);
            return;
        }

        if (NMIDXDIFF(nm_txq, equiqidx) >= nm_txq->sidx / 2) {
            wr->equiq_to_len16 |= htobe32(F_FW_WR_EQUEQ |
                F_FW_WR_EQUIQ);
            nm_txq->equeqidx = nm_txq->pidx;
            nm_txq->equiqidx = nm_txq->pidx;
        } else if (NMIDXDIFF(nm_txq, equeqidx) >= 64) {
            wr->equiq_to_len16 |= htobe32(F_FW_WR_EQUEQ);
            nm_txq->equeqidx = nm_txq->pidx;
        }
        if (NMIDXDIFF(nm_txq, dbidx) >= 2 * SGE_MAX_WR_NDESC)
            ring_nm_txq_db(sc, nm_txq);
    }

    /* Will get called again. */
    MPASS(npkt_remaining);
}
#else
static inline unsigned int flits_to_desc(unsigned int n)
{
    BUG_ON(n > SGE_MAX_WR_LEN / 8);
    return DIV_ROUND_UP(n, 8);
}

static void xmit(struct netmap_adapter *na, struct netmap_slot *slot, struct net_device *dev)
{
    struct sk_buff *skb = alloc_skb(slot->len, GFP_KERNEL);
    uint64_t ba;
    void *p = PNMB(na, slot, &ba);
    memcpy(skb->data, p, slot->len);
    skb->len = slot->len;

    t4_eth_xmit(skb, dev);
//    kfree_skb(skb);
}

/*
 * Write work requests to send 'npkt' frames and ring the doorbell to send them
 * on their way.  No need to check for wraparound.
 */
static void
cxgb4_nm_tx(struct adapter *adap, struct sge_eth_txq *q,
    struct netmap_kring *kring, int npkt, int npkt_remaining, int txcsum)
{
	struct netmap_adapter *na = kring->na;
    struct netmap_ring *ring = kring->ring;
	struct net_device *dev = na->ifp;
    const unsigned int lim = kring->nkr_num_slots - 1;
    struct port_info *pi = netdev2pinfo(dev);

    while (npkt--) {
        struct netmap_slot *slot = &ring->slot[kring->nr_hwcur];
        struct fw_eth_tx_pkt_wr *wr;
        uint64_t ba;
        u32 wr_mid, op, ctrl0;
        uint64_t *end;
        unsigned int flits;
        unsigned int len;
        struct cpl_tx_pkt_core *cpl;
        void *pos;
        int left;
        u64 *p;

        xmit(na, slot, dev);
        continue;

        len = slot->len;
        flits = (len + 7) / 8;
        wr_mid = FW_WR_LEN16_V(DIV_ROUND_UP(flits, 2));

        wr = (void *)&q->q.desc[q->q.pidx];
        wr->equiq_to_len16 = htonl(wr_mid);
        wr->r3 = cpu_to_be64(0);
        end = (u64 *)wr + flits;

        printk(KERN_INFO "%s(flits %u > ndescs %u)\n", __func__, flits, flits_to_desc(flits));

        len += sizeof(*cpl);
        op = FW_ETH_TX_PKT_WR; // PTP?
        wr->op_immdlen =  htonl(FW_WR_OP_V(op) |
                FW_WR_IMMDLEN_V(len));
        cpl = (void*)(wr+1);

		ctrl0 = TXPKT_OPCODE_V(CPL_TX_PKT_XT) | TXPKT_INTF_V(pi->tx_chan) |
			TXPKT_PF_V(adap->pf);
#ifdef CONFIG_CHELSIO_T4_DCB
		if (is_t4(adap->params.chip))
			ctrl0 |= TXPKT_OVLAN_IDX_V(q->dcb_prio);
		else
			ctrl0 |= TXPKT_T5_OVLAN_IDX_V(q->dcb_prio);
#endif
		cpl->ctrl0 = htonl(ctrl0);
		cpl->pack = htons(0);
		cpl->len = htons(len); // - eth header?
		cpl->ctrl1 = cpu_to_be64(TXPKT_L4CSUM_DIS_F | TXPKT_IPCSUM_DIS_F);

#if 0
		inline_tx_skb(skb, &q->q, cpl + 1);
#else
        pos = cpl + 1;
        p = PNMB(kring->na, slot, &ba);
        left = (void*)q->q.stat - pos;
        if (len <= left) {
            memcpy(pos, p, len);
            pos += len;
        } else {
            printk(KERN_INFO "%s(len %u > left %u)\n", __func__, len, left);
        }
        /* 0-pad to multiple of 16 */
        p = PTR_ALIGN(pos, 8);
        if ((uintptr_t)p & 8)
            *p = 0;
#endif

#if 0
		txq_advance(&q->q, ndesc);
#else
		q->q.in_use += 1;
		q->q.pidx += 1;
		if (q->q.pidx >= q->q.size)
			q->q.pidx -= q->q.size;
#endif


		ring_tx_db(adap, &q->q, 1);

        kring->nr_hwcur = nm_next(kring->nr_hwcur, lim);
	}
}
#endif

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
	unsigned int txcsum = 0;//ifp->if_capenable & (IFCAP_TXCSUM | IFCAP_TXCSUM_IPV6);
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

        n = 1; // XXX
		/* Send n packets and update nm_txq->pidx and kring->nr_hwcur */
		npkt_remaining -= n;
		cxgb4_nm_tx(adapter, nm_txq, kring, n, npkt_remaining, txcsum);
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
