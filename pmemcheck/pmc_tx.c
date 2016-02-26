/*
 * Persistent memory checker.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, or (at your option) any later version, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "pub_tool_oset.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_execontext.h"

#include "pmc_include.h"

#define MAX_OOT_STORES 10000UL

#define MAX_CROSS_EVS 1000UL

/** Transaction descriptor. */
struct tx_info {
    /** The id of the transaction. */
    UWord tx_id;

    /** Nesting counter */
    UWord counter;

    /** ExeContext of the transaction */
    ExeContext *context;

    /** Regions of memory tracked by the transaction. */
    OSet *regions;

    /** The last added region - cached. */
    struct pmem_st cached_region;
};

/** Thread to transaction descriptor. */
struct thread_info {
    /** The id of the thread. */
    UWord thread_id;

    /** Transaction ids this thread contributes to. */
    OSet *tx_ids;
};

/** Holds the cross-transaction object registration event. */
struct cross_tx_event {
    /** The memory region registered first. */
    struct pmem_st original;

    /** The transaction id of the original registration. */
    UWord orig_tx_id;

    /** The duplicate memory region registered */
    struct pmem_st duplicate;

    /** The transaction id of the duplicate registration. */
    UWord dup_tx_id;
};

/** Holds transaction related parameters and runtime data. */
static struct transaction_ops {
    /** Holds info on all running transactions. */
    OSet *transactions;

    /** Holds thread_info structures of all thread-transaction mappings. */
    OSet *threads;

    /** Holds possible out-of-transaction error events. */
    struct pmem_st **oot_stores;

    /** Holds the number of registered out-of-transaction writes. */
    UWord oot_stores_reg;

    /* Cached verbosity state */
    Bool verbose;

    /* Allow changes to PMEM to be made only within transactions */
    Bool transactions_only;

    /** Holds a list of excluded regions. */
    OSet *excludes;

    /** Holds object additions across different. */
    struct cross_tx_event **cross_tx_evs;

    /** Holds the number of registered out-of-transaction writes. */
    UWord cross_tx_reg;
} trans;

/**
 * \brief Initialize the transactions module
 */
void init_transactions(Bool transactions_only)
{
    trans.transactions = VG_(OSetGen_Create)(/*keyOff*/0, NULL, VG_(malloc),
                            "pmc.trans.cpci.1", VG_(free));

    trans.threads = VG_(OSetGen_Create)(/*keyOff*/0, NULL, VG_(malloc),
                            "pmc.trans.cpci.2", VG_(free));

    trans.oot_stores = VG_(malloc)("pmc.trans.cpci.3", MAX_OOT_STORES
                            * sizeof (struct pmem_st *));

    trans.excludes = VG_(OSetGen_Create)(/*keyOff*/0, cmp_pmem_st,
                                VG_(malloc), "pmc.trans.cpci.4", VG_(free));

    trans.cross_tx_evs = VG_(malloc)("pmc.trans.cpci.5", MAX_CROSS_EVS
                            * sizeof (struct transaction_ops *));

    trans.verbose = (VG_(clo_verbosity) > 1);

    trans.transactions_only = transactions_only;
}

/**
 * \brief Debug print all threads and their transactions.
 */
static void
print_thread_transactions(void)
{
    if (!trans.verbose)
        return;

    VG_(dmsg)("Printing thread transactions\n");
    struct thread_info *elem;
    VG_(OSetGen_ResetIter)(trans.threads);
    while ((elem = VG_(OSetGen_Next)(trans.threads)) != NULL) {
        VG_(dmsg)("Thread: %lu\n", elem->thread_id);
        UWord tmp;
        VG_(OSetWord_ResetIter)(elem->tx_ids);
        while (VG_(OSetWord_Next)(elem->tx_ids, &tmp))
            VG_(dmsg)("tx: %lu\n", tmp);
    }
}

/**
 * \brief Debug print all active transactions.
 */
