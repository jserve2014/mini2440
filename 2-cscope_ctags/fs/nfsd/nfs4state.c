/*
*  linux/fs/nfsd/nfs4state.c
*
*  Copyright (c) 2001 The Regents of the University of Michigan.
*  All rights reserved.
*
*  Kendrick Smith <kmsmith@umich.edu>
*  Andy Adamson <kandros@umich.edu>
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*  1. Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*  2. Redistributions in binary form must reproduce the above copyright
*     notice, this list of conditions and the following disclaimer in the
*     documentation and/or other materials provided with the distribution.
*  3. Neither the name of the University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
*  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
*  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
*  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
*  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
*  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
*  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
*  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
*  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
*  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

#include <linux/param.h>
#include <linux/major.h>
#include <linux/slab.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/nfs4.h>
#include <linux/nfsd/state.h>
#include <linux/nfsd/xdr4.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/mutex.h>
#include <linux/lockd/bind.h>
#include <linux/module.h>
#include <linux/sunrpc/svcauth_gss.h>
#include <linux/sunrpc/clnt.h>

#define NFSDDBG_FACILITY                NFSDDBG_PROC

/* Globals */
static time_t lease_time = 90;     /* default lease time */
static time_t user_lease_time = 90;
static time_t boot_time;
static u32 current_ownerid = 1;
static u32 current_fileid = 1;
static u32 current_delegid = 1;
static u32 nfs4_init;
static stateid_t zerostateid;             /* bits all 0 */
static stateid_t onestateid;              /* bits all 1 */
static u64 current_sessionid = 1;

#define ZERO_STATEID(stateid) (!memcmp((stateid), &zerostateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)  (!memcmp((stateid), &onestateid, sizeof(stateid_t)))

/* forward declarations */
static struct nfs4_stateid * find_stateid(stateid_t *stid, int flags);
static struct nfs4_delegation * find_delegation_stateid(struct inode *ino, stateid_t *stid);
static char user_recovery_dirname[PATH_MAX] = "/var/lib/nfs/v4recovery";
static void nfs4_set_recdir(char *recdir);

/* Locking: */

/* Currently used for almost all code touching nfsv4 state: */
static DEFINE_MUTEX(client_mutex);

/*
 * Currently used for the del_recall_lru and file hash table.  In an
 * effort to decrease the scope of the client_mutex, this spinlock may
 * eventually cover more:
 */
static DEFINE_SPINLOCK(recall_lock);

static struct kmem_cache *stateowner_slab = NULL;
static struct kmem_cache *file_slab = NULL;
static struct kmem_cache *stateid_slab = NULL;
static struct kmem_cache *deleg_slab = NULL;

void
nfs4_lock_state(void)
{
	mutex_lock(&client_mutex);
}

void
nfs4_unlock_state(void)
{
	mutex_unlock(&client_mutex);
}

static inline u32
opaque_hashval(const void *ptr, int nbytes)
{
	unsigned char *cptr = (unsigned char *) ptr;

	u32 x = 0;
	while (nbytes--) {
		x *= 37;
		x += *cptr++;
	}
	return x;
}

static struct list_head del_recall_lru;

static inline void
put_nfs4_file(struct nfs4_file *fi)
{
	if (atomic_dec_and_lock(&fi->fi_ref, &recall_lock)) {
		list_del(&fi->fi_hash);
		spin_unlock(&recall_lock);
		iput(fi->fi_inode);
		kmem_cache_free(file_slab, fi);
	}
}

static inline void
get_nfs4_file(struct nfs4_file *fi)
{
	atomic_inc(&fi->fi_ref);
}

static int num_delegations;
unsigned int max_delegations;

/*
 * Open owner state (share locks)
 */

/* hash tables for nfs4_stateowner */
#define OWNER_HASH_BITS              8
#define OWNER_HASH_SIZE             (1 << OWNER_HASH_BITS)
#define OWNER_HASH_MASK             (OWNER_HASH_SIZE - 1)

#define ownerid_hashval(id) \
        ((id) & OWNER_HASH_MASK)
#define ownerstr_hashval(clientid, ownername) \
        (((clientid) + opaque_hashval((ownername.data), (ownername.len))) & OWNER_HASH_MASK)

static struct list_head	ownerid_hashtbl[OWNER_HASH_SIZE];
static struct list_head	ownerstr_hashtbl[OWNER_HASH_SIZE];

/* hash table for nfs4_file */
#define FILE_HASH_BITS                   8
#define FILE_HASH_SIZE                  (1 << FILE_HASH_BITS)
#define FILE_HASH_MASK                  (FILE_HASH_SIZE - 1)
/* hash table for (open)nfs4_stateid */
#define STATEID_HASH_BITS              10
#define STATEID_HASH_SIZE              (1 << STATEID_HASH_BITS)
#define STATEID_HASH_MASK              (STATEID_HASH_SIZE - 1)

#define file_hashval(x) \
        hash_ptr(x, FILE_HASH_BITS)
#define stateid_hashval(owner_id, file_id)  \
        (((owner_id) + (file_id)) & STATEID_HASH_MASK)

static struct list_head file_hashtbl[FILE_HASH_SIZE];
static struct list_head stateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_delegation *
alloc_init_deleg(struct nfs4_client *clp, struct nfs4_stateid *stp, struct svc_fh *current_fh, u32 type)
{
	struct nfs4_delegation *dp;
	struct nfs4_file *fp = stp->st_file;
	struct nfs4_cb_conn *cb = &stp->st_stateowner->so_client->cl_cb_conn;

	dprintk("NFSD alloc_init_deleg\n");
	if (fp->fi_had_conflict)
		return NULL;
	if (num_delegations > max_delegations)
		return NULL;
	dp = kmem_cache_alloc(deleg_slab, GFP_KERNEL);
	if (dp == NULL)
		return dp;
	num_delegations++;
	INIT_LIST_HEAD(&dp->dl_perfile);
	INIT_LIST_HEAD(&dp->dl_perclnt);
	INIT_LIST_HEAD(&dp->dl_recall_lru);
	dp->dl_client = clp;
	get_nfs4_file(fp);
	dp->dl_file = fp;
	dp->dl_flock = NULL;
	get_file(stp->st_vfs_file);
	dp->dl_vfs_file = stp->st_vfs_file;
	dp->dl_type = type;
	dp->dl_ident = cb->cb_ident;
	dp->dl_stateid.si_boot = get_seconds();
	dp->dl_stateid.si_stateownerid = current_delegid++;
	dp->dl_stateid.si_fileid = 0;
	dp->dl_stateid.si_generation = 0;
	fh_copy_shallow(&dp->dl_fh, &current_fh->fh_handle);
	dp->dl_time = 0;
	atomic_set(&dp->dl_count, 1);
	list_add(&dp->dl_perfile, &fp->fi_delegations);
	list_add(&dp->dl_perclnt, &clp->cl_delegations);
	return dp;
}

void
nfs4_put_delegation(struct nfs4_delegation *dp)
{
	if (atomic_dec_and_test(&dp->dl_count)) {
		dprintk("NFSD: freeing dp %p\n",dp);
		put_nfs4_file(dp->dl_file);
		kmem_cache_free(deleg_slab, dp);
		num_delegations--;
	}
}

/* Remove the associated file_lock first, then remove the delegation.
 * lease_modify() is called to remove the FS_LEASE file_lock from
 * the i_flock list, eventually calling nfsd's lock_manager
 * fl_release_callback.
 */
static void
nfs4_close_delegation(struct nfs4_delegation *dp)
{
	struct file *filp = dp->dl_vfs_file;

	dprintk("NFSD: close_delegation dp %p\n",dp);
	dp->dl_vfs_file = NULL;
	/* The following nfsd_close may not actually close the file,
	 * but we want to remove the lease in any case. */
	if (dp->dl_flock)
		vfs_setlease(filp, F_UNLCK, &dp->dl_flock);
	nfsd_close(filp);
}

/* Called under the state lock. */
static void
unhash_delegation(struct nfs4_delegation *dp)
{
	list_del_init(&dp->dl_perfile);
	list_del_init(&dp->dl_perclnt);
	spin_lock(&recall_lock);
	list_del_init(&dp->dl_recall_lru);
	spin_unlock(&recall_lock);
	nfs4_close_delegation(dp);
	nfs4_put_delegation(dp);
}

/* 
 * SETCLIENTID state 
 */

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

#define clientid_hashval(id) \
	((id) & CLIENT_HASH_MASK)
#define clientstr_hashval(name) \
	(opaque_hashval((name), 8) & CLIENT_HASH_MASK)
/*
 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
 * used in reboot/reset lease grace period processing
 *
 * conf_id_hashtbl[], and conf_str_hashtbl[] hold confirmed
 * setclientid_confirmed info. 
 *
 * unconf_str_hastbl[] and unconf_id_hashtbl[] hold unconfirmed 
 * setclientid info.
 *
 * client_lru holds client queue ordered by nfs4_client.cl_time
 * for lease renewal.
 *
 * close_lru holds (open) stateowner queue ordered by nfs4_stateowner.so_time
 * for last close replay.
 */
static struct list_head	reclaim_str_hashtbl[CLIENT_HASH_SIZE];
static int reclaim_str_hashtbl_size = 0;
static struct list_head	conf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	conf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_str_hashtbl[CLIENT_HASH_SIZE];
static struct list_head	unconf_id_hashtbl[CLIENT_HASH_SIZE];
static struct list_head client_lru;
static struct list_head close_lru;

static void unhash_generic_stateid(struct nfs4_stateid *stp)
{
	list_del(&stp->st_hash);
	list_del(&stp->st_perfile);
	list_del(&stp->st_perstateowner);
}

static void free_generic_stateid(struct nfs4_stateid *stp)
{
	put_nfs4_file(stp->st_file);
	kmem_cache_free(stateid_slab, stp);
}

static void release_lock_stateid(struct nfs4_stateid *stp)
{
	unhash_generic_stateid(stp);
	locks_remove_posix(stp->st_vfs_file, (fl_owner_t)stp->st_stateowner);
	free_generic_stateid(stp);
}

static void unhash_lockowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del(&sop->so_idhash);
	list_del(&sop->so_strhash);
	list_del(&sop->so_perstateid);
	while (!list_empty(&sop->so_stateids)) {
		stp = list_first_entry(&sop->so_stateids,
				struct nfs4_stateid, st_perstateowner);
		release_lock_stateid(stp);
	}
}

static void release_lockowner(struct nfs4_stateowner *sop)
{
	unhash_lockowner(sop);
	nfs4_put_stateowner(sop);
}

static void
release_stateid_lockowners(struct nfs4_stateid *open_stp)
{
	struct nfs4_stateowner *lock_sop;

	while (!list_empty(&open_stp->st_lockowners)) {
		lock_sop = list_entry(open_stp->st_lockowners.next,
				struct nfs4_stateowner, so_perstateid);
		/* list_del(&open_stp->st_lockowners);  */
		BUG_ON(lock_sop->so_is_open_owner);
		release_lockowner(lock_sop);
	}
}

static void release_open_stateid(struct nfs4_stateid *stp)
{
	unhash_generic_stateid(stp);
	release_stateid_lockowners(stp);
	nfsd_close(stp->st_vfs_file);
	free_generic_stateid(stp);
}

static void unhash_openowner(struct nfs4_stateowner *sop)
{
	struct nfs4_stateid *stp;

	list_del(&sop->so_idhash);
	list_del(&sop->so_strhash);
	list_del(&sop->so_perclient);
	list_del(&sop->so_perstateid); /* XXX: necessary? */
	while (!list_empty(&sop->so_stateids)) {
		stp = list_first_entry(&sop->so_stateids,
				struct nfs4_stateid, st_perstateowner);
		release_open_stateid(stp);
	}
}

static void release_openowner(struct nfs4_stateowner *sop)
{
	unhash_openowner(sop);
	list_del(&sop->so_close_lru);
	nfs4_put_stateowner(sop);
}

static DEFINE_SPINLOCK(sessionid_lock);
#define SESSION_HASH_SIZE	512
static struct list_head sessionid_hashtbl[SESSION_HASH_SIZE];

static inline int
hash_sessionid(struct nfs4_sessionid *sessionid)
{
	struct nfsd4_sessionid *sid = (struct nfsd4_sessionid *)sessionid;

	return sid->sequence % SESSION_HASH_SIZE;
}

static inline void
dump_sessionid(const char *fn, struct nfs4_sessionid *sessionid)
{
	u32 *ptr = (u32 *)(&sessionid->data[0]);
	dprintk("%s: %u:%u:%u:%u\n", fn, ptr[0], ptr[1], ptr[2], ptr[3]);
}

static void
gen_sessionid(struct nfsd4_session *ses)
{
	struct nfs4_client *clp = ses->se_client;
	struct nfsd4_sessionid *sid;

	sid = (struct nfsd4_sessionid *)ses->se_sessionid.data;
	sid->clientid = clp->cl_clientid;
	sid->sequence = current_sessionid++;
	sid->reserved = 0;
}

/*
 * The protocol defines ca_maxresponssize_cached to include the size of
 * the rpc header, but all we need to cache is the data starting after
 * the end of the initial SEQUENCE operation--the rest we regenerate
 * each time.  Therefore we can advertise a ca_maxresponssize_cached
 * value that is the number of bytes in our cache plus a few additional
 * bytes.  In order to stay on the safe side, and not promise more than
 * we can cache, those additional bytes must be the minimum possible: 24
 * bytes of rpc header (xid through accept state, with AUTH_NULL
 * verifier), 12 for the compound header (with zero-length tag), and 44
 * for the SEQUENCE op response:
 */
#define NFSD_MIN_HDR_SEQ_SZ  (24 + 12 + 44)

/*
 * Give the client the number of ca_maxresponsesize_cached slots it
 * requests, of size bounded by NFSD_SLOT_CACHE_SIZE,
 * NFSD_MAX_MEM_PER_SESSION, and nfsd_drc_max_mem. Do not allow more
 * than NFSD_MAX_SLOTS_PER_SESSION.
 *
 * If we run out of reserved DRC memory we should (up to a point)
 * re-negotiate active sessions and reduce their slot usage to make
 * rooom for new connections. For now we just fail the create session.
 */
static int set_forechannel_drc_size(struct nfsd4_channel_attrs *fchan)
{
	int mem, size = fchan->maxresp_cached;

	if (fchan->maxreqs < 1)
		return nfserr_inval;

	if (size < NFSD_MIN_HDR_SEQ_SZ)
		size = NFSD_MIN_HDR_SEQ_SZ;
	size -= NFSD_MIN_HDR_SEQ_SZ;
	if (size > NFSD_SLOT_CACHE_SIZE)
		size = NFSD_SLOT_CACHE_SIZE;

	/* bound the maxreqs by NFSD_MAX_MEM_PER_SESSION */
	mem = fchan->maxreqs * size;
	if (mem > NFSD_MAX_MEM_PER_SESSION) {
		fchan->maxreqs = NFSD_MAX_MEM_PER_SESSION / size;
		if (fchan->maxreqs > NFSD_MAX_SLOTS_PER_SESSION)
			fchan->maxreqs = NFSD_MAX_SLOTS_PER_SESSION;
		mem = fchan->maxreqs * size;
	}

	spin_lock(&nfsd_drc_lock);
	/* bound the total session drc memory ussage */
	if (mem + nfsd_drc_mem_used > nfsd_drc_max_mem) {
		fchan->maxreqs = (nfsd_drc_max_mem - nfsd_drc_mem_used) / size;
		mem = fchan->maxreqs * size;
	}
	nfsd_drc_mem_used += mem;
	spin_unlock(&nfsd_drc_lock);

	if (fchan->maxreqs == 0)
		return nfserr_serverfault;

	fchan->maxresp_cached = size + NFSD_MIN_HDR_SEQ_SZ;
	return 0;
}

/*
 * fchan holds the client values on input, and the server values on output
 */
static int init_forechannel_attrs(struct svc_rqst *rqstp,
				  struct nfsd4_channel_attrs *session_fchan,
				  struct nfsd4_channel_attrs *fchan)
{
	int status = 0;
	__u32   maxcount = svc_max_payload(rqstp);

	/* headerpadsz set to zero in encode routine */

	/* Use the client's max request and max response size if possible */
	if (fchan->maxreq_sz > maxcount)
		fchan->maxreq_sz = maxcount;
	session_fchan->maxreq_sz = fchan->maxreq_sz;

	if (fchan->maxresp_sz > maxcount)
		fchan->maxresp_sz = maxcount;
	session_fchan->maxresp_sz = fchan->maxresp_sz;

	/* Use the client's maxops if possible */
	if (fchan->maxops > NFSD_MAX_OPS_PER_COMPOUND)
		fchan->maxops = NFSD_MAX_OPS_PER_COMPOUND;
	session_fchan->maxops = fchan->maxops;

	/* FIXME: Error means no more DRC pages so the server should
	 * recover pages from existing sessions. For now fail session
	 * creation.
	 */
	status = set_forechannel_drc_size(fchan);

