/*    Schmalkalden University of Applied Science 
            Project IOT & Group 3 
Developers: Tarundeep The Leader 
            Soham The Gatherer
            Naveen bedridden
            Anushaka 404
            Anjana NULL
            
 * heap_buddy.c - Multi-region buddy allocator for FreeRTOS (heap_5-style, inspired from heap_bl602 too)
 *      Quick summary of work done
 *  - Uses vPortDefineHeapRegions() like heap_5.c
 *  - Supports multiple non-contiguous regions (one buddy tree per region)
 *  - Provides pvPortMalloc(), vPortFree(), xPortGetFreeHeapSize(),
 *    xPortGetMinimumEverFreeHeapSize(), vPortGetHeapStats()
 *
 * IMPORTANT: some topic concepts are explained in text file (concept_explanation.txt) refer to that for more understanding
 */


                                //Including header files for Buddy Memory Allocator
#include <stdint.h> 
/* decleartion for fixed width integers type defines like uint8_t (read as unsigned intergere 8 byte,
/ _t : tells its a typedefine, more example: uint16_t, uint32_t,...*/
#include <stddef.h>
/* defines size_t and NULL -> very important, frequently used 
size_t – an unsigned integer type used for sizes (like number of bytes).
*/
#include <string.h>
/* defines -> memcpy, memset -> used in calloc(), realloc() */

                                //MACRO: MPU_WRAPPERS_INCLUDED_FROM_API_FILE    
/* Refer to concept_explanation.txt file Section 1 for detail explanation*/
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE
/* This macro is part of every heap_1 to heap_5 files.*/

                                // Dynamic Allocation Condition
/* checking id dynamic allocation is able, otherwise programm required static memory. Then this file should not be included
 This is checked by using #if which is conditional compilation. Throws error if dynamic allocation set to 0. It must be set  to 1
*/

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 0 )
    #error "heap_buddy.c requires configSUPPORT_DYNAMIC_ALLOCATION == 1"
#endif


                                //Buddy configuration constants                                            

/* Smallest block = 2^BUDDY_MIN_ORDER bytes. Must be >= sizeof(BuddyBlock_t)
Thus the smallest chunk this allocator will give is 32 bytes (header included)
*/
#define BUDDY_MIN_ORDER      ( 5U )      /* 2^5  = 32 bytes blocks  (Smallest block of memory)*/


/* Largest block = 2^BUDDY_MAX_ORDER bytes in any region tree                
 For PineCone BL602 total internal RAM ~192KB, 2^18 = 256KB is plenty
*/
#define BUDDY_MAX_ORDER      ( 18U )     /* 2^18 = 256 KB blocks max  (Largest block of memory)*/


/* Maximum number of regions you expect to pass via vPortDefineHeapRegions(). */
#define BUDDY_MAX_REGIONS    ( 4U )
/*The allocator supports up to 4 separate regions of memory.
     here, the term: order = n, block size = 2^n bytes.
        Example:
            order = 5 → 32 bytes
            order = 6 → 64 bytes
            order = 10 → 1024 bytes
 */




                                    //Internal types               

/* Header at the beginning of each buddy block.  The block size is implicit:
 * size = (1U << order).  For free blocks, pxNext is used in the free list.
 * For allocated blocks, pxNext is NULL.
 */
typedef struct BuddyBlock
{
    struct BuddyBlock *pxNext;    /* A pointer to another BuddyBlock_t*/
    uint8_t            ucOrder;   /* log2(block_size) Example: order 5 → size 32 bytes*/
    uint8_t            ucRegion;  /* index into xRegions[] Index into xRegions[] array → tells which region this block belongs to.*/
} BuddyBlock_t;

/* A region = one contiguous memory area handled with a buddy tree.
Per-region buddy heap descriptor.  Each region has its own buddy tree. */
typedef struct BuddyRegion
{
    uint8_t     *pucBase;                          /* Start address (aligned)   */
    size_t       xSize;                            /* Total bytes in this tree  */
    uint8_t      ucMinOrder;                       /* Smallest block order      */
    uint8_t      ucMaxOrder;                       /* Largest  block order      */
    BuddyBlock_t *pxFreeLists[BUDDY_MAX_ORDER + 1U]; /* One list per order    */
} BuddyRegion_t;


                                        // Static Global State
