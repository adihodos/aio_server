/*!
 * @file linked_list.h Implementation of a double linked list.
 * See Donald Knuth, The art of computer programming, 
 * 3rd edition, vol 1, p. 280-282.
 */
#ifndef _LINKED_LIST_H
#define _LINKED_LIST_H

#include "allocator.h"

/*!
 * Opaque handle to a double linked list.
 */
typedef void* dlinked_list_handle;

/*!
 * Pointer to item comparison function.
 */
typedef int  (*item_comparator)(const void*, const void*, void*);

/*!
 * Pointer to function that is used for walking the list.
 */
typedef void (*item_for_each)(void*, void*, void*);

/*!
 * @brief Creates a new double linked list object.
 * @param cmp Pointer to item compare function.
 * @param al Pointer to a user supplied allocator. If null the default allocator
 * is used.
 * @param userdata Aditional user data.
 * @return Handle to a double linked list if successfull, NULL on failure.
 */
dlinked_list_handle dlist_create(item_comparator cmp, 
                                 struct allocator* al, 
                                 void* userdata);

/*!
 * @brief Pushes a new item at the head of the list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @param item Pointer to item to insert. Must not be NULL.
 * @return Pointer to the item's data on success, NULL on failure.
 */
void* dlist_push_head(dlinked_list_handle list_handle, void* item);

/*!
 * @brief Pushes a new item at the tail of the list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @param item Pointer to item to insert. Must not be NULL.
 * @return Pointer to the item's data on success, NULL on failure.
 */
void* dlist_push_tail(dlinked_list_handle list_handle, void* item);

/*!
 * @brief Pops the item at the head of the list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @return Pointer to the item's data, NULL if the list was empty.
 */
void* dlist_pop_head(dlinked_list_handle list_handle);

/*!
 * @brief Pops the item at the tail of the list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @return Pointer to the item's data, NULL if the list was empty.
 */
void* dlist_pop_tail(dlinked_list_handle list_handle);

/*!
 * @brief Searches an item in a list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @param item Pointer to item to search for. Must not be NULL.
 * @return Pointer to the item if found, NULL if the item is not in the list.
 */
void* dlist_find(dlinked_list_handle list_handle, void* item);

/*!
 * @brief Deletes an item from the list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @param item Pointer to item to be deleted. Must not be NULL.
 * @return Pointer to the item's data.
 */
void* dlist_remove_item(dlinked_list_handle list_handle,
                        void* item);

/*!
 * @brief Walks all the items in a double linked list calling the user
 * supplied function for each item.
 * @fn_for_each Pointer to user defined function to be called when walking the
 * list. Must not be NULL. When called, the first argument passed is the item,
 * the second argument is the user defined data passed when creating the list,
 * and the third is the walk_data argument.
 * @param walk_data Aditional user defined data to be passed to the fn_for_each
 * function.
 */
void dlist_for_each(dlinked_list_handle list_handle,
                    void (*fn_for_each)(void*, void*, void*),
                    void* walk_data);

/*!
 * @brief Destroys a double linked list.
 * @param list_handle Handle to a linked list. Must not be NULL.
 * @remarks Make sure that all of the inserted data was deallocated before
 * calling this function, otherwise memory leaks will appear.
 */
void  dlist_destroy(dlinked_list_handle list_handle);

size_t dlist_size(dlinked_list_handle list_handle);

#endif /* _LINKED_LIST_H */