	session_fchan->maxresp_cached = fchan->maxresp_cached;
	session_fchan->maxreqs = fchan->maxreqs;

	dprintk("%s status %d\n", __func__, status);
	return status;
}

static void
free_session_slots(struct nfsd4_session *ses)
{
	int i;

	for (i = 0; i < ses->se_fchannel.maxreqs; i++)
		kfree(ses->se_slots[i]);
}

static int
alloc_init_session(struct svc_rqst *rqstp, struct nfs4_client *clp,
		   struct nfsd4_create_session *cses)
{
	struct nfsd4_session *new, tmp;
	struct nfsd4_slot *sp;
	int idx, slotsize, cachesize, i;
	int status;

	memset(&tmp, 0, sizeof(tmp));

	/* FIXME: For now, we just accept the client back channel attributes. */
	tmp.se_bchannel = cses->back_channel;
	status = init_forechannel_attrs(rqstp, &tmp.se_fchannel,
					&cses->fore_channel);
	if (status)
		goto out;

	BUILD_BUG_ON(NFSD_MAX_SLOTS_PER_SESSION * sizeof(struct nfsd4_slot)
		     + sizeof(struct nfsd4_session) > PAGE_SIZE);

	status = nfserr_serverfault;
	/* allocate struct nfsd4_session and slot table pointers in one piece */
	slotsize = tmp.se_fchannel.maxreqs * sizeof(struct nfsd4_slot *);
	new = kzalloc(sizeof(*new) + slotsize, GFP_KERNEL);
	if (!new)
		goto out;

	memcpy(new, &tmp, sizeof(*new));

	/* allocate each struct nfsd4_slot and data cache in one piece */
	cachesize = new->se_fchannel.maxresp_cached - NFSD_MIN_HDR_SEQ_SZ;
	for (i = 0; i < new->se_fchannel.maxreqs; i++) {
		sp = kzalloc(sizeof(*sp) + cachesize, GFP_KERNEL);
		if (!sp)
			goto out_free;
		new->se_slots[i] = sp;
	}

	new->se_client = clp;
	gen_sessionid(new);
	idx = hash_sessionid(&new->se_sessionid);
	memcpy(clp->cl_sessionid.data, new->se_sessionid.data,
	       NFS4_MAX_SESSIONID_LEN);

	new->se_flags = cses->flags;
	kref_init(&new->se_ref);
	spin_lock(&sessionid_lock);
	list_add(&new->se_hash, &sessionid_hashtbl[idx]);
	list_add(&new->se_perclnt, &clp->cl_sessions);
	spin_unlock(&sessionid_lock);

	status = nfs_ok;
out:
	return status;
out_free:
	free_session_slots(new);
	kfree(new);
	goto out;
}

/* caller must hold sessionid_lock */
static struct nfsd4_session *
find_in_sessionid_hashtbl(struct nfs4_sessionid *sessionid)
{
	struct nfsd4_session *elem;
	int idx;

	dump_sessionid(__func__, sessionid);
	idx = hash_sessionid(sessionid);
	dprintk("%s: idx is %d\n", __func__, idx);
	/* Search in the appropriate list */
	list_for_each_entry(elem, &sessionid_hashtbl[idx], se_hash) {
		dump_sessionid("list traversal", &elem->se_sessionid);
		if (!memcmp(elem->se_sessionid.data, sessionid->data,
			    NFS4_MAX_SESSIONID_LEN)) {
			return elem;
		}
	}

	dprintk("%s: session not found\n", __func__);
	return NULL;
}

/* caller must hold sessionid_lock */
static void
unhash_session(struct nfsd4_session *ses)
{
	list_del(&ses->se_hash);
	list_del(&ses->se_perclnt);
}

static void
release_session(struct nfsd4_session *ses)
{
	spin_lock(&sessionid_lock);
	unhash_session(ses);
	spin_unlock(&sessionid_lock);
	nfsd4_put_session(ses);
}

void
free_session(struct kref *kref)
{
	struct nfsd4_session *ses;

	ses = container_of(kref, struct nfsd4_session, se_ref);
	spin_lock(&nfsd_drc_lock);
	nfsd_drc_mem_used -= ses->se_fchannel.maxreqs * NFSD_SLOT_CACHE_SIZE;
	spin_unlock(&nfsd_drc_lock);
	free_session_slots(ses);
	kfree(ses);
}

static inline void
renew_client(struct nfs4_client *clp)
{
	/*
	* Move client to the end to the LRU list.
	*/
	dprintk("renewing client (clientid %08x/%08x)\n", 
			clp->cl_clientid.cl_boot, 
			clp->cl_clientid.cl_id);
	list_move_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

/* SETCLIENTID and SETCLIENTID_CONFIRM Helper functions */
static int
STALE_CLIENTID(clientid_t *clid)
{
	if (clid->cl_boot == boot_time)
		return 0;
	dprintk("NFSD stale clientid (%08x/%08x) boot_time %08lx\n",
		clid->cl_boot, clid->cl_id, boot_time);
	return 1;
}

/* 
 * XXX Should we use a slab cache ?
 * This type of memory management is somewhat inefficient, but we use it
 * anyway since SETCLIENTID is not a common operation.
 */
static struct nfs4_client *alloc_client(struct xdr_netobj name)
{
	struct nfs4_client *clp;

	clp = kzalloc(sizeof(struct nfs4_client), GFP_KERNEL);
	if (clp == NULL)
		return NULL;
	clp->cl_name.data = kmalloc(name.len, GFP_KERNEL);
	if (clp->cl_name.data == NULL) {
		kfree(clp);
		return NULL;
	}
	memcpy(clp->cl_name.data, name.data, name.len);
	clp->cl_name.len = name.len;
	return clp;
}

static void
shutdown_callback_client(struct nfs4_client *clp)
{
	struct rpc_clnt *clnt = clp->cl_cb_conn.cb_client;

	if (clnt) {
		/*
		 * Callback threads take a reference on the client, so there
		 * should be no outstanding callbacks at this point.
		 */
		clp->cl_cb_conn.cb_client = NULL;
		rpc_shutdown_client(clnt);
	}
}

static inline void
free_client(struct nfs4_client *clp)
{
	shutdown_callback_client(clp);
	if (clp->cl_cb_xprt)
		svc_xprt_put(clp->cl_cb_xprt);
	if (clp->cl_cred.cr_group_info)
		put_group_info(clp->cl_cred.cr_group_info);
	kfree(clp->cl_principal);
	kfree(clp->cl_name.data);
	kfree(clp);
}

void
put_nfs4_client(struct nfs4_client *clp)
{
	if (atomic_dec_and_test(&clp->cl_count))
		free_client(clp);
}

static void
expire_client(struct nfs4_client *clp)
{
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head reaplist;

	dprintk("NFSD: expire_client cl_count %d\n",
	                    atomic_read(&clp->cl_count));

	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	while (!list_empty(&clp->cl_delegations)) {
		dp = list_entry(clp->cl_delegations.next, struct nfs4_delegation, dl_perclnt);
		dprintk("NFSD: expire client. dp %p, fp %p\n", dp,
				dp->dl_flock);
		list_del_init(&dp->dl_perclnt);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	while (!list_empty(&reaplist)) {
		dp = list_entry(reaplist.next, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	list_del(&clp->cl_idhash);
	list_del(&clp->cl_strhash);
	list_del(&clp->cl_lru);
	while (!list_empty(&clp->cl_openowners)) {
		sop = list_entry(clp->cl_openowners.next, struct nfs4_stateowner, so_perclient);
		release_openowner(sop);
	}
	while (!list_empty(&clp->cl_sessions)) {
		struct nfsd4_session  *ses;
		ses = list_entry(clp->cl_sessions.next, struct nfsd4_session,
				 se_perclnt);
		release_session(ses);
	}
	put_nfs4_client(clp);
}

static void copy_verf(struct nfs4_client *target, nfs4_verifier *source)
{
	memcpy(target->cl_verifier.data, source->data,
			sizeof(target->cl_verifier.data));
}

static void copy_clid(struct nfs4_client *target, struct nfs4_client *source)
{
	target->cl_clientid.cl_boot = source->cl_clientid.cl_boot; 
	target->cl_clientid.cl_id = source->cl_clientid.cl_id; 
}

static void copy_cred(struct svc_cred *target, struct svc_cred *source)
{
	target->cr_uid = source->cr_uid;
	target->cr_gid = source->cr_gid;
	target->cr_group_info = source->cr_group_info;
	get_group_info(target->cr_group_info);
}

static int same_name(const char *n1, const char *n2)
{
	return 0 == memcmp(n1, n2, HEXDIR_LEN);
}

static int
same_verf(nfs4_verifier *v1, nfs4_verifier *v2)
{
	return 0 == memcmp(v1->data, v2->data, sizeof(v1->data));
}

static int
same_clid(clientid_t *cl1, clientid_t *cl2)
{
	return (cl1->cl_boot == cl2->cl_boot) && (cl1->cl_id == cl2->cl_id);
}

/* XXX what about NGROUP */
static int
same_creds(struct svc_cred *cr1, struct svc_cred *cr2)
{
	return cr1->cr_uid == cr2->cr_uid;
}

static void gen_clid(struct nfs4_client *clp)
{
	static u32 current_clientid = 1;

	clp->cl_clientid.cl_boot = boot_time;
	clp->cl_clientid.cl_id = current_clientid++; 
}

static void gen_confirm(struct nfs4_client *clp)
{
	static u32 i;
	u32 *p;

	p = (u32 *)clp->cl_confirm.data;
	*p++ = get_seconds();
	*p++ = i++;
}

static struct nfs4_client *create_client(struct xdr_netobj name, char *recdir,
		struct svc_rqst *rqstp, nfs4_verifier *verf)
{
	struct nfs4_client *clp;
	struct sockaddr *sa = svc_addr(rqstp);
	char *princ;

	clp = alloc_client(name);
	if (clp == NULL)
		return NULL;

	princ = svc_gss_principal(rqstp);
	if (princ) {
		clp->cl_principal = kstrdup(princ, GFP_KERNEL);
		if (clp->cl_principal == NULL) {
			free_client(clp);
			return NULL;
		}
	}

	memcpy(clp->cl_recdir, recdir, HEXDIR_LEN);
	atomic_set(&clp->cl_count, 1);
	atomic_set(&clp->cl_cb_conn.cb_set, 0);
	INIT_LIST_HEAD(&clp->cl_idhash);
	INIT_LIST_HEAD(&clp->cl_strhash);
	INIT_LIST_HEAD(&clp->cl_openowners);
	INIT_LIST_HEAD(&clp->cl_delegations);
	INIT_LIST_HEAD(&clp->cl_sessions);
	INIT_LIST_HEAD(&clp->cl_lru);
	clear_bit(0, &clp->cl_cb_slot_busy);
	rpc_init_wait_queue(&clp->cl_cb_waitq, "Backchannel slot table");
	copy_verf(clp, verf);
	rpc_copy_addr((struct sockaddr *) &clp->cl_addr, sa);
	clp->cl_flavor = rqstp->rq_flavor;
	copy_cred(&clp->cl_cred, &rqstp->rq_cred);
	gen_confirm(clp);

	return clp;
}

static int check_name(struct xdr_netobj name)
{
	if (name.len == 0) 
		return 0;
	if (name.len > NFS4_OPAQUE_LIMIT) {
		dprintk("NFSD: check_name: name too long(%d)!\n", name.len);
		return 0;
	}
	return 1;
}

static void
add_to_unconfirmed(struct nfs4_client *clp, unsigned int strhashval)
{
	unsigned int idhashval;

	list_add(&clp->cl_strhash, &unconf_str_hashtbl[strhashval]);
	idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	list_add(&clp->cl_idhash, &unconf_id_hashtbl[idhashval]);
	list_add_tail(&clp->cl_lru, &client_lru);
	clp->cl_time = get_seconds();
}

static void
move_to_confirmed(struct nfs4_client *clp)
{
	unsigned int idhashval = clientid_hashval(clp->cl_clientid.cl_id);
	unsigned int strhashval;

	dprintk("NFSD: move_to_confirm nfs4_client %p\n", clp);
	list_del_init(&clp->cl_strhash);
	list_move(&clp->cl_idhash, &conf_id_hashtbl[idhashval]);
	strhashval = clientstr_hashval(clp->cl_recdir);
	list_add(&clp->cl_strhash, &conf_str_hashtbl[strhashval]);
	renew_client(clp);
}

static struct nfs4_client *
find_confirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &conf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client(clientid_t *clid)
{
	struct nfs4_client *clp;
	unsigned int idhashval = clientid_hashval(clid->cl_id);

	list_for_each_entry(clp, &unconf_id_hashtbl[idhashval], cl_idhash) {
		if (same_clid(&clp->cl_clientid, clid))
			return clp;
	}
	return NULL;
}

/*
 * Return 1 iff clp's clientid establishment method matches the use_exchange_id
 * parameter. Matching is based on the fact the at least one of the
 * EXCHGID4_FLAG_USE_{NON_PNFS,PNFS_MDS,PNFS_DS} flags must be set for v4.1
 *
 * FIXME: we need to unify the clientid namespaces for nfsv4.x
 * and correctly deal with client upgrade/downgrade in EXCHANGE_ID
 * and SET_CLIENTID{,_CONFIRM}
 */
static inline int
match_clientid_establishment(struct nfs4_client *clp, bool use_exchange_id)
{
	bool has_exchange_flags = (clp->cl_exchange_flags != 0);
	return use_exchange_id == has_exchange_flags;
}

static struct nfs4_client *
find_confirmed_client_by_str(const char *dname, unsigned int hashval,
			     bool use_exchange_id)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &conf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname) &&
		    match_clientid_establishment(clp, use_exchange_id))
			return clp;
	}
	return NULL;
}

static struct nfs4_client *
find_unconfirmed_client_by_str(const char *dname, unsigned int hashval,
			       bool use_exchange_id)
{
	struct nfs4_client *clp;

	list_for_each_entry(clp, &unconf_str_hashtbl[hashval], cl_strhash) {
		if (same_name(clp->cl_recdir, dname) &&
		    match_clientid_establishment(clp, use_exchange_id))
			return clp;
	}
	return NULL;
}

static void
gen_callback(struct nfs4_client *clp, struct nfsd4_setclientid *se, u32 scopeid)
{
	struct nfs4_cb_conn *cb = &clp->cl_cb_conn;
	unsigned short expected_family;

	/* Currently, we only support tcp and tcp6 for the callback channel */
	if (se->se_callback_netid_len == 3 &&
	    !memcmp(se->se_callback_netid_val, "tcp", 3))
		expected_family = AF_INET;
	else if (se->se_callback_netid_len == 4 &&
		 !memcmp(se->se_callback_netid_val, "tcp6", 4))
		expected_family = AF_INET6;
	else
		goto out_err;

	cb->cb_addrlen = rpc_uaddr2sockaddr(se->se_callback_addr_val,
					    se->se_callback_addr_len,
					    (struct sockaddr *) &cb->cb_addr,
					    sizeof(cb->cb_addr));

	if (!cb->cb_addrlen || cb->cb_addr.ss_family != expected_family)
		goto out_err;

	if (cb->cb_addr.ss_family == AF_INET6)
		((struct sockaddr_in6 *) &cb->cb_addr)->sin6_scope_id = scopeid;

	cb->cb_minorversion = 0;
	cb->cb_prog = se->se_callback_prog;
	cb->cb_ident = se->se_callback_ident;
	return;
out_err:
	cb->cb_addr.ss_family = AF_UNSPEC;
	cb->cb_addrlen = 0;
	dprintk(KERN_INFO "NFSD: this client (clientid %08x/%08x) "
		"will not receive delegations\n",
		clp->cl_clientid.cl_boot, clp->cl_clientid.cl_id);

	return;
}

/*
 * Cache a reply. nfsd4_check_drc_limit() has bounded the cache size.
 */
void
nfsd4_store_cache_entry(struct nfsd4_compoundres *resp)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	unsigned int base;

	dprintk("--> %s slot %p\n", __func__, slot);

	slot->sl_opcnt = resp->opcnt;
	slot->sl_status = resp->cstate.status;

	if (nfsd4_not_cached(resp)) {
		slot->sl_datalen = 0;
		return;
	}
	slot->sl_datalen = (char *)resp->p - (char *)resp->cstate.datap;
	base = (char *)resp->cstate.datap -
					(char *)resp->xbuf->head[0].iov_base;
	if (read_bytes_from_xdr_buf(resp->xbuf, base, slot->sl_data,
				    slot->sl_datalen))
		WARN("%s: sessions DRC could not cache compound\n", __func__);
	return;
}

/*
 * Encode the replay sequence operation from the slot values.
 * If cachethis is FALSE encode the uncached rep error on the next
 * operation which sets resp->p and increments resp->opcnt for
 * nfs4svc_encode_compoundres.
 *
 */