static void
print_running_transactions(void)
{
    if (!trans.verbose)
        return;

    VG_(dmsg)("Printing running transactions\n");
    VG_(OSetGen_ResetIter)(trans.transactions);
    struct tx_info *tmp_tx;
    while ((tmp_tx = VG_(OSetGen_Next)(trans.transactions)) != NULL) {
        VG_(dmsg)("tx: %lu\t nesting: %lu\n", tmp_tx->tx_id, tmp_tx->counter);
    }
}

/**
 * \brief Debug print regions registered in a transaction.
 * \param[in] tx_id The transaction id.
 */
static void
print_regions(UWord tx_id)
{
    if (!trans.verbose)
        return;

    struct tx_info *tx = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);
    if (tx == NULL)
        VG_(dmsg)("Transaction %lu does not exist\n", tx_id);

    VG_(OSetGen_ResetIter)(tx->regions);
    struct pmem_st *tmp;
    while ((tmp = VG_(OSetGen_Next)(tx->regions)) != NULL) {
        VG_(dmsg)("\tAddress 0x%lx\tsize %llu\n", tmp->addr, tmp->size);
        VG_(pp_ExeContext)(tmp->context);
    }
}

/**
 * \brief Print cross-transaction region register events.
 */
static void
print_cross_evs(void)
{
    if (trans.cross_tx_reg) {
        VG_(umsg)("\n");
        VG_(umsg)("Number of overlapping regions registered in different "
                "transactions: %lu\n", trans.cross_tx_reg);
        VG_(umsg)("Overlapping regions:\n");
        struct cross_tx_event *tmp = NULL;
        Int i;
        for (i = 0; i < trans.cross_tx_reg; ++i) {
            tmp = trans.cross_tx_evs[i];
            VG_(umsg)("[%d] ", i);
            VG_(pp_ExeContext)(tmp->duplicate.context);
            VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\ttx_id: %lu\n",
                    tmp->duplicate.addr, tmp->duplicate.size, tmp->dup_tx_id);
            VG_(umsg)("   First registered here:\n[%d]'", i);
            VG_(pp_ExeContext)(tmp->original.context);
            VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\ttx_id: %lu\n",
                    tmp->original.addr, tmp->original.size, tmp->orig_tx_id);
        }
    }
}

/**
 * \brief Print cross-transaction region register error message.
 */
static void
print_cross_error(void)
{
    VG_(umsg)("Number of overlapping regions registered in different "
            "transactions exceeded %lu\n\n", MAX_CROSS_EVS);

    VG_(umsg)("This means your program is tracking the same memory regions"
            " within different transactions. This is a potential data"
            " consistency issue.\n");
    VG_(message_flush)();

    print_cross_evs();
}

/**
 * \brief Register a cross-transaction region registration event.
 * \param[in] orig The original registration.
 * \param[in] orig_tx The transaction id of the original registration.
 * \param[in] dup The duplicate registration.
 * \param[in] dup_tx The transaction id of the duplicate registration.
 */
static void
register_cross_event(const struct pmem_st *orig, UWord orig_tx,
                     const struct pmem_st *dup, UWord dup_tx)
{
    if (UNLIKELY(trans.cross_tx_reg == MAX_CROSS_EVS)) {
        print_cross_error();
        VG_(exit)(-1);
    }

    struct cross_tx_event *to_insert = VG_(malloc)("pmc.trans.cpci.5",
                                        sizeof (struct cross_tx_event));
    to_insert->original = *orig;
    to_insert->orig_tx_id = orig_tx;
    to_insert->duplicate = *dup;
    to_insert->dup_tx_id = dup_tx;

    /* register the event */
    trans.cross_tx_evs[(trans.cross_tx_reg)++] = to_insert;
}

/**
 * \brief Get or create and get a thread entry.
 * \param[in] thread_id The id of the thread to fetch.
 * \return Will return an existing or a new thread entry.
 */
static struct thread_info *
create_get_thread_entry(UWord thread_id)
{
    struct thread_info *new_thread;
    if (!VG_(OSetGen_Contains)(trans.threads, &thread_id)) {
        new_thread = VG_(OSetGen_AllocNode)(trans.threads,
                                            sizeof (struct thread_info));
        new_thread->thread_id = thread_id;
        new_thread->tx_ids = VG_(OSetWord_Create)(VG_(malloc),
                                    "pmc.trans.cpci.2", VG_(free));
        VG_(OSetGen_Insert)(trans.threads, new_thread);
    } else {
        new_thread = VG_(OSetGen_Lookup)(trans.threads, &thread_id);
    }

    return new_thread;
}

