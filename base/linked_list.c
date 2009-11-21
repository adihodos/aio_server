#include "linked_list.h"
#include "debug_helpers.h"

/*!
 * @brief Representation of a node in a double linked list.
 */
struct list_node {
  /*!
   * Array of pointers to the previous(0) and following(1) node.
   */
  struct list_node* n_link[2];
  /*!
   * Stored data.
   */
  void*             n_data;
};

/*!
 * @brief Representation of a double linked list.
 */
struct dlinked_list {
  /*!
   * Pointer to item compare function.
   */
  item_comparator     dl_comparator;
  /*!
   * Pointer to allocation/deallocation routines.
   */
  struct allocator*   dl_allocator;
  /*!
   * Pointer to sentinel node.
   */
  struct list_node*   dl_sentinel;
  /*!
   * Item count.
   */
  size_t              dl_items;
  /*!
   * Pointer to user defined data.
   */
  void*               dl_userdata;
  /*!
   * Static storage area for the sentinel node.
   */
  unsigned char       dl_storage[sizeof(struct list_node)];
};

typedef enum {
  kLeftLink = 0,
  kRightLink = 1
} kLinkDirection;

/*!
 * @brief Item insert function. 
 * @param list Pointer do dll where the item is to be inserted.
 * @param item Pointer to item to be inserted.
 * @param refnode Pointer to reference node when inserting the new node.
 * @param where Direction of insertion( left - before the reference node,
 * right - after the reference node ).
 * @return Pointer to the node's data on success, NULL on failure.
 */
static void* dlinked_list_internal_insert_node(struct dlinked_list* list, 
                                               void* item,
                                               struct list_node* refnode,
                                               kLinkDirection where) {
  struct list_node* newnode = list->dl_allocator->al_mem_alloc(
      list->dl_allocator, sizeof(struct list_node));
  if (!newnode) {
    return NULL;
  }

  newnode->n_data = item;
  newnode->n_link[!where] = refnode;
  newnode->n_link[where] = refnode->n_link[where];
  refnode->n_link[where]->n_link[!where] = newnode;
  refnode->n_link[where] = newnode;
  ++list->dl_items;

  return newnode->n_data;
}

/*!
 * @brief Removes a node from a linked list.
 * @param list Pointer to a double linked list that contains the node to be
 * removed.
 * @param node Pointer to the node to be removed.
 * @return Pointer to the node's data.
 */
static void* dlinked_list_internal_remove_node(struct dlinked_list* list,
                                               struct list_node* node) {
  void* node_data = NULL;
  BUGSTOP_IF((node == list->dl_sentinel), "Trying to delete sentinel node!");

  node->n_link[kRightLink]->n_link[kLeftLink] = node->n_link[kLeftLink];
  node->n_link[kLeftLink]->n_link[kRightLink] = node->n_link[kRightLink];
  node_data = node->n_data;
  list->dl_allocator->al_mem_release(list->dl_allocator, node);
  --list->dl_items;

  return node_data;
}

dlinked_list_handle dlist_create(item_comparator cmp, 
                                 struct allocator* al, 
                                 void* userdata) {
  struct dlinked_list* newlist = NULL;
	extern struct allocator* allocator_handle;

  BUGSTOP_IF((!cmp), "No comparator function for items passed!");

  if (!al) {
    al = allocator_handle;
  }

  newlist = al->al_mem_alloc(al, sizeof(struct dlinked_list));
  if (!newlist) {
    return NULL;
  }

  newlist->dl_allocator = al;
  newlist->dl_comparator = cmp;
  newlist->dl_userdata = userdata;
  newlist->dl_items = 0;
  newlist->dl_sentinel = (struct list_node*) newlist->dl_storage;
  newlist->dl_sentinel->n_data = NULL;
  newlist->dl_sentinel->n_link[kLeftLink] = 
    newlist->dl_sentinel->n_link[kRightLink] = newlist->dl_sentinel;

  return (dlinked_list_handle) newlist;
}