/* static at file scope → only visible inside this file
ucRegionCount – how many regions have been initialized so far.
*/
static BuddyRegion_t xRegions[BUDDY_MAX_REGIONS];
static uint8_t       ucRegionCount = 0;

/* Global FreeRTOS-style accounting (over all regions). */
static size_t xFreeBytesRemaining          = 0U; /*how many bytes are free right now*/
static size_t xMinimumEverFreeBytesRemaining = 0U; /*smallest “free bytes” ever seen during execution*/
static size_t xNumberOfSuccessfulAllocations = 0U; /*total successful mallocs*/
static size_t xNumberOfSuccessfulFrees       = 0U; /*total successful frees*/

/* Byte-aligned size of our block header. */
/*refere to concept_explanantion.txt file section 2*/
static const size_t xBuddyHeaderSize =
    ( sizeof( BuddyBlock_t ) + ( portBYTE_ALIGNMENT - 1U ) )
    & ~( ( size_t ) portBYTE_ALIGNMENT_MASK );


                                        //Helper functions

/* Round up to next power of two.  Returns 0 if input is 0 or would overflow. */
static size_t prvRoundUpToPowerOfTwo( size_t x )
{
    if( x == 0U )
    {
        return 0U;
    }

    /* If already power-of-two, return as-is. */
    if( ( x & ( x - 1U ) ) == 0U )
    {
        return x;
    }

    /* Shift up until we pass x. */
    size_t p = 1U;
    while( p < x )
    {
        p <<= 1U;
        if( p == 0U ) /* overflow safety */
        {
            return 0U;
        }
    }
    return p;
}

/* Compute log2(x), assuming x is a power of two and non-zero. */
static uint8_t prvLog2( size_t x )
{
    uint8_t n = 0U;
    while( x > 1U )
    {
        x >>= 1U;
        n++;
    }
    return n;
}

/* Find which region a given pointer belongs to. */
/*
puc – pointer to unsigned char, representing an address in memory.
Loop over each registered region i from 0 to ucRegionCount-1. 
    For each:
        base = start of region.
        end = base + size → first address after region.
    If puc is in [base, end):
        If caller gave pucRegionIndex (not NULL), write i into it.
        Return pointer to that region: &xRegions[i].
    If no region matches: return NULL.
*/
static BuddyRegion_t *prvFindRegionForPtr( uint8_t *puc, uint8_t *pucRegionIndex )
{
    for( uint8_t i = 0U; i < ucRegionCount; i++ )
    {
        uint8_t  *base = xRegions[ i ].pucBase;
        uint8_t  *end  = base + xRegions[ i ].xSize;

        if( ( puc >= base ) && ( puc < end ) )
        {
            if( pucRegionIndex != NULL )
            {
                *pucRegionIndex = i;
            }
            return &xRegions[ i ];
        }
    }
    return NULL;
}

/* Insert a block into the free list for its order (LIFO). */
/*
order = pxBlock->ucOrder; – find block’s order.
configASSERT(...) – if condition fails, system halts (debug).
Then link it into the free list:
    pxBlock->pxNext = pxRegion->pxFreeLists[ order ];
        Block points to current head.
    pxRegion->pxFreeLists[ order ] = pxBlock;
        New block becomes new head (LIFO).
*/
static void prvInsertFreeBlock( BuddyRegion_t *pxRegion,
                                BuddyBlock_t  *pxBlock )
{
    uint8_t order = pxBlock->ucOrder;

    configASSERT( order >= pxRegion->ucMinOrder );
    configASSERT( order <= pxRegion->ucMaxOrder );

    pxBlock->pxNext = pxRegion->pxFreeLists[ order ];
    pxRegion->pxFreeLists[ order ] = pxBlock;
}

/* Remove and return a block from a free list for a given order. */
/*
Get first block in list for that order.
If non-NULL:
    Move list head to pxBlock->pxNext.
    Set pxBlock->pxNext = NULL (not in list anymore).
Return pxBlock (or NULL if list empty).
*/
static BuddyBlock_t *prvPopFreeBlock( BuddyRegion_t *pxRegion,
                                      uint8_t        order )
{
    BuddyBlock_t *pxBlock = pxRegion->pxFreeLists[ order ];
    if( pxBlock != NULL )
    {
        pxRegion->pxFreeLists[ order ] = pxBlock->pxNext;
        pxBlock->pxNext = NULL;
    }
    return pxBlock;
}

