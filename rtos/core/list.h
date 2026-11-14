/* 双向循环链表（FreeRTOS 同款设计，作为内核最基础的数据结构）。
 *
 * 设计要点：
 *  - 链表头（List_t）内部有一个哨兵节点 xListEnd，链表永远闭合，
 *    因此插入/删除不需要特判空表。
 *  - ListItem 通过 pvContainer 记住自己属于哪个链表：NULL 表示不在任何
 *    链表中。vListInsert 按 xItemValue 升序插入（小值靠近头部），
 *    哨兵节点的 xItemValue 固定为 UINT32_MAX，用于终止查找。
 *  - pxIndex 是遍历游标：同一优先级任务按“先来先服务”轮流调度时，
 *    从 pxIndex 出发取下一个节点即可实现时间片轮转。
 */
#ifndef LIST_H
#define LIST_H

#include <stddef.h>   /* NULL */
#include <stdint.h>

typedef struct list_item {
    uint32_t xItemValue;        /* 排序值（如延时列表里的唤醒 tick） */
    struct list_item *pxNext;
    struct list_item *pxPrevious;
    void *pvOwner;              /* 拥有者，通常是任务控制块 TCB */
    void *pvContainer;          /* 所属链表 List_t*；NULL = 不在任何链表中 */
} ListItem_t;

typedef struct list {
    uint32_t uxNumberOfItems;   /* 链表内元素个数（不含哨兵） */
    ListItem_t *pxIndex;        /* 遍历游标 */
    ListItem_t xListEnd;        /* 哨兵节点 */
} List_t;

void vListInitialise(List_t *pxList);
void vListInitialiseItem(ListItem_t *pxItem);

/* 把 pxItem 插到 pxIndex 之前（哨兵之前 = 链表尾部），不排序 */
void vListInsertEnd(List_t *pxList, ListItem_t *pxItem);

/* 按 xItemValue 升序插入（值小的靠近链表头部） */
void vListInsert(List_t *pxList, ListItem_t *pxItem);

/* 从所属链表移除，返回移除后该链表的元素个数 */
uint32_t uxListRemove(ListItem_t *pxItem);

#endif /* LIST_H */