static __be32
nfsd4_enc_sequence_replay(struct nfsd4_compoundargs *args,
			  struct nfsd4_compoundres *resp)
{
	struct nfsd4_op *op;
	struct nfsd4_slot *slot = resp->cstate.slot;

	dprintk("--> %s resp->opcnt %d cachethis %u \n", __func__,
		resp->opcnt, resp->cstate.slot->sl_cachethis);

	/* Encode the replayed sequence operation */
	op = &args->ops[resp->opcnt - 1];
	nfsd4_encode_operation(resp, op);

	/* Return nfserr_retry_uncached_rep in next operation. */
	if (args->opcnt > 1 && slot->sl_cachethis == 0) {
		op = &args->ops[resp->opcnt++];
		op->status = nfserr_retry_uncached_rep;
		nfsd4_encode_operation(resp, op);
	}
	return op->status;
}

/*
 * The sequence operation is not cached because we can use the slot and
 * session values.
 */
__be32
nfsd4_replay_cache_entry(struct nfsd4_compoundres *resp,
			 struct nfsd4_sequence *seq)
{
	struct nfsd4_slot *slot = resp->cstate.slot;
	__be32 status;

	dprintk("--> %s slot %p\n", __func__, slot);

	/* Either returns 0 or nfserr_retry_uncached */
	status = nfsd4_enc_sequence_replay(resp->rqstp->rq_argp, resp);
	if (status == nfserr_retry_uncached_rep)
		return status;

	/* The sequence operation has been encoded, cstate->datap set. */
	memcpy(resp->cstate.datap, slot->sl_data, slot->sl_datalen);

	resp->opcnt = slot->sl_opcnt;
	resp->p = resp->cstate.datap + XDR_QUADLEN(slot->sl_datalen);
	status = slot->sl_status;

	return status;
}

/*
 * Set the exchange_id flags returned by the server.
 */
static void
nfsd4_set_ex_flags(struct nfs4_client *new, struct nfsd4_exchange_id *clid)
{
	/* pNFS is not supported */
	new->cl_exchange_flags |= EXCHGID4_FLAG_USE_NON_PNFS;

	/* Referrals are supported, Migration is not. */
	new->cl_exchange_flags |= EXCHGID4_FLAG_SUPP_MOVED_REFER;

	/* set the wire flags to return to client. */
	clid->flags = new->cl_exchange_flags;
}

__be32
nfsd4_exchange_id(struct svc_rqst *rqstp,
		  struct nfsd4_compound_state *cstate,
		  struct nfsd4_exchange_id *exid)
{
	struct nfs4_client *unconf, *conf, *new;
	int status;
	unsigned int		strhashval;
	char			dname[HEXDIR_LEN];
	char			addr_str[INET6_ADDRSTRLEN];
	nfs4_verifier		verf = exid->verifier;
	struct sockaddr		*sa = svc_addr(rqstp);

	rpc_ntop(sa, addr_str, sizeof(addr_str));
	dprintk("%s rqstp=%p exid=%p clname.len=%u clname.data=%p "
		"ip_addr=%s flags %x, spa_how %d\n",
		__func__, rqstp, exid, exid->clname.len, exid->clname.data,
		addr_str, exid->flags, exid->spa_how);

	if (!check_name(exid->clname) || (exid->flags & ~EXCHGID4_FLAG_MASK_A))
		return nfserr_inval;

	/* Currently only support SP4_NONE */
	switch (exid->spa_how) {
	case SP4_NONE:
		break;
	case SP4_SSV:
		return nfserr_encr_alg_unsupp;
	default:
		BUG();				/* checked by xdr code */
	case SP4_MACH_CRED:
		return nfserr_serverfault;	/* no excuse :-/ */
	}

	status = nfs4_make_rec_clidname(dname, &exid->clname);

	if (status)
		goto error;

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	status = nfs_ok;

	conf = find_confirmed_client_by_str(dname, strhashval, true);
	if (conf) {
		if (!same_verf(&verf, &conf->cl_verifier)) {
			/* 18.35.4 case 8 */
			if (exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) {
				status = nfserr_not_same;
				goto out;
			}
			/* Client reboot: destroy old state */
			expire_client(conf);
			goto out_new;
		}
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)) {
			/* 18.35.4 case 9 */
			if (exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) {
				status = nfserr_perm;
				goto out;
			}
			expire_client(conf);
			goto out_new;
		}
		/*
		 * Set bit when the owner id and verifier map to an already
		 * confirmed client id (18.35.3).
		 */
		exid->flags |= EXCHGID4_FLAG_CONFIRMED_R;

		/*
		 * Falling into 18.35.4 case 2, possible router replay.
		 * Leave confirmed record intact and return same result.
		 */
		copy_verf(conf, &verf);
		new = conf;
		goto out_copy;
	}

	/* 18.35.4 case 7 */
	if (exid->flags & EXCHGID4_FLAG_UPD_CONFIRMED_REC_A) {
		status = nfserr_noent;
		goto out;
	}

	unconf  = find_unconfirmed_client_by_str(dname, strhashval, true);
	if (unconf) {
		/*
		 * Possible retry or client restart.  Per 18.35.4 case 4,
		 * a new unconfirmed record should be generated regardless
		 * of whether any properties have changed.
		 */
		expire_client(unconf);
	}

out_new:
	/* Normal case */
	new = create_client(exid->clname, dname, rqstp, &verf);
	if (new == NULL) {
		status = nfserr_serverfault;
		goto out;
	}

	gen_clid(new);
	add_to_unconfirmed(new, strhashval);
out_copy:
	exid->clientid.cl_boot = new->cl_clientid.cl_boot;
	exid->clientid.cl_id = new->cl_clientid.cl_id;

	exid->seqid = 1;
	nfsd4_set_ex_flags(new, exid);

	dprintk("nfsd4_exchange_id seqid %d flags %x\n",
		new->cl_cs_slot.sl_seqid, new->cl_exchange_flags);
	status = nfs_ok;

out:
	nfs4_unlock_state();
error:
	dprintk("nfsd4_exchange_id returns %d\n", ntohl(status));
	return status;
}

static int
check_slot_seqid(u32 seqid, u32 slot_seqid, int slot_inuse)
{
	dprintk("%s enter. seqid %d slot_seqid %d\n", __func__, seqid,
		slot_seqid);

	/* The slot is in use, and no response has been sent. */
	if (slot_inuse) {
		if (seqid == slot_seqid)
			return nfserr_jukebox;
		else
			return nfserr_seq_misordered;
	}
	/* Normal */
	if (likely(seqid == slot_seqid + 1))
		return nfs_ok;
	/* Replay */
	if (seqid == slot_seqid)
		return nfserr_replay_cache;
	/* Wraparound */
	if (seqid == 1 && (slot_seqid + 1) == 0)
		return nfs_ok;
	/* Misordered replay or misordered new request */
	return nfserr_seq_misordered;
}

/*
 * Cache the create session result into the create session single DRC
 * slot cache by saving the xdr structure. sl_seqid has been set.
 * Do this for solo or embedded create session operations.
 */
static void
nfsd4_cache_create_session(struct nfsd4_create_session *cr_ses,
			   struct nfsd4_clid_slot *slot, int nfserr)
{
	slot->sl_status = nfserr;
	memcpy(&slot->sl_cr_ses, cr_ses, sizeof(*cr_ses));
}

static __be32
nfsd4_replay_create_session(struct nfsd4_create_session *cr_ses,
			    struct nfsd4_clid_slot *slot)
{
	memcpy(cr_ses, &slot->sl_cr_ses, sizeof(*cr_ses));
	return slot->sl_status;
}

__be32
nfsd4_create_session(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_create_session *cr_ses)
{
	struct sockaddr *sa = svc_addr(rqstp);
	struct nfs4_client *conf, *unconf;
	struct nfsd4_clid_slot *cs_slot = NULL;
	int status = 0;

	nfs4_lock_state();
	unconf = find_unconfirmed_client(&cr_ses->clientid);
	conf = find_confirmed_client(&cr_ses->clientid);

	if (conf) {
		cs_slot = &conf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status == nfserr_replay_cache) {
			dprintk("Got a create_session replay! seqid= %d\n",
				cs_slot->sl_seqid);
			/* Return the cached reply status */
			status = nfsd4_replay_create_session(cr_ses, cs_slot);
			goto out;
		} else if (cr_ses->seqid != cs_slot->sl_seqid + 1) {
			status = nfserr_seq_misordered;
			dprintk("Sequence misordered!\n");
			dprintk("Expected seqid= %d but got seqid= %d\n",
				cs_slot->sl_seqid, cr_ses->seqid);
			goto out;
		}
		cs_slot->sl_seqid++;
	} else if (unconf) {
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred) ||
		    !rpc_cmp_addr(sa, (struct sockaddr *) &unconf->cl_addr)) {
			status = nfserr_clid_inuse;
			goto out;
		}

		cs_slot = &unconf->cl_cs_slot;
		status = check_slot_seqid(cr_ses->seqid, cs_slot->sl_seqid, 0);
		if (status) {
			/* an unconfirmed replay returns misordered */
			status = nfserr_seq_misordered;
			goto out_cache;
		}

		cs_slot->sl_seqid++; /* from 0 to 1 */
		move_to_confirmed(unconf);

		/*
		 * We do not support RDMA or persistent sessions
		 */
		cr_ses->flags &= ~SESSION4_PERSIST;
		cr_ses->flags &= ~SESSION4_RDMA;

		if (cr_ses->flags & SESSION4_BACK_CHAN) {
			unconf->cl_cb_xprt = rqstp->rq_xprt;
			svc_xprt_get(unconf->cl_cb_xprt);
			rpc_copy_addr(
				(struct sockaddr *)&unconf->cl_cb_conn.cb_addr,
				sa);
			unconf->cl_cb_conn.cb_addrlen = svc_addr_len(sa);
			unconf->cl_cb_conn.cb_minorversion =
				cstate->minorversion;
			unconf->cl_cb_conn.cb_prog = cr_ses->callback_prog;
			unconf->cl_cb_seq_nr = 1;
			nfsd4_probe_callback(unconf);
		}
		conf = unconf;
	} else {
		status = nfserr_stale_clientid;
		goto out;
	}

	status = alloc_init_session(rqstp, conf, cr_ses);
	if (status)
		goto out;

	memcpy(cr_ses->sessionid.data, conf->cl_sessionid.data,
	       NFS4_MAX_SESSIONID_LEN);
	cr_ses->seqid = cs_slot->sl_seqid;

out_cache:
	/* cache solo and embedded create sessions under the state lock */
	nfsd4_cache_create_session(cr_ses, cs_slot, status);
out:
	nfs4_unlock_state();
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
}

__be32
nfsd4_destroy_session(struct svc_rqst *r,
		      struct nfsd4_compound_state *cstate,
		      struct nfsd4_destroy_session *sessionid)
{
	struct nfsd4_session *ses;
	u32 status = nfserr_badsession;

	/* Notes:
	 * - The confirmed nfs4_client->cl_sessionid holds destroyed sessinid
	 * - Should we return nfserr_back_chan_busy if waiting for
	 *   callbacks on to-be-destroyed session?
	 * - Do we need to clear any callback info from previous session?
	 */

	dump_sessionid(__func__, &sessionid->sessionid);
	spin_lock(&sessionid_lock);
	ses = find_in_sessionid_hashtbl(&sessionid->sessionid);
	if (!ses) {
		spin_unlock(&sessionid_lock);
		goto out;
	}

	unhash_session(ses);
	spin_unlock(&sessionid_lock);

	/* wait for callbacks */
	shutdown_callback_client(ses->se_client);
	nfsd4_put_session(ses);
	status = nfs_ok;
out:
	dprintk("%s returns %d\n", __func__, ntohl(status));
	return status;
}

__be32
nfsd4_sequence(struct svc_rqst *rqstp,
	       struct nfsd4_compound_state *cstate,
	       struct nfsd4_sequence *seq)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfsd4_session *session;
	struct nfsd4_slot *slot;
	int status;

	if (resp->opcnt != 1)
		return nfserr_sequence_pos;

	spin_lock(&sessionid_lock);
	status = nfserr_badsession;
	session = find_in_sessionid_hashtbl(&seq->sessionid);
	if (!session)
		goto out;

	status = nfserr_badslot;
	if (seq->slotid >= session->se_fchannel.maxreqs)
		goto out;

	slot = session->se_slots[seq->slotid];
	dprintk("%s: slotid %d\n", __func__, seq->slotid);

	/* We do not negotiate the number of slots yet, so set the
	 * maxslots to the session maxreqs which is used to encode
	 * sr_highest_slotid and the sr_target_slot id to maxslots */
	seq->maxslots = session->se_fchannel.maxreqs;

	status = check_slot_seqid(seq->seqid, slot->sl_seqid, slot->sl_inuse);
	if (status == nfserr_replay_cache) {
		cstate->slot = slot;
		cstate->session = session;
		/* Return the cached reply status and set cstate->status
		 * for nfsd4_proc_compound processing */
		status = nfsd4_replay_cache_entry(resp, seq);
		cstate->status = nfserr_replay_cache;
		goto out;
	}
	if (status)
		goto out;

	/* Success! bump slot seqid */
	slot->sl_inuse = true;
	slot->sl_seqid = seq->seqid;
	slot->sl_cachethis = seq->cachethis;

	cstate->slot = slot;
	cstate->session = session;

	/* Hold a session reference until done processing the compound:
	 * nfsd4_put_session called only if the cstate slot is set.
	 */
	nfsd4_get_session(session);
out:
	spin_unlock(&sessionid_lock);
	/* Renew the clientid on success and on replay */
	if (cstate->session) {
		nfs4_lock_state();
		renew_client(session->se_client);
		nfs4_unlock_state();
	}
	dprintk("%s: return %d\n", __func__, ntohl(status));
	return status;
}

__be32
nfsd4_setclientid(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_setclientid *setclid)
{
	struct sockaddr		*sa = svc_addr(rqstp);
	struct xdr_netobj 	clname = { 
		.len = setclid->se_namelen,
		.data = setclid->se_name,
	};
	nfs4_verifier		clverifier = setclid->se_verf;
	unsigned int 		strhashval;
	struct nfs4_client	*conf, *unconf, *new;
	__be32 			status;
	char                    dname[HEXDIR_LEN];
	
	if (!check_name(clname))
		return nfserr_inval;

	status = nfs4_make_rec_clidname(dname, &clname);
	if (status)
		return status;

	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	strhashval = clientstr_hashval(dname);

	nfs4_lock_state();
	conf = find_confirmed_client_by_str(dname, strhashval, false);
	if (conf) {
		/* RFC 3530 14.2.33 CASE 0: */
		status = nfserr_clid_inuse;
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred)) {
			char addr_str[INET6_ADDRSTRLEN];
			rpc_ntop((struct sockaddr *) &conf->cl_addr, addr_str,
				 sizeof(addr_str));
			dprintk("NFSD: setclientid: string in use by client "
				"at %s\n", addr_str);
			goto out;
		}
	}
	/*
	 * section 14.2.33 of RFC 3530 (under the heading "IMPLEMENTATION")
	 * has a description of SETCLIENTID request processing consisting
	 * of 5 bullet points, labeled as CASE0 - CASE4 below.
	 */
	unconf = find_unconfirmed_client_by_str(dname, strhashval, false);
	status = nfserr_resource;
	if (!conf) {
		/*
		 * RFC 3530 14.2.33 CASE 4:
		 * placed first, because it is the normal case
		 */
		if (unconf)
			expire_client(unconf);
		new = create_client(clname, dname, rqstp, &clverifier);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else if (same_verf(&conf->cl_verifier, &clverifier)) {
		/*
		 * RFC 3530 14.2.33 CASE 1:
		 * probable callback update
		 */
		if (unconf) {
			/* Note this is removing unconfirmed {*x***},
			 * which is stronger than RFC recommended {vxc**}.
			 * This has the advantage that there is at most
			 * one {*x***} in either list at any time.
			 */
			expire_client(unconf);
		}
		new = create_client(clname, dname, rqstp, &clverifier);
		if (new == NULL)
			goto out;
		copy_clid(new, conf);
	} else if (!unconf) {
		/*
		 * RFC 3530 14.2.33 CASE 2:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		new = create_client(clname, dname, rqstp, &clverifier);
		if (new == NULL)
			goto out;
		gen_clid(new);
	} else {
		/*
		 * RFC 3530 14.2.33 CASE 3:
		 * probable client reboot; state will be removed if
		 * confirmed.
		 */
		expire_client(unconf);
		new = create_client(clname, dname, rqstp, &clverifier);
		if (new == NULL)
			goto out;
		gen_clid(new);
	}
	gen_callback(new, setclid, rpc_get_scope_id(sa));
	add_to_unconfirmed(new, strhashval);
	setclid->se_clientid.cl_boot = new->cl_clientid.cl_boot;
	setclid->se_clientid.cl_id = new->cl_clientid.cl_id;
	memcpy(setclid->se_confirm.data, new->cl_confirm.data, sizeof(setclid->se_confirm.data));
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}


