/*
 * Persistent memory checker.
 * Copyright (c) 2014-2016, Intel Corporation.
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

/*
 * This program is based on lackey, cachegrind and memcheck.
 */
#include <sys/param.h>
#include "pub_tool_libcfile.h"
#include <fcntl.h>
#include "pub_tool_oset.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_options.h"
#include "pub_tool_libcbase.h"
#include "pub_tool_gdbserver.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_threadstate.h"
#include "pub_tool_libcassert.h"
#include "pub_tool_libcprint.h"
#include "pub_tool_machine.h"
#include "pub_tool_stacktrace.h"
#include "pub_tool_debuginfo.h"

#include "pmemcheck.h"
#include "pmc_include.h"

/* track at max this many multiple overwrites */
#define MAX_MULT_OVERWRITES 10000UL

/* track at max this many flush error events */
#define MAX_FLUSH_ERROR_EVENTS 10000UL

/* build various kinds of expressions */
#define triop(_op, _arg1, _arg2, _arg3) \
                                 IRExpr_Triop((_op),(_arg1),(_arg2),(_arg3))
#define binop(_op, _arg1, _arg2) IRExpr_Binop((_op),(_arg1),(_arg2))
#define unop(_op, _arg)          IRExpr_Unop((_op),(_arg))
#define mkU1(_n)                 IRExpr_Const(IRConst_U1(_n))
#define mkU8(_n)                 IRExpr_Const(IRConst_U8(_n))
#define mkU16(_n)                IRExpr_Const(IRConst_U16(_n))
#define mkU32(_n)                IRExpr_Const(IRConst_U32(_n))
#define mkU64(_n)                IRExpr_Const(IRConst_U64(_n))
#define mkexpr(_tmp)             IRExpr_RdTmp((_tmp))

/** Max store size */
#define MAX_DSIZE    256

/** Max allowable path length */
#define MAX_PATH_SIZE 4096

/** Holds parameters and runtime data */
static struct pmem_ops {
    /** Set of stores to persistent memory. */
    OSet *pmem_stores;

    /** Set of registered persistent memory regions. */
    OSet *pmem_mappings;

    /** Holds possible multiple overwrite error events. */
    struct pmem_st **multiple_stores;

    /** Holds the number of registered multiple overwrites. */
    UWord multiple_stores_reg;

    /** Holds possible redundant flush events. */
    struct pmem_st **redundant_flushes;

    /** Holds the number of registered redundant flush events. */
    UWord redundant_flushes_reg;

    /** Holds superfluous flush error events. */
    struct pmem_st **superfluous_flushes;

    /** Holds the number of superfluous flush events. */
    UWord superfluous_flushes_reg;

    /** Within this many SBlocks a consecutive write is not considered
    * a poss_leak. */
    UWord store_sb_indiff;

    /** Turns on multiple overwrite error tracking. */
    Bool track_multiple_stores;

    /** Turns on logging persistent memory events. */
    Bool log_stores;

    /** Toggles summary printing. */
    Bool print_summary;

    /** Toggles checking multiple and superfluous flushes */
    Bool check_flush;

    /** The size of the cache line */
    Long flush_align_size;

    /** Force flush alignment to native cache line size */
    Bool force_flush_align;

    /** Toggles transaction tracking. */
    Bool transactions_only;

    /** Toggles store stacktrace logging. */
    Bool store_traces;

    /** Depth of the printed store stacktrace. */
    UInt store_traces_depth;

    /** Toggles automatic ISA recognition. */
    Bool automatic_isa_rec;

    /** Toggles error summary message */
    Bool error_summary;

    /** Simulate 2-phase flushing. */
    Bool weak_clflush;
} pmem;

/*
 * Memory tracing pattern as in cachegrind/lackey - in case of future
 * improvements.
 */

/** A specific kind of expression. */
typedef IRExpr IRAtom;

/** Types of discernable events. */
typedef enum {
    Event_Ir,
    Event_Dr,
    Event_Dw,
    Event_Dm
} EventKind;

/** The event structure. */
typedef struct {
    EventKind ekind;
    IRAtom *addr;
    SizeT size;
    IRAtom *guard; /* :: Ity_I1, or NULL=="always True" */
    IRAtom *value;
} Event;

/** Number of sblock run. */
static ULong sblocks = 0;

/**
* \brief Check if a given store overlaps with registered persistent memory
*        regions.
* \param[in] addr The base address of the store.
* \param[in] size The size of the store.
* \return True if store overlaps with any registered region, false otherwise.
*/
static Bool
is_pmem_access(Addr addr, SizeT size)
{
    struct pmem_st tmp = {0};
    tmp.size = size;
    tmp.addr = addr;
    return VG_(OSetGen_Contains)(pmem.pmem_mappings, &tmp);
}

/**
* \brief State to string change for information purposes.
*/
static const char *
store_state_to_string(enum store_state state)
{
    switch (state) {
        case STST_CLEAN:
            return "CLEAN";
        case STST_DIRTY:
            return "DIRTY";
        case STST_FLUSHED:
            return "FLUSHED";
        default:
            return NULL;
    }
}

/**
 * \brief Prints registered redundant flushes.
 *
 * \details Flushing regions of memory which have already been flushed, but not
 * committed to memory, is a possible performance issue. This is not a data
 * consistency related problem.
 */
static void
print_redundant_flushes(void)
{
    VG_(umsg)("\nNumber of redundantly flushed stores: %lu\n",
            pmem.redundant_flushes_reg);
    VG_(umsg)("Stores flushed multiple times:\n");
    struct pmem_st *tmp;
    Int i;
    for (i = 0; i < pmem.redundant_flushes_reg; ++i) {
        tmp = pmem.redundant_flushes[i];
        VG_(umsg)("[%d] ", i);
        VG_(pp_ExeContext)(tmp->context);
        VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\tstate: %s\n",
                tmp->addr, tmp->size, store_state_to_string(tmp->state));
    }
}

/**
 * \brief Prints registered superfluous flushes.
 *
 * \details Flushing clean (with no pending stores to flush) regions of memory
 * is most certainly an error in the algorithm. This is not a data consistency
 * related problem, but a performance issue.
 */
static void
print_superfluous_flushes(void)
{
    VG_(umsg)("\nNumber of unnecessary flushes: %lu\n",
            pmem.superfluous_flushes_reg);
    struct pmem_st *tmp;
    Int i;
    for (i = 0; i < pmem.superfluous_flushes_reg; ++i) {
        tmp = pmem.superfluous_flushes[i];
        VG_(umsg)("[%d] ", i);
        VG_(pp_ExeContext)(tmp->context);
        VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\n", tmp->addr, tmp->size);
    }
}

/**
 * \brief Prints registered multiple stores.
 *
 * \details Overwriting stores before they are made persistent suggests
 * an error in the algorithm. This could be both a data consistency and
 * performance issue.
 */
static void
print_multiple_stores(void)
{

    VG_(umsg)("\nNumber of overwritten stores: %lu\n",
            pmem.multiple_stores_reg);
    VG_(umsg)("Overwritten stores before they were made persistent:\n");
    struct pmem_st *tmp;
    Int i;
    for (i = 0; i < pmem.multiple_stores_reg; ++i) {
        tmp = pmem.multiple_stores[i];
        VG_(umsg)("[%d] ", i);
        VG_(pp_ExeContext)(tmp->context);
        VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\tstate: %s\n",
                tmp->addr, tmp->size, store_state_to_string(tmp->state));
    }
}

/**
 * \brief Prints registered store statistics.
 *
 * \details Print outstanding stores which were not made persistent during the
 * whole run of the application.
 */