/* Search a free list for a block at a specific address, removing it if found. */
/*
Iterate through free list of given order:
    pxPrev starts NULL, pxCur starts at head
    If current node’s address equals uxBuddyAddr:
        If it’s first node (no previous), move head
        Else, bypass it: pxPrev->pxNext = pxCur->pxNext;
        Clear pxCur->pxNext, return that block
    If not found: return NULL
This is used during merge: “Is my buddy free? If yes, remove it from free list and merge.”
*/
static BuddyBlock_t *prvRemoveBuddyFromFreeList( BuddyRegion_t *pxRegion,
                                                 uint8_t        order,
                                                 uintptr_t      uxBuddyAddr )
{
    BuddyBlock_t *pxPrev = NULL;
    BuddyBlock_t *pxCur  = pxRegion->pxFreeLists[ order ];

    while( pxCur != NULL )
    {
        if( ( uintptr_t ) pxCur == uxBuddyAddr )
        {
            /* Remove from list. */
            if( pxPrev == NULL )
            {
                pxRegion->pxFreeLists[ order ] = pxCur->pxNext;
            }
            else
            {
                pxPrev->pxNext = pxCur->pxNext;
            }
            pxCur->pxNext = NULL;
            return pxCur;
        }

        pxPrev = pxCur;
        pxCur  = pxCur->pxNext;
    }

    return NULL;
}


                                    //Region initialisation from vPortDefineHeapRegions()                       
/*This is called by the application to tell FreeRTOS “here are my heap memory ranges”.*/
void vPortDefineHeapRegions( const HeapRegion_t * const pxHeapRegions )
{
    const HeapRegion_t *pxHR = pxHeapRegions;

    configASSERT( pxHeapRegions != NULL );
    configASSERT( ucRegionCount == 0U ); /* Can only be called once. */

    /* Reset global accounting. */
    xFreeBytesRemaining             = 0U;
    xMinimumEverFreeBytesRemaining  = 0U;
    xNumberOfSuccessfulAllocations  = 0U;
    xNumberOfSuccessfulFrees        = 0U;

    while( ( pxHR->pucStartAddress != NULL ) && ( pxHR->xSizeInBytes > 0U ) )
    {
        configASSERT( ucRegionCount < BUDDY_MAX_REGIONS );

        BuddyRegion_t *pxRegion = &xRegions[ ucRegionCount ];
        ucRegionCount++;

        uint8_t *pucBase = pxHR->pucStartAddress;
        size_t   xSize   = pxHR->xSizeInBytes;

        /* Align region start to portBYTE_ALIGNMENT. */
        uintptr_t uxAddr = ( uintptr_t ) pucBase;
        if( ( uxAddr & portBYTE_ALIGNMENT_MASK ) != 0U )
        {
            uintptr_t uxAligned = ( uxAddr + ( portBYTE_ALIGNMENT - 1U ) )
                                  & ~( ( uintptr_t ) portBYTE_ALIGNMENT_MASK );
            xSize   -= ( size_t ) ( uxAligned - uxAddr );
            pucBase  = ( uint8_t * ) uxAligned;
        }

        /* Ensure region still has some space. */
        configASSERT( xSize > ( xBuddyHeaderSize * 2U ) );

        /* We will build one big buddy tree per region.  We choose the largest
         * power-of-two size that fits in xSize.  Any leftover tail bytes are
         * ignored (wasted but keeps logic simple and deterministic).
         */
        size_t xMaxBlockSize = 1U << BUDDY_MAX_ORDER;
        while( ( xMaxBlockSize > xSize ) && ( xMaxBlockSize > ( 1U << BUDDY_MIN_ORDER ) ) )
        {
            xMaxBlockSize >>= 1U;
        }

        /* Round down to multiple of that max block size. */
        size_t xTreeSize = ( xSize / xMaxBlockSize ) * xMaxBlockSize;
        configASSERT( xTreeSize >= xMaxBlockSize );

        pxRegion->pucBase   = pucBase;
        pxRegion->xSize     = xTreeSize;
        pxRegion->ucMinOrder = BUDDY_MIN_ORDER;
        pxRegion->ucMaxOrder = prvLog2( xMaxBlockSize );

        /* Clear free lists. */
        for( uint8_t o = 0U; o <= BUDDY_MAX_ORDER; o++ )
        {
            pxRegion->pxFreeLists[ o ] = NULL;
        }

        /* Partition region into contiguous max-order blocks and push them
         * into the free list for ucMaxOrder.
         */
        uint8_t      *p = pucBase;
        const uint8_t regionIndex = (uint8_t)( ucRegionCount - 1U );

        while( p < ( pucBase + xTreeSize ) )
        {
            BuddyBlock_t *pxBlock = ( BuddyBlock_t * ) p;

            /* Ensure alignment of block header. */
            configASSERT( ( (uintptr_t)pxBlock & portBYTE_ALIGNMENT_MASK ) == 0U );

            pxBlock->ucOrder  = pxRegion->ucMaxOrder;
            pxBlock->ucRegion = regionIndex;
            pxBlock->pxNext   = pxRegion->pxFreeLists[ pxRegion->ucMaxOrder ];
            pxRegion->pxFreeLists[ pxRegion->ucMaxOrder ] = pxBlock;

            p += xMaxBlockSize;
        }

        xFreeBytesRemaining += xTreeSize;

        /* Next region. */
        pxHR++;
    }

    xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;

    configASSERT( ucRegionCount > 0U );
}

                                        //Allocation (pvPortMalloc)