/**
 * \brief Check if given store is exactly the same region as the cached value.
 * \param store The store to check.
 * \param tx The transaction against which cache the comparison is made.
 * \return True on exact match, false otherwise.
 */
static Bool
is_in_cache(const struct pmem_st *store, struct tx_info *tx) {
    return ((store->addr == tx->cached_region.addr)
            && (store->size == tx->cached_region.size));
}

/**
 * \brief Add a new transaction
 * \param[in] tx_id The transaction id to track - must be unique
 */
void
register_new_tx(UWord tx_id)
{
    UWord thread_id = VG_(get_running_tid)();

    if (!VG_(OSetGen_Contains)(trans.transactions, &tx_id)) {
        /* create and insert the new transaction */
        struct tx_info *new_tx = VG_(OSetGen_AllocNode)(trans.transactions,
                                sizeof (struct tx_info));
        new_tx->tx_id = tx_id;
        new_tx->counter = 0;
        new_tx->context = VG_(record_ExeContext)(thread_id, 0);
        new_tx->regions = VG_(OSetGen_Create)(/*keyOff*/0, cmp_pmem_st,
                                VG_(malloc), "pmc.trans.cpci.1", VG_(free));
        new_tx->cached_region.addr = 0;
        new_tx->cached_region.size = 0;
        VG_(OSetGen_Insert)(trans.transactions, new_tx);
    }

    /* add this transaction to the current thread */
    struct thread_info *tinfo = create_get_thread_entry(thread_id);
    if (!VG_(OSetWord_Contains)(tinfo->tx_ids, tx_id))
        VG_(OSetWord_Insert)(tinfo->tx_ids, tx_id);

    struct tx_info *txinf = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);
    txinf->counter += 1;

    if (trans.verbose)
        VG_(dmsg)("Starting transaction: %lu, nesting %lu\n", tx_id,
                    txinf->counter);
    print_running_transactions();
}

/**
 * \brief Remove the given thread entry from the active thread register.
 * \param[in,out] thread_entry The thread entry to be removed.
 */
static void
remove_thread_entry(struct thread_info *thread_entry)
{
    /* sanity check */
    if (!VG_(OSetGen_Contains)(trans.threads, thread_entry))
        return;

    /* remove and destroy entry */
    VG_(OSetGen_Remove)(trans.threads, thread_entry);
    VG_(OSetWord_Destroy)(thread_entry->tx_ids);
    VG_(OSetGen_FreeNode)(trans.threads, thread_entry);
}

/**
 * \brief Remove a transaction
 * \param[in] tx_id Transaction id to remove - must be previously registered.
 */
UInt
remove_tx(UWord tx_id)
{
    struct tx_info *tx = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);

    if (tx == NULL)
        return 1;

    --(tx->counter);

    if (tx->counter > 0)
        return 0;

    /* remove the transaction from each thread */
    struct thread_info *elem;
    VG_(OSetGen_ResetIter)(trans.threads);
    while ((elem = VG_(OSetGen_Next)(trans.threads)) != NULL) {
        VG_(OSetWord_Remove)(elem->tx_ids, tx_id);
        /* remove thread entry if it does not have any more active txs */
        if (VG_(OSetWord_Size)(elem->tx_ids) == 0)
            remove_thread_entry(elem);
    }

    VG_(OSetGen_Destroy)(tx->regions);
    VG_(OSetGen_Remove)(trans.transactions, &tx_id);
    VG_(OSetGen_FreeNode)(trans.transactions, tx);

    return 0;
}

/**
 * \brief Check if the running thread contributes to a given transaction.
 * \param[in] tx_id The id of the transaction.
 * \return True if thread contributes to the transaction, false otherwise.
 */