static void
print_store_stats(void)
{
    VG_(umsg)("Number of stores not made persistent: %u\n", VG_(OSetGen_Size)
            (pmem.pmem_stores));

    if (VG_(OSetGen_Size)(pmem.pmem_stores) != 0) {
        VG_(OSetGen_ResetIter)(pmem.pmem_stores);
        struct pmem_st *tmp;
        UWord total = 0;
        Int i = 0;
        VG_(umsg)("Stores not made persistent properly:\n");
        while ((tmp = VG_(OSetGen_Next)(pmem.pmem_stores)) != NULL) {
            VG_(umsg)("[%d] ", i);
            VG_(pp_ExeContext)(tmp->context);
            VG_(umsg)("\tAddress: 0x%lx\tsize: %llu\tstate: %s\n",
                    tmp->addr, tmp->size, store_state_to_string(tmp->state));
            total += tmp->size;
            ++i;
        }
        VG_(umsg)("Total memory not made persistent: %lu\n", total);
    }
}

/**
* \brief Prints the error message for exceeding the maximum allowable
*        overwrites.
* \param[in] limit The limit to print.
*/
static void
print_max_poss_overwrites_error(UWord limit)
{
    VG_(umsg)("The number of overwritten stores exceeded %lu\n\n",
            limit);

    VG_(umsg)("This either means there is something fundamentally wrong with"
            " your program, or you are using your persistent memory as "
            "volatile memory.\n");
    VG_(message_flush)();

    print_multiple_stores();
}

/**
* \brief Prints the error message for exceeding the maximum allowable
*        number of superfluous flushes.
* \param[in] limit The limit to print.
*/
static void
print_superfluous_flush_error(UWord limit)
{
    VG_(umsg)("The number of superfluous flushes exceeded %lu\n\n",
            limit);

    VG_(umsg)("This means your program is constantly flushing regions of"
            " memory, where no stores were made. This is a performance"
            " issue.\n");
    VG_(message_flush)();

    print_superfluous_flushes();
}

/**
* \brief Prints the error message for exceeding the maximum allowable
*        number of redundant flushes.
* \param[in] limit The limit to print.
*/
static void
print_redundant_flush_error(UWord limit)
{
    VG_(umsg)("The number of redundant flushes exceeded %lu\n\n",
            limit);

    VG_(umsg)("This means your program is constantly flushing regions of"
            " memory, which have already been flushed. This is a performance"
            " issue.\n");
    VG_(message_flush)();

    print_redundant_flushes();
}

/**
 * \brief Prints registered store context.
 *
 * \details Print store context.
 */
static void
print_store_ip_desc(UInt n, Addr ip, void *uu_opaque)
{
   InlIPCursor *iipc = VG_(new_IIPC)(ip);

   VG_(emit)(";");

   do {
      const HChar *buf = VG_(describe_IP)(ip, iipc);

      if (VG_(clo_xml))
         VG_(printf_xml)("%s\n", buf);
      else
         VG_(emit)("%s", buf);

      // Increase n to show "at" for only one level.
      n++;
   } while (VG_(next_IIPC)(iipc));

   VG_(delete_IIPC)(iipc);
}

/**
 * \brief Prints stack trace.
 *
 * \details Print stack trace.
 */
static void
pp_store_trace(const struct pmem_st *store, UInt n_ips)
{
    n_ips = n_ips == 0 ? VG_(get_ExeContext_n_ips)(store->context) : n_ips;

    tl_assert( n_ips > 0 );

    if (VG_(clo_xml))
         VG_(printf_xml)("    <stack>\n");

    VG_(apply_StackTrace)(print_store_ip_desc, NULL,
         VG_(get_ExeContext_StackTrace(store->context)), n_ips);

    if (VG_(clo_xml))
         VG_(printf_xml)("    </stack>\n");
}

/**
 * \brief Check if a memcpy/memset is at the given instruction address.
 *
 * \param[in] ip The instruction address to check.
 * \return True if the function name has memcpy/memset in its name,
 *         False otherwise.
 */
static Bool
is_ip_memset_memcpy(Addr ip)
{
    InlIPCursor *iipc = VG_(new_IIPC)(ip);
    const HChar *buf = VG_(describe_IP)(ip,  iipc);
    Bool present = (VG_(strstr)(buf, "memcpy") != NULL);
    present |= (VG_(strstr)(buf, "memset") != NULL);
    VG_(delete_IIPC)(iipc);
    return present;
}

/**
 * \brief Compare two ExeContexts.
 * Checks if two ExeContext are equal not counting the possible first
 * memset/memcpy function in the callstack.
 *
 * \param[in] lhs The first ExeContext to compare.
 * \param[in] rhs The second ExeContext to compare.
 *
 * Return True if the ExeContexts are equal, not counting the first
 * memcpy/memset function, False otherwise.
 */
static Bool
cmp_exe_context(const ExeContext* lhs, const ExeContext* rhs)
{
    if (lhs == NULL || rhs == NULL)
        return False;

    if (lhs == rhs)
        return True;

    /* retrieve stacktraces */
    UInt n_ips1;
    UInt n_ips2;
    const Addr *ips1 = VG_(make_StackTrace_from_ExeContext)(lhs, &n_ips1);
    const Addr *ips2 = VG_(make_StackTrace_from_ExeContext)(rhs, &n_ips2);

    /* Must be at least one address in each trace. */
    tl_assert(n_ips1 >= 1 && n_ips2 >= 1);

    /* different stacktrace depth */
    if (n_ips1 != n_ips2)
        return False;

    /* omit memcpy/memset at the top of the callstack */
    Int i = 0;
    if ((ips1[0] == ips2[0])
            || (is_ip_memset_memcpy(ips1[0]) && is_ip_memset_memcpy(ips2[0])))
        ++i;
    /* compare instruction pointers */
    for (; i < n_ips1; i++)
        if (ips1[i] != ips2[i])
            return False;

    return True;
}

/**
 * \brief Checks if two stores are merge'able.
 * Does not check the adjacency of the stores. Checks only the context and state
 * of the store.
 *
 * \param[in] lhs The first store to check.
 * \param[in] rhs The second store to check.
 *
 * \return True if stores are merge'able, False otherwise.
 */
static Bool
is_store_mergeable(const struct pmem_st *lhs,
        const struct pmem_st *rhs)
{
    Bool state_eq = lhs->state == rhs->state;
    return state_eq && cmp_exe_context(lhs->context, rhs->context);
}

/**
 * \brief Merge two stores together.
 * Does not check whether the two stores can in fact be merged. The two stores
 * should be adjacent or overlapping for the merging to make sense.
 *
 * \param[in,out] to_merge the store with which the merge will happen.
 * \param[in] to_be_merged the store that will be merged.
 */
static inline void
merge_stores(struct pmem_st *to_merge,
        const struct pmem_st *to_be_merged)
{
    ULong max_addr = MAX(to_merge->addr + to_merge->size,
            to_be_merged->addr + to_be_merged->size);
    to_merge->addr = MIN(to_merge->addr, to_be_merged->addr);
    to_merge->size = max_addr - to_merge->addr;
}

typedef void (*split_clb)(struct pmem_st *store,  OSet *set, Bool preallocated);

/**
 * \brief Free store if it was preallocated.
 *
 * \param[in,out] store The store to be freed.
 * \param[in,out] set The set the store belongs to.
 * \param[in] preallocated True if the store is in the heap and not the stack.
 */
static void
free_clb(struct pmem_st *store,  OSet *set, Bool preallocated)
{
    if (preallocated)
        VG_(OSetGen_FreeNode)(set, store);
}

/**
 * \brief Issues a warning event with the given store as the offender.
 *
 * \param[in,out] store The store to be registered as a warning.
 * \param[in,out] set The set the store belongs to.
 * \param[in] preallocated True if the store is in the heap and not the stack.
 */
static void
add_mult_overwrite_warn(struct pmem_st *store,  OSet *set, Bool preallocated)
{
    if (!preallocated) {
        struct pmem_st *new = VG_(OSetGen_AllocNode)(set,
                (SizeT) sizeof(struct pmem_st));
        *new = *store;
        store = new;
    }

    add_warning_event(pmem.multiple_stores, &pmem.multiple_stores_reg,
            store, MAX_MULT_OVERWRITES, print_max_poss_overwrites_error);
}