/*Function that user code / FreeRTOS uses instead of malloc
    xWantedSize – number of user bytes requested
    If zero requested, return NULL (do nothing)
    pvReturn will hold resulting pointer
*/
/*
 pvPortMalloc()
- Allocates a memory block using a buddy allocator.
This size will be added to accommodate the internal header and then rounded up to a multiple of a power of two. 
The memory allocator then proceeds to look for a free block in any area. If found, a block that is larger than
requested would be divided into two "buddy" blocks until the size requested by the user is obtained. One of these 
blocks would then be allocated to the user, with the other being allocated back to the free list.
Example:
    pvPortMalloc(50) → rounded to 64 bytes → memory allocator divides bigger blocks 
    suntil a 64-byte block is available → returns a pointer to user data.
 */


void *pvPortMalloc( size_t xWantedSize )
{
    void *pvReturn = NULL;

    configASSERT( ucRegionCount > 0U ); /* Must have called vPortDefineHeapRegions. */

    if( xWantedSize == 0U )
    {
        return NULL;
    }

    vTaskSuspendAll();
    {
        /* User bytes + header, aligned to power-of-two. */
        size_t xTotalSize = xWantedSize + xBuddyHeaderSize;

        /* Ensure min block size covers header. */
        if( xTotalSize < ( ( size_t ) 1U << BUDDY_MIN_ORDER ) )
        {
            xTotalSize = ( ( size_t ) 1U << BUDDY_MIN_ORDER );
        }

        size_t  xBlockSize = prvRoundUpToPowerOfTwo( xTotalSize );
        if( xBlockSize == 0U )
        {
            ( void ) xTaskResumeAll();
            return NULL;
        }

        uint8_t ucOrder = prvLog2( xBlockSize );

        /* Try each region in order to find a block >= required order. */
        for( uint8_t i = 0U; i < ucRegionCount; i++ )
        {
            BuddyRegion_t *pxRegion = &xRegions[ i ];

            if( ( ucOrder < pxRegion->ucMinOrder ) || ( ucOrder > pxRegion->ucMaxOrder ) )
            {
                continue;
            }

            uint8_t searchOrder = ucOrder;
            BuddyBlock_t *pxBlock = NULL;

            /* Find first non-empty free list with order >= ucOrder. */
            while( ( searchOrder <= pxRegion->ucMaxOrder ) &&
                   ( ( pxBlock = prvPopFreeBlock( pxRegion, searchOrder ) ) == NULL ) )
            {
                searchOrder++;
            }

            if( pxBlock == NULL )
            {
                /* No block big enough in this region, try next region. */
                continue;
            }

            /* Split block down until we reach desired order. */
            while( searchOrder > ucOrder )
            {
                searchOrder--;

                size_t splitSize = ( ( size_t )1U << searchOrder );
                uint8_t *puc      = ( uint8_t * ) pxBlock;

                /* First half: keep in pxBlock. */
                pxBlock->ucOrder = searchOrder;
                pxBlock->ucRegion = i;

                /* Second half: new buddy, push to free list[searchOrder]. */
                BuddyBlock_t *pxBuddy = ( BuddyBlock_t * )( puc + splitSize );
                pxBuddy->ucOrder      = searchOrder;
                pxBuddy->ucRegion     = i;
                pxBuddy->pxNext       = NULL;

                prvInsertFreeBlock( pxRegion, pxBuddy );
            }

            /* Now pxBlock has order == ucOrder.  Mark allocated by pxNext = NULL. */
            pxBlock->pxNext = NULL;

            xFreeBytesRemaining -= ( ( size_t )1U << ucOrder );
            if( xFreeBytesRemaining < xMinimumEverFreeBytesRemaining )
            {
                xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;
            }

            xNumberOfSuccessfulAllocations++;

            /* Return pointer after header, aligned. */
            pvReturn = ( void * )( ( ( uint8_t * ) pxBlock ) + xBuddyHeaderSize );

            traceMALLOC( pvReturn, xWantedSize );
            break; /* stop searching regions */
        }
    }
    ( void ) xTaskResumeAll();

    #if ( configUSE_MALLOC_FAILED_HOOK == 1 )
    {
        if( pvReturn == NULL )
        {
            extern void vApplicationMallocFailedHook( void );
            vApplicationMallocFailedHook();
        }
    }
    #endif

    return pvReturn;
}


                                            //Free (vPortFree)
