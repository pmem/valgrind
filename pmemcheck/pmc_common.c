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
#include <sys/param.h>
#include "pub_tool_oset.h"
#include "pub_tool_mallocfree.h"
#include "pub_tool_tooliface.h"
#include "pub_tool_libcassert.h"

#include "pmc_include.h"

/**
* \brief A compare function for regions stored in the OSetGen.
* \param[in] key The key to compare.
* \param[in] elem The element to compare with.
* \return -1 if key is smaller, 1 if key is greater and 0 if key is equal
* to elem. This means that the region is either before, after or overlaps.
*/
Word
cmp_pmem_st(const void *key, const void *elem)
{
    struct pmem_st *lhs = (struct pmem_st *) (key);
    struct pmem_st *rhs = (struct pmem_st *) (elem);

    if (lhs->addr + lhs->size <= rhs->addr)
        return -1;
    else if (lhs->addr >= rhs->addr + rhs->size)
        return 1;
    else
        return 0;
}

/**
 * \brief Check if regions overlap.
 * \param lhs Region to check.
 * \param rhs Region to check against.
 * \return 0 if lhs and rhs do not overlap, 1 if lhs is within rhs, 2 otherwise.
 */
UWord
check_overlap(const struct pmem_st *lhs, const struct pmem_st *rhs)
{
    if (cmp_pmem_st(lhs, rhs))
        /* regions do not overlap */
        return 0;
    else if ((lhs->addr < rhs->addr)
            || (lhs->addr + lhs->size) > (rhs->addr + rhs->size))
        /* partial overlap */
        return 2;
    else
        /* lhs fully within rhs */
        return 1;
}

/**
* \brief Check if the given region is in the set.
* \param[in] region Region to find.
* \param[in] region_set Region set to check.
* \return 0 if not in set, 1 if fully in the set, 2 if it overlaps over
* an existing mapping.
*/
UWord
is_in_mapping_set(const struct pmem_st *region, OSet *region_set)
{
    if (!VG_(OSetGen_Contains)(region_set, region)) {
        /* not in set */
        return 0;
    }

    return check_overlap(region, VG_(OSetGen_Lookup)(region_set, region));
}

/**
* \brief Adds a region to a set.
*
* Overlapping and neighboring regions will be merged.
* \param[in] region The region to register.
* \param[in, out] region_set The region set to which region will be registered.
*/
void
add_region(const struct pmem_st *region, OSet *region_set)
{
    struct pmem_st *entry = VG_(OSetGen_AllocNode(region_set,
            (SizeT)sizeof (struct pmem_st)));
    *entry= *region;
    entry->state = STST_CLEAN;

    struct pmem_st *old_entry;
    struct pmem_st search_entry = *entry;
    search_entry.addr -= 1;
    search_entry.size += 2;
    while ((old_entry = VG_(OSetGen_Remove)(region_set, &search_entry)) != NULL) {
        /* registering overlapping memory regions, glue them together */
        ULong max_addr = MAX(entry->addr + entry->size, old_entry->addr +
                old_entry->size);
        entry->addr = MIN(entry->addr, old_entry->addr);
        entry->size = max_addr - entry->addr;
        VG_(OSetGen_FreeNode)(region_set, old_entry);
    }
    VG_(OSetGen_Insert)(region_set, entry);
}

/**
* \brief Removes a region from a set.
*
* Partial overlaps will remove only the overlapping parts. For example if you
* have two regions registered (0x100-0x140) and (0x150-0x200) and you remove
* (0x130-0x160) you will end up with two regions (0x100-0x130)
* and (0x160-0x200).
* \param[in] region The region to remove.
* \param[in, out] region_set The region set to which region will be removed.
*/
void
remove_region(const struct pmem_st *region, OSet *region_set)
{
    struct pmem_st *modified_entry = NULL;
    SizeT region_max_addr = region->addr + region->size;
    while ((modified_entry = VG_(OSetGen_Remove)(region_set, region)) !=
            NULL) {
        SizeT mod_entry_max_addr = modified_entry->addr + modified_entry->size;
        if ((modified_entry->addr > region->addr) && (mod_entry_max_addr <
                region_max_addr)) {
            /* modified entry fully within removed region */
            VG_(OSetGen_FreeNode)(region_set, modified_entry);
        } else if ((modified_entry->addr < region->addr) &&
                (mod_entry_max_addr > region_max_addr)) {
            /* modified entry is larger than the removed region - slice */
            modified_entry->size = region->addr - modified_entry->addr;
            VG_(OSetGen_Insert)(region_set, modified_entry);
            struct pmem_st *new_region = VG_(OSetGen_AllocNode)(region_set,
                    sizeof (struct pmem_st));
            new_region->addr = region_max_addr;
            new_region->size = mod_entry_max_addr - new_region->addr;
            VG_(OSetGen_Insert)(region_set, new_region);
        } else if ((modified_entry->addr >= region->addr) &&
                (mod_entry_max_addr > region_max_addr)) {
            /* head overlaps */
            modified_entry->size -= region_max_addr - modified_entry->addr;
            modified_entry->addr = region_max_addr;
            VG_(OSetGen_Insert)(region_set, modified_entry);
        } else if ((mod_entry_max_addr <= region_max_addr) &&
                (region->addr > modified_entry->addr)) {
            /* tail overlaps */
            modified_entry->size = region->addr - modified_entry->addr;
            VG_(OSetGen_Insert)(region_set, modified_entry);
        } else {
            /* exact match */
            VG_(OSetGen_FreeNode)(region_set, modified_entry);
        }
    }
}


/**
 * \brief Check and update the given warning event register.
 * \param[in,out] event_register The register to be updated.
 * \param[in,out] nevents The event counter to be updated.
 * \param[in] event The event to be registered.
 * \param[in] limit The limit against which the registered is to be checked.
 * \param[in] err_msg Pointer to the error message function.
 */
void
add_warning_event(struct pmem_st **event_register, UWord *nevents,
                  struct pmem_st *event, UWord limit, void (*err_msg)(UWord))
{
    if (UNLIKELY(*nevents == limit)) {
        err_msg(limit);
        VG_(exit)(-1);
    }
    /* register the event */
    event_register[(*nevents)++] = event;
}