/**
 * \brief Splits-adjusts the two given stores so that they do not overlap.
 *
 * The stores need to be from the same set and have to overlap.
 *
 * \param[in,out] old The old store that will be modified.
 * \param[in] new The new store that will not be modified.
 * \param[in,out] set The set both of the stores belong to.
 * \param[in,out] clb The callback to be called for the overlapping part of the
 *  old store.
 */
static void
split_stores(struct pmem_st *old, const struct pmem_st *new, OSet *set,
        split_clb clb)
{
    Addr new_max = new->addr + new->size;
    Addr old_max = old->addr + old->size;

    /* new store encapsulates old, it needs to be removed */
    if (old->addr >= new->addr && old_max <= new_max) {
        VG_(OSetGen_Remove)(set, old);
        clb(old, set, True);
        return;
    }

    struct pmem_st tmp;
    if (old->addr < new->addr) {
        /* the new store is within the old store */
        if (old_max > new_max) {
            struct pmem_st *after = VG_(OSetGen_AllocNode)(set,
                    (SizeT) sizeof(struct pmem_st));
            *after = *old;
            after->addr = new_max;
            after->size = old_max - new_max;
            after->value &= (1 << (after->size * 8 + 1)) - 1;
            /* adjust the size and value of the old entry */
            old->value >>= old_max - new->addr;
            old->size = new->addr - old->addr;
            /* insert the new store fragment */
            VG_(OSetGen_Insert)(set, after);
            /* clb the cut out fragment with the old ExeContext */
            tmp = *new;
            tmp.context = old->context;
            clb(&tmp, set, False);
        } else {
            /* old starts before new */

            /* callback for removed part */
            tmp = *old;
            tmp.addr = new->addr;
            tmp.size = old_max - new->addr;
            /* adjust leftover */
            clb(&tmp, set, False);
            old->value >>= old_max - new->addr;
            old->size = new->addr - old->addr;
        }
        return;
    }

    /* now old->addr >= new->addr */

    /* end of old is behind end of new */
    if (old_max > new_max) {
        /* callback for removed part */
        tmp = *old;
        tmp.size -= old_max - new_max;
        clb(&tmp, set, False);
        /* adjust leftover */
        old->addr = new_max;
        old->size = old_max - new_max;
        old->value &= (1 << (old->size * 8 + 1)) - 1;
        return;
    }

    /* you should never end up here */
    tl_assert(False);
}

/**
 * \brief Add and merges adjacent stores if possible.
 * Should not be used if track_multiple_stores is enabled.
 *
 * param[in,out] region the store to be added and merged with adjacent stores.
 */
static void
add_and_merge_store(struct pmem_st *region)
{
    struct pmem_st *old_entry;
    /* remove old overlapping entries */
    while ((old_entry = VG_(OSetGen_Lookup)(pmem.pmem_stores, region)) != NULL)
        split_stores(old_entry, region, pmem.pmem_stores, free_clb);

    /* check adjacent entries */
    struct pmem_st search_entry = *region;
    search_entry.addr -= 1;
    int i = 0;
    for (i = 0; i < 2; ++i, search_entry.addr += 2) {
        old_entry = VG_(OSetGen_Lookup)(pmem.pmem_stores, &search_entry);
        /* no adjacent entry */
        if (old_entry == NULL)
            continue;
        /* adjacent entry not merge'able */
        if (!is_store_mergeable(region, old_entry))
            continue;

        /* registering overlapping stores, glue them together */
        merge_stores(region, old_entry);
        old_entry = VG_(OSetGen_Remove)(pmem.pmem_stores, &search_entry);
        VG_(OSetGen_FreeNode)(pmem.pmem_stores, old_entry);
    }
    VG_(OSetGen_Insert)(pmem.pmem_stores, region);
}

/**
 * \brief Handle a new store checking for multiple overwrites.
 * This should be called when track_multiple_stores is enabled.
 *
 * \param[in,out] store the store to be handled.
 */
static void
handle_with_mult_stores(struct pmem_st *store)
{
    struct pmem_st *existing;
    /* remove any overlapping stores from the collection */
    while ((existing = VG_(OSetGen_Lookup)(pmem.pmem_stores, store)) !=
    NULL) {
        /* check store indifference */
        if ((store->block_num - existing->block_num) < pmem.store_sb_indiff
                && existing->addr == store->addr
                && existing->size == store->size
                && existing->value == store->value) {
            VG_(OSetGen_Remove)(pmem.pmem_stores, store);
            VG_(OSetGen_FreeNode)(pmem.pmem_stores, existing);
            continue;
        }
        split_stores(existing, store, pmem.pmem_stores,
                add_mult_overwrite_warn);
    }
    /* it is now safe to insert the new store */
    VG_(OSetGen_Insert)(pmem.pmem_stores, store);
}

/**
* \brief Trace the given store if it was to any of the registered persistent
*        memory regions.
* \param[in] addr The base address of the store.
* \param[in] size The size of the store.
* \param[in] value The value of the store.
*/
static VG_REGPARM(3) void
trace_pmem_store(Addr addr, SizeT size, UWord value)
{
    if (LIKELY(!is_pmem_access(addr, size)))
        return;

    struct pmem_st *store = VG_(OSetGen_AllocNode)(pmem.pmem_stores,
            (SizeT) sizeof (struct pmem_st));
    store->addr = addr;
    store->size = size;
    store->state = STST_DIRTY;
    store->block_num = sblocks;
    store->value = value;
    store->context = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);

    /* log the store, regardless if it is a double store */
    if (pmem.log_stores) {
        VG_(emit)("|STORE;0x%lx;0x%lx;0x%lx", addr, value, size);
        if (pmem.store_traces)
            pp_store_trace(store, pmem.store_traces_depth);
    }
    if (pmem.track_multiple_stores)
        handle_with_mult_stores(store);
    else
        add_and_merge_store(store);

    /* do transaction check */
    handle_tx_store(store);
}

/**
* \brief Register the entry of a new SB.
*
* Useful when handling implementation independent multiple writes under
* the same address.
*/
static void
add_one_SB_entered(void)
{
    ++sblocks;
}

/**
* \brief Make a new atomic expression from e.
*
* A very handy function to have for creating binops, triops and widens.
* \param[in,out] sb The IR superblock to which the new expression will be added.
* \param[in] ty The IRType of the expression.
* \param[in] e The new expression to make.
* \return The Rd_tmp of the new expression.
*/
static IRAtom *
make_expr(IRSB *sb, IRType ty, IRExpr *e)
{
    IRTemp t;
    IRType tyE = typeOfIRExpr(sb->tyenv, e);

    tl_assert(tyE == ty); /* so 'ty' is redundant (!) */

    t = newIRTemp(sb->tyenv, tyE);
    addStmtToIRSB(sb, IRStmt_WrTmp(t,e));

    return mkexpr(t);
}

/**
* \brief Check if the expression needs to be widened.
* \param[in] sb The IR superblock to which the expression belongs.
* \param[in] e The checked expression.
* \return True if needs to be widened, false otherwise.
*/
static Bool
tmp_needs_widen(IRType type)
{
    switch (type) {
        case Ity_I1:
        case Ity_I8:
        case Ity_I16:
        case Ity_I32:
            return True;

        default:
            return False;
    }
}

/**
* \brief Check if the const expression needs to be widened.
* \param[in] e The checked expression.
* \return True if needs to be widened, false otherwise.
*/
static Bool
const_needs_widen(IRAtom *e)
{
    /* make sure this is a const */
    tl_assert(e->tag == Iex_Const);

    switch (e->Iex.Const.con->tag) {
        case Ico_U1:
        case Ico_U8:
        case Ico_U16:
        case Ico_U32:
        case Ico_U64:
            return True;

        default:
            return False;
    }
}