/*
Function that user code / FreeRTOS uses instead of free
    Example:
    Region base = 1000.
    You free a block at address 1128 with order 5 (size 32).
    Suppose its buddy at order 5 is free at 1096.
    They merge into order 6 (size 64), starting at 1096.
    Then maybe that buddy also free; merge again, etc.
*/
/*
 * vPortFree() – Frees a previously allocated block and merges buddies.
 *
 * It receives a user pointer, moves backward to find the block header, and
 * identifies which region the block belongs to. The allocator then tries to
 * merge this free block with its “buddy” (the other half of the same power-of-two
 * block) if that buddy is also free. This merging continues repeatedly, allowing
 * small blocks to combine back into large blocks, reducing fragmentation.
 *
 * Example:
 *   If a 64-byte block is freed and its sibling 64-byte buddy is also free,
 *   vPortFree() merges them into one 128-byte free block.
 */


void vPortFree( void *pv )
{
    if( pv == NULL )
    {
        return;
    }

    uint8_t *puc = ( uint8_t * ) pv;

    /* Go back to block header. */
    puc -= xBuddyHeaderSize;
    BuddyBlock_t *pxBlock = ( BuddyBlock_t * ) puc;

    uint8_t regionIndex = 0U;
    BuddyRegion_t *pxRegion = prvFindRegionForPtr( ( uint8_t * ) pxBlock, &regionIndex );
    configASSERT( pxRegion != NULL );
    ( void ) regionIndex;

    vTaskSuspendAll();
    {
        traceFREE( pv, ( ( size_t )1U << pxBlock->ucOrder ) );


        /* Buddy merge loop: try to merge with free buddies of same order. */
        uint8_t  order     = pxBlock->ucOrder;
        uintptr_t baseAddr = ( uintptr_t ) pxRegion->pucBase;
        uintptr_t addr     = ( uintptr_t ) pxBlock;

        for( ;; )
        {
            /* Compute buddy address inside this region. */
            uintptr_t buddyAddr = ( ( addr - baseAddr ) ^ ( ( uintptr_t )1U << order ) ) + baseAddr;

            /* Stop if buddy is outside region range. */
            if( ( buddyAddr < ( uintptr_t ) pxRegion->pucBase ) ||
                ( buddyAddr >= ( uintptr_t )( pxRegion->pucBase + pxRegion->xSize ) ) )
            {
                break;
            }

            /* Try to find buddy in free list[order]. */
            BuddyBlock_t *pxBuddy =
                prvRemoveBuddyFromFreeList( pxRegion, order, buddyAddr );

            if( pxBuddy == NULL )
            {
                /* Buddy not free → cannot merge further. */
                break;
            }

            /* Merge: choose lower address as new block start. */
            if( buddyAddr < addr )
            {
                addr = buddyAddr;
            }

            order++;

            if( order > pxRegion->ucMaxOrder )
            {
                break;
            }
        }

        BuddyBlock_t *pxMerged = ( BuddyBlock_t * ) addr;
        pxMerged->ucOrder      = order;
        pxMerged->ucRegion     = regionIndex;
        pxMerged->pxNext       = NULL;

        prvInsertFreeBlock( pxRegion, pxMerged );

        xFreeBytesRemaining += ( ( size_t )1U << order );
        xNumberOfSuccessfulFrees++;

        traceFREE(pv, 1U << pxMerged->ucOrder);

    }
    ( void ) xTaskResumeAll();
}




                                                //Calloc (pvPortCalloc)