/*
 * Section 14.2.34 of RFC 3530 (under the heading "IMPLEMENTATION") has
 * a description of SETCLIENTID_CONFIRM request processing consisting of 4
 * bullets, labeled as CASE1 - CASE4 below.
 */
__be32
nfsd4_setclientid_confirm(struct svc_rqst *rqstp,
			 struct nfsd4_compound_state *cstate,
			 struct nfsd4_setclientid_confirm *setclientid_confirm)
{
	struct sockaddr *sa = svc_addr(rqstp);
	struct nfs4_client *conf, *unconf;
	nfs4_verifier confirm = setclientid_confirm->sc_confirm; 
	clientid_t * clid = &setclientid_confirm->sc_clientid;
	__be32 status;

	if (STALE_CLIENTID(clid))
		return nfserr_stale_clientid;
	/* 
	 * XXX The Duplicate Request Cache (DRC) has been checked (??)
	 * We get here on a DRC miss.
	 */

	nfs4_lock_state();

	conf = find_confirmed_client(clid);
	unconf = find_unconfirmed_client(clid);

	status = nfserr_clid_inuse;
	if (conf && !rpc_cmp_addr((struct sockaddr *) &conf->cl_addr, sa))
		goto out;
	if (unconf && !rpc_cmp_addr((struct sockaddr *) &unconf->cl_addr, sa))
		goto out;

	/*
	 * section 14.2.34 of RFC 3530 has a description of
	 * SETCLIENTID_CONFIRM request processing consisting
	 * of 4 bullet points, labeled as CASE1 - CASE4 below.
	 */
	if (conf && unconf && same_verf(&confirm, &unconf->cl_confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 1:
		 * callback update
		 */
		if (!same_creds(&conf->cl_cred, &unconf->cl_cred))
			status = nfserr_clid_inuse;
		else {
			/* XXX: We just turn off callbacks until we can handle
			  * change request correctly. */
			atomic_set(&conf->cl_cb_conn.cb_set, 0);
			expire_client(unconf);
			status = nfs_ok;

		}
	} else if (conf && !unconf) {
		/*
		 * RFC 3530 14.2.34 CASE 2:
		 * probable retransmitted request; play it safe and
		 * do nothing.
		 */
		if (!same_creds(&conf->cl_cred, &rqstp->rq_cred))
			status = nfserr_clid_inuse;
		else
			status = nfs_ok;
	} else if (!conf && unconf
			&& same_verf(&unconf->cl_confirm, &confirm)) {
		/*
		 * RFC 3530 14.2.34 CASE 3:
		 * Normal case; new or rebooted client:
		 */
		if (!same_creds(&unconf->cl_cred, &rqstp->rq_cred)) {
			status = nfserr_clid_inuse;
		} else {
			unsigned int hash =
				clientstr_hashval(unconf->cl_recdir);
			conf = find_confirmed_client_by_str(unconf->cl_recdir,
							    hash, false);
			if (conf) {
				nfsd4_remove_clid_dir(conf);
				expire_client(conf);
			}
			move_to_confirmed(unconf);
			conf = unconf;
			nfsd4_probe_callback(conf);
			status = nfs_ok;
		}
	} else if ((!conf || (conf && !same_verf(&conf->cl_confirm, &confirm)))
	    && (!unconf || (unconf && !same_verf(&unconf->cl_confirm,
				    				&confirm)))) {
		/*
		 * RFC 3530 14.2.34 CASE 4:
		 * Client probably hasn't noticed that we rebooted yet.
		 */
		status = nfserr_stale_clientid;
	} else {
		/* check that we have hit one of the cases...*/
		status = nfserr_clid_inuse;
	}
out:
	nfs4_unlock_state();
	return status;
}

/* OPEN Share state helper functions */
static inline struct nfs4_file *
alloc_init_file(struct inode *ino)
{
	struct nfs4_file *fp;
	unsigned int hashval = file_hashval(ino);

	fp = kmem_cache_alloc(file_slab, GFP_KERNEL);
	if (fp) {
		atomic_set(&fp->fi_ref, 1);
		INIT_LIST_HEAD(&fp->fi_hash);
		INIT_LIST_HEAD(&fp->fi_stateids);
		INIT_LIST_HEAD(&fp->fi_delegations);
		spin_lock(&recall_lock);
		list_add(&fp->fi_hash, &file_hashtbl[hashval]);
		spin_unlock(&recall_lock);
		fp->fi_inode = igrab(ino);
		fp->fi_id = current_fileid++;
		fp->fi_had_conflict = false;
		return fp;
	}
	return NULL;
}

static void
nfsd4_free_slab(struct kmem_cache **slab)
{
	if (*slab == NULL)
		return;
	kmem_cache_destroy(*slab);
	*slab = NULL;
}

void
nfsd4_free_slabs(void)
{
	nfsd4_free_slab(&stateowner_slab);
	nfsd4_free_slab(&file_slab);
	nfsd4_free_slab(&stateid_slab);
	nfsd4_free_slab(&deleg_slab);
}

static int
nfsd4_init_slabs(void)
{
	stateowner_slab = kmem_cache_create("nfsd4_stateowners",
			sizeof(struct nfs4_stateowner), 0, 0, NULL);
	if (stateowner_slab == NULL)
		goto out_nomem;
	file_slab = kmem_cache_create("nfsd4_files",
			sizeof(struct nfs4_file), 0, 0, NULL);
	if (file_slab == NULL)
		goto out_nomem;
	stateid_slab = kmem_cache_create("nfsd4_stateids",
			sizeof(struct nfs4_stateid), 0, 0, NULL);
	if (stateid_slab == NULL)
		goto out_nomem;
	deleg_slab = kmem_cache_create("nfsd4_delegations",
			sizeof(struct nfs4_delegation), 0, 0, NULL);
	if (deleg_slab == NULL)
		goto out_nomem;
	return 0;
out_nomem:
	nfsd4_free_slabs();
	dprintk("nfsd4: out of memory while initializing nfsv4\n");
	return -ENOMEM;
}

void
nfs4_free_stateowner(struct kref *kref)
{
	struct nfs4_stateowner *sop =
		container_of(kref, struct nfs4_stateowner, so_ref);
	kfree(sop->so_owner.data);
	kmem_cache_free(stateowner_slab, sop);
}

static inline struct nfs4_stateowner *
alloc_stateowner(struct xdr_netobj *owner)
{
	struct nfs4_stateowner *sop;

	if ((sop = kmem_cache_alloc(stateowner_slab, GFP_KERNEL))) {
		if ((sop->so_owner.data = kmalloc(owner->len, GFP_KERNEL))) {
			memcpy(sop->so_owner.data, owner->data, owner->len);
			sop->so_owner.len = owner->len;
			kref_init(&sop->so_ref);
			return sop;
		} 
		kmem_cache_free(stateowner_slab, sop);
	}
	return NULL;
}

static struct nfs4_stateowner *
alloc_init_open_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&open->op_owner)))
		return NULL;
	idhashval = ownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);  /* not used */
	INIT_LIST_HEAD(&sop->so_close_lru);
	sop->so_time = 0;
	list_add(&sop->so_idhash, &ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perclient, &clp->cl_openowners);
	sop->so_is_open_owner = 1;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	sop->so_seqid = open->op_seqid;
	sop->so_confirmed = 0;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static inline void
init_stateid(struct nfs4_stateid *stp, struct nfs4_file *fp, struct nfsd4_open *open) {
	struct nfs4_stateowner *sop = open->op_stateowner;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners);
	INIT_LIST_HEAD(&stp->st_perfile);
	list_add(&stp->st_hash, &stateid_hashtbl[hashval]);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = get_seconds();
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_access_bmap = 0;
	stp->st_deny_bmap = 0;
	__set_bit(open->op_share_access & ~NFS4_SHARE_WANT_MASK,
		  &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);
	stp->st_openstp = NULL;
}

static void
move_to_close_lru(struct nfs4_stateowner *sop)
{
	dprintk("NFSD: move_to_close_lru nfs4_stateowner %p\n", sop);

	list_move_tail(&sop->so_close_lru, &close_lru);
	sop->so_time = get_seconds();
}

static int
same_owner_str(struct nfs4_stateowner *sop, struct xdr_netobj *owner,
							clientid_t *clid)
{
	return (sop->so_owner.len == owner->len) &&
		0 == memcmp(sop->so_owner.data, owner->data, owner->len) &&
		(sop->so_client->cl_clientid.cl_id == clid->cl_id);
}

static struct nfs4_stateowner *
find_openstateowner_str(unsigned int hashval, struct nfsd4_open *open)
{
	struct nfs4_stateowner *so = NULL;

	list_for_each_entry(so, &ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(so, &open->op_owner, &open->op_clientid))
			return so;
	}
	return NULL;
}

/* search file_hashtbl[] for file */
static struct nfs4_file *
find_file(struct inode *ino)
{
	unsigned int hashval = file_hashval(ino);
	struct nfs4_file *fp;

	spin_lock(&recall_lock);
	list_for_each_entry(fp, &file_hashtbl[hashval], fi_hash) {
		if (fp->fi_inode == ino) {
			get_nfs4_file(fp);
			spin_unlock(&recall_lock);
			return fp;
		}
	}
	spin_unlock(&recall_lock);
	return NULL;
}

static inline int access_valid(u32 x, u32 minorversion)
{
	if ((x & NFS4_SHARE_ACCESS_MASK) < NFS4_SHARE_ACCESS_READ)
		return 0;
	if ((x & NFS4_SHARE_ACCESS_MASK) > NFS4_SHARE_ACCESS_BOTH)
		return 0;
	x &= ~NFS4_SHARE_ACCESS_MASK;
	if (minorversion && x) {
		if ((x & NFS4_SHARE_WANT_MASK) > NFS4_SHARE_WANT_CANCEL)
			return 0;
		if ((x & NFS4_SHARE_WHEN_MASK) > NFS4_SHARE_PUSH_DELEG_WHEN_UNCONTENDED)
			return 0;
		x &= ~(NFS4_SHARE_WANT_MASK | NFS4_SHARE_WHEN_MASK);
	}
	if (x)
		return 0;
	return 1;
}

static inline int deny_valid(u32 x)
{
	/* Note: unlike access bits, deny bits may be zero. */
	return x <= NFS4_SHARE_DENY_BOTH;
}

/*
 * We store the NONE, READ, WRITE, and BOTH bits separately in the
 * st_{access,deny}_bmap field of the stateid, in order to track not
 * only what share bits are currently in force, but also what
 * combinations of share bits previous opens have used.  This allows us
 * to enforce the recommendation of rfc 3530 14.2.19 that the server
 * return an error if the client attempt to downgrade to a combination
 * of share bits not explicable by closing some of its previous opens.
 *
 * XXX: This enforcement is actually incomplete, since we don't keep
 * track of access/deny bit combinations; so, e.g., we allow:
 *
 *	OPEN allow read, deny write
 *	OPEN allow both, deny none
 *	DOWNGRADE allow read, deny none
 *
 * which we should reject.
 */
static void
set_access(unsigned int *access, unsigned long bmap) {
	int i;

	*access = 0;
	for (i = 1; i < 4; i++) {
		if (test_bit(i, &bmap))
			*access |= i;
	}
}

static void
set_deny(unsigned int *deny, unsigned long bmap) {
	int i;

	*deny = 0;
	for (i = 0; i < 4; i++) {
		if (test_bit(i, &bmap))
			*deny |= i ;
	}
}

static int
test_share(struct nfs4_stateid *stp, struct nfsd4_open *open) {
	unsigned int access, deny;

	set_access(&access, stp->st_access_bmap);
	set_deny(&deny, stp->st_deny_bmap);
	if ((access & open->op_share_deny) || (deny & open->op_share_access))
		return 0;
	return 1;
}

/*
 * Called to check deny when READ with all zero stateid or
 * WRITE with all zero or all one stateid
 */
static __be32
nfs4_share_conflict(struct svc_fh *current_fh, unsigned int deny_type)
{
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_file *fp;
	struct nfs4_stateid *stp;
	__be32 ret;

	dprintk("NFSD: nfs4_share_conflict\n");

	fp = find_file(ino);
	if (!fp)
		return nfs_ok;
	ret = nfserr_locked;
	/* Search for conflicting share reservations */
	list_for_each_entry(stp, &fp->fi_stateids, st_perfile) {
		if (test_bit(deny_type, &stp->st_deny_bmap) ||
		    test_bit(NFS4_SHARE_DENY_BOTH, &stp->st_deny_bmap))
			goto out;
	}
	ret = nfs_ok;
out:
	put_nfs4_file(fp);
	return ret;
}

static inline void
nfs4_file_downgrade(struct file *filp, unsigned int share_access)
{
	if (share_access & NFS4_SHARE_ACCESS_WRITE) {
		drop_file_write_access(filp);
		filp->f_mode = (filp->f_mode | FMODE_READ) & ~FMODE_WRITE;
	}
}

/*
 * Spawn a thread to perform a recall on the delegation represented
 * by the lease (file_lock)
 *
 * Called from break_lease() with lock_kernel() held.
 * Note: we assume break_lease will only call this *once* for any given
 * lease.
 */
static
void nfsd_break_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_break_deleg_cb: dp %p fl %p\n",dp,fl);
	if (!dp)
		return;

	/* We're assuming the state code never drops its reference
	 * without first removing the lease.  Since we're in this lease
	 * callback (and since the lease code is serialized by the kernel
	 * lock) we know the server hasn't removed the lease yet, we know
	 * it's safe to take a reference: */
	atomic_inc(&dp->dl_count);
	atomic_inc(&dp->dl_client->cl_count);

	spin_lock(&recall_lock);
	list_add_tail(&dp->dl_recall_lru, &del_recall_lru);
	spin_unlock(&recall_lock);

	/* only place dl_time is set. protected by lock_kernel*/
	dp->dl_time = get_seconds();

	/*
	 * We don't want the locks code to timeout the lease for us;
	 * we'll remove it ourself if the delegation isn't returned
	 * in time.
	 */
	fl->fl_break_time = 0;

	dp->dl_file->fi_had_conflict = true;
	nfsd4_cb_recall(dp);
}

/*
 * The file_lock is being reapd.
 *
 * Called by locks_free_lock() with lock_kernel() held.
 */
static
void nfsd_release_deleg_cb(struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)fl->fl_owner;

	dprintk("NFSD nfsd_release_deleg_cb: fl %p dp %p dl_count %d\n", fl,dp, atomic_read(&dp->dl_count));

	if (!(fl->fl_flags & FL_LEASE) || !dp)
		return;
	dp->dl_flock = NULL;
}

/*
 * Set the delegation file_lock back pointer.
 *
 * Called from setlease() with lock_kernel() held.
 */
static
void nfsd_copy_lock_deleg_cb(struct file_lock *new, struct file_lock *fl)
{
	struct nfs4_delegation *dp = (struct nfs4_delegation *)new->fl_owner;

	dprintk("NFSD: nfsd_copy_lock_deleg_cb: new fl %p dp %p\n", new, dp);
	if (!dp)
		return;
	dp->dl_flock = new;
}

/*
 * Called from setlease() with lock_kernel() held
 */
static
int nfsd_same_client_deleg_cb(struct file_lock *onlist, struct file_lock *try)
{
	struct nfs4_delegation *onlistd =
		(struct nfs4_delegation *)onlist->fl_owner;
	struct nfs4_delegation *tryd =
		(struct nfs4_delegation *)try->fl_owner;

	if (onlist->fl_lmops != try->fl_lmops)
		return 0;

	return onlistd->dl_client == tryd->dl_client;
}


static
int nfsd_change_deleg_cb(struct file_lock **onlist, int arg)
{
	if (arg & F_UNLCK)
		return lease_modify(onlist, arg);
	else
		return -EAGAIN;
}

static const struct lock_manager_operations nfsd_lease_mng_ops = {
	.fl_break = nfsd_break_deleg_cb,
	.fl_release_private = nfsd_release_deleg_cb,
	.fl_copy_lock = nfsd_copy_lock_deleg_cb,
	.fl_mylease = nfsd_same_client_deleg_cb,
	.fl_change = nfsd_change_deleg_cb,
};


__be32
nfsd4_process_open1(struct nfsd4_compound_state *cstate,
		    struct nfsd4_open *open)
{
	clientid_t *clientid = &open->op_clientid;
	struct nfs4_client *clp = NULL;
	unsigned int strhashval;
	struct nfs4_stateowner *sop = NULL;

	if (!check_name(open->op_owner))
		return nfserr_inval;

	if (STALE_CLIENTID(&open->op_clientid))
		return nfserr_stale_clientid;