/**
* \brief Widen a given const expression to a word sized expression.
* \param[in] e The expression being widened.
* \return The widened const expression.
*/
static IRAtom *
widen_const(IRAtom *e)
{
    /* make sure this is a const */
    tl_assert(e->tag == Iex_Const);

    switch (e->Iex.Const.con->tag) {
        case Ico_U1:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U1);

        case Ico_U8:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U8);

        case Ico_U16:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U16);

        case Ico_U32:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U32);

        case Ico_U64:
            return mkIRExpr_HWord((UInt)e->Iex.Const.con->Ico.U64);

        default:
            tl_assert(False); /* cannot happen */
    }
}

/**
* \brief A generic widening function.
* \param[in] sb The IR superblock to which the expression belongs.
* \param[in] e The expression being widened.
* \return The widening operation.
*/
static IROp
widen_operation(IRSB *sb, IRAtom *e)
{
    switch (typeOfIRExpr(sb->tyenv, e)) {
        case Ity_I1:
            return Iop_1Uto64;

        case Ity_I8:
            return Iop_8Uto64;

        case Ity_I16:
            return Iop_16Uto64;

        case Ity_I32:
            return Iop_32Uto64;

        default:
            tl_assert(False); /* cannot happen */
    }
}

/**
* \brief Handle wide sse operations.
* \param[in,out] sb The IR superblock to which add expressions.
* \param[in] end The endianess.
* \param[in] addr The expression with the address of the operation.
* \param[in] data The expression with the value of the operation.
* \param[in] guard The guard expression.
* \param[in] size The size of the operation.
*/
static void
handle_wide_expr(IRSB *sb, IREndness end, IRAtom *addr, IRAtom *data,
        IRAtom *guard, SizeT size)
{
    IROp mkAdd;
    IRType ty, tyAddr;
    void *helper = trace_pmem_store;
    const HChar *hname = "trace_pmem_store";

    ty = typeOfIRExpr(sb->tyenv, data);

    tyAddr = typeOfIRExpr(sb->tyenv, addr);
    mkAdd = tyAddr==Ity_I32 ? Iop_Add32 : Iop_Add64;
    tl_assert( tyAddr == Ity_I32 || tyAddr == Ity_I64 );
    tl_assert( end == Iend_LE || end == Iend_BE );

    Int i;
    Int parts = 0;
    /* These are the offsets of the parts in memory. */
    UInt offs[4];

    /* Various bits for constructing the 4/2 lane helper calls */
    IROp ops[4];
    IRDirty *dis[4];
    IRAtom *addrs[4];
    IRAtom *datas[4];
    IRAtom *eBiass[4];

    if (ty == Ity_V256) {
         /* V256-bit case -- phrased in terms of 64 bit units (Qs), with
           Q3 being the most significant lane. */

        ops[0] =Iop_V256to64_0;
        ops[1] =Iop_V256to64_1;
        ops[2] =Iop_V256to64_2;
        ops[3] = Iop_V256to64_3;

        if (end == Iend_LE) {
            offs[0] = 0; offs[1] = 8; offs[2] = 16; offs[3] = 24;
        } else {
            offs[3] = 0; offs[2] = 8; offs[1] = 16; offs[0] = 24;
        }

        parts = 4;
    } else if (ty == Ity_V128) {

        /* V128-bit case
           See comment in next clause re 64-bit regparms also, need to be
           careful about endianness */
        ops[0] =Iop_V128to64;
        ops[1] =Iop_V128HIto64;

        if (end == Iend_LE) {
            offs[0] = 0; offs[1] = 8;
        } else {
            offs[0] = 8; offs[1] = 0;
        }

        parts = 2;
    }

    for(i = 0; i < parts; ++i) {
        eBiass[i] = tyAddr == Ity_I32 ? mkU32(offs[i]) : mkU64(offs[i]);
        addrs[i] = make_expr(sb, tyAddr, binop(mkAdd, addr, eBiass[i]));
        datas[i] = make_expr(sb, Ity_I64, unop(ops[i], data));
        dis[i] = unsafeIRDirty_0_N(3/*regparms*/, hname,
                VG_(fnptr_to_fnentry)(helper), mkIRExprVec_3(addrs[i],
                mkIRExpr_HWord(size / parts), datas[i]));
        if (guard)
            dis[i]->guard = guard;

        addStmtToIRSB(sb, IRStmt_Dirty(dis[i]));
    }
}

/**
* \brief Add a guarded write event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] dsize The size of the operation.
* \param[in] guard The guard expression.
* \param[in] value The expression with the value of the operation.
*/
static void
add_event_dw_guarded(IRSB *sb, IRAtom *daddr, Int dsize, IRAtom *guard,
        IRAtom *value)
{
    tl_assert(isIRAtom(daddr));
    tl_assert(isIRAtom(value));
    tl_assert(dsize >= 1 && dsize <= MAX_DSIZE);

    const HChar *helperName = "trace_pmem_store";
    void *helperAddr = trace_pmem_store;
    IRExpr **argv;
    IRDirty *di;
    IRType type = typeOfIRExpr(sb->tyenv, value);

    if (value->tag == Iex_RdTmp && type == Ity_I64) {
        /* handle the normal case */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                value);
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_RdTmp && type == Ity_F64) {
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                make_expr(sb, Ity_I64, unop(Iop_ReinterpF64asI64,
                        value)));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_RdTmp && tmp_needs_widen(type)) {
        /* the operation needs to be widened */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                make_expr(sb, Ity_I64, unop(widen_operation(sb, value),
                        value)));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (value->tag == Iex_Const && const_needs_widen(value)) {
        /* the operation needs to be widened */
        argv = mkIRExprVec_3(daddr, mkIRExpr_HWord(dsize),
                widen_const(value));
        di = unsafeIRDirty_0_N(/*regparms*/3, helperName,
                VG_(fnptr_to_fnentry)(helperAddr), argv);
        if (guard) {
            di->guard = guard;
        }
        addStmtToIRSB(sb, IRStmt_Dirty(di));
    } else if (type == Ity_V128 || type == Ity_V256 ) {
        handle_wide_expr(sb, Iend_LE, daddr, value, guard, dsize);
    } else {
        VG_(umsg)("Unable to trace store - unsupported type of store 0x%x 0x%x\n",
                  value->tag, type);
    }
}

/**
* \brief Add an ordinary write event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
* \param[in] dsize The size of the operation.
* \param[in] value The expression with the value of the operation.
*/
static void
add_event_dw(IRSB *sb, IRAtom *daddr, Int dsize, IRAtom *value)
{
    add_event_dw_guarded(sb, daddr, dsize, NULL, value);
}

/**
* \brief Register a fence.
*
* Marks flushed stores as persistent.
* The proper state transitions are DIRTY->FLUSHED->CLEAN.
* The CLEAN state is not registered, the store is removed from the set.
*/
static void
do_fence(void)
{
    if (pmem.log_stores)
        VG_(emit)("|FENCE");

    /* go through the stores and remove all flushed */
    VG_(OSetGen_ResetIter)(pmem.pmem_stores);
    struct pmem_st *being_fenced = NULL;
    while ((being_fenced = VG_(OSetGen_Next)(pmem.pmem_stores)) != NULL) {
        if (being_fenced->state == STST_FLUSHED) {
            /* remove it from the oset */
            struct pmem_st temp = *being_fenced;
            VG_(OSetGen_Remove)(pmem.pmem_stores, being_fenced);
            VG_(OSetGen_FreeNode)(pmem.pmem_stores, being_fenced);
            /* reset the iterator (remove invalidated store) */
            VG_(OSetGen_ResetIterAt)(pmem.pmem_stores, &temp);
        }
    }
}