void* dlist_push_head(dlinked_list_handle list_handle, void* item) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;

  BUGSTOP_IF((!list_handle), "Invalid hande to linked list.");
  BUGSTOP_IF((!item), "Invalid item");

  return dlinked_list_internal_insert_node(list, 
                                           item, 
                                           list->dl_sentinel->n_link[kRightLink], 
                                           kLeftLink);
}

void* dlist_push_tail(dlinked_list_handle list_handle, void* item) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;

  BUGSTOP_IF((!list_handle), "Invalid hande to linked list.");
  BUGSTOP_IF((!item), "Invalid item");

  return dlinked_list_internal_insert_node(list, 
                                           item, 
                                           list->dl_sentinel->n_link[kLeftLink],
                                           kRightLink);
}

void* dlist_pop_head(dlinked_list_handle list_handle ) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  if (list->dl_sentinel->n_link[kRightLink] != list->dl_sentinel) {
    return dlinked_list_internal_remove_node(
        list, 
        list->dl_sentinel->n_link[kRightLink]);
  }

  BUGSTOP_IF((list->dl_items != 0), "Incorrect item count!" );
  return NULL;
}

void* dlist_pop_tail(dlinked_list_handle list_handle) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  if (list->dl_sentinel->n_link[kLeftLink] != list->dl_sentinel) {
    return dlinked_list_internal_remove_node(
        list,
        list->dl_sentinel->n_link[kLeftLink]);
  }

  BUGSTOP_IF((list->dl_items != 0), "Incorrect item count!" );
  return NULL;
}

void* dlist_find(dlinked_list_handle list_handle, void* item) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  struct list_node* curr_node = NULL;

  BUGSTOP_IF((!list_handle), "Invalid parameter");
  BUGSTOP_IF((!item), "Invalid parameter");

  curr_node = list->dl_sentinel->n_link[kRightLink];

  while (curr_node != list->dl_sentinel) {
    if (list->dl_comparator(curr_node->n_data, item, list->dl_userdata) == 0) {
      return curr_node->n_data;
    }
    curr_node = curr_node->n_link[kRightLink];
  }

  return NULL;
}

void* dlist_remove_item(dlinked_list_handle list_handle,
                        void* item) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  struct list_node* curr_node = NULL;

  BUGSTOP_IF((!list_handle), "Invalid parameter");
  BUGSTOP_IF((!item), "Invalid parameter");

  curr_node = list->dl_sentinel->n_link[kRightLink];
  for (; ;) {
    if (curr_node == list->dl_sentinel) {
      return NULL;
    }

    if (list->dl_comparator(curr_node->n_data, item, list->dl_userdata) == 0 ) {
      --list->dl_items;
      return dlinked_list_internal_remove_node(list, curr_node);
    }
    curr_node = curr_node->n_link[kRightLink];
  }
}

void dlist_for_each(dlinked_list_handle list_handle,
                    void (*fn_for_each)(void*, void*, void*),
                    void* walk_data) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  struct list_node* curr_node = NULL;

  BUGSTOP_IF((!list_handle), "Invalid parameter!");
  BUGSTOP_IF((!fn_for_each), "Invalid parameter!");

  curr_node = list->dl_sentinel->n_link[kRightLink];
  while (curr_node != list->dl_sentinel) {
    fn_for_each(curr_node->n_data, list->dl_userdata, walk_data);
    curr_node = curr_node->n_link[kRightLink];
  }
}

void dlist_destroy(dlinked_list_handle list_handle) {
  struct dlinked_list* list = (struct dlinked_list*) list_handle;
  struct list_node* curr_node = NULL;

  BUGSTOP_IF((!list_handle), "Invalid parameter!");

  curr_node = list->dl_sentinel->n_link[kRightLink];
  for (; ;) {
    struct list_node* old_node = curr_node;
    if (curr_node == list->dl_sentinel) {
      break;
    }
    curr_node = curr_node->n_link[kRightLink];
    list->dl_allocator->al_mem_release(list->dl_allocator, old_node);
  }

  list->dl_allocator->al_mem_release(list->dl_allocator, list);
}