static Bool
is_tx_in_thread(UWord tx_id)
{
    /* search all transactions this thread is in */
    UWord thread_id = VG_(get_running_tid)();
    struct thread_info *tinfo= VG_(OSetGen_Lookup)(trans.threads, &thread_id);
    /* thread not part of any transaction */
    if (tinfo == NULL) {
        if (trans.verbose)
            VG_(dmsg)("thread %lu not part of any transaction\n", thread_id);
        return False;
    }

    return VG_(OSetWord_Contains)(tinfo->tx_ids, tx_id);
}

/**
 * \brief Flush tx cache to main region tree.
 * \param[in,out] tx Transaction whose cache is to be flushed.
 */
static void
flush_cache(struct tx_info *tx)
{
    /* cache is empty, do not try to flush it */
    if ((tx->cached_region.addr == 0) && (tx->cached_region.size == 0))
        return;

    add_region(&(tx->cached_region), tx->regions);
    tx->cached_region.addr = 0;
    tx->cached_region.size = 0;
}

/**
 * \brief Add a memory region to a transaction.
 * \param[in] tx_id The id of the transaction the object will be added to.
 * \param[in] base The starting address of the region to be added.
 * \param[in] size The size of the added region.
 * \return -1 when tx_id is invalid, -2 when the thread does not contribute
 *            to the given transaction, 0 otherwise.
 */
UInt
add_obj_to_tx(UWord tx_id, UWord base, UWord size)
{
    struct tx_info *tx = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);
    if (tx == NULL) {
        /* no matching transaction found */
        if (trans.verbose)
            VG_(dmsg)("no matching transaction found\n");
        return 1;
    }

    /* this thread does not participate in this transaction */
    if (!is_tx_in_thread(tx_id)) {
        if (trans.verbose)
            VG_(dmsg)("this thread does not participate in this transaction\n");
        print_running_transactions();
        print_thread_transactions();
        return 2;
    }

    struct pmem_st reg = {0};
    reg.addr = base;
    reg.size = size;
    reg.context = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);

    /* check if it is already in any other transaction */
    VG_(OSetGen_ResetIter)(trans.transactions);
    struct tx_info *tx_iter;
    while ((tx_iter = VG_(OSetGen_Next)(trans.transactions)) != NULL) {
        /* omit self */
        if (tx_iter->tx_id == tx_id)
            continue;
        if (cmp_pmem_st(&reg, &(tx_iter->cached_region)) == 0)
            register_cross_event(&tx_iter->cached_region, tx_iter->tx_id,
                                 &reg, tx_id);
        else if (is_in_mapping_set(&reg, tx_iter->regions))
            register_cross_event(VG_(OSetGen_Lookup)(tx_iter->regions, &reg),
                                 tx_iter->tx_id, &reg, tx_id);
    }

    /* cache not empty, consider options */
    if (LIKELY((tx->cached_region.addr != 0)
            && (tx->cached_region.size != 0))) {
        UWord overlap = check_overlap(&(tx->cached_region), &reg);

        if (LIKELY(overlap == 0)) {
            /* no overlap - insert old cached region */
            flush_cache(tx);
        } else if (UNLIKELY(overlap == 2)) {
            /* partial overlap - cut out new cache from regions */
            flush_cache(tx);
            remove_region(&reg, tx->regions);
        }
        /* overlap == 1 - do nothing, new cache includes old cache */
    }

    /* update cache */
    tx->cached_region = reg;
    return 0;
}

/**
 * \brief Remove a registered region from the given transaction.
 * \param[in] tx_id The transaction the object is to be removed from.
 * \param[in] base The starting address of the region to be removed.
 * \param[in] size The size of the removed region.
 * \return -1 when tx_id is invalid, -2 when the thread does not contribute
 *            to the given transaction, 0 otherwise.
 */