	strhashval = ownerstr_hashval(clientid->cl_id, open->op_owner);
	sop = find_openstateowner_str(strhashval, open);
	open->op_stateowner = sop;
	if (!sop) {
		/* Make sure the client's lease hasn't expired. */
		clp = find_confirmed_client(clientid);
		if (clp == NULL)
			return nfserr_expired;
		goto renew;
	}
	/* When sessions are used, skip open sequenceid processing */
	if (nfsd4_has_session(cstate))
		goto renew;
	if (!sop->so_confirmed) {
		/* Replace unconfirmed owners without checking for replay. */
		clp = sop->so_client;
		release_openowner(sop);
		open->op_stateowner = NULL;
		goto renew;
	}
	if (open->op_seqid == sop->so_seqid - 1) {
		if (sop->so_replay.rp_buflen)
			return nfserr_replay_me;
		/* The original OPEN failed so spectacularly
		 * that we don't even have replay data saved!
		 * Therefore, we have no choice but to continue
		 * processing this OPEN; presumably, we'll
		 * fail again for the same reason.
		 */
		dprintk("nfsd4_process_open1: replay with no replay cache\n");
		goto renew;
	}
	if (open->op_seqid != sop->so_seqid)
		return nfserr_bad_seqid;
renew:
	if (open->op_stateowner == NULL) {
		sop = alloc_init_open_stateowner(strhashval, clp, open);
		if (sop == NULL)
			return nfserr_resource;
		open->op_stateowner = sop;
	}
	list_del_init(&sop->so_close_lru);
	renew_client(sop->so_client);
	return nfs_ok;
}

static inline __be32
nfs4_check_delegmode(struct nfs4_delegation *dp, int flags)
{
	if ((flags & WR_STATE) && (dp->dl_type == NFS4_OPEN_DELEGATE_READ))
		return nfserr_openmode;
	else
		return nfs_ok;
}

static struct nfs4_delegation *
find_delegation_file(struct nfs4_file *fp, stateid_t *stid)
{
	struct nfs4_delegation *dp;

	list_for_each_entry(dp, &fp->fi_delegations, dl_perfile) {
		if (dp->dl_stateid.si_stateownerid == stid->si_stateownerid)
			return dp;
	}
	return NULL;
}

static __be32
nfs4_check_deleg(struct nfs4_file *fp, struct nfsd4_open *open,
		struct nfs4_delegation **dp)
{
	int flags;
	__be32 status = nfserr_bad_stateid;

	*dp = find_delegation_file(fp, &open->op_delegate_stateid);
	if (*dp == NULL)
		goto out;
	flags = open->op_share_access == NFS4_SHARE_ACCESS_READ ?
						RD_STATE : WR_STATE;
	status = nfs4_check_delegmode(*dp, flags);
	if (status)
		*dp = NULL;
out:
	if (open->op_claim_type != NFS4_OPEN_CLAIM_DELEGATE_CUR)
		return nfs_ok;
	if (status)
		return status;
	open->op_stateowner->so_confirmed = 1;
	return nfs_ok;
}

static __be32
nfs4_check_open(struct nfs4_file *fp, struct nfsd4_open *open, struct nfs4_stateid **stpp)
{
	struct nfs4_stateid *local;
	__be32 status = nfserr_share_denied;
	struct nfs4_stateowner *sop = open->op_stateowner;

	list_for_each_entry(local, &fp->fi_stateids, st_perfile) {
		/* ignore lock owners */
		if (local->st_stateowner->so_is_open_owner == 0)
			continue;
		/* remember if we have seen this open owner */
		if (local->st_stateowner == sop)
			*stpp = local;
		/* check for conflicting share reservations */
		if (!test_share(local, open))
			goto out;
	}
	status = 0;
out:
	return status;
}

static inline struct nfs4_stateid *
nfs4_alloc_stateid(void)
{
	return kmem_cache_alloc(stateid_slab, GFP_KERNEL);
}

static __be32
nfs4_new_open(struct svc_rqst *rqstp, struct nfs4_stateid **stpp,
		struct nfs4_delegation *dp,
		struct svc_fh *cur_fh, int flags)
{
	struct nfs4_stateid *stp;

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		return nfserr_resource;

	if (dp) {
		get_file(dp->dl_vfs_file);
		stp->st_vfs_file = dp->dl_vfs_file;
	} else {
		__be32 status;
		status = nfsd_open(rqstp, cur_fh, S_IFREG, flags,
				&stp->st_vfs_file);
		if (status) {
			if (status == nfserr_dropit)
				status = nfserr_jukebox;
			kmem_cache_free(stateid_slab, stp);
			return status;
		}
	}
	*stpp = stp;
	return 0;
}

static inline __be32
nfsd4_truncate(struct svc_rqst *rqstp, struct svc_fh *fh,
		struct nfsd4_open *open)
{
	struct iattr iattr = {
		.ia_valid = ATTR_SIZE,
		.ia_size = 0,
	};
	if (!open->op_truncate)
		return 0;
	if (!(open->op_share_access & NFS4_SHARE_ACCESS_WRITE))
		return nfserr_inval;
	return nfsd_setattr(rqstp, fh, &iattr, 0, (time_t)0);
}

static __be32
nfs4_upgrade_open(struct svc_rqst *rqstp, struct svc_fh *cur_fh, struct nfs4_stateid *stp, struct nfsd4_open *open)
{
	struct file *filp = stp->st_vfs_file;
	struct inode *inode = filp->f_path.dentry->d_inode;
	unsigned int share_access, new_writer;
	__be32 status;

	set_access(&share_access, stp->st_access_bmap);
	new_writer = (~share_access) & open->op_share_access
			& NFS4_SHARE_ACCESS_WRITE;

	if (new_writer) {
		int err = get_write_access(inode);
		if (err)
			return nfserrno(err);
		err = mnt_want_write(cur_fh->fh_export->ex_path.mnt);
		if (err)
			return nfserrno(err);
		file_take_write(filp);
	}
	status = nfsd4_truncate(rqstp, cur_fh, open);
	if (status) {
		if (new_writer)
			put_write_access(inode);
		return status;
	}
	/* remember the open */
	filp->f_mode |= open->op_share_access;
	__set_bit(open->op_share_access, &stp->st_access_bmap);
	__set_bit(open->op_share_deny, &stp->st_deny_bmap);

	return nfs_ok;
}


static void
nfs4_set_claim_prev(struct nfsd4_open *open)
{
	open->op_stateowner->so_confirmed = 1;
	open->op_stateowner->so_client->cl_firststate = 1;
}

/*
 * Attempt to hand out a delegation.
 */
static void
nfs4_open_delegation(struct svc_fh *fh, struct nfsd4_open *open, struct nfs4_stateid *stp)
{
	struct nfs4_delegation *dp;
	struct nfs4_stateowner *sop = stp->st_stateowner;
	struct nfs4_cb_conn *cb = &sop->so_client->cl_cb_conn;
	struct file_lock fl, *flp = &fl;
	int status, flag = 0;

	flag = NFS4_OPEN_DELEGATE_NONE;
	open->op_recall = 0;
	switch (open->op_claim_type) {
		case NFS4_OPEN_CLAIM_PREVIOUS:
			if (!atomic_read(&cb->cb_set))
				open->op_recall = 1;
			flag = open->op_delegate_type;
			if (flag == NFS4_OPEN_DELEGATE_NONE)
				goto out;
			break;
		case NFS4_OPEN_CLAIM_NULL:
			/* Let's not give out any delegations till everyone's
			 * had the chance to reclaim theirs.... */
			if (locks_in_grace())
				goto out;
			if (!atomic_read(&cb->cb_set) || !sop->so_confirmed)
				goto out;
			if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
				flag = NFS4_OPEN_DELEGATE_WRITE;
			else
				flag = NFS4_OPEN_DELEGATE_READ;
			break;
		default:
			goto out;
	}

	dp = alloc_init_deleg(sop->so_client, stp, fh, flag);
	if (dp == NULL) {
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}
	locks_init_lock(&fl);
	fl.fl_lmops = &nfsd_lease_mng_ops;
	fl.fl_flags = FL_LEASE;
	fl.fl_type = flag == NFS4_OPEN_DELEGATE_READ? F_RDLCK: F_WRLCK;
	fl.fl_end = OFFSET_MAX;
	fl.fl_owner =  (fl_owner_t)dp;
	fl.fl_file = stp->st_vfs_file;
	fl.fl_pid = current->tgid;

	/* vfs_setlease checks to see if delegation should be handed out.
	 * the lock_manager callbacks fl_mylease and fl_change are used
	 */
	if ((status = vfs_setlease(stp->st_vfs_file, fl.fl_type, &flp))) {
		dprintk("NFSD: setlease failed [%d], no delegation\n", status);
		unhash_delegation(dp);
		flag = NFS4_OPEN_DELEGATE_NONE;
		goto out;
	}

	memcpy(&open->op_delegate_stateid, &dp->dl_stateid, sizeof(dp->dl_stateid));

	dprintk("NFSD: delegation stateid=(%08x/%08x/%08x/%08x)\n\n",
	             dp->dl_stateid.si_boot,
	             dp->dl_stateid.si_stateownerid,
	             dp->dl_stateid.si_fileid,
	             dp->dl_stateid.si_generation);
out:
	if (open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS
			&& flag == NFS4_OPEN_DELEGATE_NONE
			&& open->op_delegate_type != NFS4_OPEN_DELEGATE_NONE)
		dprintk("NFSD: WARNING: refusing delegation reclaim\n");
	open->op_delegate_type = flag;
}

/*
 * called with nfs4_lock_state() held.
 */
__be32
nfsd4_process_open2(struct svc_rqst *rqstp, struct svc_fh *current_fh, struct nfsd4_open *open)
{
	struct nfsd4_compoundres *resp = rqstp->rq_resp;
	struct nfs4_file *fp = NULL;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	__be32 status;

	status = nfserr_inval;
	if (!access_valid(open->op_share_access, resp->cstate.minorversion)
			|| !deny_valid(open->op_share_deny))
		goto out;
	/*
	 * Lookup file; if found, lookup stateid and check open request,
	 * and check for delegations in the process of being recalled.
	 * If not found, create the nfs4_file struct
	 */
	fp = find_file(ino);
	if (fp) {
		if ((status = nfs4_check_open(fp, open, &stp)))
			goto out;
		status = nfs4_check_deleg(fp, open, &dp);
		if (status)
			goto out;
	} else {
		status = nfserr_bad_stateid;
		if (open->op_claim_type == NFS4_OPEN_CLAIM_DELEGATE_CUR)
			goto out;
		status = nfserr_resource;
		fp = alloc_init_file(ino);
		if (fp == NULL)
			goto out;
	}

	/*
	 * OPEN the file, or upgrade an existing OPEN.
	 * If truncate fails, the OPEN fails.
	 */
	if (stp) {
		/* Stateid was found, this is an OPEN upgrade */
		status = nfs4_upgrade_open(rqstp, current_fh, stp, open);
		if (status)
			goto out;
		update_stateid(&stp->st_stateid);
	} else {
		/* Stateid was not found, this is a new OPEN */
		int flags = 0;
		if (open->op_share_access & NFS4_SHARE_ACCESS_READ)
			flags |= NFSD_MAY_READ;
		if (open->op_share_access & NFS4_SHARE_ACCESS_WRITE)
			flags |= NFSD_MAY_WRITE;
		status = nfs4_new_open(rqstp, &stp, dp, current_fh, flags);
		if (status)
			goto out;
		init_stateid(stp, fp, open);
		status = nfsd4_truncate(rqstp, current_fh, open);
		if (status) {
			release_open_stateid(stp);
			goto out;
		}
		if (nfsd4_has_session(&resp->cstate))
			update_stateid(&stp->st_stateid);
	}
	memcpy(&open->op_stateid, &stp->st_stateid, sizeof(stateid_t));

	if (nfsd4_has_session(&resp->cstate))
		open->op_stateowner->so_confirmed = 1;

	/*
	* Attempt to hand out a delegation. No error return, because the
	* OPEN succeeds even if we fail.
	*/
	nfs4_open_delegation(current_fh, open, stp);

	status = nfs_ok;

	dprintk("nfs4_process_open2: stateid=(%08x/%08x/%08x/%08x)\n",
	            stp->st_stateid.si_boot, stp->st_stateid.si_stateownerid,
	            stp->st_stateid.si_fileid, stp->st_stateid.si_generation);
out:
	if (fp)
		put_nfs4_file(fp);
	if (status == 0 && open->op_claim_type == NFS4_OPEN_CLAIM_PREVIOUS)
		nfs4_set_claim_prev(open);
	/*
	* To finish the open response, we just need to set the rflags.
	*/
	open->op_rflags = NFS4_OPEN_RESULT_LOCKTYPE_POSIX;
	if (!open->op_stateowner->so_confirmed &&
	    !nfsd4_has_session(&resp->cstate))
		open->op_rflags |= NFS4_OPEN_RESULT_CONFIRM;

	return status;
}

__be32
nfsd4_renew(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    clientid_t *clid)
{
	struct nfs4_client *clp;
	__be32 status;

	nfs4_lock_state();
	dprintk("process_renew(%08x/%08x): starting\n", 
			clid->cl_boot, clid->cl_id);
	status = nfserr_stale_clientid;
	if (STALE_CLIENTID(clid))
		goto out;
	clp = find_confirmed_client(clid);
	status = nfserr_expired;
	if (clp == NULL) {
		/* We assume the client took too long to RENEW. */
		dprintk("nfsd4_renew: clientid not found!\n");
		goto out;
	}
	renew_client(clp);
	status = nfserr_cb_path_down;
	if (!list_empty(&clp->cl_delegations)
			&& !atomic_read(&clp->cl_cb_conn.cb_set))
		goto out;
	status = nfs_ok;
out:
	nfs4_unlock_state();
	return status;
}

struct lock_manager nfsd4_manager = {
};

static void
nfsd4_end_grace(void)
{
	dprintk("NFSD: end of grace period\n");
	nfsd4_recdir_purge_old();
	locks_end_grace(&nfsd4_manager);
}

static time_t
nfs4_laundromat(void)
{
	struct nfs4_client *clp;
	struct nfs4_stateowner *sop;
	struct nfs4_delegation *dp;
	struct list_head *pos, *next, reaplist;
	time_t cutoff = get_seconds() - NFSD_LEASE_TIME;
	time_t t, clientid_val = NFSD_LEASE_TIME;
	time_t u, test_val = NFSD_LEASE_TIME;

	nfs4_lock_state();

	dprintk("NFSD: laundromat service - starting\n");
	if (locks_in_grace())
		nfsd4_end_grace();
	list_for_each_safe(pos, next, &client_lru) {
		clp = list_entry(pos, struct nfs4_client, cl_lru);
		if (time_after((unsigned long)clp->cl_time, (unsigned long)cutoff)) {
			t = clp->cl_time - cutoff;
			if (clientid_val > t)
				clientid_val = t;
			break;
		}
		dprintk("NFSD: purging unused client (clientid %08x)\n",
			clp->cl_clientid.cl_id);
		nfsd4_remove_clid_dir(clp);
		expire_client(clp);
	}
	INIT_LIST_HEAD(&reaplist);
	spin_lock(&recall_lock);
	list_for_each_safe(pos, next, &del_recall_lru) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		if (time_after((unsigned long)dp->dl_time, (unsigned long)cutoff)) {
			u = dp->dl_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused delegation dp %p, fp %p\n",
			            dp, dp->dl_flock);
		list_move(&dp->dl_recall_lru, &reaplist);
	}
	spin_unlock(&recall_lock);
	list_for_each_safe(pos, next, &reaplist) {
		dp = list_entry (pos, struct nfs4_delegation, dl_recall_lru);
		list_del_init(&dp->dl_recall_lru);
		unhash_delegation(dp);
	}
	test_val = NFSD_LEASE_TIME;
	list_for_each_safe(pos, next, &close_lru) {
		sop = list_entry(pos, struct nfs4_stateowner, so_close_lru);
		if (time_after((unsigned long)sop->so_time, (unsigned long)cutoff)) {
			u = sop->so_time - cutoff;
			if (test_val > u)
				test_val = u;
			break;
		}
		dprintk("NFSD: purging unused open stateowner (so_id %d)\n",
			sop->so_id);
		release_openowner(sop);
	}
	if (clientid_val < NFSD_LAUNDROMAT_MINTIMEOUT)
		clientid_val = NFSD_LAUNDROMAT_MINTIMEOUT;
	nfs4_unlock_state();
	return clientid_val;
}

static struct workqueue_struct *laundry_wq;
static void laundromat_main(struct work_struct *);
static DECLARE_DELAYED_WORK(laundromat_work, laundromat_main);

static void
laundromat_main(struct work_struct *not_used)
{
	time_t t;

	t = nfs4_laundromat();
	dprintk("NFSD: laundromat_main - sleeping for %ld seconds\n", t);
	queue_delayed_work(laundry_wq, &laundromat_work, t*HZ);
}

