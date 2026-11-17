/**
 * 侵入式链表举例 学生信息
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct list_item // 与要存的学生信息无关 单纯的链表节点
{
    struct list_item *pxPrevious;
    struct list_item *pxNext;
    void *pvOwner;     // 指回拥有者
    void *pvContainer; // 所属链表
    uint32_t xItemValue;
    /**
     * 其他的一些字段
     */
} ListItem_t;

typedef struct list
{
    ListItem_t xListEnd; // 哨兵节点
    uint32_t xCount;
    /* 可以使用游标 */
} List_t;

typedef struct student
{
    uint16_t xId;
    char cName[100];
    ListItem_t xListItem; // 把节点内嵌在保存信息的结构体里，同一套链表的api可以保存和操作不同的结构体
} Student_t;

/* 链表的增删改查操作都和student无关，任何结构体只要内嵌ListItem_t就可以使用这套api*/
void vListInit(List_t *list)
{
    // 哨兵节点的pre和next初始化都指向自己
    list->xListEnd.pxPrevious = &list->xListEnd;
    list->xListEnd.pxNext = &list->xListEnd;
    list->xListEnd.xItemValue = UINT32_MAX;
    list->xListEnd.pvContainer = NULL;
    list->xListEnd.pvOwner = NULL;
    list->xCount = 0;
}
void vListAdd(ListItem_t *item, List_t *list)
{
    item->pvContainer = list;
    item->pxPrevious = list->xListEnd.pxPrevious;
    item->pxNext = &list->xListEnd;

    list->xListEnd.pxPrevious->pxNext = item;
    list->xListEnd.pxPrevious = item;

    list->xCount++;
}

void vListDel(ListItem_t *item)
{
    if (item->pvContainer == NULL)
        return; /* 不在任何链表，拒绝删除 */

    List_t *list = (List_t *)item->pvContainer;
    item->pxPrevious->pxNext = item->pxNext;
    item->pxNext->pxPrevious = item->pxPrevious;
    item->pvContainer = NULL;

    list->xCount--;
}

void vPrintStudents(List_t *student_list)
{
    Student_t *student;
    for (ListItem_t *pxIt = student_list->xListEnd.pxNext; pxIt->xItemValue < UINT32_MAX; pxIt = pxIt->pxNext)
    {
        student = (Student_t *)(pxIt->pvOwner);
        printf("name = %s, id = %d\n", student->cName, student->xId);
    }
}

int main()
{
    List_t student_list;
    vListInit(&student_list);
    Student_t *student;
    for (int i = 0; i < 10; i++)
    {
        student = malloc(sizeof(*student));
        sprintf(student->cName, "Tarvier %d", i);
        student->xId = i;
        student->xListItem.pvOwner = student;
        student->xListItem.xItemValue = 0;
        vListAdd(&student->xListItem, &student_list);
    }
    vPrintStudents(&student_list);

    printf("delete odd id\n");
    for (ListItem_t *pxIt = student_list.xListEnd.pxNext; pxIt->xItemValue < UINT32_MAX; pxIt = pxIt->pxNext)
    {
        student = (Student_t *)(pxIt->pvOwner);
        if ((student->xId) % 2 == 1)
        {
            vListDel(&student->xListItem);
            free(student);
        }
    }
    vPrintStudents(&student_list);
    return 0;
}