UInt
remove_obj_from_tx(UWord tx_id, UWord base, UWord size)
{
    struct tx_info *tx = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);
    if (tx == NULL) {
        /* no matching transaction found */
        if (trans.verbose)
            VG_(dmsg)("no matching transaction found\n");
        return 1;
    }

    /* this thread does not participate in this transaction */
    if (!is_tx_in_thread(tx_id)) {
        if (trans.verbose)
            VG_(dmsg)("this thread does not participate in this transaction\n");
        print_running_transactions();
        print_thread_transactions();
        return 2;
    }

    struct pmem_st reg = {0};
    reg.addr = base;
    reg.size = size;

    /* check for cache match */
    if (is_in_cache(&reg, tx)){
        /* clear cache */
        tx->cached_region.addr = 0;
        tx->cached_region.size = 0;
        return 0;
    } else if (cmp_pmem_st(&reg, &(tx->cached_region)) == 0) {
        /* partial match, add to main storage for splicing */
        add_region(&(tx->cached_region), tx->regions);
    }

    /* remove region from main storage */
    remove_region(&reg, tx->regions);
    return 0;
}

/**
 * \brief Check if the given store is registered in the transaction.
 * \param[in] store The store to be checked.
 * \param[in] tx_id The transaction to be checked.
 * \return True when store is made fully within a registered region of the
 *         transaction, false otherwise.
 */
static Bool
is_store_in_tx(const struct pmem_st *store, UWord tx_id)
{
    struct tx_info *tx = VG_(OSetGen_Lookup)(trans.transactions, &tx_id);
    if (tx == NULL) {
        /* no matching transaction found */
        if (trans.verbose)
            VG_(dmsg)("no matching transaction found\n");
        return False;
    }

    /* check if store is fully within cache */
    if (check_overlap(store, &tx->cached_region) == 1)
        return True;

    /* flush cache because of possible coalescing */
    flush_cache(tx);

    /* return true only if store is fully within one of the regions */
    if (is_in_mapping_set(store, tx->regions) == 1)
        return True;
    else
        return False;
}

/**
 * \brief Print the summary of transaction analysis.
 */
void
print_tx_summary(void)
{
    if (trans.oot_stores_reg) {
        VG_(umsg)("\n");
        VG_(umsg)("Number of stores made without adding to transaction: "
        "%lu\n", trans.oot_stores_reg);
        VG_(umsg)("Stores made without adding to transactions:\n");
        struct pmem_st *tmp = NULL;
        Int i;
        for (i = 0; i < trans.oot_stores_reg; ++i) {
            tmp = trans.oot_stores[i];
            VG_(umsg)("[%d] ", i);
            VG_(pp_ExeContext)(tmp->context);
            VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\n",
                    tmp->addr, tmp->size);
        }
    }

    print_cross_evs();

    /* left over running transactions */
    UWord active_txs = VG_(OSetGen_Size)(trans.transactions);
    if (active_txs != 0) {
        VG_(umsg)("\n");
        VG_(umsg)("Number of active transactions: %lu\n", active_txs);
        VG_(OSetGen_ResetIter)(trans.transactions);
        struct tx_info *tmp_tx;
        UWord idx = 0;
        while ((tmp_tx = VG_(OSetGen_Next)(trans.transactions)) != NULL) {
            VG_(umsg)("[%lu] ", idx++);
            VG_(pp_ExeContext)(tmp_tx->context);
            VG_(umsg)("\ttx_id: %lu\t nesting: %lu\n",
                        tmp_tx->tx_id, tmp_tx->counter);
        }
    }
}

UWord
get_tx_all_err(void)
{
	UWord active_txs = VG_(OSetGen_Size)(trans.transactions);
	return trans.oot_stores_reg + trans.cross_tx_reg + active_txs;
}

/**
 * \brief Print out the error message on OOT stores overflow.
 * \param[in] limit The exceeded limit of OOT stores.
 */
static void
print_tx_err_msg(UWord limit)
{
    VG_(umsg)("The number of out of transaction stores exceeded %lu\n\n",
            limit);

    VG_(umsg)("This means your applications is changing objects that are not"
            " tracked by the ongoing transaction. This may lead to an"
            " inconsistent state of persistent memory.\n");
    VG_(message_flush)();

    print_tx_summary();
}

/**
 * \brief Record an out-of-transaction store.
 * \param[in] store The store to be recorded.
 * \param[in] tinfo Thread info for debugging purposes.
 */