/**
* \brief Register a flush.
*
* Marks dirty stores as flushed. The proper state transitions are
* DIRTY->FLUSHED->FENCED->COMMITTED->CLEAN. The CLEAN state is not registered,
* the store is removed from the set.
*
* \param[in] base The base address of the flush.
* \param[in] size The size of the flush in bytes.
*/
static void
do_flush(UWord base, UWord size)
{
    struct pmem_st flush_info = {0};

    if (LIKELY(pmem.force_flush_align == False)) {
        flush_info.addr = base;
        flush_info.size = size;
    } else {
        /* align flushed memory */
        flush_info.addr = base & ~(pmem.flush_align_size - 1);
        flush_info.size = roundup(size, pmem.flush_align_size);
    }

    if (pmem.log_stores)
        VG_(emit)("|FLUSH;0x%lx;0x%llx", flush_info.addr, flush_info.size);

    /* unfortunately lookup doesn't work here, the oset is an avl tree */

    Bool valid_flush = False;
    /* reset the iterator */
    VG_(OSetGen_ResetIter)(pmem.pmem_stores);
    Addr flush_max = flush_info.addr + flush_info.size;
    struct pmem_st *being_flushed;
    while ((being_flushed = VG_(OSetGen_Next)(pmem.pmem_stores)) != NULL){

       /* not an interesting entry, flush doesn't matter */
       if (cmp_pmem_st(&flush_info, being_flushed) != 0) {
           continue;
       }

       valid_flush = True;
       /* check for multiple flushes of stores */
       if (being_flushed->state != STST_DIRTY) {
           if (pmem.check_flush) {
               /* multiple flush of the same store - probably an issue */
               struct pmem_st *wrong_flush = VG_(malloc)("pmc.main.cpci.3",
                       sizeof(struct pmem_st));
               *wrong_flush = *being_flushed;
               add_warning_event(pmem.redundant_flushes,
                                 &pmem.redundant_flushes_reg,
                                 wrong_flush, MAX_FLUSH_ERROR_EVENTS,
                                 print_redundant_flush_error);
           }
           continue;
       }

       being_flushed->state = STST_FLUSHED;

       /* store starts before base flush address */
       if (being_flushed->addr < flush_info.addr) {
            /* split and reinsert */
            struct pmem_st *split = VG_(OSetGen_AllocNode)(pmem.pmem_stores,
                    (SizeT)sizeof (struct pmem_st));
            *split = *being_flushed;
            split->size = flush_info.addr - being_flushed->addr;
            split->state = STST_DIRTY;

            /* adjust original */
            VG_(OSetGen_Remove)(pmem.pmem_stores, being_flushed);
            being_flushed->addr = flush_info.addr;
            being_flushed->size -= split->size;
            VG_(OSetGen_Insert)(pmem.pmem_stores, split);
            VG_(OSetGen_Insert)(pmem.pmem_stores, being_flushed);
            /* reset iter */
            VG_(OSetGen_ResetIterAt)(pmem.pmem_stores, being_flushed);
       }

       /* end of store is behind max flush */
       if (being_flushed->addr + being_flushed->size > flush_max) {
            /* split and reinsert */
            struct pmem_st *split = VG_(OSetGen_AllocNode)(pmem.pmem_stores,
                    (SizeT)sizeof (struct pmem_st));
            *split = *being_flushed;
            split->addr = flush_max;
            split->size = being_flushed->addr + being_flushed->size - flush_max;
            split->state = STST_DIRTY;

            /* adjust original */
            VG_(OSetGen_Remove)(pmem.pmem_stores, being_flushed);
            being_flushed->size -= split->size;
            VG_(OSetGen_Insert)(pmem.pmem_stores, split);
            VG_(OSetGen_Insert)(pmem.pmem_stores, being_flushed);
            /* reset iter */
            VG_(OSetGen_ResetIterAt)(pmem.pmem_stores, split);
       }
    }

    if (!valid_flush && pmem.check_flush) {
        /* unnecessary flush event - probably an issue */
        struct pmem_st *wrong_flush = VG_(malloc)("pmc.main.cpci.6",
                       sizeof(struct pmem_st));
        *wrong_flush = flush_info;
        wrong_flush->context = VG_(record_ExeContext)(VG_(get_running_tid)(),
                                                            0);
        add_warning_event(pmem.superfluous_flushes,
                          &pmem.superfluous_flushes_reg,
                          wrong_flush, MAX_FLUSH_ERROR_EVENTS,
                          print_superfluous_flush_error);
    }
}

/**
 * \brief Register runtime flush.
 * \param addr[in] addr The expression with the address of the operation.
 */
static VG_REGPARM(1) void
trace_pmem_flush(Addr addr)
{
    /* use native cache size for flush */
    do_flush(addr, pmem.flush_align_size);
}