static struct nfs4_stateowner *
search_close_lru(u32 st_id, int flags)
{
	struct nfs4_stateowner *local = NULL;

	if (flags & CLOSE_STATE) {
		list_for_each_entry(local, &close_lru, so_close_lru) {
			if (local->so_id == st_id)
				return local;
		}
	}
	return NULL;
}

static inline int
nfs4_check_fh(struct svc_fh *fhp, struct nfs4_stateid *stp)
{
	return fhp->fh_dentry->d_inode != stp->st_vfs_file->f_path.dentry->d_inode;
}

static int
STALE_STATEID(stateid_t *stateid)
{
	if (time_after((unsigned long)boot_time,
			(unsigned long)stateid->si_boot)) {
		dprintk("NFSD: stale stateid (%08x/%08x/%08x/%08x)!\n",
			stateid->si_boot, stateid->si_stateownerid,
			stateid->si_fileid, stateid->si_generation);
		return 1;
	}
	return 0;
}

static int
EXPIRED_STATEID(stateid_t *stateid)
{
	if (time_before((unsigned long)boot_time,
			((unsigned long)stateid->si_boot)) &&
	    time_before((unsigned long)(stateid->si_boot + lease_time), get_seconds())) {
		dprintk("NFSD: expired stateid (%08x/%08x/%08x/%08x)!\n",
			stateid->si_boot, stateid->si_stateownerid,
			stateid->si_fileid, stateid->si_generation);
		return 1;
	}
	return 0;
}

static __be32
stateid_error_map(stateid_t *stateid)
{
	if (STALE_STATEID(stateid))
		return nfserr_stale_stateid;
	if (EXPIRED_STATEID(stateid))
		return nfserr_expired;

	dprintk("NFSD: bad stateid (%08x/%08x/%08x/%08x)!\n",
		stateid->si_boot, stateid->si_stateownerid,
		stateid->si_fileid, stateid->si_generation);
	return nfserr_bad_stateid;
}

static inline int
access_permit_read(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_READ, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap);
}

static inline int
access_permit_write(unsigned long access_bmap)
{
	return test_bit(NFS4_SHARE_ACCESS_WRITE, &access_bmap) ||
		test_bit(NFS4_SHARE_ACCESS_BOTH, &access_bmap);
}

static
__be32 nfs4_check_openmode(struct nfs4_stateid *stp, int flags)
{
        __be32 status = nfserr_openmode;

	if ((flags & WR_STATE) && (!access_permit_write(stp->st_access_bmap)))
                goto out;
	if ((flags & RD_STATE) && (!access_permit_read(stp->st_access_bmap)))
                goto out;
	status = nfs_ok;
out:
	return status;
}

static inline __be32
check_special_stateids(svc_fh *current_fh, stateid_t *stateid, int flags)
{
	if (ONE_STATEID(stateid) && (flags & RD_STATE))
		return nfs_ok;
	else if (locks_in_grace()) {
		/* Answer in remaining cases depends on existance of
		 * conflicting state; so we must wait out the grace period. */
		return nfserr_grace;
	} else if (flags & WR_STATE)
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_WRITE);
	else /* (flags & RD_STATE) && ZERO_STATEID(stateid) */
		return nfs4_share_conflict(current_fh,
				NFS4_SHARE_DENY_READ);
}

/*
 * Allow READ/WRITE during grace period on recovered state only for files
 * that are not able to provide mandatory locking.
 */
static inline int
grace_disallows_io(struct inode *inode)
{
	return locks_in_grace() && mandatory_lock(inode);
}

static int check_stateid_generation(stateid_t *in, stateid_t *ref, int flags)
{
	/*
	 * When sessions are used the stateid generation number is ignored
	 * when it is zero.
	 */
	if ((flags & HAS_SESSION) && in->si_generation == 0)
		goto out;

	/* If the client sends us a stateid from the future, it's buggy: */
	if (in->si_generation > ref->si_generation)
		return nfserr_bad_stateid;
	/*
	 * The following, however, can happen.  For example, if the
	 * client sends an open and some IO at the same time, the open
	 * may bump si_generation while the IO is still in flight.
	 * Thanks to hard links and renames, the client never knows what
	 * file an open will affect.  So it could avoid that situation
	 * only by serializing all opens and IO from the same open
	 * owner.  To recover from the old_stateid error, the client
	 * will just have to retry the IO:
	 */
	if (in->si_generation < ref->si_generation)
		return nfserr_old_stateid;
out:
	return nfs_ok;
}

static int is_delegation_stateid(stateid_t *stateid)
{
	return stateid->si_fileid == 0;
}

/*
* Checks for stateid operations
*/
__be32
nfs4_preprocess_stateid_op(struct nfsd4_compound_state *cstate,
			   stateid_t *stateid, int flags, struct file **filpp)
{
	struct nfs4_stateid *stp = NULL;
	struct nfs4_delegation *dp = NULL;
	struct svc_fh *current_fh = &cstate->current_fh;
	struct inode *ino = current_fh->fh_dentry->d_inode;
	__be32 status;

	if (filpp)
		*filpp = NULL;

	if (grace_disallows_io(ino))
		return nfserr_grace;

	if (nfsd4_has_session(cstate))
		flags |= HAS_SESSION;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		return check_special_stateids(current_fh, stateid, flags);

	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid)) 
		goto out;

	status = nfserr_bad_stateid;
	if (is_delegation_stateid(stateid)) {
		dp = find_delegation_stateid(ino, stateid);
		if (!dp) {
			status = stateid_error_map(stateid);
			goto out;
		}
		status = check_stateid_generation(stateid, &dp->dl_stateid,
						  flags);
		if (status)
			goto out;
		status = nfs4_check_delegmode(dp, flags);
		if (status)
			goto out;
		renew_client(dp->dl_client);
		if (filpp)
			*filpp = dp->dl_vfs_file;
	} else { /* open or lock stateid */
		stp = find_stateid(stateid, flags);
		if (!stp) {
			status = stateid_error_map(stateid);
			goto out;
		}
		if (nfs4_check_fh(current_fh, stp))
			goto out;
		if (!stp->st_stateowner->so_confirmed)
			goto out;
		status = check_stateid_generation(stateid, &stp->st_stateid,
						  flags);
		if (status)
			goto out;
		status = nfs4_check_openmode(stp, flags);
		if (status)
			goto out;
		renew_client(stp->st_stateowner->so_client);
		if (filpp)
			*filpp = stp->st_vfs_file;
	}
	status = nfs_ok;
out:
	return status;
}

static inline int
setlkflg (int type)
{
	return (type == NFS4_READW_LT || type == NFS4_READ_LT) ?
		RD_STATE : WR_STATE;
}

/* 
 * Checks for sequence id mutating operations. 
 */
static __be32
nfs4_preprocess_seqid_op(struct nfsd4_compound_state *cstate, u32 seqid,
			 stateid_t *stateid, int flags,
			 struct nfs4_stateowner **sopp,
			 struct nfs4_stateid **stpp, struct nfsd4_lock *lock)
{
	struct nfs4_stateid *stp;
	struct nfs4_stateowner *sop;
	struct svc_fh *current_fh = &cstate->current_fh;
	__be32 status;

	dprintk("NFSD: preprocess_seqid_op: seqid=%d " 
			"stateid = (%08x/%08x/%08x/%08x)\n", seqid,
		stateid->si_boot, stateid->si_stateownerid, stateid->si_fileid,
		stateid->si_generation);

	*stpp = NULL;
	*sopp = NULL;

	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid)) {
		dprintk("NFSD: preprocess_seqid_op: magic stateid!\n");
		return nfserr_bad_stateid;
	}

	if (STALE_STATEID(stateid))
		return nfserr_stale_stateid;

	if (nfsd4_has_session(cstate))
		flags |= HAS_SESSION;

	/*
	* We return BAD_STATEID if filehandle doesn't match stateid, 
	* the confirmed flag is incorrecly set, or the generation 
	* number is incorrect.  
	*/
	stp = find_stateid(stateid, flags);
	if (stp == NULL) {
		/*
		 * Also, we should make sure this isn't just the result of
		 * a replayed close:
		 */
		sop = search_close_lru(stateid->si_stateownerid, flags);
		if (sop == NULL)
			return stateid_error_map(stateid);
		*sopp = sop;
		goto check_replay;
	}

	*stpp = stp;
	*sopp = sop = stp->st_stateowner;

	if (lock) {
		clientid_t *lockclid = &lock->v.new.clientid;
		struct nfs4_client *clp = sop->so_client;
		int lkflg = 0;
		__be32 status;

		lkflg = setlkflg(lock->lk_type);

		if (lock->lk_is_new) {
			if (!sop->so_is_open_owner)
				return nfserr_bad_stateid;
			if (!(flags & HAS_SESSION) &&
			    !same_clid(&clp->cl_clientid, lockclid))
				return nfserr_bad_stateid;
			/* stp is the open stateid */
			status = nfs4_check_openmode(stp, lkflg);
			if (status)
				return status;
		} else {
			/* stp is the lock stateid */
			status = nfs4_check_openmode(stp->st_openstp, lkflg);
			if (status)
				return status;
               }
	}

	if (nfs4_check_fh(current_fh, stp)) {
		dprintk("NFSD: preprocess_seqid_op: fh-stateid mismatch!\n");
		return nfserr_bad_stateid;
	}

	/*
	*  We now validate the seqid and stateid generation numbers.
	*  For the moment, we ignore the possibility of 
	*  generation number wraparound.
	*/
	if (!(flags & HAS_SESSION) && seqid != sop->so_seqid)
		goto check_replay;

	if (sop->so_confirmed && flags & CONFIRM) {
		dprintk("NFSD: preprocess_seqid_op: expected"
				" unconfirmed stateowner!\n");
		return nfserr_bad_stateid;
	}
	if (!sop->so_confirmed && !(flags & CONFIRM)) {
		dprintk("NFSD: preprocess_seqid_op: stateowner not"
				" confirmed yet!\n");
		return nfserr_bad_stateid;
	}
	status = check_stateid_generation(stateid, &stp->st_stateid, flags);
	if (status)
		return status;
	renew_client(sop->so_client);
	return nfs_ok;

check_replay:
	if (seqid == sop->so_seqid - 1) {
		dprintk("NFSD: preprocess_seqid_op: retransmission?\n");
		/* indicate replay to calling function */
		return nfserr_replay_me;
	}
	dprintk("NFSD: preprocess_seqid_op: bad seqid (expected %d, got %d)\n",
			sop->so_seqid, seqid);
	*sopp = NULL;
	return nfserr_bad_seqid;
}

__be32
nfsd4_open_confirm(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		   struct nfsd4_open_confirm *oc)
{
	__be32 status;
	struct nfs4_stateowner *sop;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_open_confirm on file %.*s\n",
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0);
	if (status)
		return status;

	nfs4_lock_state();

	if ((status = nfs4_preprocess_seqid_op(cstate,
					oc->oc_seqid, &oc->oc_req_stateid,
					CONFIRM | OPEN_STATE,
					&oc->oc_stateowner, &stp, NULL)))
		goto out; 

	sop = oc->oc_stateowner;
	sop->so_confirmed = 1;
	update_stateid(&stp->st_stateid);
	memcpy(&oc->oc_resp_stateid, &stp->st_stateid, sizeof(stateid_t));
	dprintk("NFSD: nfsd4_open_confirm: success, seqid=%d " 
		"stateid=(%08x/%08x/%08x/%08x)\n", oc->oc_seqid,
		         stp->st_stateid.si_boot,
		         stp->st_stateid.si_stateownerid,
		         stp->st_stateid.si_fileid,
		         stp->st_stateid.si_generation);

	nfsd4_create_clid_dir(sop->so_client);
out:
	if (oc->oc_stateowner) {
		nfs4_get_stateowner(oc->oc_stateowner);
		cstate->replay_owner = oc->oc_stateowner;
	}
	nfs4_unlock_state();
	return status;
}


/*
 * unset all bits in union bitmap (bmap) that
 * do not exist in share (from successful OPEN_DOWNGRADE)
 */
static void
reset_union_bmap_access(unsigned long access, unsigned long *bmap)
{
	int i;
	for (i = 1; i < 4; i++) {
		if ((i & access) != i)
			__clear_bit(i, bmap);
	}
}

static void
reset_union_bmap_deny(unsigned long deny, unsigned long *bmap)
{
	int i;
	for (i = 0; i < 4; i++) {
		if ((i & deny) != i)
			__clear_bit(i, bmap);
	}
}

__be32
nfsd4_open_downgrade(struct svc_rqst *rqstp,
		     struct nfsd4_compound_state *cstate,
		     struct nfsd4_open_downgrade *od)
{
	__be32 status;
	struct nfs4_stateid *stp;
	unsigned int share_access;

	dprintk("NFSD: nfsd4_open_downgrade on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	if (!access_valid(od->od_share_access, cstate->minorversion)
			|| !deny_valid(od->od_share_deny))
		return nfserr_inval;

	nfs4_lock_state();
	if ((status = nfs4_preprocess_seqid_op(cstate,
					od->od_seqid,
					&od->od_stateid, 
					OPEN_STATE,
					&od->od_stateowner, &stp, NULL)))
		goto out; 

	status = nfserr_inval;
	if (!test_bit(od->od_share_access, &stp->st_access_bmap)) {
		dprintk("NFSD:access not a subset current bitmap: 0x%lx, input access=%08x\n",
			stp->st_access_bmap, od->od_share_access);
		goto out;
	}
	if (!test_bit(od->od_share_deny, &stp->st_deny_bmap)) {
		dprintk("NFSD:deny not a subset current bitmap: 0x%lx, input deny=%08x\n",
			stp->st_deny_bmap, od->od_share_deny);
		goto out;
	}
	set_access(&share_access, stp->st_access_bmap);
	nfs4_file_downgrade(stp->st_vfs_file,
	                    share_access & ~od->od_share_access);

	reset_union_bmap_access(od->od_share_access, &stp->st_access_bmap);
	reset_union_bmap_deny(od->od_share_deny, &stp->st_deny_bmap);

	update_stateid(&stp->st_stateid);
	memcpy(&od->od_stateid, &stp->st_stateid, sizeof(stateid_t));
	status = nfs_ok;
out:
	if (od->od_stateowner) {
		nfs4_get_stateowner(od->od_stateowner);
		cstate->replay_owner = od->od_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * nfs4_unlock_state() called after encode
 */
__be32
nfsd4_close(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_close *close)
{
	__be32 status;
	struct nfs4_stateid *stp;

	dprintk("NFSD: nfsd4_close on file %.*s\n", 
			(int)cstate->current_fh.fh_dentry->d_name.len,
			cstate->current_fh.fh_dentry->d_name.name);

	nfs4_lock_state();
	/* check close_lru for replay */
	if ((status = nfs4_preprocess_seqid_op(cstate,
					close->cl_seqid,
					&close->cl_stateid, 
					OPEN_STATE | CLOSE_STATE,
					&close->cl_stateowner, &stp, NULL)))
		goto out; 
	status = nfs_ok;
	update_stateid(&stp->st_stateid);
	memcpy(&close->cl_stateid, &stp->st_stateid, sizeof(stateid_t));

	/* release_stateid() calls nfsd_close() if needed */
	release_open_stateid(stp);

	/* place unused nfs4_stateowners on so_close_lru list to be
	 * released by the laundromat service after the lease period
	 * to enable us to handle CLOSE replay
	 */
	if (list_empty(&close->cl_stateowner->so_stateids))
		move_to_close_lru(close->cl_stateowner);
out:
	if (close->cl_stateowner) {
		nfs4_get_stateowner(close->cl_stateowner);
		cstate->replay_owner = close->cl_stateowner;
	}
	nfs4_unlock_state();
	return status;
}

__be32
nfsd4_delegreturn(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
		  struct nfsd4_delegreturn *dr)
{
	struct nfs4_delegation *dp;
	stateid_t *stateid = &dr->dr_stateid;
	struct inode *inode;
	__be32 status;
	int flags = 0;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0)))
		return status;
	inode = cstate->current_fh.fh_dentry->d_inode;

	if (nfsd4_has_session(cstate))
		flags |= HAS_SESSION;
	nfs4_lock_state();
	status = nfserr_bad_stateid;
	if (ZERO_STATEID(stateid) || ONE_STATEID(stateid))
		goto out;
	status = nfserr_stale_stateid;
	if (STALE_STATEID(stateid))
		goto out;
	status = nfserr_bad_stateid;
	if (!is_delegation_stateid(stateid))
		goto out;
	dp = find_delegation_stateid(inode, stateid);
	if (!dp) {
		status = stateid_error_map(stateid);
		goto out;
	}
	status = check_stateid_generation(stateid, &dp->dl_stateid, flags);
	if (status)
		goto out;
	renew_client(dp->dl_client);

	unhash_delegation(dp);
out:
	nfs4_unlock_state();

	return status;
}


/* 
 * Lock owner state (byte-range locks)
 */
