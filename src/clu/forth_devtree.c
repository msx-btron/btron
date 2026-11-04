/*
 * B-System BTRON3 / T-FW — forth_devtree.c
 * OpenFirmware (IEEE 1275) Device-Tree Implementation
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/forth_vm.h>
#include <string.h>

FCodeNode* forth_dev_create(ForthVM *vm, const char *name, FCodeNode *parent) {
    if (!vm || !name || vm->num_dev_nodes >= FORTH_DEV_MAX) {
        return NULL;
    }

    FCodeNode *node = &vm->dev_nodes[vm->num_dev_nodes++];
    memset(node, 0, sizeof(*node));
    strncpy(node->name, name, FCODE_NODE_NAME_LEN - 1);
    node->name[FCODE_NODE_NAME_LEN - 1] = '\0';
    node->parent = parent;

    if (parent) {
        if (!parent->child) {
            parent->child = node;
        } else {
            FCodeNode *p = parent->child;
            while (p->peer) {
                p = p->peer;
            }
            p->peer = node;
        }
    }

    return node;
}

static FCodeNode* find_child_by_name(FCodeNode *parent, const char *name, size_t name_len) {
    if (!parent) return NULL;
    FCodeNode *c = parent->child;
    while (c) {
        if (strlen(c->name) == name_len && strncmp(c->name, name, name_len) == 0) {
            return c;
        }
        c = c->peer;
    }
    return NULL;
}

FCodeNode* forth_dev_find(ForthVM *vm, const char *path) {
    if (!vm || !path || vm->num_dev_nodes == 0) return NULL;
    if (strcmp(path, "/") == 0) return &vm->dev_nodes[0];

    const char *p = path;
    if (*p == '/') p++;

    FCodeNode *curr = &vm->dev_nodes[0]; /* Root is index 0 */

    while (*p && curr) {
        const char *next_slash = strchr(p, '/');
        size_t seg_len = next_slash ? (size_t)(next_slash - p) : strlen(p);
        if (seg_len == 0) break;

        curr = find_child_by_name(curr, p, seg_len);
        if (!curr) return NULL;

        if (next_slash) p = next_slash + 1;
        else break;
    }

    return curr;
}

int forth_dev_set_prop(FCodeNode *node, const char *key, const void *val, uint16_t len) {
    if (!node || !key) return -1;

    /* Check for existing property update */
    for (uint8_t i = 0; i < node->num_props; i++) {
        if (strcmp(node->props[i].key, key) == 0) {
            node->props[i].val = (const uint8_t *)val;
            node->props[i].len = len;
            return 0;
        }
    }

    /* Add new property */
    if (node->num_props >= FCODE_MAX_PROPS) return -1;
    FCodeProp *p = &node->props[node->num_props++];
    strncpy(p->key, key, FCODE_PROP_KEY_LEN - 1);
    p->key[FCODE_PROP_KEY_LEN - 1] = '\0';
    p->val = (const uint8_t *)val;
    p->len = len;

    return 0;
}

const void* forth_dev_get_prop(FCodeNode *node, const char *key, uint16_t *out_len) {
    if (!node || !key) return NULL;
    for (uint8_t i = 0; i < node->num_props; i++) {
        if (strcmp(node->props[i].key, key) == 0) {
            if (out_len) *out_len = node->props[i].len;
            return node->props[i].val;
        }
    }
    return NULL;
}