/**
* \brief Add an ordinary flush event.
* \param[in,out] sb The IR superblock to which the expression belongs.
* \param[in] daddr The expression with the address of the operation.
*/
static void
add_flush_event(IRSB *sb, IRAtom *daddr)
{
    tl_assert(isIRAtom(daddr));

    const HChar *helperName = "trace_pmem_flush";
    void *helperAddr = trace_pmem_flush;
    IRExpr **argv;
    IRDirty *di;

    argv = mkIRExprVec_1(daddr);
    di = unsafeIRDirty_0_N(/*regparms*/1, helperName,
            VG_(fnptr_to_fnentry)(helperAddr), argv);

    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

/**
* \brief Add an event without any parameters.
* \param[in,out] sb The IR superblock to which the expression belongs.
*/
static void
add_simple_event(IRSB *sb, void *helperAddr, const HChar *helperName)
{
    IRDirty *di;

    di = unsafeIRDirty_0_N(/*regparms*/0, helperName,
            VG_(fnptr_to_fnentry)(helperAddr), mkIRExprVec_0());

    addStmtToIRSB(sb, IRStmt_Dirty(di));
}

/**
* \brief Read the cache line size - linux specific.
* \return The size of the cache line.
*/
static Int
read_cache_line_size(void)
{
    /* the assumed cache line size */
    Int ret_val = 64;

    int fp;
    if ((fp = VG_(fd_open)("/proc/cpuinfo",O_RDONLY, 0)) < 0) {
        return ret_val;
    }

    int proc_read_size = 2048;
    char read_buffer[proc_read_size];

    while (VG_(read)(fp, read_buffer, proc_read_size - 1) > 0) {
        static const char clflush[] = "clflush size\t: ";
        read_buffer[proc_read_size] = 0;

        char *cache_str = NULL;
        if ((cache_str = VG_(strstr)(read_buffer, clflush)) != NULL) {
            /* move to cache line size */
            cache_str += sizeof (clflush) - 1;
            ret_val = VG_(strtoll10)(cache_str, NULL) ? : 64;
            break;
        }
    }

    VG_(close)(fp);
    return ret_val;
}

/**
* \brief Try to register a file mapping.
* \param[in] fd The file descriptor to be registered.
* \param[in] addr The address at which this file will be mapped.
* \param[in] size The size of the registered file mapping.
* \param[in] offset Offset within the mapped file.
* \return Returns 1 on success, 0 otherwise.
*/
static UInt
register_new_file(Int fd, UWord base, UWord size, UWord offset)
{
    char fd_path[64];
    VG_(sprintf(fd_path, "/proc/self/fd/%d", fd));
    UInt retval = 0;

    char *file_name = VG_(malloc)("pmc.main.nfcc", MAX_PATH_SIZE);
    int read_length = VG_(readlink)(fd_path, file_name, MAX_PATH_SIZE - 1);
    if (read_length <= 0) {
        retval = 1;
        goto out;
    }

    file_name[read_length] = 0;

    /* logging_on shall have no effect on this */
    if (pmem.log_stores)
        VG_(emit)("|REGISTER_FILE;%s;0x%lx;0x%lx;0x%lx", file_name, base,
                size, offset);
out:
    VG_(free)(file_name);
    return retval;
}

/**
 * \brief Print the summary of whole analysis.
 */
static void
print_general_summary(void)
{
	UWord all_errors = pmem.redundant_flushes_reg +
		pmem.superfluous_flushes_reg +
		pmem.multiple_stores_reg +
		VG_(OSetGen_Size)(pmem.pmem_stores) +
		get_tx_all_err();
	VG_(umsg)("ERROR SUMMARY: %lu errors\n", all_errors);
}

/**
* \brief Print tool statistics.
*/
static void
print_pmem_stats(Bool append_blank_line)
{
    print_store_stats();

    print_tx_summary();

    if (pmem.redundant_flushes_reg)
        print_redundant_flushes();

    if (pmem.superfluous_flushes_reg)
        print_superfluous_flushes();

    if (pmem.track_multiple_stores && (pmem.multiple_stores_reg > 0))
        print_multiple_stores();

    if (pmem.error_summary) {
        print_general_summary();
    }

    if (append_blank_line)
        VG_(umsg)("\n");
}

/**
* \brief Print the registered persistent memory mappings
*/
static void
print_persistent_mappings(void)
{
    VG_(OSetGen_ResetIter)(pmem.pmem_mappings);
    struct pmem_st *mapping;
    Int i = 0;
    while ((mapping = VG_(OSetGen_Next)(pmem.pmem_mappings)) != NULL) {
        VG_(umsg)("[%d] Mapping base: 0x%lx\tsize: %llu\n", i++, mapping->addr,
                mapping->size);
    }
}

/**
* \brief Print gdb monitor commands.
*/
static void
print_monitor_help(void)
{
    VG_(gdb_printf)
            ("\n"
            "pmemcheck gdb monitor commands:\n"
            "  print_stats\n"
            "        prints the summary\n"
            "  print_pmem_regions \n"
            "        prints the registered persistent memory regions\n"
            "\n");
}

/**
* \brief Gdb monitor command handler.
* \param[in] tid Id of the calling thread.
* \param[in] req Command request string.
* \return True if command is recognized, true otherwise.
*/
static Bool handle_gdb_monitor_command(ThreadId tid, HChar *req)
{
    HChar* wcmd;
    HChar s[VG_(strlen(req)) + 1]; /* copy for strtok_r */
    HChar *ssaveptr;

    VG_(strcpy) (s, req);

    wcmd = VG_(strtok_r) (s, " ", &ssaveptr);
    switch (VG_(keyword_id)
            ("help print_stats print_pmem_regions",
                    wcmd, kwd_report_duplicated_matches)) {
        case -2: /* multiple matches */
            return True;

        case -1: /* not found */
            return False;

        case  0: /* help */
            print_monitor_help();
            return True;

        case  1:  /* print_stats */
            print_pmem_stats(True);
            return True;

        case  2: {/* print_pmem_regions */
            VG_(gdb_printf)("Registered persistent memory regions:\n");
            struct pmem_st *tmp;
            while ((tmp = VG_(OSetGen_Next)(pmem.pmem_stores)) != NULL) {
                VG_(gdb_printf)("\tAddress: 0x%lx \tsize: %llu\n",
                        tmp->addr, tmp->size);
            }
            return True;
        }

        default:
            tl_assert(0);
            return False;
    }
}

/**
* \brief The main instrumentation function - the heart of the tool.
*
* The translated client code is passed into this function, where appropriate
* instrumentation is made. All uninteresting operations are copied straight
* to the returned IRSB. The only interesting operations are stores, which are
* instrumented for further analysis.
* \param[in] closure Valgrind closure - unused.
* \param[in] bb The IR superblock provided by the core.
* \param[in] layout Vex quest layout - unused.
* \param[in] vge Vex quest extents - unused.
* \param[in] archinfo_host Vex architecture info - unused.
* \param[in] gWordTy Guest word type.
* \param[in] hWordTy Host word type.
* \return The modified IR superblock.
*/
static IRSB*
pmc_instrument(VgCallbackClosure *closure,
        IRSB *bb,
        const VexGuestLayout *layout,
        const VexGuestExtents *vge,
        const VexArchInfo *archinfo_host,
        IRType gWordTy, IRType hWordTy)
{
    Int i;
    IRSB *sbOut;
    IRTypeEnv *tyenv = bb->tyenv;

    if (gWordTy != hWordTy) {
        /* We don't currently support this case. */
        VG_(tool_panic)("host/guest word size mismatch");
    }

    /* Set up SB */
    sbOut = deepCopyIRSBExceptStmts(bb);

    /* Copy verbatim any IR preamble preceding the first IMark */
    i = 0;
    while (i < bb->stmts_used && bb->stmts[i]->tag != Ist_IMark) {
        addStmtToIRSB(sbOut, bb->stmts[i]);
        ++i;
    }

    /* Count this superblock. */
    IRDirty *di = unsafeIRDirty_0_N( 0, "add_one_SB_entered",
            VG_(fnptr_to_fnentry)(&add_one_SB_entered), mkIRExprVec_0());
    addStmtToIRSB(sbOut, IRStmt_Dirty(di));

    for (/*use current i*/; i < bb->stmts_used; i++) {
        IRStmt *st = bb->stmts[i];
        if (!st || st->tag == Ist_NoOp)
            continue;

        switch (st->tag) {
            case Ist_IMark:
            case Ist_AbiHint:
            case Ist_Put:
            case Ist_PutI:
            case Ist_LoadG:
            case Ist_WrTmp:
            case Ist_Exit:
            case Ist_Dirty:
                /* for now we are not interested in any of the above */
                addStmtToIRSB(sbOut, st);
                break;

            case Ist_Flush: {
                addStmtToIRSB(sbOut, st);
                if (LIKELY(pmem.automatic_isa_rec)) {
                    IRExpr *addr = st->Ist.Flush.addr;
                    IRType type = typeOfIRExpr(tyenv, addr);
                    tl_assert(type != Ity_INVALID);
                    add_flush_event(sbOut, st->Ist.Flush.addr);

		    /* treat clflush as strong memory ordered */
		    if (st->Ist.Flush.fk == Ifk_flush)
                       if (!pmem.weak_clflush)
                          add_simple_event(sbOut, do_fence, "do_fence");
                }
                break;
            }

            case Ist_MBE: {
                addStmtToIRSB(sbOut, st);
                if (LIKELY(pmem.automatic_isa_rec)) {
                    switch (st->Ist.MBE.event) {
                        case Imbe_Fence:
                        case Imbe_SFence:
                            add_simple_event(sbOut, do_fence, "do_fence");
                            break;
                        default:
                            break;
                    }
                }
                break;
            }

            case Ist_Store: {
                addStmtToIRSB(sbOut, st);
                IRExpr *data = st->Ist.Store.data;
                IRType type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                add_event_dw(sbOut, st->Ist.Store.addr, sizeofIRType(type),
                        data);
                break;
            }

            case Ist_StoreG: {
                addStmtToIRSB(sbOut, st);
                IRStoreG *sg = st->Ist.StoreG.details;
                IRExpr *data = sg->data;
                IRType type = typeOfIRExpr(tyenv, data);
                tl_assert(type != Ity_INVALID);
                add_event_dw_guarded(sbOut, sg->addr, sizeofIRType(type),
                        sg->guard, data);
                break;
            }

            case Ist_CAS: {
                Int dataSize;
                IRType dataTy;
                IRCAS *cas = st->Ist.CAS.details;
                tl_assert(cas->addr != NULL);
                tl_assert(cas->dataLo != NULL);
                dataTy = typeOfIRExpr(tyenv, cas->dataLo);
                dataSize = sizeofIRType(dataTy);
                /* has to be done before registering the guard */
                addStmtToIRSB(sbOut, st);
                /* the guard statement on the CAS */
                IROp opCasCmpEQ;
                IROp opOr;
                IROp opXor;
                IRAtom *zero = NULL;
                IRType loType = typeOfIRExpr(tyenv, cas->expdLo);
                switch (loType) {
                    case Ity_I8:
                        opCasCmpEQ = Iop_CasCmpEQ8;
                        opOr = Iop_Or8;
                        opXor = Iop_Xor8;
                        break;
                    case Ity_I16:
                        opCasCmpEQ = Iop_CasCmpEQ16;
                        opOr = Iop_Or16;
                        opXor = Iop_Xor16;
                        break;
                    case Ity_I32:
                        opCasCmpEQ = Iop_CasCmpEQ32;
                        opOr = Iop_Or32;
                        opXor = Iop_Xor32;
                        break;
                    case Ity_I64:
                        opCasCmpEQ = Iop_CasCmpEQ64;
                        opOr = Iop_Or64;
                        opXor = Iop_Xor64;
                        break;
                    default:
                        tl_assert(0);
                }

                if (cas->dataHi != NULL) {
                    IRAtom *xHi = NULL;
                    IRAtom *xLo = NULL;
                    IRAtom *xHL = NULL;
                    xHi = make_expr(sbOut, loType, binop(opXor, cas->expdHi,
                            mkexpr(cas->oldHi)));
                    xLo = make_expr(sbOut, loType, binop(opXor, cas->expdLo,
                            mkexpr(cas->oldLo)));
                    xHL = make_expr(sbOut, loType, binop(opOr, xHi, xLo));
                    IRAtom *guard = make_expr(sbOut, Ity_I1,
                            binop(opCasCmpEQ, xHL, zero));

                    add_event_dw_guarded(sbOut, cas->addr, dataSize, guard,
                            cas->dataLo);
                    add_event_dw_guarded(sbOut, cas->addr + dataSize,
                            dataSize, guard, cas->dataHi);
                } else {
                    IRAtom *guard = make_expr(sbOut, Ity_I1, binop(opCasCmpEQ,
                            cas->expdLo, mkexpr(cas->oldLo)));

                    add_event_dw_guarded(sbOut, cas->addr, dataSize, guard,
                            cas->dataLo);
                }
                break;
            }

            case Ist_LLSC: {
                addStmtToIRSB(sbOut, st);
                IRType dataTy;
                if (st->Ist.LLSC.storedata != NULL) {
                    dataTy = typeOfIRExpr(tyenv, st->Ist.LLSC.storedata);
                    add_event_dw(sbOut, st->Ist.LLSC.addr, sizeofIRType
                            (dataTy), st->Ist.LLSC.storedata);
                }
                break;
            }

            default:
                ppIRStmt(st);
                tl_assert(0);
        }
    }

    return sbOut;
}

/**
* \brief Client mechanism handler.
* \param[in] tid Id of the calling thread.
* \param[in] arg Arguments passed in the request, 0-th is the request name.
* \param[in,out] ret Return value passed to the client.
* \return True if the request has been handled, false otherwise.
*/
static Bool
pmc_handle_client_request(ThreadId tid, UWord *arg, UWord *ret )
{
    if (!VG_IS_TOOL_USERREQ('P', 'C', arg[0])
            && VG_USERREQ__PMC_REGISTER_PMEM_MAPPING != arg[0]
            && VG_USERREQ__PMC_REGISTER_PMEM_FILE != arg[0]
            && VG_USERREQ__PMC_REMOVE_PMEM_MAPPING != arg[0]
            && VG_USERREQ__PMC_CHECK_IS_PMEM_MAPPING != arg[0]
            && VG_USERREQ__PMC_DO_FLUSH != arg[0]
            && VG_USERREQ__PMC_DO_FENCE != arg[0]
            && VG_USERREQ__PMC_WRITE_STATS != arg[0]
            && VG_USERREQ__GDB_MONITOR_COMMAND != arg[0]
            && VG_USERREQ__PMC_PRINT_PMEM_MAPPINGS != arg[0]
            && VG_USERREQ__PMC_EMIT_LOG != arg[0]
            && VG_USERREQ__PMC_START_TX != arg[0]
            && VG_USERREQ__PMC_START_TX_N != arg[0]
            && VG_USERREQ__PMC_END_TX != arg[0]
            && VG_USERREQ__PMC_END_TX_N != arg[0]
            && VG_USERREQ__PMC_ADD_TO_TX != arg[0]
            && VG_USERREQ__PMC_ADD_TO_TX_N != arg[0]
            && VG_USERREQ__PMC_REMOVE_FROM_TX != arg[0]
            && VG_USERREQ__PMC_REMOVE_FROM_TX_N != arg[0]
            && VG_USERREQ__PMC_ADD_THREAD_TO_TX_N != arg[0]
            && VG_USERREQ__PMC_REMOVE_THREAD_FROM_TX_N != arg[0]
            && VG_USERREQ__PMC_ADD_TO_GLOBAL_TX_IGNORE != arg[0]
            && VG_USERREQ__PMC_RESERVED1 != arg[0]
            && VG_USERREQ__PMC_RESERVED2 != arg[0]
            && VG_USERREQ__PMC_RESERVED3 != arg[0]
            && VG_USERREQ__PMC_RESERVED4 != arg[0]
            && VG_USERREQ__PMC_RESERVED5 != arg[0]
            && VG_USERREQ__PMC_RESERVED6 != arg[0]
            && VG_USERREQ__PMC_RESERVED7 != arg[0]
            && VG_USERREQ__PMC_RESERVED8 != arg[0]
            && VG_USERREQ__PMC_RESERVED9 != arg[0]
            && VG_USERREQ__PMC_RESERVED10 != arg[0]
            )
        return False;

    switch (arg[0]) {
        case VG_USERREQ__PMC_REGISTER_PMEM_MAPPING: {
            struct pmem_st temp_info = {0};
            temp_info.addr = arg[1];
            temp_info.size = arg[2];

            add_region(&temp_info, pmem.pmem_mappings);
            break;
        }

        case VG_USERREQ__PMC_REMOVE_PMEM_MAPPING: {
            struct pmem_st temp_info = {0};
            temp_info.addr = arg[1];
            temp_info.size = arg[2];

            remove_region(&temp_info, pmem.pmem_mappings);
            break;
        }

        case VG_USERREQ__PMC_REGISTER_PMEM_FILE: {
            *ret = 1;
            Int fd = (Int)arg[1];
            if (fd >= 0)
                *ret = register_new_file(fd, arg[2], arg[3], arg[4]);
            break;
        }

        case VG_USERREQ__PMC_CHECK_IS_PMEM_MAPPING: {
            struct pmem_st temp_info = {0};
            temp_info.addr = arg[1];
            temp_info.size = arg[2];

            *ret = is_in_mapping_set(&temp_info, pmem.pmem_mappings);
            break;
        }

        case VG_USERREQ__PMC_PRINT_PMEM_MAPPINGS: {
            print_persistent_mappings();
            break;
        }

        case VG_USERREQ__PMC_DO_FLUSH: {
            do_flush(arg[1], arg[2]);
            break;
        }

        case VG_USERREQ__PMC_DO_FENCE: {
            do_fence();
            break;
        }

        case VG_USERREQ__PMC_WRITE_STATS: {
            print_pmem_stats(True);
            break;
        }

        case VG_USERREQ__GDB_MONITOR_COMMAND: {
            Bool handled = handle_gdb_monitor_command (tid, (HChar*)arg[1]);
            if (handled)
                *ret = 0;
            else
                *ret = 1;
            return handled;
        }

        case VG_USERREQ__PMC_EMIT_LOG: {
            if (pmem.log_stores) {
                VG_(emit)("|%s", (char *)arg[1]);
            }
            break;
        }

        case VG_USERREQ__PMC_SET_CLEAN: {
            struct pmem_st temp_info = {0};
            temp_info.addr = arg[1];
            temp_info.size = arg[2];

            remove_region(&temp_info, pmem.pmem_stores);
            break;
        }

        /* transaction support */
        case VG_USERREQ__PMC_START_TX: {
            register_new_tx(VG_(get_running_tid)());
            break;
        }

        case VG_USERREQ__PMC_START_TX_N: {
            register_new_tx(arg[1]);
            break;
        }

        case VG_USERREQ__PMC_END_TX: {
            *ret = remove_tx(VG_(get_running_tid)());
            break;
        }

        case VG_USERREQ__PMC_END_TX_N: {
            *ret = remove_tx(arg[1]);
            break;
        }

        case VG_USERREQ__PMC_ADD_TO_TX: {
            *ret = add_obj_to_tx(VG_(get_running_tid)(), arg[1], arg[2]);
            break;
        }

        case VG_USERREQ__PMC_ADD_TO_TX_N: {
            *ret = add_obj_to_tx(arg[1], arg[2], arg[3]);
            break;
        }

        case VG_USERREQ__PMC_REMOVE_FROM_TX: {
            *ret = remove_obj_from_tx(VG_(get_running_tid)(), arg[1], arg[2]);
            break;
        }

        case VG_USERREQ__PMC_REMOVE_FROM_TX_N: {
            *ret = remove_obj_from_tx(arg[1], arg[2], arg[3]);
            break;
        }

        case VG_USERREQ__PMC_ADD_THREAD_TO_TX_N: {
            *ret = remove_obj_from_tx(arg[1], arg[2], arg[3]);
            break;
        }

        case VG_USERREQ__PMC_REMOVE_THREAD_FROM_TX_N: {
            *ret = remove_obj_from_tx(arg[1], arg[2], arg[3]);
            break;
        }

        case VG_USERREQ__PMC_ADD_TO_GLOBAL_TX_IGNORE: {
            struct pmem_st temp_info = {0};
            temp_info.addr = arg[1];
            temp_info.size = arg[2];

            add_to_global_excludes(&temp_info);
            break;
        }

        case VG_USERREQ__PMC_RESERVED1: {
            /* deprecated - do not use */
            break;
        }

        case VG_USERREQ__PMC_RESERVED2:
        case VG_USERREQ__PMC_RESERVED3:
        case VG_USERREQ__PMC_RESERVED4:
        case VG_USERREQ__PMC_RESERVED5:
        case VG_USERREQ__PMC_RESERVED6:
        case VG_USERREQ__PMC_RESERVED7:
        case VG_USERREQ__PMC_RESERVED8:
        case VG_USERREQ__PMC_RESERVED9:
        case VG_USERREQ__PMC_RESERVED10: {
            VG_(message)(
                    Vg_UserMsg,
                    "Warning: deprecated pmemcheck client request code 0x%llx\n",
                    (ULong)arg[0]
            );
            return False;
        }

        default:
            VG_(message)(
                    Vg_UserMsg,
                    "Warning: unknown pmemcheck client request code 0x%llx\n",
                    (ULong)arg[0]
            );
            return False;
    }
    return True;
}

/**
* \brief Handle tool command line arguments.
* \param[in] arg Tool command line arguments.
* \return True if the parameter is recognized, false otherwise.
*/
static Bool
pmc_process_cmd_line_option(const HChar *arg)
{
    if VG_BOOL_CLO(arg, "--mult-stores", pmem.track_multiple_stores) {}
    else if VG_BINT_CLO(arg, "--indiff", pmem.store_sb_indiff, 0, UINT_MAX) {}
    else if VG_BOOL_CLO(arg, "--log-stores", pmem.log_stores) {}
    else if VG_BOOL_CLO(arg, "--log-stores-stacktraces", pmem.store_traces) {}
    else if VG_BINT_CLO(arg, "--log-stores-stacktraces-depth",
                        pmem.store_traces_depth, 1, UINT_MAX) {}
    else if VG_BOOL_CLO(arg, "--print-summary", pmem.print_summary) {}
    else if VG_BOOL_CLO(arg, "--flush-check", pmem.check_flush) {}
    else if VG_BOOL_CLO(arg, "--flush-align", pmem.force_flush_align) {}
    else if VG_BOOL_CLO(arg, "--tx-only", pmem.transactions_only) {}
    else if VG_BOOL_CLO(arg, "--isa-rec", pmem.automatic_isa_rec) {}
    else if VG_BOOL_CLO(arg, "--error-summary", pmem.error_summary) {}
    else if VG_BOOL_CLO(arg, "--expect-fence-after-clflush",
		    pmem.weak_clflush) {}
    else
        return False;

    return True;
}

/**
* \brief Post command line options initialization.
*/
static void
pmc_post_clo_init(void)
{
    pmem.pmem_stores = VG_(OSetGen_Create)(/*keyOff*/0, cmp_pmem_st,
            VG_(malloc), "pmc.main.cpci.1", VG_(free));

    if (pmem.track_multiple_stores)
        pmem.multiple_stores = VG_(malloc)("pmc.main.cpci.2",
                MAX_MULT_OVERWRITES * sizeof (struct pmem_st *));

    pmem.redundant_flushes = VG_(malloc)("pmc.main.cpci.3",
            MAX_FLUSH_ERROR_EVENTS * sizeof (struct pmem_st *));

    pmem.pmem_mappings = VG_(OSetGen_Create)(/*keyOff*/0, cmp_pmem_st,
            VG_(malloc), "pmc.main.cpci.4", VG_(free));

    pmem.superfluous_flushes = VG_(malloc)("pmc.main.cpci.6",
            MAX_FLUSH_ERROR_EVENTS * sizeof (struct pmem_st *));

    pmem.flush_align_size = read_cache_line_size();

    init_transactions(pmem.transactions_only);

    if (pmem.log_stores)
        VG_(emit)("START");
}

/**
* \brief Print usage.
*/
static void
pmc_print_usage(void)
{
    VG_(printf)(
            "    --indiff=<uint>                        multiple store indifference\n"
            "                                           default [0 SBlocks]\n"
            "    --mult-stores=<yes|no>                 track multiple stores to the same\n"
            "                                           address default [no]\n"
            "    --log-stores=<yes|no>                  log all stores to persistence\n"
            "                                           default [no]\n"
            "    --log-stores-stacktraces=<yes|no>      dump stacktrace with each logged store\n"
            "                                           default [no]\n"
            "    --log-stores-stacktraces-depth=<uint>  depth of logged stacktraces\n"
            "                                           default [1]\n"
            "    --print-summary=<yes|no>               print summary on program exit\n"
            "                                           default [yes]\n"
            "    --flush-check=<yes|no>                 register multiple flushes of stores\n"
            "                                           default [no]\n"
            "    --flush-align=<yes|no>                 force flush alignment to native cache\n"
            "                                           line size default [no]\n"
            "    --tx-only=<yes|no>                     turn on transaction only memory\n"
            "                                           modifications default [no]\n"
            "    --isa-rec=<yes|no>                     turn on automatic flush/commit/fence\n"
            "                                           recognition default [yes]\n"
            "    --error-summary=<yes|no>               turn on error summary message\n"
            "                                           default [yes]\n"
            "    --expect-fence-after-clflush=<yes|no>  simulate 2-phase flushing on old CPUs\n"
            "                                           default [no]\n"

    );
}

/**
* \brief Print debug usage.
*/
static void
pmc_print_debug_usage(void)
{
    VG_(printf)(
            "    (none)\n"
    );
}

/**
 * \brief Function called on program exit.
 */
static void
pmc_fini(Int exitcode)
{
    if (pmem.log_stores)
        VG_(emit)("|STOP\n");

    if (pmem.print_summary)
        print_pmem_stats(False);
}

/**
* \brief Pre command line options initialization.
*/
static void
pmc_pre_clo_init(void)
{
    VG_(details_name)("pmemcheck");
    VG_(details_version)("1.0");
    VG_(details_description)("a simple persistent store checker");
    VG_(details_copyright_author)("Copyright (c) 2014-2016, Intel Corporation");
    VG_(details_bug_reports_to)("tomasz.kapela@intel.com");

    VG_(details_avg_translation_sizeB)(275);

    VG_(basic_tool_funcs)(pmc_post_clo_init, pmc_instrument, pmc_fini);

    VG_(needs_command_line_options)(pmc_process_cmd_line_option,
            pmc_print_usage, pmc_print_debug_usage);

    VG_(needs_client_requests)(pmc_handle_client_request);

    /* support only 64 bit architectures */
    tl_assert(VG_WORDSIZE == 8);
    tl_assert(sizeof(void*) == 8);
    tl_assert(sizeof(Addr) == 8);
    tl_assert(sizeof(UWord) == 8);
    tl_assert(sizeof(Word) == 8);

    pmem.print_summary = True;
    pmem.store_traces_depth = 1;
    pmem.automatic_isa_rec = True;
    pmem.error_summary = True;
}

VG_DETERMINE_INTERFACE_VERSION(pmc_pre_clo_init)
