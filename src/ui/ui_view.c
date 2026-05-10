#include "ui/ui_view.h"

UiViewStore *ui_view_store_new (Mem *mem) {
    UiViewStore *store = mem_new(mem, UiViewStore);
    store->mem = mem;
    array_init(&store->instances, mem);
    array_init(&store->types, mem);
    return store;
}

UiViewType *ui_view_type_get (UiViewStore *store, U32 type_id) {
    array_iter (it, &store->types) if (it->id == type_id) return it;
    badpath;
}

UiViewInstance *ui_view_instance_new (UiViewStore *store, U32 type_id) {
    UiViewInstance *instance = mem_new(store->mem, UiViewInstance);
    instance->type = ui_view_type_get(store, type_id);
    instance->type->init(instance);
    return instance;
}

Void ui_view_instance_remove (UiViewStore *store, UiViewInstance *instance) {
    instance->type->free(instance);
    array_find_remove_fast(&store->instances, IT == instance);
    mem_free(store->mem, .old_ptr=instance);
}

Void ui_view_type_add (UiViewStore *store, UiViewType type) {
    UiViewType *t = mem_new(store->mem, UiViewType);
    *t = type;
    array_push(&store->types, t);
}