/*
 * pvPortCalloc() – Allocates memory and sets all bytes to zero.
 *
 * It works like pvPortMalloc(n * size) but automatically fills the allocated
 * memory with zeros using memset(). This is useful when you need clean,
 * initialized memory.
 *
 * Example:
 *   pvPortCalloc(10, 4) allocates 40 bytes and returns them already set to 0.
 */

void *pvPortCalloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = pvPortMalloc(total);
    if (p) memset(p, 0, total);
    return p;
}


                                    //pvPortRealloc() - Buddy Allocator Reallocation 
 /* Behaviour:
 *  - realloc(NULL, newSize)      → pvPortMalloc(newSize)
 *  - realloc(ptr, 0)             → vPortFree(ptr), return NULL
 *  - If newSize fits inside same buddy block → return same ptr
 *  - Else allocate new block, copy old data, free old block
 * 
 */
void *pvPortRealloc(void *pvOld, size_t xNewSize)
{
    /* Case 1: behaves like malloc() */
    if (pvOld == NULL)
    {
        return pvPortMalloc(xNewSize);
    }

    /* Case 2: behaves like free() */
    if (xNewSize == 0)
    {
        vPortFree(pvOld);
        return NULL;
    }

    /* Calculate old block header */
    uint8_t *pucOld = (uint8_t *)pvOld - xBuddyHeaderSize;
    BuddyBlock_t *pxOldBlock = (BuddyBlock_t *)pucOld;

    /* Validate region */
    uint8_t regionIndex = 0;
    BuddyRegion_t *pxRegion = prvFindRegionForPtr((uint8_t*)pxOldBlock, &regionIndex);
    if (pxRegion == NULL)
    {
        /* Invalid pointer: FreeRTOS would normally assert */
        return NULL;
    }

    /* Determine old block usable size */
    size_t xOldBlockSize = (size_t)1U << pxOldBlock->ucOrder;
    size_t xOldUsable = xOldBlockSize - xBuddyHeaderSize;

    /* Case 3: if new size fits into the old block, keep it */
    if (xNewSize <= xOldUsable)
    {
        return pvOld;     /* No need to move, block already large enough */
    }

    /* Case 4: need to allocate new block */
    void *pvNew = pvPortMalloc(xNewSize);
    if (pvNew == NULL)
    {
        /* Not enough memory: old block preserved */
        return NULL;
    }

    /* Copy old data → new block */
    /* Only copy minimum of old and new size (no overflow risk) */
    size_t toCopy = (xOldUsable < xNewSize) ? xOldUsable : xNewSize;
    memcpy(pvNew, pvOld, toCopy);

    /* Free old block */
    vPortFree(pvOld);

    return pvNew;
}



                                        //FreeRTOS heap API helpers