#define LOFF_OVERFLOW(start, len)      ((u64)(len) > ~(u64)(start))
#define LOCK_HASH_BITS              8
#define LOCK_HASH_SIZE             (1 << LOCK_HASH_BITS)
#define LOCK_HASH_MASK             (LOCK_HASH_SIZE - 1)

static inline u64
end_offset(u64 start, u64 len)
{
	u64 end;

	end = start + len;
	return end >= start ? end: NFS4_MAX_UINT64;
}

/* last octet in a range */
static inline u64
last_byte_offset(u64 start, u64 len)
{
	u64 end;

	BUG_ON(!len);
	end = start + len;
	return end > start ? end - 1: NFS4_MAX_UINT64;
}

#define lockownerid_hashval(id) \
        ((id) & LOCK_HASH_MASK)

static inline unsigned int
lock_ownerstr_hashval(struct inode *inode, u32 cl_id,
		struct xdr_netobj *ownername)
{
	return (file_hashval(inode) + cl_id
			+ opaque_hashval(ownername->data, ownername->len))
		& LOCK_HASH_MASK;
}

static struct list_head lock_ownerid_hashtbl[LOCK_HASH_SIZE];
static struct list_head	lock_ownerstr_hashtbl[LOCK_HASH_SIZE];
static struct list_head lockstateid_hashtbl[STATEID_HASH_SIZE];