static void
record_store(const struct pmem_st *store, const struct thread_info *tinfo)
{
    struct pmem_st *store_copy = VG_(malloc)("pmc.trans.cpci.3",
                                        sizeof (struct pmem_st));
    *store_copy = *store;
    add_warning_event(trans.oot_stores, &trans.oot_stores_reg,
                  store_copy, MAX_OOT_STORES, print_tx_err_msg);
    if (trans.verbose) {
        VG_(dmsg)("Store outside of transaction\n\taddress 0x%lx\tsize %llu\n",
                        store_copy->addr, store_copy->size);
        VG_(dmsg)("Registered objects:\n");
        UWord tx_id;
        VG_(OSetWord_ResetIter)(tinfo->tx_ids);
        while (VG_(OSetWord_Next)(tinfo->tx_ids, &tx_id)) {
            print_regions(tx_id);
        }
    }
}

/**
 * \brief Handle the store made to PMEM in regards to running transactions.
 * \param[in] store The store to be handled.
 */
void
handle_tx_store(const struct pmem_st *store)
{
    /* check global exclude list, only full includes count */
    if (is_in_mapping_set(store, trans.excludes) == 1)
        return;

    /* search all transactions this thread is in */
    UWord thread_id = VG_(get_running_tid)();
    struct thread_info *tinfo= VG_(OSetGen_Lookup)(trans.threads, &thread_id);

    if (tinfo == NULL) {
        /* report if stores can be made only within transactions */
        if (trans.transactions_only)
            record_store(store, tinfo);
        /* thread is not part of any transaction */
        if (trans.verbose)
            VG_(dmsg)("thread is not part of any transaction\n");
        return;
    }

    /* ensure store is within any of the transactions */
    UWord tx_id;
    VG_(OSetWord_ResetIter)(tinfo->tx_ids);
    while (VG_(OSetWord_Next)(tinfo->tx_ids, &tx_id)) {
        if (is_store_in_tx(store, tx_id))
            return;
    }

    if (trans.verbose) {
        VG_(OSetWord_ResetIter)(tinfo->tx_ids);
        while (VG_(OSetWord_Next)(tinfo->tx_ids, &tx_id)) {
            print_regions(tx_id);
        }
    }

    /* report if not */
    record_store(store, tinfo);
}

/**
 * \brief Explicitely add a thread to a transaction.
 * \param tx_id The id of the transaction.
 * \return 1 if no such transaction is running, 0 otherwise.
 */
UInt
add_thread_to_tx(UWord tx_id)
{
    if (!VG_(OSetGen_Contains)(trans.transactions, &tx_id)) {
        if (trans.verbose)
            VG_(dmsg)("no matching transaction found\n");
        return 1;
    }

    UWord thread_id = VG_(get_running_tid)();

    /* add this transaction to the current thread */
    struct thread_info *tinfo = create_get_thread_entry(thread_id);
    if (!VG_(OSetWord_Contains)(tinfo->tx_ids, tx_id))
        VG_(OSetWord_Insert)(tinfo->tx_ids, tx_id);

    return 0;
}

/**
 * \brief Explicitely remove a thread from a transaction
 * \param tx_id The id of the transaction.
 * \return 1 if no such transaction is running, 2 if the thread is not part
 * of the given transaction, 0 otherwise.
 */
UInt
remove_thread_from_tx(UWord tx_id)
{
    if (!VG_(OSetGen_Contains)(trans.transactions, &tx_id)) {
        if (trans.verbose)
            VG_(dmsg)("no matching transaction found\n");
        return 1;
    }

    if (!is_tx_in_thread(tx_id)) {
        if (trans.verbose)
            VG_(dmsg)("this thread does not participate in this transaction\n");
        print_running_transactions();
        print_thread_transactions();
        return 2;
    }

    UWord thread_id = VG_(get_running_tid)();
    struct thread_info *tinfo= VG_(OSetGen_Lookup)(trans.threads, &thread_id);

    VG_(OSetWord_Remove)(tinfo->tx_ids, tx_id);

    return 0;
}

/**
 * \brief Add region to the global exclude list.
 * \param[in] region The region to be added.
 */
void
add_to_global_excludes(const struct pmem_st *region)
{
    add_region(region, trans.excludes);
}