/*
 * pvPortRealloc() – Changes the size of an existing allocation.
 *
 * Behaviour:
 *   - If old pointer is NULL → works like pvPortMalloc(newSize).
 *   - If newSize is 0 → frees the memory and returns NULL.
 *   - If the new size fits inside the old buddy block → keeps the block and
 *     returns the same pointer without moving data.
 *   - Otherwise it allocates a new larger block, copies old data into it,
 *     frees the old block, and returns the new pointer.
 *
 * Example:
 *   pvPortRealloc(ptr, 200) → if old block was too small, it allocates a new
 *   block (e.g., 256 bytes), copies existing content, and frees the old block.
 */

size_t xPortGetFreeHeapSize( void )
{
    return xFreeBytesRemaining;
}

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return xMinimumEverFreeBytesRemaining;
}

/* Optional: heap stats similar to heap_5.c */
void vPortGetHeapStats( HeapStats_t *pxHeapStats )
{
    size_t xLargestFree = 0U;
    size_t xSmallestFree = ( size_t ) -1;
    size_t xBlocks = 0U;

    vTaskSuspendAll();
    {
        for( uint8_t r = 0U; r < ucRegionCount; r++ )
        {
            BuddyRegion_t *pxRegion = &xRegions[ r ];

            for( uint8_t o = pxRegion->ucMinOrder; o <= pxRegion->ucMaxOrder; o++ )
            {
                size_t blockSize = ( ( size_t )1U << o );
                BuddyBlock_t *pxBlock = pxRegion->pxFreeLists[ o ];

                while( pxBlock != NULL )
                {
                    xBlocks++;

                    if( blockSize > xLargestFree )
                    {
                        xLargestFree = blockSize;
                    }
                    if( ( blockSize < xSmallestFree ) && ( blockSize != 0U ) )
                    {
                        xSmallestFree = blockSize;
                    }

                    pxBlock = pxBlock->pxNext;
                }
            }
        }
    }
    ( void ) xTaskResumeAll();

    if( xSmallestFree == ( size_t ) -1 )
    {
        xSmallestFree = 0U;
    }

    pxHeapStats->xSizeOfLargestFreeBlockInBytes   = xLargestFree;
    pxHeapStats->xSizeOfSmallestFreeBlockInBytes  = xSmallestFree;
    pxHeapStats->xNumberOfFreeBlocks              = xBlocks;

    taskENTER_CRITICAL();
    {
        pxHeapStats->xAvailableHeapSpaceInBytes      = xFreeBytesRemaining;
        pxHeapStats->xNumberOfSuccessfulAllocations  = xNumberOfSuccessfulAllocations;
        pxHeapStats->xNumberOfSuccessfulFrees        = xNumberOfSuccessfulFrees;
        pxHeapStats->xMinimumEverFreeBytesRemaining  = xMinimumEverFreeBytesRemaining;
    }
    taskEXIT_CRITICAL();
}
/* Imaginary example to show working of this project
Let’s pretend there is 1 region:
    Base at address 1000
    Size = 256 bytes
    BUDDY_MIN_ORDER = 5 → 32 bytes
    BUDDY_MAX_ORDER = 8 → 256 bytes

After Initialization
    Free list:
        Order 8: one block at 1000 (size 256)
        Orders 5,6,7: empty
    xFreeBytesRemaining = 256.

pvPortMalloc(50)
    xWantedSize = 50
    xTotalSize = 50 + header
        say header = 16 → total = 66.
    Round up to power of two → 128 → order = 7.
    Search region:
        Order 7 empty → try order 8 → one block at 1000.
    Split order 8 → order 7:
        Block [1000–1128) and [1128–1256)
        Keep first as pxBlock (1000), insert second to free list order 7.
    Now we have block of size 128 (order 7).
    xFreeBytesRemaining -= 128 → 256–128=128.
    Return pointer = 1000 + headerSize (e.g. 1016).
So user sees pointer ~1016.

vPortFree(ptr)
    They pass pointer 1016.
    We subtract header size 16 → 1000 → header.
    Find region (1000 in region).
    Buddy compute:
        order 7, block size 128.
        buddy offset = XOR with 128 → [1128].
    Check free list: there is block at 1128 → found.
    Merge: new order 8, address = min(1000,1128) = 1000.
    Insert order 8 block [1000–1256) into free list.
    Free bytes += 256 → 128+256?? (In real, slight differences due to example start but idea stands
*/