static struct nfs4_stateid *
find_stateid(stateid_t *stid, int flags)
{
	struct nfs4_stateid *local;
	u32 st_id = stid->si_stateownerid;
	u32 f_id = stid->si_fileid;
	unsigned int hashval;

	dprintk("NFSD: find_stateid flags 0x%x\n",flags);
	if (flags & (LOCK_STATE | RD_STATE | WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &lockstateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	} 

	if (flags & (OPEN_STATE | RD_STATE | WR_STATE)) {
		hashval = stateid_hashval(st_id, f_id);
		list_for_each_entry(local, &stateid_hashtbl[hashval], st_hash) {
			if ((local->st_stateid.si_stateownerid == st_id) &&
			    (local->st_stateid.si_fileid == f_id))
				return local;
		}
	}
	return NULL;
}

static struct nfs4_delegation *
find_delegation_stateid(struct inode *ino, stateid_t *stid)
{
	struct nfs4_file *fp;
	struct nfs4_delegation *dl;

	dprintk("NFSD:find_delegation_stateid stateid=(%08x/%08x/%08x/%08x)\n",
                    stid->si_boot, stid->si_stateownerid,
                    stid->si_fileid, stid->si_generation);

	fp = find_file(ino);
	if (!fp)
		return NULL;
	dl = find_delegation_file(fp, stid);
	put_nfs4_file(fp);
	return dl;
}

/*
 * TODO: Linux file offsets are _signed_ 64-bit quantities, which means that
 * we can't properly handle lock requests that go beyond the (2^63 - 1)-th
 * byte, because of sign extension problems.  Since NFSv4 calls for 64-bit
 * locking, this prevents us from being completely protocol-compliant.  The
 * real solution to this problem is to start using unsigned file offsets in
 * the VFS, but this is a very deep change!
 */
static inline void
nfs4_transform_lock_offset(struct file_lock *lock)
{
	if (lock->fl_start < 0)
		lock->fl_start = OFFSET_MAX;
	if (lock->fl_end < 0)
		lock->fl_end = OFFSET_MAX;
}

/* Hack!: For now, we're defining this just so we can use a pointer to it
 * as a unique cookie to identify our (NFSv4's) posix locks. */
static const struct lock_manager_operations nfsd_posix_mng_ops  = {
};

static inline void
nfs4_set_lock_denied(struct file_lock *fl, struct nfsd4_lock_denied *deny)
{
	struct nfs4_stateowner *sop;
	unsigned int hval;

	if (fl->fl_lmops == &nfsd_posix_mng_ops) {
		sop = (struct nfs4_stateowner *) fl->fl_owner;
		hval = lockownerid_hashval(sop->so_id);
		kref_get(&sop->so_ref);
		deny->ld_sop = sop;
		deny->ld_clientid = sop->so_client->cl_clientid;
	} else {
		deny->ld_sop = NULL;
		deny->ld_clientid.cl_boot = 0;
		deny->ld_clientid.cl_id = 0;
	}
	deny->ld_start = fl->fl_start;
	deny->ld_length = NFS4_MAX_UINT64;
	if (fl->fl_end != NFS4_MAX_UINT64)
		deny->ld_length = fl->fl_end - fl->fl_start + 1;        
	deny->ld_type = NFS4_READ_LT;
	if (fl->fl_type != F_RDLCK)
		deny->ld_type = NFS4_WRITE_LT;
}

static struct nfs4_stateowner *
find_lockstateowner_str(struct inode *inode, clientid_t *clid,
		struct xdr_netobj *owner)
{
	unsigned int hashval = lock_ownerstr_hashval(inode, clid->cl_id, owner);
	struct nfs4_stateowner *op;

	list_for_each_entry(op, &lock_ownerstr_hashtbl[hashval], so_strhash) {
		if (same_owner_str(op, owner, clid))
			return op;
	}
	return NULL;
}

/*
 * Alloc a lock owner structure.
 * Called in nfsd4_lock - therefore, OPEN and OPEN_CONFIRM (if needed) has 
 * occured. 
 *
 * strhashval = lock_ownerstr_hashval 
 */

static struct nfs4_stateowner *
alloc_init_lock_stateowner(unsigned int strhashval, struct nfs4_client *clp, struct nfs4_stateid *open_stp, struct nfsd4_lock *lock) {
	struct nfs4_stateowner *sop;
	struct nfs4_replay *rp;
	unsigned int idhashval;

	if (!(sop = alloc_stateowner(&lock->lk_new_owner)))
		return NULL;
	idhashval = lockownerid_hashval(current_ownerid);
	INIT_LIST_HEAD(&sop->so_idhash);
	INIT_LIST_HEAD(&sop->so_strhash);
	INIT_LIST_HEAD(&sop->so_perclient);
	INIT_LIST_HEAD(&sop->so_stateids);
	INIT_LIST_HEAD(&sop->so_perstateid);
	INIT_LIST_HEAD(&sop->so_close_lru); /* not used */
	sop->so_time = 0;
	list_add(&sop->so_idhash, &lock_ownerid_hashtbl[idhashval]);
	list_add(&sop->so_strhash, &lock_ownerstr_hashtbl[strhashval]);
	list_add(&sop->so_perstateid, &open_stp->st_lockowners);
	sop->so_is_open_owner = 0;
	sop->so_id = current_ownerid++;
	sop->so_client = clp;
	/* It is the openowner seqid that will be incremented in encode in the
	 * case of new lockowners; so increment the lock seqid manually: */
	sop->so_seqid = lock->lk_new_lock_seqid + 1;
	sop->so_confirmed = 1;
	rp = &sop->so_replay;
	rp->rp_status = nfserr_serverfault;
	rp->rp_buflen = 0;
	rp->rp_buf = rp->rp_ibuf;
	return sop;
}

static struct nfs4_stateid *
alloc_init_lock_stateid(struct nfs4_stateowner *sop, struct nfs4_file *fp, struct nfs4_stateid *open_stp)
{
	struct nfs4_stateid *stp;
	unsigned int hashval = stateid_hashval(sop->so_id, fp->fi_id);

	stp = nfs4_alloc_stateid();
	if (stp == NULL)
		goto out;
	INIT_LIST_HEAD(&stp->st_hash);
	INIT_LIST_HEAD(&stp->st_perfile);
	INIT_LIST_HEAD(&stp->st_perstateowner);
	INIT_LIST_HEAD(&stp->st_lockowners); /* not used */
	list_add(&stp->st_hash, &lockstateid_hashtbl[hashval]);
	list_add(&stp->st_perfile, &fp->fi_stateids);
	list_add(&stp->st_perstateowner, &sop->so_stateids);
	stp->st_stateowner = sop;
	get_nfs4_file(fp);
	stp->st_file = fp;
	stp->st_stateid.si_boot = get_seconds();
	stp->st_stateid.si_stateownerid = sop->so_id;
	stp->st_stateid.si_fileid = fp->fi_id;
	stp->st_stateid.si_generation = 0;
	stp->st_vfs_file = open_stp->st_vfs_file; /* FIXME refcount?? */
	stp->st_access_bmap = open_stp->st_access_bmap;
	stp->st_deny_bmap = open_stp->st_deny_bmap;
	stp->st_openstp = open_stp;

out:
	return stp;
}

static int
check_lock_length(u64 offset, u64 length)
{
	return ((length == 0)  || ((length != NFS4_MAX_UINT64) &&
	     LOFF_OVERFLOW(offset, length)));
}

/*
 *  LOCK operation 
 */
__be32
nfsd4_lock(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	   struct nfsd4_lock *lock)
{
	struct nfs4_stateowner *open_sop = NULL;
	struct nfs4_stateowner *lock_sop = NULL;
	struct nfs4_stateid *lock_stp;
	struct file *filp;
	struct file_lock file_lock;
	struct file_lock conflock;
	__be32 status = 0;
	unsigned int strhashval;
	unsigned int cmd;
	int err;

	dprintk("NFSD: nfsd4_lock: start=%Ld length=%Ld\n",
		(long long) lock->lk_offset,
		(long long) lock->lk_length);

	if (check_lock_length(lock->lk_offset, lock->lk_length))
		 return nfserr_inval;

	if ((status = fh_verify(rqstp, &cstate->current_fh,
				S_IFREG, NFSD_MAY_LOCK))) {
		dprintk("NFSD: nfsd4_lock: permission denied!\n");
		return status;
	}

	nfs4_lock_state();

	if (lock->lk_is_new) {
		/*
		 * Client indicates that this is a new lockowner.
		 * Use open owner and open stateid to create lock owner and
		 * lock stateid.
		 */
		struct nfs4_stateid *open_stp = NULL;
		struct nfs4_file *fp;
		
		status = nfserr_stale_clientid;
		if (!nfsd4_has_session(cstate) &&
		    STALE_CLIENTID(&lock->lk_new_clientid))
			goto out;

		/* validate and update open stateid and open seqid */
		status = nfs4_preprocess_seqid_op(cstate,
				        lock->lk_new_open_seqid,
		                        &lock->lk_new_open_stateid,
					OPEN_STATE,
		                        &lock->lk_replay_owner, &open_stp,
					lock);
		if (status)
			goto out;
		open_sop = lock->lk_replay_owner;
		/* create lockowner and lock stateid */
		fp = open_stp->st_file;
		strhashval = lock_ownerstr_hashval(fp->fi_inode, 
				open_sop->so_client->cl_clientid.cl_id, 
				&lock->v.new.owner);
		/* XXX: Do we need to check for duplicate stateowners on
		 * the same file, or should they just be allowed (and
		 * create new stateids)? */
		status = nfserr_resource;
		lock_sop = alloc_init_lock_stateowner(strhashval,
				open_sop->so_client, open_stp, lock);
		if (lock_sop == NULL)
			goto out;
		lock_stp = alloc_init_lock_stateid(lock_sop, fp, open_stp);
		if (lock_stp == NULL)
			goto out;
	} else {
		/* lock (lock owner + lock stateid) already exists */
		status = nfs4_preprocess_seqid_op(cstate,
				       lock->lk_old_lock_seqid, 
				       &lock->lk_old_lock_stateid, 
				       LOCK_STATE,
				       &lock->lk_replay_owner, &lock_stp, lock);
		if (status)
			goto out;
		lock_sop = lock->lk_replay_owner;
	}
	/* lock->lk_replay_owner and lock_stp have been created or found */
	filp = lock_stp->st_vfs_file;

	status = nfserr_grace;
	if (locks_in_grace() && !lock->lk_reclaim)
		goto out;
	status = nfserr_no_grace;
	if (!locks_in_grace() && lock->lk_reclaim)
		goto out;

	locks_init_lock(&file_lock);
	switch (lock->lk_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
			cmd = F_SETLK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
			cmd = F_SETLK;
		break;
		default:
			status = nfserr_inval;
		goto out;
	}
	file_lock.fl_owner = (fl_owner_t)lock_sop;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX;
	file_lock.fl_lmops = &nfsd_posix_mng_ops;

	file_lock.fl_start = lock->lk_offset;
	file_lock.fl_end = last_byte_offset(lock->lk_offset, lock->lk_length);
	nfs4_transform_lock_offset(&file_lock);

	/*
	* Try to lock the file in the VFS.
	* Note: locks.c uses the BKL to protect the inode's lock list.
	*/

	err = vfs_lock_file(filp, cmd, &file_lock, &conflock);
	switch (-err) {
	case 0: /* success! */
		update_stateid(&lock_stp->st_stateid);
		memcpy(&lock->lk_resp_stateid, &lock_stp->st_stateid, 
				sizeof(stateid_t));
		status = 0;
		break;
	case (EAGAIN):		/* conflock holds conflicting lock */
		status = nfserr_denied;
		dprintk("NFSD: nfsd4_lock: conflicting lock found!\n");
		nfs4_set_lock_denied(&conflock, &lock->lk_denied);
		break;
	case (EDEADLK):
		status = nfserr_deadlock;
		break;
	default:        
		dprintk("NFSD: nfsd4_lock: vfs_lock_file() failed! status %d\n",err);
		status = nfserr_resource;
		break;
	}
out:
	if (status && lock->lk_is_new && lock_sop)
		release_lockowner(lock_sop);
	if (lock->lk_replay_owner) {
		nfs4_get_stateowner(lock->lk_replay_owner);
		cstate->replay_owner = lock->lk_replay_owner;
	}
	nfs4_unlock_state();
	return status;
}

/*
 * The NFSv4 spec allows a client to do a LOCKT without holding an OPEN,
 * so we do a temporary open here just to get an open file to pass to
 * vfs_test_lock.  (Arguably perhaps test_lock should be done with an
 * inode operation.)
 */
static int nfsd_test_lock(struct svc_rqst *rqstp, struct svc_fh *fhp, struct file_lock *lock)
{
	struct file *file;
	int err;

	err = nfsd_open(rqstp, fhp, S_IFREG, NFSD_MAY_READ, &file);
	if (err)
		return err;
	err = vfs_test_lock(file, lock);
	nfsd_close(file);
	return err;
}

/*
 * LOCKT operation
 */
__be32
nfsd4_lockt(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_lockt *lockt)
{
	struct inode *inode;
	struct file_lock file_lock;
	int error;
	__be32 status;

	if (locks_in_grace())
		return nfserr_grace;

	if (check_lock_length(lockt->lt_offset, lockt->lt_length))
		 return nfserr_inval;

	lockt->lt_stateowner = NULL;
	nfs4_lock_state();

	status = nfserr_stale_clientid;
	if (!nfsd4_has_session(cstate) && STALE_CLIENTID(&lockt->lt_clientid))
		goto out;

	if ((status = fh_verify(rqstp, &cstate->current_fh, S_IFREG, 0))) {
		dprintk("NFSD: nfsd4_lockt: fh_verify() failed!\n");
		if (status == nfserr_symlink)
			status = nfserr_inval;
		goto out;
	}

	inode = cstate->current_fh.fh_dentry->d_inode;
	locks_init_lock(&file_lock);
	switch (lockt->lt_type) {
		case NFS4_READ_LT:
		case NFS4_READW_LT:
			file_lock.fl_type = F_RDLCK;
		break;
		case NFS4_WRITE_LT:
		case NFS4_WRITEW_LT:
			file_lock.fl_type = F_WRLCK;
		break;
		default:
			dprintk("NFSD: nfs4_lockt: bad lock type!\n");
			status = nfserr_inval;
		goto out;
	}

	lockt->lt_stateowner = find_lockstateowner_str(inode,
			&lockt->lt_clientid, &lockt->lt_owner);
	if (lockt->lt_stateowner)
		file_lock.fl_owner = (fl_owner_t)lockt->lt_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_flags = FL_POSIX;

	file_lock.fl_start = lockt->lt_offset;
	file_lock.fl_end = last_byte_offset(lockt->lt_offset, lockt->lt_length);

	nfs4_transform_lock_offset(&file_lock);

	status = nfs_ok;
	error = nfsd_test_lock(rqstp, &cstate->current_fh, &file_lock);
	if (error) {
		status = nfserrno(error);
		goto out;
	}
	if (file_lock.fl_type != F_UNLCK) {
		status = nfserr_denied;
		nfs4_set_lock_denied(&file_lock, &lockt->lt_denied);
	}
out:
	nfs4_unlock_state();
	return status;
}

__be32
nfsd4_locku(struct svc_rqst *rqstp, struct nfsd4_compound_state *cstate,
	    struct nfsd4_locku *locku)
{
	struct nfs4_stateid *stp;
	struct file *filp = NULL;
	struct file_lock file_lock;
	__be32 status;
	int err;
						        
	dprintk("NFSD: nfsd4_locku: start=%Ld length=%Ld\n",
		(long long) locku->lu_offset,
		(long long) locku->lu_length);

	if (check_lock_length(locku->lu_offset, locku->lu_length))
		 return nfserr_inval;

	nfs4_lock_state();
									        
	if ((status = nfs4_preprocess_seqid_op(cstate,
					locku->lu_seqid, 
					&locku->lu_stateid, 
					LOCK_STATE,
					&locku->lu_stateowner, &stp, NULL)))
		goto out;

	filp = stp->st_vfs_file;
	BUG_ON(!filp);
	locks_init_lock(&file_lock);
	file_lock.fl_type = F_UNLCK;
	file_lock.fl_owner = (fl_owner_t) locku->lu_stateowner;
	file_lock.fl_pid = current->tgid;
	file_lock.fl_file = filp;
	file_lock.fl_flags = FL_POSIX; 
	file_lock.fl_lmops = &nfsd_posix_mng_ops;
	file_lock.fl_start = locku->lu_offset;

	file_lock.fl_end = last_byte_offset(locku->lu_offset, locku->lu_length);
	nfs4_transform_lock_offset(&file_lock);

	/*
	*  Try to unlock the file in the VFS.
	*/
	err = vfs_lock_file(filp, F_SETLK, &file_lock, NULL);
	if (err) {
		dprintk("NFSD: nfs4_locku: vfs_lock_file failed!\n");
		goto out_nfserr;
	}
	/*
	* OK, unlock succeeded; the only thing left to do is update the stateid.
	*/
	update_stateid(&stp->st_stateid);
	memcpy(&locku->lu_stateid, &stp->st_stateid, sizeof(stateid_t));

out:
	if (locku->lu_stateowner) {
		nfs4_get_stateowner(locku->lu_stateowner);
		cstate->replay_owner = locku->lu_stateowner;
	}
	nfs4_unlock_state();
	return status;

out_nfserr:
	status = nfserrno(err);
	goto out;
}

/*
 * returns
 * 	1: locks held by lockowner
 * 	0: no locks held by lockowner
 */
static int
check_for_locks(struct file *filp, struct nfs4_stateowner *lowner)
{
	struct file_lock **flpp;
	struct inode *inode = filp->f_path.dentry->d_inode;
	int status = 0;

	lock_kernel();
	for (flpp = &inode->i_flock; *flpp != NULL; flpp = &(*flpp)->fl_next) {
		if ((*flpp)->fl_owner == (fl_owner_t)lowner) {
			status = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
	return status;
}

__be32
nfsd4_release_lockowner(struct svc_rqst *fsd/p,
			 linux/nfsd4_compound_state *c1 Thete.c
*
*  Copyrighrelease_locko/he U*rigan.e Un)
{
	clientid_t *clid = &l sityts r->rl_ed.the U;
the Univers40gentshts rAlsopdros@ndy Adamson <kaid *stch.edu>the xdr_netobj *ndros@ Smith <kmsorms@umhts rh.edu>
*
*list_head matches;
	int i;
	__be32 gentus;

	dprintk("ersity of Mich rights rAich.edu>: (%08x/buti):\n" of dric->cl_boot, 1. e mustid)ollo/* XXX check for   are expiration */*
*  he f =verserron <lemi and use  if (STALE_CLIENTID(n th))
		returnt tns acopy  Red rigon <ka( copyions andorm must rigs_helimerh <km
: we're doing a*
* ear search through allorm m rights rs.
	 * Yipes!  For nowondill just hopein tReds aren't really usingmenta
*  are met:
*
*  much, but eventutribuwe have to fixionsse
*  3data *
*  Cures.cond	INIT_LIST_HEAD(&e permi);
	otic(i = 0; i < LOCK_HASH_SIZE; i++) {
		ct offor_each_entry(sop, &uce thts rid_hashtbl[i], so_idon.
tware rhts !samr promo_strior wROVID aiions in b		continue;PLIEwithout specific priot wrisop->sodi ANDbus of t the_peristrindrosE IS PR EXPREY ANNCLUDtice,OT L->st_vfs_fileHIS pIMNTIED	gotoD FIANTIEist Note:IS Sperich.ed unusednrodueITY ANdocIED WA * so it's OKames ool S, I herey be uTIES, INadd(ITED TO, HALL THE , or promote p		}
NTIAL}cati CdedCT, probab norohe dexpect uD ``sinary fTNTALsome (e Unnoter i)
*  3ofionseLITY ANdoform e3. Neithd;BLE
d LIMI
*  are any untilOODS
*  3he nabeenITY ANedCT, INpyright
*   _ok;
	while (!withoempns
**  CONSEE IS Psop =iwithoBUT NOr promo.next, IMPtor*  Redistri*  MES BE LRWARREXEMPLARY, EQUEN/* unTWARENCE OR OTH@deletesNT SEMPLARY, Ronly
 LIABroduopenIBUTORuOF
* CwithodelPECIAL, SING IN ANY WAY 
*  are met:
*
* D ANEQUE}
out: reproduntten he above bREMENT orme abo}

pyriic inns aNGXPRENEGLIGEich.ed_reclaim *
alloc  Copyri(voideservREMENT kmsd.h>(sizeofributor/include <
*  Copyri), GFP_KERNEL).h>
#int
  Redhas  Copyrie0the aboconst char *name, bIRECuse_exchangenux/ux/nunsigned d prstrTWARval =viNCLUDINri<linux/(mp_lux/s#includAR P.h>
#inendrpcopyclISEDfind_confirinclh>
#incby' ANDux/noch>
#includ,ude le.h>
#includx/slabnclusd/x? 1 : 0.h>
#/*
ED Oailure =>OODSnreset beT, INt off,orm mustno_grace...
 be .h>
#inclh>
#inctoswap.h>
#.h>
#includsux/n/kthreah>
#swap.h>
#include sd/1 The
#define NFSD  Copyri/ncrISEDNULLcopyw and be NFSDh>
#include c
#define N NAME: %.*ssour HEXDIR_LEN, FSDDBG_POC

/*.h>
#swap.h>
#inclOVIDEcrpMPLIREMENT 0ILITY #include <4
#define NFSDDBG_FACDBG_Psedamesendorse ocrp->c'ei.hTWARE;
DENT.
*
SPEtLAR Pick S1;
swrit = 1;
ei.hlude he Utime;
1 Th]ta
stamcpy(ent_delerecdir/ic stf Mic;
sta *
#incltic u32 ic u_init_p.h>++<linux/mut1NFSDDBG_FACI
#in
#incl
*  are urrent_ownerid = TYTY AND 1;

#defic tiDBG_PRime_t  Globaencluovi(!meducts derived
 THE I  from this sofE IS PABILITY, WHETHER IN       ic 1 The*  Ke[i]T, STRI
sta_t )
#definOR ID(d)  (!m)  (!_t zmp(NCLUDI (!m x/nfst of scondstate.h>
#include le.h>
#i, elegid = 1statOF SUCH DAMAent_delegid = 1stat		kfreeosta);
staID(stateid)  (!memconest--;
st *stGEBUG_ONncludeeof(stat_t)))
legatfs4_st
*  CocallTS O sizOPcondCLAIM_PREVIOUS OF
* a newvided wis#def*
*  Cosvime = 90;  h>
#include <l/nf  Redu32 cID(stateich.ed of the UnK<linicclnter_l
#definene ZERO_SFAC
#def = 1;

#defin  
#includ touching nfsv4 stae ZERO_STATEID(stateidlGES (u32 ar/lib/nfIMPLs4_s_ssfs4_init;, INex.hdru32 current_ownerid =ions * = 90;tex.hd(stateic time_t b
/*
 *  */
s(stateiti:h>
#inr *      );ID(stLoc OF Tefau_MAX] /
stati%ult lVISEPIN*   (recclAR Pl_ux/n.lenSS Ohiganventst
*  ard dx/nf linux/k/
statie corigntibutiruct kmem_ude ID(stateid)  (!memc ON ANYtatic sttatic scurrent_o/*
*iner_inux =  Glo;
DENTALforwaNG, BUT NOcANY i kmem_cachtateid_t uct kmem_sta* 4.h>
d)  (! IS Phts D ``A
#incd)  (!m;/
staticb = NULL;
statid)  (!mnux/muterutex, teid(
*  de <ed.
*)s);
s* Cinux/usernux/ov. Loo  OR CENTSnoticthed = 1;
swith.
*/
atichacry";aui.h> HE P_lease_timng:condD(stC *statlyREMENT r(inux/sy
 * eventually kingd) ?Ye:
 */
pinlomusttex, thibad
}em_ca d_t ializx/mo cto pro

#inat modulS; Lad;
sta: be ioid nfs4_gents_);

/
#include (!me voim must rpyright
*   d44LAR  NULLsme to dece *fi)ic time_t b
#includp(eof(state, &zerutex);
},  inode *ino, stat kmem_cache *statdelnux/all_lrump((nlocct kme		iput(fi->fi_iateid_t)))

/*(cache_ str(AR P NULLun, fote p}x;
}

smem_inhe f  <li
get_tateiAR P(srecall_lru  Te *fi)
{
	atomic_inc(izeof(stateid_t)))

/*(#inclp((stateid), &zerSESSIONid, sizeof(statenline void
get_nfs4serecal_ref);t nfs4_file((stateid), &zerFILEspin_ucachk(&node);
	e_free(file_slabAR Pnfs4_stateownertions entua
 * OpenOWNERSH_BITS              8
#define OWNER_IBUTORruct nfs4_file *fi)
{
	atomic_inc(promo_ref);
}

staeleg   (1 << OWNER_HASH TATEIDSH_BITS              8
#define OWNER_THE IMPnfs4_stateownerd prnum_S
*  ((idESS Frmp_l) \
/
static(c in  (1 << OWNER_HASH*/
sta, sizeof(stateid_t).
*
* ) + opaqueion. promo_ref);
}

stata),d.
*
* ct list_head	owneridER  from thi - 1)
}*  Keset(&oneh<lin(o, ~0, _stateid al((owt)gs);
stam_cache *statlore mrunc(&fi->condd for alFIUTEX(fromBITSching nfsv4 stadeLL;
salld for alelegation * find_delegateid),ic sxs devoid n  /* bitser in1d(staa     retructovery
#inte  linux/tatei
#includjor.h>
#clude <linux/or.hec_and_lame.datusetructr_ha/* ird = 1;
stt (atomic_dec_/
statR_HA_ne SASK)define NFSDDBG_FACIir nf,        utexule.f(stpiFockd/bi.h>
ght
from t*ptr   10statt\n"fs4_sth>

#defilongomic_<< SToid nING CH D nfs4_set_reccacmax/
stat  are ile ,ceSH_MAsion) * HZoid nfs4_seSinceICES; ifeile ERVIaIS
* gct lisisLIMIlimitedtionthatct l(nRY, P, a      ribucmay qui DATAasone *de, FI cationd
get_id)) & as<linxtionit x/workqid *in(x, inliar * TMASKbecoOCUR_stabvioum_cachlembl[STfirst4_filecation, arf'sASK)
#  from approfromson *
d forRVICES;ux/ner'TATEtal memory  Copux//or other  ats altmem_+ opaqueby imp	if . a hard_clien]s);
HOWEumbert_fhofc struct lis, which vari strccor  2.TASK)
*stp,onn ucfh *cus4_stoveryu            set_max_t *    owne, FILE_HAS/*
*  3Aals inode)rren&fi->alloc_in, areegabyc_ineiRAM.  Quick
*  3estima SOFTuggeoc_iem_ciS     woloc_cUSIN(wDIREnivestr_SDheadoc_
*  3iscovera differd fiSK)
#),ateid)inux/liscould tak <linut 1.5K,
*  3giv*dp;
	tic 	dp = kusa= 37+ ( ((id6%PART->so_crr	 and k("c tideleg_sit
* r_
		r_butted_pages() >> (20 - 2 - PAGE_SHIFTIZE]s);
stateid)return dion, arfi_iwheS     ffsdstateile(GFPstartede *fi)ecoveryunt
       c_inc(atic , FILE_HASget_on.
head   d_haid_ha)

/om thd_t o= wnerse be &lab, flict)dp->->cb_id/
statie;
	_ptr(x,k_statl(id_sl_ =ude <id)  (!meha_staSS F atic _state(&fs4_clmanageateid*/
statunt._INFO MASK   atic IST_%ld->dl_id cb->cn, aiodlt lceINLOCK(cb->cb_ide/HZk_statundry_wqic urecb_idingleth.dat_workqueuendi of for      _idedl_fh, &_mutesTHE ent_mut-ENOMEM>cb_ided_t *ayelete prSH_SOWNERd_slaic_seomat, &fp,	fh_e co_sh1;
stg condlnt);
	INIT_Lverynux/mut
et     back_cre ret* hash tabinuree(mic_setICULAR P =ncluret)

/hts      ile his s, &on ret     (     ead	ownerstr_ha/*tatei
#inclu = CULAR Pstruct nfs4_ 0;
	atoEID_HASHtest(&peseror.h>
ile *=/
stt)) {
		MASK     c_, FILE_ mic_>dl_i, FILE_HASH_BITS)ic_setock(&cKhing nfsv4 stap\n",dp);
		puhutdid_s table for (o STATE                tion(zeof(statestate.h>
#inoc( + op_s *dremnux/le *FS_LEAS, (own, ar*pos, * kmem_reap, (omax_deel(&fi->fi_hash);
		spin_unlock(&recall_l))         ONE_1 << S fi)TEIDree(fkmemciated d) * thestateid, stion(struct nfs4_dt kmem_cc.h>
#incluIN ANYORlinuxgs

stattisrn dng discULL;
sile(IAL)))
#define ONE_STATE&fi->fition.
 * lealock  ((id) *d	kmemloc_ict* The following nfsd_cs4_delegatollowing condc ti: cflase_delegatay ndp %p\n",dpstruct nfs}4_file */
#define sd's loc1;
stpinNESS  (1 <Y AND cks4_file(fps4_decacsafe(y cal kmem_uching nfsv4 sta IS P * thestateid, sdeoid ck listE 
get_ 8
#ER_H, dhing nfsv4 sta(1ateid_the (&_ideeserv);
	dfi_i
	reCK, erfild}_set(los4_sta
	pyri_closid
gelock}r *) palled union(st	dp->dl_legadl_pc{
	atounateid + opaquenfollowing nfsrecall_locnot actualist_del_d_t _lru)/
stsd_cler",dp);
	list_del
	dp-coverHEe_lock fro(led to rc_setcoup->dlfi_ine FILE_(1atomice voiddvoid n        le (the i_florecall_loc.
 canc/
staarmingNTIEd_cl, &fpmic_setionrecall_loce_deSETCLI, SP4_fidede <y_del_iniatomdistri_HAtructsi_stend_slack Se *state + opid++ck(&cl            1 << mutex, thisu64)

#4_fiule.hn *dp)       4
#d/
#definefs4_client ne F)_delegat_HASH_& CLIE10         GFPprotecb, dbyfT_HEru);_utexx s& CLI*  FOF ADV *n *cSOFTS i->fomic_s
static ing mustcb
#innollowi  Rediefine FILE /* bit_state(les fohash 8) & CLIENT_HASH_MASpyristatic l((name),Ced toPARTsNFSv4d	ownerstr_irecto_clOCUREd
nfstp->strpigned 90;    CLIENT_ ude < proiofs4_fiessr (HE P)ic uock listpathmed 
ctua.
* _HASHkern_* senclutr_haLOOKUP_FOLLOW, &* seab, fiefine STATEI  8
#dtware );
	s4_client;
	dTDIRme EXPRE_ISDIR(* se.dBUT N->d_SK)
#->i_m== N
	l_fl/includ /* bigras4_lock_st*noticf Mi    }
	* se_le_sdp->d ord	return s4_stateid /* bitUT Ocolock*
* ])
{
	sE_HASH_BITS) 8) & CLIENT_HASH_MAS' ANion.
*
*inlinepreviions-dl_idisbl[Cnged*statefhTutex);
 wadeler_WNERocold toes, TATEUhandlonf_-
	liflyhval(owlinux/ftructo prosimul,perfs4_clie.  InsttualST_Hight
n bi,ype)rialswaiIT_LIMPLtheforw4_fil prowgned rtinclumgisterock);nfclaim_si->f0;
4_fillegaeid *sadminTHE atosns);THE IMPwaCT, to	reclaVICES; .
*
* CLI *now*,PARTer tn go antualht
*b <lin *ious r 4
#4_filtrevi <li up agSS OafMASK)y cl
	dp->_lock_starucist_headstruct nfs4legaK"NFSNER_ux/nfsdion.
*
* ] holds knownex);
e(fp);
	nfoER_HAZE];
renfs4s inclu/fs4_putp->sBITS      /
nconme), (	tid) hashtbateiclnt.hTS)
#defidel(&s=	put_nfs4_htbl