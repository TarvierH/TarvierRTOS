#include "list.h"

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
