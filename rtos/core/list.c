#include "list.h"

/**
 * @brief 初始化一个链表。
 *
 * 将链表置为空：哨兵节点（xListEnd）的前后指针指向自身，
 * 游标（pxIndex）指向哨兵，哨兵值设为 UINT32_MAX 作为查找终止值，
 * 并将链表项计数清零。
 *
 * @param pxList 待初始化的链表，不能为 NULL。
 */
void vListInitialise(List_t *pxList)
{
    /* 初始时链表为空：哨兵自己指向自己，游标也指向哨兵 */
    pxList->pxIndex = &pxList->xListEnd;
    pxList->xListEnd.xItemValue = UINT32_MAX; /* 查找终止值 */
    pxList->xListEnd.pxNext = &pxList->xListEnd;
    pxList->xListEnd.pxPrevious = &pxList->xListEnd;
    pxList->xListEnd.pvOwner = NULL;
    pxList->xListEnd.pvContainer = NULL;
    pxList->uxNumberOfItems = 0;
}

void vListInitialiseItem(ListItem_t *pxItem)
{
    pxItem->pvContainer = NULL; /* 关键：标记“不在任何链表中” */
}

/**
 * @brief 在链表游标（pxIndex）之前插入列表项。
 *
 * 与 vListInsert 不同，本函数不按 xItemValue 排序，而是直接将 pxItem
 * 插入到 pxList->pxIndex 所指向节点之前；当游标指向哨兵节点时，
 * 等价于追加到链表尾部。
 *
 * @param pxList 目标链表，不能为 NULL。
 * @param pxItem 待插入的列表项，不能为 NULL。
 */
void vListInsertEnd(List_t *pxList, ListItem_t *pxItem)
{
    ListItem_t *pxIndex = pxList->pxIndex;

    /* 在 pxIndex 之前插入（pxIndex 指向哨兵时即为追加到尾部） */
    pxItem->pxNext = pxIndex;
    pxItem->pxPrevious = pxIndex->pxPrevious;
    pxIndex->pxPrevious->pxNext = pxItem;
    pxIndex->pxPrevious = pxItem;

    pxItem->pvContainer = pxList;
    pxList->uxNumberOfItems++;
}

/**
 * @brief 按 xItemValue 升序将列表项插入链表。
 *
 * 从哨兵节点出发向后遍历，找到第一个值大于 xItemValue 的节点，
 * 将 pxItem 插入其之前（即插入位置之后为严格大于 xItemValue 的节点，
 * 相同值的项排在已有项之后）。由于哨兵值为 UINT32_MAX，遍历必然终止。
 * 若 xItemValue 等于哨兵值 UINT32_MAX，则直接插入到尾部（哨兵之前）。
 *
 * @param pxList 目标链表，不能为 NULL。
 * @param pxItem 待插入的列表项，不能为 NULL。
 */
void vListInsert(List_t *pxList, ListItem_t *pxItem)
{
    ListItem_t *pxIterator;
    const uint32_t xValue = pxItem->xItemValue;

    if (xValue == UINT32_MAX) {
        /* 等于哨兵值：直接插到尾部（哨兵之前） */
        pxIterator = pxList->xListEnd.pxPrevious;
    } else {
        /* 从哨兵出发，找到第一个 next 值大于 xValue 的位置；
         * 由于哨兵值为 UINT32_MAX，循环必然终止 */
        for (pxIterator = &pxList->xListEnd;
             pxIterator->pxNext->xItemValue <= xValue;
             pxIterator = pxIterator->pxNext) {
            /* 空循环：仅移动到插入位置 */
        }
    }

    /* 在 pxIterator 之后插入 pxItem */
    pxItem->pxNext = pxIterator->pxNext;
    pxItem->pxNext->pxPrevious = pxItem;
    pxItem->pxPrevious = pxIterator;
    pxIterator->pxNext = pxItem;

    pxItem->pvContainer = pxList;
    pxList->uxNumberOfItems++;
}

uint32_t uxListRemove(ListItem_t *pxItem)
{
    List_t *pxList = (List_t *)pxItem->pvContainer;

    pxItem->pxNext->pxPrevious = pxItem->pxPrevious;
    pxItem->pxPrevious->pxNext = pxItem->pxNext;

    /* 若游标正指向被删节点，把游标挪到前一个节点 */
    if (pxList->pxIndex == pxItem) {
        pxList->pxIndex = pxItem->pxPrevious;
    }

    pxItem->pvContainer = NULL; /* 移除后不再属于任何链表 */
    pxList->uxNumberOfItems--;

    return pxList->uxNumberOfItems;
}
